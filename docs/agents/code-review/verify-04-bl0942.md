# Verification: bl0942

## ✅ 确认的问题

### BL0942-MUTEX-DELAY — 硬复位期间持锁 2 秒以上

- **原报告条目**: `main/bl0942/bl0942.c:1071, 1074, 1015, 1018, 1107` — 持锁 vTaskDelay
- **验证结论**: 确认。重新阅读 `bl0942_hard_reset`（`:1065-1109`）与 `bl0942_power_cycle`（`:997-1022`）：`xSemaphoreTake(me->mutex, portMAX_DELAY)` 在 `:1071`，`bl0942_power_cycle(me->config.en_gpio)` 在 `:1074`，后者内部 `vTaskDelay(pdMS_TO_TICKS(BL0942_EN_LOW_DELAY_MS))`（`:1015`，1000ms）与 `vTaskDelay(pdMS_TO_TICKS(BL0942_EN_SETTLE_DELAY_MS))`（`:1018`，1000ms），`(void)xSemaphoreGive(me->mutex)` 在 `:1107`。持锁 ≥ 2s 确认。`rg` 搜索 `bl0942_read|bl0942_get_latest` 在 `main/app/` 下无调用，当前实时影响为理论性，但二者为公开 API，未来调用方会被阻塞。`bl0942_power_cycle` 仅依赖 `en_gpio` 参数、不访问 `me->` 任何字段，持锁无技术必要性。

### BL0942-DOC-DRIFT — classes.md 与实现不一致

- **原报告条目**: `docs/agents/classes.md:689, 679, 614-626` vs `main/bl0942/bl0942.c:661, 59-76`
- **验证结论**: 确认。`rg -n "无锁|不加锁" classes.md` 命中行 679（线程模型图"不加锁"）与 689（§5.7"无锁读取"）。`bl0942.c:661` `xSemaphoreTake(me->mutex, portMAX_DELAY)` 确实加锁。`bl0942.c:646-678` 全流程加锁拷贝 `me->latest`。对比 classes.md §5.5 行 614-626 的 `struct bl0942` 与 `bl0942.c:59-76` 实际结构体：实现多出 `lifecycle_mutex`、`active_ops`、`active_ops_done_sema`、`stop_in_progress`、`destroying` 五个字段，且 `sample_task_done_sema` 在文档中有但 `sample_task` 字段文档漏列类型注释。文档漂移确认。实现加锁比文档"无锁"更安全（`latest.capture_time_us` 为 `uint64_t`，32 位 MCU 无锁读会撕裂），故无需改实现，应修文档。

### BL0942-DESTROY-FAULT-NOISE — destroy 期间误计采集失败

- **原报告条目**: `main/bl0942/bl0942.c:1118-1125` — enter_op 失败被当作采集失败
- **验证结论**: 确认。重新阅读 `bl0942_destroy`（`:413-468`）：`me->destroying = true`（`:425`）后 `(void)xSemaphoreGive` 释放 `lifecycle_mutex`（`:426`），随后 `bl0942_wait_active_ops_drained`（`:428`）、`bl0942_stop_impl`（`:433`）。`bl0942_enter_op`（`:684-707`）在 `:699` 检测 `me->destroying` 为 true 时返回 `ESP_ERR_INVALID_STATE`（`:700`）且不递增 `active_ops`。`bl0942_read`（`:554-644`）在 `:556` 调 `enter_op`，失败直接返回该错误码。采样任务 `:1118` `bl0942_read` 返回非 OK 即进入 `:1124` 失败分支递增 `consecutive_failures`。窗口为 `wait_active_ops_drained` 返回到 `stop_impl` 置 `sample_task_running=false` 之间，约 1 个采样周期。影响小但语义不洁，确认。

### BL0942-TASK-COMPLEXITY — 采样任务故障处理嵌套较深

- **原报告条目**: `main/bl0942/bl0942.c:1116-1154` — 圈复杂度偏高
- **验证结论**: 确认。`bl0942_sample_task_entry`（`:1111-1166`）失败分支 `:1124-1145` 嵌套 4 层（while→else→if threshold→if en_gpio→if reset_ret），分支数约 12，处于 review-checklist §G 建议上限（≤10-15）内但偏高。该路径已值得记录为低严重度代码质量问题，后续可通过拆分故障处理分支改善可读性与维护性。

## ⚠️ 部分正确（需调整修复方案）

### BL0942-FAULT-STOP — 故障自停仅覆盖“有 EN 且达到硬复位上限”场景

- **原报告条目**: `main/bl0942/bl0942.c:1130-1131, 1141-1144` — 故障停止条件失效
- **验证结论**: 部分正确。重新阅读 `bl0942.c:1116-1154`（采样任务完整循环）、`:854-856`（`apply_defaults` 对 `hard_reset_max_attempts` 仅在 `< 0` 时填默认）、`:873-890`（校验允许 `en_gpio == GPIO_NUM_NC` 且 `hard_reset_max_attempts >= 0`）以及 `bl0942.h:49,58`（公开配置字段注释）。源码事实成立：`hard_reset_count` 只在 `bl0942.c:1130-1134` 的 `en_gpio != GPIO_NUM_NC && hard_reset_count < hard_reset_max_attempts` 分支内自增，而停止条件 `:1141-1142` 还要求 `hard_reset_count >= hard_reset_max_attempts && hard_reset_max_attempts > 0`。因此当前自停逻辑只覆盖“有 EN 且达到硬复位上限”的情形。但公开配置契约没有把 `en_gpio == GPIO_NUM_NC` 或 `hard_reset_max_attempts == 0` 的期望语义说清楚：它们究竟表示“禁用硬复位但仍应在有限容忍后停机”，还是“允许持续故障上报而不自停”，源码与头文件都未定义。更准确的判定是设计/文档缺口，而非已确认实现缺陷；只有在项目明确期望无 EN 或 `max=0` 也应有限容忍后停机时，这才构成真实缺陷。

## ❌ 误报

无。

## 修复记录

N/A（review-only，本阶段不修改源代码）。

## 模块交付清单

- **Change summary**: N/A（review-only，无代码改动）

- **Resource budget**:
  - 启动 heap：
    - `calloc(1, sizeof(struct bl0942))` ≈ 80 字节（struct 含 config + 2 mutex + 2 sema handle + latest + 计数器，约 80 字节）
    - 4 个 FreeRTOS 对象：`mutex` + `lifecycle_mutex`（各 ~80B）+ `sample_task_done_sema` + `active_ops_done_sema`（binary semaphore 各 ~80B）≈ 320 字节
    - UART 驱动 RX 环形 buffer：`rx_buf_size`（默认 256、最小 128）= 256 字节（`uart_driver_install` 内部 `calloc`）
    - 合计常驻 ≈ 650 字节
  - 运行 heap：常驻同上 + `bl0942_start` 后 sample task stack `BL0942_SAMPLE_TASK_STACK = 4096` 字节（`xTaskCreate` 从 heap 分配）
  - 峰值 heap：sample task stack 4096 字节为最大单块分配
  - task stack：`BL0942_SAMPLE_TASK_STACK = 4096`（优先级 5）
  - queue size：无自有 queue（使用 `esp_event` 默认事件循环，payload 由事件循环按 `sizeof` 拷贝）
  - buffer size：UART RX = 256（`rx_buf_size`，单环形 buffer）；`bl0942_read` 栈上 `frame[23]` + `cmd[2]` + `measurement`(~48B)
  - `count*size` 显式计算：**无乘法型分配**。`uart_driver_install(uart_num, rx_buf_size, 0, NULL, 0)` 中 `rx_buf_size` 是单个环形 buffer 大小，非 `count*size`；无 pool、无二维数组、无大 payload queue。符合维度 A 预期。

- **Lifecycle / ownership notes**:
  - `bl0942_measurement_t`：值对象。`bl0942_read` 写入栈局部 `measurement`，拷贝到 `me->latest`（owned，受 `mutex` 保护），并通过 `esp_event_post` 按 `sizeof` 拷贝到事件循环。消费者（metering_service）拿到的是事件循环 owned 的拷贝。**borrowed** 跨层无。
  - `bl0942_fault_info_t`：值对象。`bl0942_post_fault` 构造栈局部 `payload`，`esp_event_post` 拷贝。无 ownership 转移。
  - `me->latest`：owned by `bl0942_t`，`mutex` 保护写（`bl0942_read:635`）与读（`bl0942_get_latest:671`）。
  - `cmd[2]`/`frame[23]`/`measurement`：`bl0942_read` 栈局部，作用域内有效。
  - `me` 句柄：`app_controller` owned，`bl0942_destroy` 后释放。

- **Failure-path review**:
  - `calloc` 失败：返回 NULL，无半初始化对象（`:309-313`）✅
  - mutex/semaphore 创建失败：`goto err` 反序释放已创建对象 + `free(me)` + 返回 NULL（`:389-410`）✅
  - `bl0942_power_cycle` 失败（create 中）：`goto err` 清理（`:341-345`）✅
  - `uart_driver_install`/`uart_configure` 失败：`goto err`，`uart_installed` 标记决定是否 `uart_delete`（`:353-364, 390-396`）✅
  - UART 读超时：返回 `ESP_ERR_TIMEOUT`，下次 `bl0942_read` 先 `uart_flush_input`（`:591`）清残留 ✅
  - 帧头/checksum 校验失败：返回 `ESP_ERR_INVALID_RESPONSE`（`:619-632`）✅
  - `esp_event_post` 失败：仅 `ESP_LOGW`，payload 为栈值对象无泄漏（`:972-975, 992-994`）—— fire-and-forget 设计语义 ✅
  - `bl0942_hard_reset` 中 `uart_install` 失败：`goto out` 返回错误，UART 未安装；下次 `bl0942_read` `uart_is_driver_installed` 检测返回 `INVALID_STATE`（`:586-589`），触发 fault 路径再次 hard_reset 重装 ✅
  - `bl0942_hard_reset` 中 `uart_configure` 失败：先 `uart_delete` 再 `goto out`（`:1095-1101`）✅
  - `xTaskCreate` 失败：`running=false`、`sample_task=NULL`、返回 `ESP_ERR_NO_MEM`（`:524-530`）✅
  - `bl0942_stop_impl` 超时：返回 `ESP_ERR_TIMEOUT`，`stop_in_progress` 复位（`:816-820`）✅
  - `bl0942_destroy` 中 `uart_delete` 失败：give mutex、记日志、返回错误，句柄保留可重试（`:443-448`）✅
  - 无 `ESP_ERROR_CHECK`/`abort()` 在可恢复路径 ✅
  - **缺口**：公开配置契约未定义 `en_gpio == GPIO_NUM_NC` 或 `hard_reset_max_attempts == 0` 时故障是否仍应在有限容忍后自停；当前实现仅覆盖“有 EN 且达到硬复位上限”的自停路径 ⚠️

- **Cross-module contract review**:
  - bl0942 属驱动适配层（architecture.md §3.4），不 include 任何业务/网络/应用层头文件 ✅
  - 通过 `esp_event` 发布 `BL0942_EVENT_MEASUREMENT`/`BL0942_EVENT_FAULT`，消费者 metering_service 通过事件松耦合，不直接依赖 `bl0942_t` 句柄（architecture.md §8.1）✅
  - 不直接操作 relay/LVGL/safety/ThingsBoard（architecture.md §8.2 禁止项均未违反）✅
  - GPIO/UART 参数由 `app_controller` 从 `board_pinmap` 填入 `bl0942_config_t`，bl0942 不直接依赖 `board_pinmap`（classes.md §5.5 调用方模式）✅
  - 不破坏分层契约。

- **Residual risks**:
  - **故障自停语义仍待澄清**：`en_gpio == GPIO_NUM_NC` 或 `hard_reset_max_attempts == 0` 时，当前实现会持续发布 FAULT 事件而不自停。若项目期望“禁用硬复位但仍在有限容忍后停机”，则这里仍需补实现；若项目允许持续故障上报，则应补公开文档说明。默认硬件配置（en_gpio 有效、max=3）不受影响。
  - **BL0942-MUTEX-DELAY 未修复**：未来若有任务并发调用 `bl0942_read`/`bl0942_get_latest`，硬复位期间会被阻塞 2s+。当前无外部调用方。
  - **UART 帧字段字节序/偏移未对照 datasheet 核验**：`bl0942_parse_packet`（`:950-962`）中 `freq_raw` 按 big-endian 解码（`frame[17]<<8 | frame[16]`），其余 24-bit 字段按 little-endian；`status_raw` 为单字节填入 `uint16_t`（高字节为 0）；`frame[18]`/`frame[20]`/`frame[21]` 未解析。checksum 覆盖全帧保证完整性，但字段语义正确性依赖未验证的协议假设，需上机抓包或对照 `reference/` 下 BL0942 datasheet 确认。
  - **`bl0942_get_latest` 文档漂移即时影响为零**（当前无外部调用方），但 API 公开后若据文档改为无锁会引入 `uint64_t` 撕裂读。
  - **采样任务优先级 5**：与默认事件循环任务（ESP-IDF `sys_evt` 通常优先级 20）相比偏低，`esp_event_post` 的 10ms 超时在事件循环繁忙时可能丢弃测量事件——属设计权衡，当前未单列为本轮问题。
