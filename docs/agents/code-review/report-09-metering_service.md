# Code Review: metering_service

**日期**: 2026-07-07
**文件**:
- `main/metering/metering_service.c`
- `main/metering/metering_service.h`
- `main/metering/metering_service_internal.c`
- `main/metering/metering_service_internal.h`

## 🔴 高严重度

（无）

## 🟡 中严重度

### MS-01: `esp_event_post` 失败后 pending 电能增量未回滚，服务卡死
- **文件:行号**: `main/metering/metering_service.c:700-712` + `main/metering/metering_service.c:756-769`
- **问题描述**: `metering_on_bl0942_measurement` 在持锁状态下调用 `metering_energy_delta_prepare`，设置 `have_pending = true`、`pending_token = T`，更新 `me->latest`，然后释放锁并调用 `metering_post_snapshot`。若 `esp_event_post` 因事件队列满（10ms 超时）返回失败，快照被丢弃，但 pending 状态未被回滚。消费者（`app_controller` / `safety_guard` / `lvgl_dashboard`）永远收不到该快照事件，无法调用 `confirm` 或 `discard`。下一次 `metering_energy_delta_prepare` 因 `have_pending == true` 返回 `ESP_ERR_INVALID_STATE`，measurement handler 提前返回不再发快照——服务静默卡死，safety_guard 不再收到数据。
- **影响**: 安全相关——safety_guard 失去输入数据源，过流/过功率检测失效。恢复需外部调用 `reset_energy()` 或 `get_latest()` + `discard_energy_delta()`。
- **建议修复**: `metering_post_snapshot` 失败后，重新获取锁并调用 `metering_energy_delta_discard(&me->energy_delta_state, token)` 回滚 pending 状态，使下一次采样能正常 prepare。

### MS-02: `metering_service_stop` 第二次 `ESP_RETURN_ON_FALSE` mutex take 失败时 `stopping` 标志永久为 true
- **文件:行号**: `main/metering/metering_service.c:363-364`
- **问题描述**: `stop()` 在释放锁后执行 handler 注销（行 335-361），然后用 `ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE, ESP_ERR_TIMEOUT, ...)` 重新获取锁。若 take 失败（理论上 `portMAX_DELAY` 不会失败，但若 mutex 被删除或任务异常则可能），`ESP_RETURN_ON_FALSE` 直接返回 `ESP_ERR_TIMEOUT`，跳过行 365-375 的全部清理：`me->measurement_handler` / `me->fault_handler` 未清空、`me->started` 未置 false、**`me->stopping` 未置 false**。`stopping` 永久为 true 导致后续 `start()` 和 `stop()` 均返回 `ESP_ERR_INVALID_STATE`（行 229、326），服务永久不可用。`destroy()` 也因 `stop()` 失败而无法释放内存。
- **建议修复**: 将 `ESP_RETURN_ON_FALSE` 改为直接 `if` 检查，在 take 失败的 error path 中仍清除 `me->stopping = false`（至少保证服务不被永久锁死），或参考 `start()` 行 269-288 的模式用 `if` + 手动清理。

### MS-03: `metering_service_destroy` 在 `stop()` 失败时泄漏内存
- **文件:行号**: `main/metering/metering_service.c:198-201`
- **问题描述**: `destroy()` 调用 `stop()`，若返回非 `ESP_OK`（如 `ESP_ERR_INVALID_STATE`——当 `starting` 为 true 时），`destroy()` 直接返回错误码，不执行 `vSemaphoreDelete` 和 `free(me)`，造成 mutex 和对象内存泄漏。`err.md` §3.2 要求 `destroy` 对 NULL 安全返回，但未覆盖 `stop()` 失败的场景。调用者无法简单重试（若 `starting` 永久为 true 则永远无法 destroy）。
- **建议修复**: `stop()` 失败时记录 warning 日志但继续执行清理（删除 mutex、free 对象），保证 `destroy()` 总是释放资源。或至少在 `starting`/`stopping` 为 true 时强制清除标志后重试 stop。

## 🟢 低严重度

### MS-04: 事件 handler 中 `portMAX_DELAY` 获取锁——实时性风险
- **文件:行号**: `main/metering/metering_service.c:671`（measurement handler）、`main/metering/metering_service.c:729`（fault handler）
- **问题描述**: 两个 esp_event handler 在默认事件循环任务上下文中以 `portMAX_DELAY` 获取 `me->mutex`。若另一任务持锁被阻塞（如因优先级反转或下游阻塞），事件循环任务会无限阻塞，导致所有模块的事件处理（BL0942 / relay / safety / network）全部停滞。当前 mutex 持有者均为快速操作（标志检查、结构体拷贝、定点运算），实际风险低。
- **建议修复**: 考虑使用有限超时（如 `pdMS_TO_TICKS(100)`），超时后跳过本次快照并记录 warning。

### MS-05: `metering_convert_with_config` 负功率处理在两条路径间不一致
- **文件:行号**: `main/metering/metering_service.c:568-570`
- **问题描述**: 当 `watt_coeff > 0` 时，`out->power = (float)in->watt_raw * me->config.watt_coeff`，负 `watt_raw` 产生负功率（可用于光伏反送场景）。当 `watt_coeff == 0`（默认路径）时，走 `fixed.power_cw / 100.0f`，而 `metering_convert_default` 在行 513-515 将负功率钳位到 0。两条路径对负功率行为不同，但无文档说明。self-test（行 630-632）只验证默认路径的钳位行为。
- **建议修复**: 在 `metering_config_t` 的 `watt_coeff` 文档注释中说明"非零系数路径不钳位负功率，默认公式路径钳位到 0"，或统一两条路径的钳位策略。

### MS-06: `metering_service_stop` 部分 handler 注销失败后 `started` 仍为 true
- **文件:行号**: `main/metering/metering_service.c:339-345, 372-374`
- **问题描述**: 若 `esp_event_handler_instance_unregister` 返回非 `ESP_OK`（`first_error` 被设置），行 372 的 `if (first_error == ESP_OK)` 条件不满足，`me->started` 不被置 false。但 `me->stopping` 被置 false（行 375），handler 指针也未被清空（因 `*_unregistered` 为 false）。服务处于 `started = true` 但 handler 可能已部分注销的不一致状态。恢复需重试 `stop()`。
- **建议修复**: 在注释或文档中说明部分失败后的恢复策略，或在 `first_error != ESP_OK` 时仍将 `started` 置 false 并记录哪些 handler 未注销。

### MS-07: 非 hard-reset fault 未清除 pending——设计意图需更清晰文档
- **文件:行号**: `main/metering/metering_service.c:740-742`
- **问题描述**: `metering_on_bl0942_fault` 仅在 `fault_info->hard_reset_attempted` 为 true 时调用 `metering_energy_delta_reset_baseline`。若为普通 fault（非硬复位），pending 状态保留。fault 快照 `valid = false`、`energy_delta_token = 0`，消费者无法 confirm/discard 该快照。下一次有效测量的 `prepare` 因 `have_pending == true` 返回 `ESP_ERR_INVALID_STATE`，不再发快照——直到消费者用旧 token 调用 confirm/discard 或调用 `reset_energy()`。此行为符合 classes.md §7.6 "未确认时新的电能快照不会覆盖 pending 结果"的设计意图，但 fault 场景下消费者可能丢失旧 token（被 fault 快照覆盖），导致卡死。
- **建议修复**: 在 `metering_service.h` 的 fault 行为或 classes.md 中补充说明：非 hard-reset fault 期间 pending 不会被自动清除，消费者必须保留上一个有效 token 以便 confirm/discard。

### MS-08: `metering_energy_next_token` 存在不可达分支
- **文件:行号**: `main/metering/metering_service_internal.c:187-193`
- **问题描述**: `metering_energy_next_token` 的 `if (token == 0)` 分支（行 187-193）不可达。`state_init`（行 56）设置 `next_token = 1`；`reset_baseline`（行 67-71）保留 `next_token` 或回退到 `state_init`；行 182-185 的 `if (state->next_token == 0) state->next_token = 1` 保证 `next_token` 永远 ≥ 1。因此 `token = state->next_token` 永远 ≠ 0，`if (token == 0)` 永远为 false。
- **建议修复**: 删除不可达分支，或替换为 `assert(token != 0)` 以明确不变量。

### MS-09: `s_snapshot_log_count` 为模块级全局变量而非实例级
- **文件:行号**: `main/metering/metering_service.c:152`
- **问题描述**: `static uint32_t s_snapshot_log_count` 是文件级静态变量，所有 `metering_service_t` 实例共享。若创建多个实例（非典型用法但 API 允许），日志计数会混合。该变量仅在事件循环任务中递增（单线程），无竞态。
- **建议修复**: 若需每实例独立计数，移入 `struct metering_service`。若接受共享计数，添加注释说明。

## 无问题维度

- **A. 资源账本与乘法型分配**: 无大块堆分配。`metering_service_t` 单实例 `calloc(1, sizeof(*me))` ≈ 100 字节；mutex 一个；无 queue / task / 大 buffer。`metering_energy_delta_state_t` 是内嵌结构体，无额外分配。✅
- **B. 内存安全与生命周期**（除 MS-03 外）: 指针偏移前均有长度校验；无 VLA / 大块栈分配；`esp_event_post` 的 snapshot payload 在栈上，ESP-IDF 内部拷贝，失败无泄漏；`metering_convert_default` 对 `in == NULL` 和 `!in->valid` 做了防护。✅
- **E. 跨模块契约**: `metering_service` 不 include `relay.h`，不调用任何 relay API（`rg` 验证无 `relay_` 引用）。符合 architecture.md §8.2 "metering_service 禁止直接控制继电器"。通过 esp_event 消费 BL0942 事件、发布 SNAPSHOT 事件，不直接依赖 `bl0942_t` 句柄。✅
- **F. 类型与边界**（除 MS-05 外）: CF 计数 → mWh 换算全用 `uint64_t`，最大 `delta = 0xFFFFFF * 62297938 ≈ 1.045e15`，远低于 `UINT64_MAX`；24-bit 回绕由 `metering_energy_u24_delta` 的 mask 正确处理；`watt_raw` 乘法用 `int64_t`，无溢出。✅
- **G. 代码质量**（除 MS-08 / MS-09 外）: 圈复杂度均在 15 以内；命名一致；section 组织、Doxygen 双语注释、include 风格均符合 `coding-style.md`。✅
- **H. 文档一致性**: `classes.md` §7 的 API 签名、配置结构体字段、内部结构、事件声明、关键设计决策均与实现一致。`metering_snapshot_t` 字段、`metering_config_t` 字段、`struct metering_service` 内部结构完全匹配。✅
