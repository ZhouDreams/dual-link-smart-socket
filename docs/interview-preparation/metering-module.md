# Metering Service 模块深度分析

> 面试准备材料 — 适用于讲解电参量采集→换算→电能增量→确认/丢弃的完整数据流与设计决策。

---

## 1. 模块定位与职责边界

### 1.1 在架构中的位置

```
驱动适配层          bl0942（UART 通信、帧校验、原始寄存器值）
                        │  BL0942_EVENT_MEASUREMENT
                        ▼
业务服务层          metering_service（工程量换算 + 电能增量 + 快照分发）
                        │  METERING_EVENT_SNAPSHOT
                        ▼
上层消费者         safety_guard / thingsboard_client / display_service
```

Metering Service 是业务服务层的第一个模块，是硬件驱动与业务逻辑之间的桥梁。

### 1.2 职责（做什么）

1. 消费 BL0942 原始测量事件（`bl0942_measurement_t`）
2. 将 24-bit 原始寄存器值换算为工程量（V、A、W、Hz）
3. 基于 CF 脉冲计数器计算上报区间电能增量（mWh）
4. 提供 confirm/discard 语义保证电能增量不丢失
5. 发布 `METERING_EVENT_SNAPSHOT` 事件供上层消费
6. 缓存最新快照供主动查询

### 1.3 不负责（不做什么）

| 不负责的内容 | 谁负责 |
|-------------|--------|
| UART 通信和帧校验 | bl0942 驱动 |
| 继电器控制 | relay 模块 |
| 安全规则判定 | safety_guard |
| 云端上报 | thingsboard_client |
| 显示更新 | lvgl_dashboard |

---

## 2. 文件结构与分层

```
main/metering/
├── metering_service.h           # 公共 API（用户可见）
├── metering_service.c           # 主实现（换算、事件处理、生命周期管理）
├── metering_service_internal.h  # 内部 API（电能增量辅助接口）
└── metering_service_internal.c  # 内部实现（CF 计数→mWh 换算、确认令牌）
```

**设计意图**：将电能增量的"确认/丢弃"状态机从主逻辑中独立出来，放在 `_internal` 文件中。这样：
- 主文件 `metering_service.c` 专注换算和事件分发
- 电能增量的 prepare/confirm/discard 状态机可独立测试
- `_internal.h` 不暴露给模块外部，只在 metering 内部使用

---

## 3. 核心数据结构

### 3.1 公共类型（`metering_service.h`）

#### `metering_config_t` — 换算系数配置

```c
typedef struct {
    float v_rms_coeff;   // 电压：V = raw × coeff
    float i_rms_coeff;   // 电流：A = raw × coeff
    float watt_coeff;    // 功率：W = raw × coeff
} metering_config_t;
```

系数 > 0 时直接用系数换算（`raw × coeff`），系数 = 0 时回退到硬编码公式换算。这样设计支持：
- 校准后的精确系数（生产阶段标定）
- 未校准时使用参考电路默认公式

#### `metering_snapshot_t` — 电参量快照（值对象）

```c
typedef struct {
    float voltage;              // 电压 (V)
    float current;              // 电流 (A)
    float power;                // 有功功率 (W)
    float energy_delta;         // 上报区间电能增量 (mWh)
    float frequency;            // 电网频率 (Hz)
    uint64_t timestamp_us;      // 快照时间戳 (μs)
    uint32_t energy_delta_token;// 电能增量确认令牌
    bool valid;                 // 快照有效性
} metering_snapshot_t;
```

**面试要点**：`energy_delta_token` 是实现"可靠电能上报"的关键。每次快照携带一个令牌，上层必须用这个令牌来 confirm 或 discard，否则后续电能快照会卡在 pending 状态。

### 3.2 内部类型（`metering_service_internal.h`）

#### `metering_energy_delta_state_t` — 电能增量状态机

```c
typedef struct {
    // 已确认基线
    bool have_confirmed_cf_cnt_raw;
    uint32_t confirmed_cf_cnt_raw;      // 上次确认的 CF 计数
    uint32_t confirmed_residual_nwh;    // 上次确认的余数（nWh）

    // 待确认结果（最多一个 pending）
    bool have_pending;
    uint32_t pending_cf_cnt_raw;        // 待确认的 CF 计数
    uint32_t pending_residual_nwh;      // 待确认的余数（nWh）
    uint32_t pending_token;             // 待确认令牌

    uint32_t next_token;                // 单调递增令牌生成器
} metering_energy_delta_state_t;
```

**设计决策**：最多只有一个 pending 结果。如果上层未确认前一次快照就来了新测量，`prepare()` 返回 `ESP_ERR_INVALID_STATE`，这意味着这次测量事件被跳过（不累加电能）。这是有意的取舍——防止电能增量无限制堆积导致状态膨胀。

### 3.3 内部服务结构（定义在 `metering_service.c`）

```c
struct metering_service {
    metering_config_t config;
    SemaphoreHandle_t mutex;
    metering_snapshot_t latest;
    bool has_latest;
    metering_energy_delta_state_t energy_delta_state;
    esp_event_handler_instance_t measurement_handler;
    esp_event_handler_instance_t fault_handler;
    bool started;
    bool starting;      // 防止并发启动
    bool stopping;      // 防止并发停止
    bool initialized;
};
```

**面试要点**：
- `starting` / `stopping` 是中间态标志，防止 `start()` 和 `stop()` 并发调用导致事件处理器泄漏
- 事件注册（`esp_event_handler_instance_register`）在 mutex 外部执行，避免在 esp_event handler 上下文中持锁阻塞
- `measurement_handler` 和 `fault_handler` 在 mutex 内快照出来再注销，实现安全的"锁外注销、锁内清理"

---

## 4. 原始数据→工程量换算

### 4.1 硬件常量

```c
#define METERING_VREF_MV         (1218)      // BL0942 内部基准电压 (mV)
#define METERING_RL_MILLIOHM     (3)         // 采样电阻 (mΩ)
#define METERING_R1_OHM          (510)       // 电压分压电阻 R1 (Ω)
#define METERING_R2_OHM          (1950000)   // 电压分压电阻 R2 (Ω)
#define METERING_KI_SCALE        (305978)    // 电流校准系数
#define METERING_KV_SCALE        (73989)     // 电压校准系数
#define METERING_POWER_NUM       (2679285553LL)
#define METERING_POWER_DEN       (50107500000LL)
#define METERING_FREQ_CLOCK_HZ   (1000000UL) // 频率测量时钟
```

### 4.2 换算公式

BL0942 输出 24-bit 无符号/有符号原始寄存器值。换算公式基于 BL0942 数据手册和参考电路参数：

**电流（mA）**：
```
current_ma = (i_rms_raw × VREF_MV) / (KI_SCALE × RL_MILLIOHM)
```

**电压（cV = 0.01V）**：
```
voltage_cv = (v_rms_raw × VREF_MV × RSUM) / (KV_SCALE × R1 × 10000)
```

**有功功率（cW = 0.01W）**：
```
power_cw = (watt_raw × POWER_NUM) / POWER_DEN
```
负功率钳位为 0（防倒灌时出现负值）。

**频率（cHz = 0.01Hz）**：
```
frequency_chz = (FREQ_CLOCK × 100) / freq_raw
```

### 4.3 两级换算策略

```
原始测量 (bl0942_measurement_t)
    │
    ▼ metering_convert_default()
定点中间值 (metering_fixed_sample_t)  — voltage_cv, current_ma, power_cw, frequency_chz
    │
    ▼ metering_convert_with_config()
浮点快照 (metering_snapshot_t)        — voltage (V), current (A), power (W), frequency (Hz)
```

1. `metering_convert_default()`：硬编码公式，输出定点中间值
2. `metering_convert_with_config()`：如果配置了系数 > 0，用系数直接换算；否则回退到定点值除以精度因子

**为什么分两级？**
- 自检（self-test）只验证硬编码公式的正确性，使用定点中间值可以精确断言范围
- 运行时换算支持校准系数覆盖，同时保留回退路径

---

## 5. 电能增量计算（核心难点）

### 5.1 基本原理

BL0942 内部有一个 24-bit CF 脉冲计数器（`cf_cnt_raw`），每累计一定能量值加 1。两个快照之间的 CF 计数差 × 单个脉冲代表的能量 = 区间电能。

```
单个脉冲能量 = 62297938 nWh（约 62.3 μWh）
```

### 5.2 24-bit 环绕处理

CF 计数器是 24-bit 无符号，到 `0xFFFFFF` 后回绕到 `0x000000`：

```c
uint32_t metering_energy_u24_delta(uint32_t current, uint32_t previous)
{
    return (current - previous) & 0x00FFFFFF;
}
```

利用无符号整数减法的环绕特性，正确处理计数器回绕。

### 5.3 余数累积

由于 CF 脉冲到 mWh 的换算不是整数关系，每次换算保留余数：

```c
total_nwh = confirmed_residual_nwh + delta × PULSE_NWH;
mwh_thousandths = total_nwh / 1000;       // mWh × 1000（千分之一 mWh）
next_residual_nwh = total_nwh % 1000;     // 余数留给下次
```

最终输出：`energy_delta_mwh = mwh_thousandths / 1000.0f`

余数累积保证长时间运行的累计精度不丢失。

### 5.4 确认/丢弃状态机

```
                  ┌──────────────────────────────────────────┐
                  │                                          │
                  ▼                                          │
         ┌─────────────┐   prepare(cf_cnt)   ┌───────────┐  │
         │ 无 pending  │ ──────────────────→ │  pending   │  │
         │ confirmed   │                      │  prepared  │  │
         │ baseline    │                      │  result    │  │
         └─────────────┘                      └─────┬─────┘  │
              ▲                                     │        │
              │                      ┌──────────────┤        │
              │  confirm(token)      │              │        │
              │  推进 confirmed      │              │discard │
              │  baseline 到 pending │              │(token) │
              └──────────────────────┘              │        │
                                                  回到无    │
                                                  pending   │
                                                  不推进    │
                                                  baseline  │
                                                            │
                                         prepare() 时       │
                                         have_pending=true  │
                                         返回 ERR_INVALID_STATE ─┘
```

**关键规则**：
1. `prepare()` 只在没有 pending 时才成功，否则返回 `ESP_ERR_INVALID_STATE`
2. `confirm(token)` 验证令牌匹配后，将 confirmed baseline 推进到 pending 值
3. `discard(token)` 验证令牌匹配后，仅清除 pending，不推进 confirmed baseline
4. 令牌是非零单调递增的（回绕时跳过 0），用于防止 ABA 问题

### 5.5 令牌机制

```c
static uint32_t metering_energy_next_token(metering_energy_delta_state_t *state)
{
    uint32_t token = state->next_token++;
    if (state->next_token == 0) state->next_token = 1;  // 跳过 0
    if (token == 0) {                                     // 初始化时 next_token=1，不会发生
        token = state->next_token++;
        if (state->next_token == 0) state->next_token = 1;
    }
    return token;
}
```

**为什么用令牌而不是指针？**
- 令牌是值类型，可以随快照拷贝，不需要管理生命周期
- 上层只需保存快照，通过 `snapshot.energy_delta_token` 调用 confirm/discard
- 防止上层用旧快照的令牌去确认新快照

### 5.6 故障恢复时的电能基线

当 BL0942 故障触发硬复位后，CF 计数器会被重置。`metering_on_bl0942_fault()` 检测到 `hard_reset_attempted=true` 时，调用 `metering_energy_delta_reset_baseline()` 清除基线：

```c
void metering_energy_delta_reset_baseline(metering_energy_delta_state_t *state)
{
    uint32_t next_token = state->next_token;
    metering_energy_delta_state_init(state);   // 清空所有状态
    if (next_token != 0) state->next_token = next_token;  // 保留令牌递增序列
}
```

下次 `prepare()` 会重新建立基线（`baseline_established=true`，`energy_delta=0`）。

---

## 6. 自检（Self-Test）

`metering_run_conversion_selftest()` 在 `metering_service_create()` 时运行，验证：

### 6.1 黄金向量测试

```c
const bl0942_measurement_t golden = {
    .i_rms_raw = 753639,
    .v_rms_raw = 3494335,
    .watt_raw  = 411438,
    .freq_raw  = 20000,
    .valid     = true,
};
```

期望换算结果：
| 参数 | 期望范围 | 物理含义 |
|------|---------|---------|
| current_ma | 994 ~ 1004 | ~1.0 A |
| voltage_cv | 21989 ~ 22009 | ~220 V |
| power_cw | 21989 ~ 22009 | ~220 W |
| frequency_chz | 4995 ~ 5005 | ~50 Hz |

### 6.2 边界值测试

- **高功率**：`watt_raw = 0x7FFFFF`（最大正值）→ 换算后 > 0 且 < INT32_MAX
- **负功率**：`watt_raw = -0x800000`（最小负值）→ 钳位为 0

### 6.3 电能增量测试

1. `prepare(10)` 建立基线 → `energy_delta=0`，`token != 0`
2. `confirm(token)` 确认基线
3. `prepare(12)` 计算增量 → `energy_delta ≈ 124.595 mWh`（2 个 CF 脉冲 × 62.3 μWh × 1000）

---

## 7. 生命周期与线程安全

### 7.1 状态转换

```
create()  ─→  initialized=true
    │
    ▼
start()   ─→  starting → 注册事件 → started=true
    │
    ▼
stop()    ─→  stopping → 注销事件 → started=false
    │
    ▼
destroy() ─→  stop() → 删除 mutex → free
```

### 7.2 并发保护

| 操作 | 保护方式 |
|------|---------|
| `start()` / `stop()` | mutex + starting/stopping 标志 |
| 事件回调（measurement/fault）| mutex 保护 latest 和 energy_delta_state |
| `get_latest()` | mutex 拷贝整个快照 |
| `confirm_energy_delta()` | mutex 保护 energy_delta_state |
| 事件注销 | 锁外执行 esp_event_handler_instance_unregister |

**关键模式**：事件注销在 mutex 外执行（避免在 esp_event handler 上下文中持锁导致死锁），但 handler 指针在 mutex 内快照到局部变量。注销后再取锁清理。

### 7.3 为什么 start/stop 会有 starting/stopping 中间态？

因为 `esp_event_handler_instance_register` 是阻塞调用，在注册期间不能持 mutex（否则 measurement 回调可能死锁）。`starting` 标志防止两个任务同时调用 `start()` 导致注册两次 handler。

---

## 8. 事件处理流程

### 8.1 测量事件处理

```
BL0942_EVENT_MEASUREMENT
    │
    ▼ metering_on_bl0942_measurement()
    ├── 检查 me/measurement/valid
    ├── mutex lock
    ├── 检查 started && !stopping
    ├── metering_convert_with_config() → sample
    ├── metering_energy_delta_prepare() → energy_delta
    ├── 更新 me->latest = sample（含 energy_delta 和 token）
    ├── mutex unlock
    └── metering_post_snapshot() → esp_event_post(METERING_EVENT_SNAPSHOT)
```

### 8.2 故障事件处理

```
BL0942_EVENT_FAULT
    │
    ▼ metering_on_bl0942_fault()
    ├── mutex lock
    ├── 检查 started && !stopping
    ├── 如果 hard_reset_attempted → 重置电能基线
    ├── 构造 valid=false 快照（保留之前的有效值用于参考）
    ├── 更新 me->latest
    ├── mutex unlock
    └── metering_post_snapshot() → 广播无效快照
```

**面试要点**：故障时不停止服务。发布 `valid=false` 的快照让上层知道数据暂时无效，同时保留最近有效值（用于 display 显示最后已知值）。

---

## 9. 面试高频问题

### Q1: 为什么不把换算放在 BL0942 驱动里？

**关注点分离**。BL0942 驱动只负责"怎么读"，不负责"怎么算"。换算公式依赖于具体电路参数（分压电阻、采样电阻），这些参数是板级设计决定的，不是芯片通用的。如果换算放在驱动里，换一块分压电阻值不同的板子就要改驱动代码。

### Q2: 电能增量为什么用 confirm/discard 而不是简单的累计？

**可靠上报**。智能插座的电能数据需要上报到云端。如果网络断了，上报失败，累计值不能直接覆盖——否则上报成功后云端会计入重复电能。confirm/discard 模式保证：
- 上报成功 → confirm → 推进基线 → 下次增量从新基线开始
- 上报失败 → discard → 不推进基线 → 下次增量包含遗漏的电能
- 最多丢失一次 pending 期间的测量（`prepare()` 返回 `ESP_ERR_INVALID_STATE` 时）

### Q3: CF 计数器回绕怎么处理？

24-bit 无符号减法 + mask。`(current - previous) & 0x00FFFFFF` 在 current ≥ previous 时得到正确差值，在 current < previous（回绕）时也能正确计算（无符号减法的补码特性）。唯一前提是两次采样之间计数器没有回绕超过一轮（0xFFFFFF ≈ 16M 脉冲），按 ~62.3 μWh/脉冲，约 1000 kWh，远超单次上报区间的可能增量。

### Q4: 为什么事件注销要在 mutex 外执行？

`esp_event_handler_instance_unregister()` 内部可能等待当前正在执行的同事件 handler 完成。如果持 mutex 注销，而 measurement handler 正在等 mutex，就会死锁。解决方法：mutex 内快照 handler 到局部变量 → 释放 mutex → 用局部变量注销 → 再取 mutex 清理。

### Q5: 换算自检的意义是什么？

嵌入式系统的数值计算错误不会抛异常——只会给你错误结果。自检用已知输入验证换算公式的正确性，确保：
1. 常量没有写错
2. 整数溢出不会发生
3. 负功率正确钳位
4. 电能增量状态机在典型场景下行为正确

自检在 `create()` 时运行，如果失败直接返回 NULL，拒绝创建服务。

### Q6: 为什么 metering_service 不做窗口聚合（均值、最大值等）？

当前设计中 BL0942 采样周期配置为 1000ms（1 Hz），metering_service 直接透传每次快照。窗口聚合的需求应由上层（如 thingsboard_client 或专门的聚合模块）根据业务需要实现。metering_service 保持单一职责：raw → 工程量 + 电能增量。

### Q7: 如果长时间没有 confirm 也不 discard 会怎样？

`prepare()` 检测到 `have_pending=true` 时返回 `ESP_ERR_INVALID_STATE`，measurement handler 会跳过这次测量（不更新 latest、不发布快照）。这意味着如果上层忘了 confirm/discard，系统会"冻结"——不再发布新的电参量快照。这是一个**显式的设计决策**：宁可不更新数据，也不丢失电能。

---

## 10. 数据流总览

```
BL0942 sample task (1000ms 周期)
    │
    │  UART 读取 + 帧校验
    │
    ▼
BL0942_EVENT_MEASUREMENT (esp_event 广播)
    │  bl0942_measurement_t: i_rms_raw, v_rms_raw, watt_raw, cf_cnt_raw, freq_raw
    │
    ▼
metering_on_bl0942_measurement()
    │
    │  1. metering_convert_with_config() → 浮点快照 (V, A, W, Hz)
    │  2. metering_energy_delta_prepare(cf_cnt_raw) → energy_delta (mWh) + token
    │  3. 缓存到 me->latest
    │
    ▼
METERING_EVENT_SNAPSHOT (esp_event 广播)
    │  metering_snapshot_t: voltage, current, power, energy_delta, frequency, token, valid
    │
    ├─→ safety_guard: 判定过流/过功率
    ├─→ thingsboard_client: 构建 telemetry JSON 上报
    │       │
    │       ├─ 上报成功 → metering_service_confirm_energy_delta(ms, &snapshot)
    │       └─ 上报失败 → metering_service_discard_energy_delta(ms, &snapshot)
    │
    └─→ lvgl_dashboard: 更新显示

BL0942_EVENT_FAULT (连续读取失败)
    │
    ▼
metering_on_bl0942_fault()
    │
    │  1. 如果硬复位 → 重置电能基线
    │  2. 构造 valid=false 快照
    │
    ▼
METERING_EVENT_SNAPSHOT (valid=false)
```

---

## 11. 代码量统计

| 文件 | 行数 | 职责 |
|------|------|------|
| `metering_service.h` | 183 | 公共 API 和类型定义 |
| `metering_service.c` | 770 | 换算、事件处理、生命周期、自检 |
| `metering_service_internal.h` | 81 | 电能增量辅助接口 |
| `metering_service_internal.c` | 196 | CF 计数→mWh 换算、状态机 |
| **合计** | **1230** | |
