# App Controller M3 Fix Design

## 背景

`app_controller M3` 的根因仍然是默认 `esp_event` 循环按注册顺序派发 `METERING_EVENT_SNAPSHOT`：如果 `app_controller` 的 metering handler 先于 `safety_guard` 注册，同一条快照就会先走 telemetry publish，再做本地安全判定。

原始文档把方案描述成“仅把 `app_controller` 的 steady-state metering handler 延后到 `safety_guard_start()` 之后注册”。最终代码在此基础上又补了一个启动阶段专用 metering handler，因为单纯延后注册无法覆盖启动窗口内的 energy-delta token 处置。

## 目标

- 稳态下保证 `safety_guard_on_metering_snapshot` 先于 `app_controller_on_metering_snapshot`
- 启动窗口和 startup-to-steady handoff 期间不遗留未处理的 energy-delta token
- 不改变 steady-state telemetry publish / confirm / discard 语义

## 最终方案

### 1. 双阶段 metering handler

- 基础事件注册仍只包含 `SAFETY_GUARD_EVENT_SNAPSHOT` 和 `RELAY_EVENT_STATE_CHANGED`
- `app_controller_start()` 在模块启动前额外注册一个 startup-only metering handler：`app_controller_on_startup_metering_snapshot()`
- `app_controller_start_modules()` 成功后，再注册 steady-state metering handler：`app_controller_on_metering_snapshot()`

这样稳态下同一条 `METERING_EVENT_SNAPSHOT` 的顺序变为：

1. `safety_guard_on_metering_snapshot`
2. `app_controller_on_metering_snapshot`

### 2. 为什么不是“只做简单延后注册”

只把 steady-state handler 延后到 `safety_guard_start()` 之后，确实能修正稳态顺序，但仍有两个空档：

- `metering_service` / `safety_guard` 已启动、`app_controller` 还未进入 `started` 的启动窗口
- steady-state handler 已注册、startup handler 正在注销时的 handoff 竞态窗口

这两个窗口内都可能出现 metering snapshot。最终实现因此拆成两段职责：

- startup-only handler 只负责在控制器未运行时丢弃启动窗口 token，不发布 telemetry
- late steady-state handler 只在安全模块完成订阅后接管正常 telemetry 逻辑

### 3. `startup_metering_discard_by_app` 归属切换

`startup_metering_discard_by_app` 用来明确“启动窗口 token 的 discard 责任当前属于谁”。

- `false`：startup-only handler 持有 discard 责任
- steady-state handler 注册成功后，`app_controller_start()` 先把该标志置为 `true`
- startup-only handler 看到 `true` 后不再 discard；如果此时快照正好落在 handoff 窗口，steady-state handler 会在“控制器尚未 running，但 app 已接管 discard”分支里丢弃 token
- rollback / teardown 若仍保留 startup handler，则在注销 steady-state handler 之前先把该标志恢复为 `false`，把 discard 责任交回 startup handler

这个标志的作用是避免 handoff 期间出现“两个 handler 都 discard”或“两个 handler 都不 discard”。

### 4. 生命周期摘要

`app_controller_start()` 的最终顺序为：

1. 注册按键回调
2. 注册基础事件 handlers
3. 注册 ThingsBoard command callback
4. 注册 startup-only metering handler
5. 启动下层模块，其中 `safety_guard_start()` 会注册 safety 的 metering handler
6. 注册 steady-state metering handler
7. 置 `startup_metering_discard_by_app = true`
8. 注销 startup-only handler
9. 将控制器标记为 `started`

stop / rollback 继续沿用 instance-null 门控清理，但现在需要对称处理：

- `startup_metering_handler`
- `metering_handler`
- `startup_metering_discard_by_app` 的归属恢复

## Telemetry 语义

- steady-state `app_controller_on_metering_snapshot()` 仍负责 publish telemetry
- publish 成功仍 confirm energy delta
- publish 失败仍 discard energy delta
- startup-only handler 不做 publish，只处理启动窗口 token discard

## 测试覆盖

`test/host/test_app_controller_event_order.c` 的最终覆盖点为：

- steady-state handler order：验证 safety handler 在前、app handler 在后，并验证 stop cleanup 后 handler 全部注销
- startup-window token discard：验证快照在 `safety_guard_start()` 期间到达时，只发生一次 discard，不发生 confirm
- startup-to-steady handoff race：验证快照在 startup handler 注销过程中到达时，token 仍只被消费一次
- late steady-state metering registration rollback：验证晚注册的 app metering handler 失败时，startup handler、基础 handlers、模块状态与回调注册都能完整回滚

## 非目标

- 不把 telemetry publish 改成异步队列或后台任务
- 不修改 `thingsboard_client`、`metering_service`、`safety_guard` 的公共 API
- 不顺带修复 `M4/L4/L5` 等相邻问题

## 风险边界

本轮仍未把 telemetry publish 从默认事件循环剥离；修复范围仅限于顺序保证和启动窗口 token 处置收敛。
