# 架构概览

## 文档定位

本文档定义**基于 ESP32-S3 的双模联网智能插座系统**的软件总体架构、模块边界、依赖规则和核心数据流。

本文档只回答以下问题：

1. 系统为什么这样分层。
2. 每一层负责什么，不负责什么。
3. 哪些模块通过句柄封装，哪些模块使用继承与多态。
4. `Wi-Fi / LTE` 双模网络如何通过统一接口接入 `network_manager`。
5. `BL0942`、继电器、按键、`ThingsBoard`、`LVGL` 在系统主干中如何协作。

本文档不负责：

1. 目录结构说明。目录结构放在 `docs/agents/directory-structure.md`。
2. 错误处理规范。错误处理放在 `docs/agents/err.md`。
3. 逐个结构体字段和 API 清单。类设计放在 `docs/agents/classes.md`。
4. 构建、烧录、串口调试流程。相关内容放在 `docs/agents/build-and-debug.md`。
5. 代码模板和 C OOP 细则。相关内容放在 `docs/agents/coding-style.md` 和 `docs/agents/oop-design.md`。

---

## 1. 项目定位

本项目是一个**基于 ESP32-S3 的双模联网智能插座系统**。

系统以 `ESP32-S3-LCD-1.47B` 开发板为主控平台，围绕智能插座的真实工程闭环展开：

1. 通过 `BL0942` 采集电压、电流、功率、电能等电参量。
2. 通过继电器控制负载通断。
3. 通过本地按键提供离线可用的手动控制入口。
4. 通过 `Wi-Fi + LTE` 双模联网提供可靠的云端连接能力。
5. 通过 `ThingsBoard` 实现远程控制、遥测展示和阈值配置。
6. 通过 `LVGL + TFT` 提供本地状态显示。
7. 通过本地安全规则实现过功率、过流保护。

本项目不再以“电动自行车电池充电异常 AI 模型”为主线。原旧项目中的 AI 风险模型不进入本轮架构主干；安全保护采用可解释、可验证、适合面试讲解的规则逻辑。

---

## 2. 设计原则

### 2.1 直接基于 ESP-IDF

本项目只面向 `ESP-IDF` 平台，不设计跨 RTOS、跨 SDK 的平台抽象层。

各模块可以直接使用 ESP-IDF / FreeRTOS API，例如：

1. `xTaskCreate`
2. `xQueueCreate`
3. `xSemaphoreCreateMutex`
4. `esp_event_post`
5. `uart_write_bytes`
6. `gpio_set_level`

这与 Espressif 官方组件的设计哲学一致：项目运行环境已经确定时，不为不存在的移植需求增加抽象层。

### 2.2 只在网络层使用继承和多态

本项目使用 C 语言 OOP 思想，但不把所有模块都强行设计成继承体系。

继承与多态只用于最自然、最有价值的场景：`Wi-Fi / LTE` 双模网络。

原因是：

1. `Wi-Fi` 和 `LTE` 都表现为“可启动、可停止、可发布 MQTT、可订阅 MQTT、可查询状态”的链路。
2. 两条链路底层实现完全不同，但对上层应暴露相同能力。
3. `network_manager` 的职责是选择和切换链路，不应该知道当前链路的具体实现类型。

其他模块使用 opaque handle 完成封装，例如：

1. `bl0942_t *`
2. `relay_t *`
3. `button_t *`
4. `metering_service_t *`
5. `safety_guard_t *`
6. `thingsboard_client_t *`
7. `display_service_t *`

这些模块没有自然的子类关系，使用句柄封装即可，避免过度设计。

### 2.3 App 负责装配，不承载底层细节

`app_controller` 是系统装配和业务编排入口。

它可以知道项目有哪些模块，可以创建对象、连接依赖、注册回调、决定启动顺序，但不应直接实现：

1. BL0942 UART 协议解析。
2. 继电器 GPIO 驱动细节。
3. Wi-Fi 或 LTE 底层连接流程。
4. ThingsBoard topic 和 RPC JSON 细节。
5. LVGL widget 更新细节。
6. 安全规则的具体判定细节。

当某段逻辑可以被独立描述、独立测试、独立替换时，应从 `app_controller` 下沉到对应服务模块。

---

## 3. 分层架构

系统采用四层结构：

```text
┌────────────────────────────────────────────────────────────┐
│ 应用编排层                                                  │
│ app_controller                                              │
│ 创建对象、装配依赖、注册回调、协调启动顺序和业务流程          │
└──────────────────────────────┬─────────────────────────────┘
                               │
                               ▼
┌────────────────────────────────────────────────────────────┐
│ 业务服务层                                                  │
│ metering_service / safety_guard / thingsboard_client / display_service │
│ 电参量聚合、安全规则、云平台语义、本地显示状态模型            │
└──────────────────────────────┬─────────────────────────────┘
                               │
                               ▼
┌────────────────────────────────────────────────────────────┐
│ 网络抽象层                                                  │
│ network_manager / network_link / wifi_link / lte_link       │
│ 统一网络链路接口、Wi-Fi/LTE 多态实现、主备切换、MQTT 通道     │
└──────────────────────────────┬─────────────────────────────┘
                               │
                               ▼
┌────────────────────────────────────────────────────────────┐
│ 驱动适配层                                                  │
│ bl0942 / relay / button / tft_panel / lvgl_dashboard / board_pinmap │
│ 具体硬件、GPIO、UART、SPI、TFT、LVGL 适配                    │
└────────────────────────────────────────────────────────────┘
```

调用方向总体向下；事件、回调和状态快照向上返回。

### 3.1 应用编排层

应用编排层只有一个核心模块：`app_controller`。

职责：

1. 获取板级引脚映射。
2. 创建各模块对象。
3. 注入模块依赖。
4. 注册事件处理函数和回调。
5. 决定系统启动顺序。
6. 协调本地控制、云端控制、显示刷新和安全动作。

不负责：

1. 直接解析 BL0942 数据帧。
2. 直接拼接 ThingsBoard topic。
3. 直接操作 LVGL 控件。
4. 直接判断过流、过功率规则。
5. 直接选择 Wi-Fi 或 LTE 的底层连接流程。

### 3.2 业务服务层

业务服务层承接项目语义，是驱动与应用之间的中间层。

核心模块：

1. `metering_service`
2. `safety_guard`
3. `thingsboard_client`
4. `display_service`

#### `metering_service`

职责：

1. 消费 `BL0942` 测量事件。
2. 将原始测量值转换为工程量。
3. 维护短窗口聚合状态。
4. 输出周期性电参量快照。
5. 计算区间电量或累计电量所需的基础数据。

不负责：

1. 继电器控制。
2. 云端上报。
3. 安全动作执行。
4. 显示控件更新。

#### `safety_guard`

职责：

1. 持有本地安全阈值，例如功率阈值、电流阈值。
2. 根据 `metering_snapshot_t` 做可解释规则判断。
3. 输出 `safety_snapshot_t`，包括风险状态和建议动作。
4. 支持阈值被 `thingsboard_client` 或本地配置更新。

第一版只保留规则保护：

1. 过功率保护。
2. 过流保护。
3. 可选的短暂持续判定，避免瞬态误触发。

不负责：

1. AI 模型推理。
2. 直接操作继电器。
3. 直接访问 ThingsBoard。

#### `thingsboard_client`

职责：

1. 管理 ThingsBoard 平台语义。
2. 构建 telemetry JSON。
3. 发布属性、遥测和 RPC 响应。
4. 解析 ThingsBoard RPC 为项目内部命令。
5. 处理功率阈值远程读取和设置语义。

依赖规则：

1. `thingsboard_client` 只依赖 `network_manager` 提供的统一通道。
2. `thingsboard_client` 不直接依赖 `wifi_link` 或 `lte_link`。
3. `thingsboard_client` 不直接操作继电器，而是把语义命令回调给 `app_controller`。

#### `display_service`

职责：

1. 汇总系统状态为统一的 `display_snapshot_t`。
2. 接收电参量、继电器状态、网络状态、安全状态。
3. 将快照提交给 `lvgl_dashboard`。
4. 管理屏幕开关状态。

不负责：

1. 直接读取 BL0942。
2. 直接控制继电器。
3. 直接发布云端遥测。
4. 在非 LVGL 任务上下文中更新 widget。

### 3.3 网络抽象层

网络抽象层是本项目 C OOP 设计的核心。

核心模块：

1. `network_link`
2. `wifi_link`
3. `lte_link`
4. `network_manager`

职责：

1. 抽象 Wi-Fi 和 LTE 的共同链路能力。
2. 用继承和 ops 表实现多态。
3. 让 `network_manager` 只依赖基类接口。
4. 对上层提供统一 MQTT publish / subscribe / receive 能力。
5. 实现 Wi-Fi 主链路与 LTE 备链路的 P0 级切换能力。

### 3.4 驱动适配层

驱动适配层直接面对硬件和底层库。

核心模块：

1. `bl0942`
2. `relay`
3. `button`
4. `tft_panel`
5. `lvgl_dashboard`
6. `board_pinmap`

职责：

1. 封装 UART、GPIO、SPI、TFT、LVGL 等具体细节。
2. 对上层暴露稳定、简洁的句柄 API。
3. 发布硬件事件或接收状态快照。

不负责：

1. 云平台语义。
2. 网络主备切换。
3. 业务安全策略。
4. 应用级状态编排。

---

## 4. 网络层 OOP 设计

### 4.1 抽象关系

`network_link_t` 是网络链路基类。

`wifi_link_t` 和 `lte_link_t` 是两个子类：

```text
network_link_t
  │
  ├── wifi_link_t
  │     使用 ESP-IDF Wi-Fi + esp-mqtt 实现 MQTT over Wi-Fi
  │
  └── lte_link_t
        使用 esp-lwlte / Air780EP 实现 MQTT over LTE
```

`network_manager_t` 只持有 `network_link_t *`，通过基类包装 API 调用链路能力。

### 4.2 基类接口

`network_link` 对外暴露 opaque handle 和包装 API。

```c
typedef struct network_link network_link_t;

typedef struct {
    esp_err_t (*destroy)(network_link_t *me);
    esp_err_t (*start)(network_link_t *me);
    esp_err_t (*stop)(network_link_t *me);
    esp_err_t (*get_status)(network_link_t *me, network_link_status_t *out);
    esp_err_t (*publish)(network_link_t *me, const network_publish_request_t *req);
    esp_err_t (*subscribe)(network_link_t *me, const char *topic, network_mqtt_qos_t qos);
    esp_err_t (*unsubscribe)(network_link_t *me, const char *topic);
    esp_err_t (*register_rx_cb)(network_link_t *me, network_rx_cb_t cb, void *ctx);
} network_link_ops_t;
```

基类内部结构由网络层内部文件定义，不直接暴露给普通上层业务模块：

```c
struct network_link {
    const network_link_ops_t *ops;
    network_link_type_t type;
};
```

外部调用不直接访问 `ops`，而是走包装 API：

```c
esp_err_t network_link_destroy(network_link_t *me);
esp_err_t network_link_start(network_link_t *me);
esp_err_t network_link_stop(network_link_t *me);
esp_err_t network_link_get_status(network_link_t *me, network_link_status_t *out);
esp_err_t network_link_publish(network_link_t *me, const network_publish_request_t *req);
esp_err_t network_link_subscribe(network_link_t *me, const char *topic, network_mqtt_qos_t qos);
esp_err_t network_link_unsubscribe(network_link_t *me, const char *topic);
esp_err_t network_link_register_rx_cb(network_link_t *me, network_rx_cb_t cb, void *ctx);
network_link_type_t network_link_get_type(const network_link_t *me);
```

这样可以保证：

1. 调用者不直接碰虚函数表。
2. 参数校验集中在包装 API。
3. `network_manager` 只依赖基类行为，不依赖子类结构。

### 4.3 子类实现模式

子类必须把基类放在结构体第一个成员，以实现 C 语言单继承：

```c
struct wifi_link {
    network_link_t base;
    wifi_link_config_t config;
    /* Wi-Fi STA、MQTT client、订阅表、运行状态等 */
};

struct lte_link {
    network_link_t base;
    lte_link_config_t config;
    /* LTE modem、MQTT client、订阅表、运行状态等 */
};
```

每个子类提供自己的 `static const network_link_ops_t`：

```c
static const network_link_ops_t wifi_link_ops = {
    .destroy = wifi_link_destroy_impl,
    .start = wifi_link_start_impl,
    .stop = wifi_link_stop_impl,
    .get_status = wifi_link_get_status_impl,
    .publish = wifi_link_publish_impl,
    .subscribe = wifi_link_subscribe_impl,
    .unsubscribe = wifi_link_unsubscribe_impl,
    .register_rx_cb = wifi_link_register_rx_cb_impl,
};
```

创建函数返回子类句柄或基类句柄均可，但注入 `network_manager` 时统一转为 `network_link_t *`。

推荐装配模式：

```c
wifi_link_t *wifi = wifi_link_create(&wifi_config);
lte_link_t *lte = lte_link_create(&lte_config);

network_manager_config_t net_config = {
    .primary = wifi_link_as_network_link(wifi),
    .backup = lte_link_as_network_link(lte),
};

network_manager_t *net_mgr = network_manager_create(&net_config);
```

### 4.4 network_manager 职责

`network_manager` 是双模网络的唯一上层入口。

职责：

1. 持有主链路和备链路的 `network_link_t *`。
2. 启动、停止和查询两条链路。
3. 维护当前活动链路。
4. 在 Wi-Fi 不可用时切换到 LTE。
5. 在 Wi-Fi 恢复并满足回切条件时切回 Wi-Fi。
6. 对上层提供统一 publish / subscribe / receive API。
7. 保存长期订阅意图，并在链路切换或重连后重放订阅。

不负责：

1. 解析 ThingsBoard topic。
2. 构建 telemetry JSON。
3. 直接控制继电器。
4. 理解 BL0942 数据。

### 4.5 双模联网策略

本项目将 LTE 定义为 P0 能力，不是可选增强。

默认策略：

1. Wi-Fi 是首选主链路。
2. LTE 是备链路。
3. 两条链路都应进入 `network_manager` 管理范围。
4. MQTT 发布始终经由当前活动链路。
5. 当前活动链路不可用时，`network_manager` 尝试切换到另一条 ready 链路。
6. 订阅意图由 `network_manager` 保存，活动链路变化后由 manager 负责重放。

状态语义：

| 状态 | 含义 |
|------|------|
| `IDLE` | 链路未启动 |
| `STARTING` | 链路启动中 |
| `CONNECTING` | 物理网络连接中 |
| `DEGRADED` | 物理链路可用但 MQTT 不可用 |
| `READY` | 物理链路与 MQTT 均可用 |
| `ERROR` | 链路故障，需重试或降级 |

`network_manager` 对上层只暴露聚合后的可用性，不要求上层关心 Wi-Fi 和 LTE 的内部差异。

---

## 5. 主干模块职责

### 5.1 BL0942 采集路径

`bl0942` 是电参量采集驱动适配模块。

职责：

1. 初始化 UART 和 BL0942 相关 GPIO。
2. 周期性读取 BL0942 全电参数数据包。
3. 校验帧头和 checksum。
4. 发布原始测量事件。
5. 在连续失败时发布故障事件。

`bl0942` 不负责工程量换算后的业务聚合；换算和窗口聚合属于 `metering_service`。

### 5.2 继电器控制路径

`relay` 是执行器驱动适配模块。

职责：

1. 初始化继电器控制 GPIO。
2. 提供 `set`、`toggle`、`get` API。
3. 为每次状态变化附带来源标签。
4. 发布继电器状态变化事件。

来源标签至少包括：

1. `LOCAL_BUTTON`
2. `CLOUD`
3. `SAFETY`
4. `INTERNAL`

来源标签用于避免云端回声和方便调试。

### 5.3 本地按键路径

`button` 是本地输入驱动适配模块。

职责：

1. 初始化按键 GPIO 或 `iot_button`。
2. 识别单击、双击、长按等事件。
3. 通过回调通知 `app_controller`。

默认语义：

1. 单击切换继电器状态。
2. 长按切换屏幕背光或显示状态。

按键模块不直接控制继电器或 LVGL，只上报输入事件。

### 5.4 ThingsBoard 云端路径

`thingsboard_client` 是 ThingsBoard 语义模块。

职责：

1. 注册 `network_manager` 的 MQTT 下行回调。
2. 声明 ThingsBoard RPC 和属性订阅。
3. 构建遥测 JSON。
4. 发布继电器状态、功率阈值、安全事件。
5. 将 RPC payload 转换为内部命令。

支持的核心命令：

1. 设置继电器状态。
2. 获取功率阈值。
3. 设置功率阈值。

`thingsboard_client` 不直接执行命令。它只把解析后的语义命令交给 `app_controller`，由 `app_controller` 协调 `relay`、`safety_guard` 和持久化逻辑。

### 5.5 LVGL 显示路径

`LVGL` 是本项目主干能力，不是可选边角功能。

显示路径由两个模块组成：

1. `display_service`
2. `lvgl_dashboard` / `tft_panel`

`display_service` 面向业务状态，`lvgl_dashboard` 面向 UI 控件，`tft_panel` 面向 LCD 硬件。

依赖关系：

```text
metering_service ─┐
relay             ├─→ display_service → lvgl_dashboard → tft_panel
network_manager ──┤
safety_guard ─────┘
```

设计规则：

1. 非 LVGL 任务不得直接操作 widget。
2. 上层只提交显示快照。
3. LVGL 任务内部读取最新快照并更新控件。
4. 关闭屏幕优先表现为背光关闭，不影响系统主逻辑运行。

---

## 6. 核心数据流

### 6.1 上行遥测流

```text
BL0942
  │  原始测量事件
  ▼
metering_service
  │  工程量换算 + 窗口聚合
  ▼
app_controller
  │  汇总 relay / network / safety 状态
  ▼
thingsboard_client
  │  ThingsBoard telemetry JSON
  ▼
network_manager
  │  当前活动链路 publish
  ▼
Wi-Fi 或 LTE
  │
  ▼
ThingsBoard
```

上行遥测的权威触发点是 `metering_service` 输出的周期性快照。

### 6.2 下行 RPC 控制流

```text
ThingsBoard
  │  MQTT RPC topic
  ▼
Wi-Fi 或 LTE
  │
  ▼
network_manager
  │  原始 MQTT message callback
  ▼
thingsboard_client
  │  解析为内部命令
  ▼
app_controller
  │  协调 relay / safety_guard / NVS / RPC response
  ▼
relay 或 safety_guard
```

下行命令不绕过 `app_controller`，避免云端模块直接修改执行器或安全阈值。

### 6.3 本地按键控制流

```text
button
  │  单击 / 长按回调
  ▼
app_controller
  │
  ├─ 单击 → relay_toggle(LOCAL_BUTTON)
  │
  └─ 长按 → display_service_set_screen_enabled(...)
```

本地按键必须在网络不可用时仍然可用。

### 6.4 安全保护流

```text
metering_service
  │  metering_snapshot_t
  ▼
safety_guard
  │  safety_snapshot_t + suggested_action
  ▼
app_controller
  │  action == RELAY_OFF
  ▼
relay_set(SAFETY, false)
  │
  ├─ relay event → display_service
  └─ relay event → thingsboard_client 上报状态
```

安全保护必须是本地闭环，不依赖 ThingsBoard 在线状态。

### 6.5 显示刷新流

```text
metering snapshot
relay state
network status
safety snapshot
  │
  ▼
display_service
  │  display_snapshot_t
  ▼
lvgl_dashboard
  │  LVGL task 内部更新 widget
  ▼
tft_panel
```

显示模块展示系统状态，但不拥有系统状态。

---

## 7. 事件与回调原则

本项目同时使用 ESP-IDF 事件循环和直接回调。

### 7.1 适合事件循环的场景

适合用 `esp_event` 的场景：

1. 传感器周期性测量事件。
2. 继电器状态变化事件。
3. 电参量快照事件。
4. 安全状态变化事件。
5. 网络状态变化事件。

这些事件通常有多个消费者，适合广播。

### 7.2 适合回调的场景

适合用直接回调的场景：

1. 按键事件。
2. ThingsBoard 语义命令。
3. network manager 的 MQTT 下行消息。

这些事件通常有明确单一消费者，使用回调更直接。

### 7.3 注册规则

1. `create` 只创建对象和资源，不注册外部事件。
2. `start` 注册事件订阅并启动任务。
3. `stop` 注销事件订阅并停止任务。
4. 回调注册由依赖装配方负责，通常是 `app_controller`。

---

## 8. 模块依赖规则

### 8.1 允许的依赖

| 模块 | 允许依赖 |
|------|----------|
| `app_controller` | 所有公共模块 API，用于装配和编排 |
| `metering_service` | `bl0942` 事件和公共类型 |
| `safety_guard` | `metering_service` 输出类型 |
| `thingsboard_client` | `network_manager`、业务输入类型 |
| `display_service` | 业务快照类型、`lvgl_dashboard` |
| `network_manager` | `network_link` 基类 API |
| `wifi_link` | `network_link`、ESP Wi-Fi、esp-mqtt |
| `lte_link` | `network_link`、esp-lwlte |
| 驱动适配模块 | ESP-IDF 驱动或对应第三方组件 |

### 8.2 禁止的依赖

1. `thingsboard_client` 禁止直接依赖 `wifi_link` 或 `lte_link`。
2. `network_manager` 禁止解析 ThingsBoard topic。
3. `wifi_link` 和 `lte_link` 禁止理解业务遥测字段。
4. `metering_service` 禁止直接控制继电器。
5. `safety_guard` 禁止直接访问 ThingsBoard。
6. `button` 禁止直接控制继电器或显示。
7. 任意非显示模块禁止直接操作 LVGL widget。

---

## 9. 面试叙述主线

本项目在面试中应被描述为：

> 我将一个功能发散的 AIoT 原型收敛重构为“基于 ESP32-S3 的双模联网智能插座系统”。系统围绕智能插座的完整闭环展开：BL0942 电参量采集、继电器控制、本地按键、LVGL 本地显示、ThingsBoard 云端遥测与 RPC、Wi-Fi/LTE 双模联网和本地安全保护。架构上，我用 C 语言 OOP 思想做了模块边界重构：普通硬件模块用 opaque handle 封装，网络层用 `network_link` 基类和 ops 表实现 Wi-Fi/LTE 的继承与多态，让 `network_manager` 只依赖统一链路接口完成主备切换。

这条叙述突出：

1. 项目真实可落地。
2. 技术栈清晰。
3. 有软硬件结合能力。
4. 有网络可靠性设计。
5. 有 C 语言架构设计能力。
6. 没有难以证明的 AI 夸大成分。
