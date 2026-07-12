# P0 Code Review 修复设计文档

## 目标

闭环本轮 code review 中已确认的 3 个 P0 问题，且只修复这些问题本身，不顺手扩展到 P1/P3：

1. `metering_service` 的 `MS-01`：`esp_event_post` 失败后 pending 电量增量未回滚，导致服务卡死。
2. `metering_service` 的 `MS-07`：非 hard-reset fault 路径未清 pending，导致后续 `prepare` 返回 `ESP_ERR_INVALID_STATE`。
3. `relay` 的 TOCTOU：状态变化事件在释放 mutex 后发布，可能导致事件乱序与错误状态传播。

完成后应满足以下结果：

1. `metering_service` 不会因 snapshot 发布失败或 fault 快照而把 energy delta 状态永久卡在 pending。
2. `relay` 事件载荷与 `me->on` 在同一临界区内保持一致。
3. 修复可通过测试和构建验证，而不改变模块对外职责。

## 背景

`docs/agents/code-review/verify-09-metering_service.md` 和 `verify-02-relay.md` 已确认这 3 个问题都是真问题，且影响系统主干：

1. `MS-01` / `MS-07` 会让 `metering_service` 停止产生后续有效增量，从而影响 `app_controller`、`safety_guard`、`lvgl_dashboard` 对 `METERING_EVENT_SNAPSHOT` 的消费。
2. `relay` TOCTOU 会让 `app_controller` 的本地缓存、ThingsBoard 上报和 `lvgl_dashboard` 显示收到乱序状态。

本轮只处理已经确认的问题，不把 `app_controller` 的安全路径时延风险（`M3`，verify 为 `⚠️`）并入当前批次。

## 范围

### 本次修改范围

1. `main/metering/metering_service.c`
2. `main/relay/relay.c`
3. 与 `metering_service` 相关的 host tests
4. 如有必要，新增最小化的 `relay` host test 支撑

### 非目标

1. 不修 `metering_service` 的 `MS-02`、`MS-03`、`MS-06` 等非本轮项。
2. 不修 `relay` 的 destroy 文档问题或 create 清理重复问题。
3. 不修改 `app_controller`、`thingsboard_client`、`safety_guard` 的行为。
4. 不同步处理文档漂移问题。

## 方案对比

### 方案 1：最小行为修复

1. `metering_service` 在 snapshot post 失败时即时回滚当前 pending token。
2. `metering_service` 在 fault 快照路径统一 reset energy delta baseline。
3. `relay` 在持锁状态下发布状态变化事件。

优点：

1. 直接命中已确认根因。
2. 改动小，回归面最小。
3. 不引入新的模块边界或状态机。

缺点：

1. 同文件里的其他已确认问题暂时保留。

### 方案 2：模块内顺手收敛

在方案 1 基础上，同批次处理 `MS-02`、`MS-03`、`MS-06` 和 relay 的低优先级问题。

优点：

1. 单模块一次清得更干净。

缺点：

1. scope 明显变大。
2. 验证矩阵增大，不符合当前只收 P0 的目标。

### 方案 3：结构性重构

重写 metering snapshot 发布/确认状态机，或给 relay 增加专门的串行事件发布路径。

优点：

1. 长期边界更清晰。

缺点：

1. 对当前问题属于过度设计。
2. 会引入额外行为变化与测试负担。

### 结论

采用方案 1。

## 设计

### 1. `metering_service`：发布失败回滚 pending

当前 `metering_on_bl0942_measurement()` 在锁内调用 `metering_energy_delta_prepare()`，生成 `energy_delta.token` 并写入 `me->latest`，随后释放锁并调用 `metering_post_snapshot()`。

问题在于：

1. `metering_post_snapshot()` 只记录 `esp_event_post()` 失败日志，不向调用方返回结果。
2. 一旦 post 失败，当前 pending token 无法被 `confirm` 或 `discard`，下次 `prepare` 会被 `have_pending` 拒绝。

修复设计：

1. 将 `metering_post_snapshot()` 改为返回 `esp_err_t`。
2. `metering_on_bl0942_measurement()` 在 post 失败后，重新获取 `me->mutex`。
3. 仅当服务仍存在且当前 pending token 仍等于本次 `energy_delta.token` 时，调用 `metering_energy_delta_discard()` 回滚本次 pending 状态。
4. 若重新拿锁失败，或 pending 已被其他路径改变，只记录日志，不做额外推断。

边界约束：

1. 只回滚当前这次 `prepare` 生成的 token，不直接重写整个 `energy_delta_state`。
2. 不在 post 失败路径里修改 `me->latest` / `me->has_latest`，避免扩散为“最新测量是否可见”的另一套语义变更。

### 2. `metering_service`：fault 路径统一清 baseline

当前 `metering_on_bl0942_fault()` 仅在 `fault_info->hard_reset_attempted == true` 时调用 `metering_energy_delta_reset_baseline()`。

问题在于：

1. fault 快照总是发 `valid=false` 且 `energy_delta_token=0`。
2. `app_controller` 不会对这种快照执行 `confirm` / `discard`。
3. 如果 fault 发生前已经有 pending，且这条 fault 快照替代了正常消费路径，则下一次有效测量可能继续被 `have_pending` 卡住。

修复设计：

1. `metering_on_bl0942_fault()` 在生成 fault snapshot 前统一调用 `metering_energy_delta_reset_baseline()`。
2. 这样无论 fault 是否伴随 hard reset，都明确表达“当前电量增量状态中断，下一次有效测量重新建立 baseline”。

设计取舍：

1. 这里优先保证状态机可恢复，不保留 fault 前未确认增量。
2. 这是与 fault snapshot `valid=false`、无 confirm/discard 消费者契约相一致的最小修复。

### 3. `relay`：事件发布移回临界区

当前 `relay_set()` / `relay_toggle()` 在持锁状态下更新 GPIO 和 `me->on`，随后先 `xSemaphoreGive()`，再调用 `relay_post_state_changed()`。

问题在于：

1. mutex 释放后，其他任务可在事件发布前再次修改 relay 状态。
2. 事件载荷使用的是旧的局部 `on` / `new_on`，不再保证与 `me->on` 一致。

修复设计：

1. 保持 `gpio_set_level()` 与 `me->on` 更新逻辑不变。
2. 若状态变化成立，则在 `xSemaphoreGive()` 之前调用 `relay_post_state_changed()`。
3. 若 `esp_event_post()` 失败，仍保持现有 fire-and-forget 语义，只记日志，不回滚 GPIO 状态。

设计依据：

1. `relay_post_state_changed()` 内部调用 `esp_event_post(..., 0)`，行为是异步入队，不会同步执行 handler。
2. 因此在临界区内调用它，不会引入“handler 回调反向取 relay mutex”的同步死锁。

## 数据流与失败路径

### `metering_service` 测量事件

修复后的流程：

1. 收到有效 BL0942 measurement。
2. 锁内 `prepare` 当前 energy delta，更新 `me->latest`。
3. 解锁后调用 `metering_post_snapshot()`。
4. 若 post 成功，保持现有流程，由 `app_controller` 后续 `confirm` 或 `discard`。
5. 若 post 失败，重新取锁并对当前 token 执行 `discard`，使下次测量可继续 `prepare`。

### `metering_service` fault 事件

修复后的流程：

1. 收到 BL0942 fault。
2. 锁内读取 `me->latest`，并立刻 `reset_baseline`。
3. 构造 `valid=false`、`energy_delta_token=0` 的 fault snapshot。
4. 解锁后正常 post fault snapshot。

### `relay` 状态变更

修复后的流程：

1. 锁内切 GPIO。
2. 成功后更新 `me->on`。
3. 若状态确实变化，则锁内 post 事件。
4. 释放锁。

## 错误处理

1. `metering_post_snapshot()` 的返回值只用于调用方决定是否回滚 pending；不改变 `esp_event_post` 的超时与队列策略。
2. `metering_service` 在 post 失败后的回滚使用现有 `metering_energy_delta_discard()`，避免手写状态机分支。
3. 若回滚时 mutex 获取失败，则保留现有错误返回/日志策略，不新增阻塞等待设计。
4. `relay_post_state_changed()` 失败时维持现有语义：继电器实际状态已生效，事件只是广播失败，不回滚硬件输出。

## 测试与验证

### TDD 策略

先写失败测试，再写实现。

### `metering_service` 测试

优先新增 host tests，覆盖：

1. snapshot post 失败后，当前 pending token 被回滚，下次 `prepare` 可继续成功。
2. fault 路径会 reset baseline，旧 pending token 失效，下一次有效测量重新建立 baseline。

若现有 `metering_service_internal` tests 无法直接覆盖 `.c` 文件中的事件 handler，则新增最小化 host harness，而不是跳过测试。

### `relay` 测试

优先检查是否能以最小成本补一个 host test，验证：

1. 状态事件发布发生在 mutex 释放之前。
2. 事件载荷与 `me->on` 一致。

如果当前 host test 基础不足以低成本稳定覆盖 FreeRTOS mutex + GPIO + `esp_event_post` 交互，则本轮允许退化为：

1. 先用失败测试锁定可测的 helper/顺序行为；
2. 至少完成 `idf` 构建验证。

### 执行验证

实现完成后按以下顺序验证：

1. 跑相关 host tests。
2. 跑 `./test/host/run_host_tests.sh`。
3. 跑 ESP-IDF 项目构建。

## 风险与取舍

### 风险 1：`metering_service` post 失败回滚与并发确认路径交错

应对：只基于 token 做条件回滚；token 不匹配则不写状态。

### 风险 2：fault 时统一 reset baseline 会丢掉未确认区间电量

这是有意取舍。fault snapshot 本身已标记 `valid=false` 且无确认消费者，本轮优先保证状态机恢复，不保留这段未确认增量。

### 风险 3：`relay` 在锁内 post 事件延长临界区

影响可接受，因为 `esp_event_post(..., 0)` 为快速入队，且 relay 临界区本身很短。

## 完成标准

当以下条件同时满足时，本轮 P0 修复设计算完成：

1. `metering_service` 的 snapshot post 失败不再导致 pending 永久卡死。
2. `metering_service` 的 fault 路径不再保留旧 pending。
3. `relay` 的状态变化事件不再存在 release-lock 后的 TOCTOU 窗口。
4. 本轮未扩展到 `app_controller M3` 或其他 P1/P3 问题。
5. 相关测试与构建验证路径在设计中明确可执行。

## 备注

根据仓库规则，本次只写设计文档，不自动创建 git commit。若后续需要提交该文档，再单独请求授权。
