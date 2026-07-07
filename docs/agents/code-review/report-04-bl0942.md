# Code Review: bl0942

**日期**: 2026-07-07
**文件**: main/bl0942/bl0942.c, main/bl0942/bl0942.h

## 🔴 高严重度

无。

## 🟡 中严重度

### BL0942-FAULT-STOP — 故障停止条件在多数配置下失效

- **文件:行号**: `main/bl0942/bl0942.c:1130-1131`（硬复位触发条件），`main/bl0942/bl0942.c:1141-1144`（停止条件）
- **问题描述**: 采样任务在连续失败达 `fault_threshold` 后尝试硬复位并发布 FAULT 事件，停止条件为：

  ```c
  if (me->hard_reset_count >= me->config.hard_reset_max_attempts &&
      me->config.hard_reset_max_attempts > 0) {
      me->sample_task_running = false;
  }
  ```

  `hard_reset_count` 仅在 `en_gpio != GPIO_NUM_NC && hard_reset_count < hard_reset_max_attempts` 分支内自增（行 1131-1133）。逐项分析四种配置组合：

  | en_gpio | max_attempts | 会硬复位？ | 会停止？ | 行为 |
  |---------|--------------|-----------|---------|------|
  | 有效    | >0（默认 3） | 是        | 是      | ✅ 正确：max 次硬复位后停止 |
  | NC      | >0           | 否        | 否      | ❌ `hard_reset_count` 恒为 0，`0 >= max` 永假，无限发布 FAULT |
  | 有效    | 0            | 否        | 否      | ❌ `0 < 0` 永假不复位；`0 >= 0 && 0 > 0` 永假不停止 |
  | NC      | 0            | 否        | 否      | ❌ 同上，无限发布 FAULT |

  四种组合中三种无法自停。当 BL0942 永久故障且无 EN 引脚（或用户显式设 `hard_reset_max_attempts=0` 禁用硬复位）时，采样任务每 `fault_threshold * sample_period_ms`（默认 10×100ms = 1s）发布一次 FAULT 事件并执行一次无意义的 UART 读，永不退出，只能靠外部 `bl0942_stop()` 终止。

  `hard_reset_max_attempts == 0` 是配置校验允许的合法值（`bl0942.c:888-890` 仅要求 `>= 0`），`apply_defaults`（行 854）也只在 `< 0` 时填默认值，因此用户显式传 0 会落入此坑。

- **建议修复**: 将停止条件与硬复位解耦——引入独立的"故障周期上限"或在 `hard_reset_max_attempts == 0` / `en_gpio == GPIO_NUM_NC` 时也设置一个故障周期上限。例如：当 `en_gpio == GPIO_NUM_NC` 时，把 `hard_reset_count` 视为已达上限直接停止；或新增 `fault_cycles` 上限判定（`fault_cycles >= hard_reset_max_attempts` 即停），使 `max_attempts` 语义统一为"故障容忍上限"而非仅"硬复位次数上限"。同时建议在 `bl0942.h` 配置字段注释中明确 `hard_reset_max_attempts == 0` 的语义。

### BL0942-MUTEX-DELAY — 硬复位期间持锁 2 秒以上

- **文件:行号**: `main/bl0942/bl0942.c:1071`（取 mutex），`main/bl0942/bl0942.c:1074`（调用 `bl0942_power_cycle`），`main/bl0942/bl0942.c:1015` 与 `:1018`（`vTaskDelay` 各 1000ms），`main/bl0942/bl0942.c:1107`（释放 mutex）
- **问题描述**: `bl0942_hard_reset` 在持有 `me->mutex` 的整个期间调用 `bl0942_power_cycle`，后者执行两次 `vTaskDelay(pdMS_TO_TICKS(1000))`（EN 拉低 1s + 拉高后等待 1s）外加 `gpio_config`/`gpio_set_level`/`uart_driver_delete`/`uart_driver_install`/`uart_param_config`/`uart_set_pin`。`me->mutex` 保护 `latest` 与硬件访问（见 classes.md §5.5），持锁 ≥ 2 秒会阻塞任何并发的 `bl0942_read()`（同步读取，行 569 取同一 mutex）或 `bl0942_get_latest()`（行 661 取同一 mutex）。

  当前 `bl0942_read`/`bl0942_get_latest` 无外部调用方（`rg` 验证 app 层只调用 `bl0942_start`/`bl0942_stop`），实时影响为理论性；但二者是公开 API，未来调用方加入后会被 2s+ 阻塞。硬复位是故障恢复路径（低频），但持锁 `vTaskDelay` 属反模式——`vTaskDelay` 本就是为了让出 CPU，持锁等待完全抵消其意义。

- **建议修复**: 重构 `bl0942_hard_reset` 使其在 `vTaskDelay` 期间不持锁。例如：持锁仅做 UART delete + 标记"复位中"，释放锁后执行 `bl0942_power_cycle`（含 vTaskDelay），再持锁做 UART install/configure。或让 `bl0942_power_cycle` 内部不要求调用方持锁（它本身不访问 `me->` 任何字段，只依赖 `en_gpio` 参数）。

### BL0942-DOC-DRIFT — classes.md 与实现不一致（无锁读取 / 内部结构体过时）

- **文件:行号**: `docs/agents/classes.md:689`（"无锁读取"）、`docs/agents/classes.md:679`（线程模型图"不加锁"）vs `main/bl0942/bl0942.c:661`（`xSemaphoreTake(me->mutex, portMAX_DELAY)`）；`docs/agents/classes.md:614-626`（内部结构体）vs `main/bl0942/bl0942.c:59-76`（实际结构体）
- **问题描述**:
  1. **`bl0942_get_latest` 锁语义**: classes.md §5.7 行 689 与 §5.6 线程模型图行 679 均称 `bl0942_get_latest()` "无锁读取"/"不加锁"，但实现 `bl0942.c:661` 取 `me->mutex` 后才拷贝 `me->latest`。实现更安全（`latest` 含 `uint64_t capture_time_us`，32 位 MCU 上非原子读，无锁会撕裂），但文档错误。reviewer 据文档可能误判存在竞态，或未来按文档"优化"为无锁而引入撕裂读。
  2. **内部结构体过时**: classes.md §5.5 行 614-626 的 `struct bl0942` 缺少实现中实际存在的生命周期管理字段：`lifecycle_mutex`、`active_ops`、`active_ops_done_sema`、`stop_in_progress`、`destroying`。这些字段是 `bl0942_destroy` 的 active-op 排空机制与并发安全的核心，文档未记录会误导后续维护者。

- **建议修复**:
  1. 将 classes.md §5.7 行 689 改为"`bl0942_get_latest()` 返回缓存的最新测量值，加锁读取（与 `bl0942_read` 共享 `mutex`，保证 `uint64_t` 时间戳不撕裂）"，同步修改 §5.6 线程模型图行 679 的"不加锁"标注。
  2. 更新 classes.md §5.5 内部结构体，补齐 `lifecycle_mutex`、`active_ops`、`active_ops_done_sema`、`stop_in_progress`、`destroying` 字段并简述 active-op 排空机制。

## 🟢 低严重度

### BL0942-DESTROY-FAULT-NOISE — destroy 期间采样任务把 enter_op 失败误计为采集失败

- **文件:行号**: `main/bl0942/bl0942.c:1118-1125`
- **问题描述**: `bl0942_destroy` 先设 `destroying=true`（行 425）再排空 active_ops、再调 `bl0942_stop_impl`。在 `stop_impl` 把 `sample_task_running` 置 false 之前，采样任务下一轮 `bl0942_read`（行 1118）会经 `bl0942_enter_op`（行 684-707）检测到 `destroying` 返回 `ESP_ERR_INVALID_STATE`。该返回值进入失败分支（行 1124），`consecutive_failures++`，若恰好跨过 `fault_threshold` 还会发布一次虚假 FAULT 事件并可能触发一次无意义的硬复位判定。实际影响小（`stop_impl` 紧随 `wait_active_ops_drained` 执行，窗口约 1 个采样周期），但语义不洁——destroy 触发的状态拒绝不应被当作硬件采集失败。

- **建议修复**: 在采样任务循环中识别"非采集错误"（如 `ESP_ERR_INVALID_STATE` 来自 `enter_op`）直接 `break` 退出，不递增失败计数。

### BL0942-TASK-COMPLEXITY — 采样任务故障处理嵌套较深

- **文件:行号**: `main/bl0942/bl0942.c:1116-1154`
- **问题描述**: `bl0942_sample_task_entry` 的失败分支嵌套 4 层（while → else → if threshold → if en_gpio → if reset_ret），圈复杂度约 12。可读性可改善。

- **建议修复**: 将"达阈值后的故障处理"抽取为 `bl0942_handle_fault_cycle(me, ret)` 静态函数，主循环只保留成功/失败二分与周期延迟。

## 无问题维度

- **A. 资源账本与乘法型分配**: UART RX buffer 为单环形 buffer（`uart_driver_install` 的 `rx_buf_size`，默认 256、最小 128），无 `count*size` 池式分配；task stack 4096 字节固定；`calloc(1, sizeof(*me))` 约 80 字节。无大块一次性分配。
- **B（部分）指针偏移前长度校验**: `bl0942_read` 在 `uart_read_bytes` 返回后先校验 `read_len == sizeof(frame)`（行 612）再进入帧头/校验和/解析，`bl0942_parse_packet` 访问的最大下标 19 < 23，无越界。`remaining = len - N` 类 uint 下溢模式不存在。
- **B（部分）失败路径内存泄漏**: `esp_event_post` 的 payload 为栈上值对象（`bl0942_measurement_t` / `bl0942_fault_info_t`），由事件循环按 `sizeof` 拷贝，post 失败仅记日志无泄漏——符合 fire-and-forget 设计语义（与 relay/metering/safety 一致，误报防范 §3）。
- **B（部分）DMA / cache 一致性**: `uart_driver_install` 未启用 DMA（flags=0），RX buffer 由 ESP-IDF UART 驱动内部管理，`frame` 为栈 buffer 经 `uart_read_bytes` 拷贝，无手动 DMA/cache 操作。
- **C（部分）死锁**: 锁获取顺序一致（`lifecycle_mutex` 短暂持有用于计数，`mutex` 用于硬件/`latest`）；`bl0942_read` 持 `mutex` 期间不调 `esp_event_post`（post 在 `bl0942_post_measurement` 中、`bl0942_read` 返回后调用）；`bl0942_wait_active_ops_drained` 的排空机制正确（`destroying=true` 后 `enter_op` 拒绝新 op，`leave_op` 在 `active_ops==0` 时 give 信号量）。
- **C（部分）中断上下文误用**: 本模块无 ISR/事件回调上下文（采样任务为普通任务），所有阻塞 API（mutex take / vTaskDelay / uart_read_bytes）均在任务上下文。
- **D（部分）UART RX 帧同步**: 每次 `bl0942_read` 发送命令前先 `uart_flush_input`（行 591），清除上一轮残留，保证帧同步不因超时残留数据污染。
- **E. 跨模块契约**: bl0942 属驱动适配层，不 include 任何上层头文件；通过 `esp_event` 发布值对象，消费者（metering_service）borrowed 拷贝；不直接操作 relay/LVGL/safety。符合 architecture.md §8.1/§8.2。
- **F. 类型与边界**: `decode_u24_le`/`decode_s24_le` 用 `uint32_t` 中间变量无溢出；checksum 累加 `uint32_t`（最大 23×255=5865）；`device_address` 校验 `<= 3` 后 `& 0x03`；`baud_rate` 枚举校验。无静默截断。
- **G（部分）命名与 section 组织**: DEFINES/TYPEDEFS/STATIC PROTOTYPES/STATIC VARIABLES/GLOBAL FUNCTIONS/STATIC FUNCTIONS section 组织符合 coding-style；`MACROS` section 为空（行 285-287）属轻微风格，不影响。
