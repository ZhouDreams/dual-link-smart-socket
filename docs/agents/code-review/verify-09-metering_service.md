# Verification: metering_service

## ✅ 确认的问题

### MS-01: `esp_event_post` 失败后 pending 电能增量未回滚，服务卡死
- **验证结论**: 确认。追踪调用链：
  1. `metering_on_bl0942_measurement`（行 685-690）持锁调用 `metering_energy_delta_prepare`，在 `metering_service_internal.c:102` 和 `:120` 设置 `have_pending = true`、`pending_token = T`。
  2. 行 698 释放锁，行 711 调用 `metering_post_snapshot`。
  3. `metering_post_snapshot`（行 762-769）调用 `esp_event_post`，失败时仅 `ESP_LOGW`，无回滚。
  4. 消费者 `app_controller_on_metering_snapshot`（`app_controller.c:488-509`）仅在收到事件时运行；`app_controller_publish_telemetry`（`app_controller.c:1029-1054`）在 publish 成功后调 `confirm`、失败后调 `discard`——但前提是收到了快照事件。
  5. 若 `esp_event_post` 失败，消费者无事件可处理，无法 confirm/discard。下一次 `prepare` 在 `internal.c:91-93` 因 `have_pending == true` 返回 `ESP_ERR_INVALID_STATE`，handler 在行 687-689 提前返回。
  - safety_guard、lvgl_dashboard 也订阅 `METERING_EVENT_SNAPSHOT`（`rg` 验证），同样受影响。

### MS-02: `metering_service_stop` 第二次 mutex take 失败时 `stopping` 永久为 true
- **验证结论**: 确认。`stop()` 行 363-364 使用 `ESP_RETURN_ON_FALSE(xSemaphoreTake(...))`。若 take 失败，宏直接 `return ESP_ERR_TIMEOUT`，跳过行 365-375：
  - `me->measurement_handler` / `me->fault_handler` 未清空
  - `me->started` 未置 false（行 372-373 被跳过）
  - **`me->stopping` 未置 false**（行 375 被跳过）
  - 后续 `start()` 行 229 和 `stop()` 行 326 均检查 `me->starting || me->stopping` → 返回 `ESP_ERR_INVALID_STATE`，服务永久不可用。
  - 对比 `start()` 行 269-288：使用直接 `if` 检查并手动 unregister handlers + goto `clear_starting`，正确清理 `starting` 标志。`stop()` 未遵循相同模式。
  - 虽 `portMAX_DELAY` 在正常情况下不会失败，但这是代码健壮性缺陷。

### MS-03: `metering_service_destroy` 在 `stop()` 失败时泄漏内存
- **验证结论**: 确认。行 198-201：`stop_ret = metering_service_stop(me)`，若非 `ESP_OK` 直接 `return stop_ret`，不执行行 203-207 的 `vSemaphoreDelete` 和 `free(me)`。`stop()` 可因 `starting`/`stopping` 为 true（行 326-328）返回 `ESP_ERR_INVALID_STATE`，或因 mutex take 失败返回 `ESP_ERR_TIMEOUT`。这些场景下 `destroy()` 泄漏 mutex + 对象内存。

### MS-04: 事件 handler 中 `portMAX_DELAY` 获取锁
- **验证结论**: 确认为低风险。行 671 和 729 在 esp_event handler 上下文中以 `portMAX_DELAY` 获取锁。`rg` 验证所有 mutex 持有者（`get_latest`、`reset_energy`、`confirm`、`discard`、`start`、`stop`）均为快速操作（标志检查 / 结构体拷贝 / 定点运算），无 I/O 或长循环。`stop()` 的 handler 注销在锁外执行（行 335-361）。实际死锁概率极低，但事件循环任务阻塞会影响全局事件分发。

### MS-05: 负功率处理在两条路径间不一致
- **验证结论**: 确认。`metering_convert_default` 行 513-515 将 `power_cw < 0` 钳位到 0。`metering_convert_with_config` 行 568-570 在 `watt_coeff > 0` 时直接 `(float)in->watt_raw * me->config.watt_coeff`，不钳位。self-test（行 630-632）仅验证默认路径的钳位。两条路径对负 `watt_raw`（BL0942 的 24-bit 有符号寄存器）行为不同。

### MS-06: `stop()` 部分 handler 注销失败后 `started` 仍为 true
- **验证结论**: 确认。行 339-345 / 348-360：若 `esp_event_handler_instance_unregister` 返回非 `ESP_OK`，`*_unregistered` 保持 false，`first_error` 被设置。行 372 `if (first_error == ESP_OK)` 为 false，`started` 不被置 false。但 `stopping` 被置 false（行 375），handler 指针未清空（因 `*_unregistered` 为 false）。服务处于 `started = true` + handler 部分注销的不一致状态。

### MS-07: 非 hard-reset fault 未清除 pending
- **验证结论**: 确认。行 740-742：仅 `fault_info->hard_reset_attempted` 为 true 时调 `reset_baseline`。否则 pending 保留。fault 快照 `energy_delta_token = 0`、`valid = false`（行 746-748）。消费者 `app_controller_internal_has_energy_delta_token(valid, token)` 对 `valid = false` 返回 false，不调 confirm/discard。下一次有效测量的 `prepare` 在 `internal.c:91` 因 `have_pending == true` 返回 `ESP_ERR_INVALID_STATE`。符合 classes.md §7.6 设计意图，但文档未明确 fault 场景的消费者责任。

### MS-08: `metering_energy_next_token` 不可达分支
- **验证结论**: 确认。`state_init`（`internal.c:56`）设 `next_token = 1`。`reset_baseline`（`internal.c:67-71`）保留 `next_token` 或回退到 `state_init`。`next_token++` 后若为 0 则设为 1（行 183-185）。因此 `token = state->next_token`（行 180）永远 ≥ 1，`if (token == 0)`（行 187）永远为 false。该分支为死代码。

### MS-09: `s_snapshot_log_count` 为模块级全局变量
- **验证结论**: 确认。行 152：`static uint32_t s_snapshot_log_count` 是文件级静态变量。仅在 `metering_on_bl0942_measurement`（行 701）中递增，该 handler 在事件循环任务中运行（单线程），无竞态。但多实例共享同一计数器。

## ❌ 误报

（无）

## ⚠️ 部分正确（需调整修复方案）

（无——所有发现均确认为真实问题）

## 修复记录

- N/A（review-only，无代码改动）

## 模块交付清单

### Change summary
N/A（review-only，无代码改动）

### Resource budget
- **启动 heap**: `calloc(1, sizeof(struct metering_service))` ≈ 100 字节（含 `metering_config_t` 12B + mutex 指针 4B + `metering_snapshot_t` ≈ 40B + `metering_energy_delta_state_t` ≈ 40B + 2 handler 指针 8B + 4 bool 4B）。单次分配，无乘法型膨胀。
- **运行 heap**: 无额外堆分配。所有快照在栈上构造，`esp_event_post` 内部拷贝。
- **峰值 heap**: 同启动 heap（无动态增长）。
- **Task stack**: metering_service 无独立任务。handler 在默认事件循环任务（ESP-IDF 默认 `sys_evt` task，stack 2304-4096B）上下文中运行。`metering_on_bl0942_measurement` 栈使用：局部变量 `sample`(40B) + `snapshot`(40B) + `energy_delta`(16B) + `emit`(4B) + 函数调用链 ≈ 200B，远低于 task stack。
- **Queue size**: 无自建 queue。依赖 ESP-IDF 默认事件循环 queue（默认 32 条）。
- **Buffer size**: 无自建 buffer。`metering_fixed_sample_t` 在栈上（≈ 32B）。
- **乘法型分配审查**: 无 `count * size` 模式。`METERING_ENERGY_PULSE_NWH * delta` 在 uint64_t 中计算，最大 `0xFFFFFF * 62297938 ≈ 1.045e15`，远低于 `UINT64_MAX`。

### Lifecycle / ownership notes
- `metering_service_t *`: owned by `app_controller`。`create` 分配，`destroy` 释放。
- `me->config`: 值拷贝（`metering_apply_defaults` 行 482），owned。
- `me->latest`: 值拷贝，owned。`get_latest()` 返回拷贝（borrowed by caller，调用方栈变量）。
- `me->energy_delta_state`: 内嵌结构体，owned。所有访问在 `me->mutex` 保护下。
- `me->measurement_handler` / `me->fault_handler`: `esp_event_handler_instance_t` 值，owned。`start` 注册，`stop` 注销。
- snapshot 事件 payload: 栈上构造 → `esp_event_post` 内部拷贝 → 消费者 handler 收到的是 event loop 内部拷贝的指针（borrowed from event loop，handler 返回后失效）。
- `metering_config_t *config` 参数: borrowed（`create` 时拷贝，不持有）。

### Failure-path review
- **malloc 失败**: `calloc` 失败返回 NULL（行 174-177）；mutex 创建失败时 free `me` 返回 NULL（行 182-186）。✅
- **esp_event_post 失败**: ⚠️ MS-01——仅日志，pending 状态未回滚，服务卡死。
- **esp_event_handler_instance_register 失败**: `start()` 行 244-267 正确 unregister 已注册的 handler 并 goto `clear_starting`。✅
- **esp_event_handler_instance_unregister 失败**: ⚠️ MS-06——`first_error` 记录，`started` 不置 false，但 `stopping` 清除，可重试。
- **mutex take 失败**: ⚠️ MS-02——`stop()` 第二次 take 失败时 `stopping` 永久为 true；其他函数的 take 失败仅返回错误码，无状态不一致。
- **BL0942 测量无效** (`!measurement->valid`): handler 行 668 提前返回，不发快照。✅
- **BL0942 fault**: 发 `valid=false` 快照，不停止服务。hard-reset 时 reset baseline。⚠️ MS-07——非 hard-reset fault 不清 pending。
- **`esp_event_post` payload ownership**: snapshot 在栈上，`esp_event_post` 拷贝后失败也无泄漏。✅

### Cross-module contract review
- **分层契约**: metering_service 属业务服务层，依赖 `bl0942` 事件类型（`BL0942_EVENT_BASE` + `bl0942_measurement_t` + `bl0942_fault_info_t`），通过 esp_event 消费，不直接依赖 `bl0942_t` 句柄。✅ 符合 architecture.md §8.1。
- **禁止直接控制继电器** (§8.2): `rg` 验证 `main/metering/` 下无 `relay_` 引用，不 include `relay.h`。✅
- **禁止直接操作 LVGL widget**: 不 include 任何 LVGL 头文件。✅
- **上行遥测流** (§6.1): metering_service → `METERING_EVENT_SNAPSHOT` → app_controller → thingsboard_client。metering_service 不直接调用 thingsboard_client 或 network_manager。✅
- **事件发布不持锁**: `metering_post_snapshot` 在 mutex 释放后调用（行 698 → 711，行 751 → 753）。✅ 无死锁风险。
- **消费者契约**: app_controller 在 publish 成功后 confirm、失败后 discard，fault 快照（`valid=false`）不触发 confirm/discard。但 `esp_event_post` 失败时消费者无感知。⚠️ 见 MS-01。

### Residual risks
1. **MS-01 未修复前的安全风险**: 若事件循环过载导致 `esp_event_post` 失败，metering_service 卡死，safety_guard 失去输入。上机长时间运行 + 事件循环过载场景才可能暴露。建议 safety_guard 增加超时无数据保护（系统级改进，非本模块职责）。
2. **`portMAX_DELAY` 在事件 handler 中的实时性**: 若未来有高优先级任务持锁阻塞，事件循环任务会无限等待。当前 mutex 持有者均快速，但随代码演进可能恶化。
3. **多实例共享 `s_snapshot_log_count`**: 当前仅单实例使用，若未来支持多实例需重构。
4. **CF 计数 24-bit 回绕边界**: 若两次采样间 CF 计数增量超过 `0x7FFFFF`（8388607 脉冲），`metering_energy_u24_delta` 的 mask 会给出错误结果。在 1Hz 采样 + 典型功率下不会发生，但极端场景（芯片故障后 CF 跳变）可能误报。
5. **`float` 精度**: `energy_delta_mwh = (float)mwh_thousandths / 1000.0f`，当 `mwh_thousandths` 超过 ~16M（float 精度极限）时丢失精度。正常 1Hz 采样下 `mwh_thousandths` 在千级，无风险。
