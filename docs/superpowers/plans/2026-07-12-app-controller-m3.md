# App Controller M3 Implementation Plan

> 本文件保留为实施记录；内容已按最终实现更新，不再使用原始“仅简单延后注册”的草案描述。

**Goal:** 修复 `app_controller M3`，保证 `METERING_EVENT_SNAPSHOT` 在稳态下总是先经过 `safety_guard`，再进入 `app_controller` 的 telemetry publish 路径，同时补齐启动窗口与 handoff 期间的 token 处置。

**Final Architecture:** 最终实现采用“双阶段 metering handler”而不是单一 late registration。`app_controller` 先注册 startup-only metering handler 处理控制器未 running 时的 token discard，再在 `safety_guard_start()` 所在的模块启动完成后注册 steady-state metering handler；`startup_metering_discard_by_app` 用于两者之间的 discard 归属切换与 rollback 保护。

**Tech Stack:** ESP-IDF C / FreeRTOS mutex + task shim / `esp_event` handler registry shim / host C tests via `cc` and `test/host/run_host_tests.sh`.

**关联设计文档:** `docs/superpowers/specs/2026-07-12-app-controller-m3-design.md`

**提交策略:** 仓库规则要求未经用户明确授权不得执行 `git commit`。本任务无 commit。

---

## Files

- `main/app/app_controller.c`
- `test/host/test_app_controller_event_order.c`
- `test/host/run_host_tests.sh`
- `test/support/esp_check.h`
- `test/support/esp_event.h`
- `test/support/freertos/task.h`

---

## Final Task Breakdown

### Task 1: Host Shim 补齐

- 在 `test/support/esp_check.h` 增加 `ESP_GOTO_ON_FALSE`
- 在 `test/support/esp_event.h` 增加 `esp_event_loop_handle_t` 与 `*_register_with()` / `*_unregister_with()` 声明
- 新增 `test/support/freertos/task.h`，提供 `vTaskDelay()` 最小声明

这些 shim 的目标是让 `app_controller.c` 能直接在 host test 中编译，并覆盖 start / stop / rollback / handoff 逻辑。

### Task 2: Host Test Harness 与覆盖点

- `test/host/test_app_controller_event_order.c` 直接编译 `app_controller_internal.c` 与 `app_controller.c`
- host event registry shim 同时记录注册顺序、派发顺序、注销顺序，并可注入启动窗口与注销窗口事件
- `test/host/run_host_tests.sh` 接入新测试

最终 host-test 覆盖点如下：

- steady-state handler order：验证 safety handler 先于 app handler 执行
- startup-window token discard：验证快照在 `safety_guard_start()` 期间到达时会被 discard 一次
- startup-to-steady handoff race：验证快照在 startup handler 注销过程中到达时不会漏掉 token，也不会重复消费
- late steady-state metering registration rollback：验证晚注册的 steady-state app handler 失败时，所有 handler / 模块 / 回调状态都能回滚

### Task 3: `app_controller` 最终收敛

`main/app/app_controller.c` 的最终实现点为：

- 基础事件注册只保留 safety snapshot 与 relay state changed
- 新增 `startup_metering_handler`，由 `app_controller_on_startup_metering_snapshot()` 处理启动期 metering snapshot
- 保留 `metering_handler`，由 `app_controller_on_metering_snapshot()` 处理 steady-state telemetry
- `app_controller_start()` 在模块启动前注册 startup handler，在模块启动成功后注册 steady-state handler
- steady-state handler 注册成功后，先把 `startup_metering_discard_by_app` 置为 `true`，再注销 startup handler
- `app_controller_unregister_event_handlers()` 若发现 startup handler 仍存在，会先把 `startup_metering_discard_by_app` 恢复为 `false`，再注销 steady-state handler 与 startup handler，保证 teardown/rollback handoff 不丢 token

这一步说明最终实现已经不再是“只把 app metering handler 晚注册”的简单拆分，而是显式管理 startup discard ownership。

### Task 4: 验收关注点

最终实现应满足：

1. 稳态 `METERING_EVENT_SNAPSHOT` 的 handler 顺序为 `safety_guard` 在前、`app_controller` 在后
2. 启动窗口快照不会触发 telemetry publish，但会完成一次 token discard
3. startup-to-steady handoff race 中 token 仍只被消费一次
4. late steady-state metering registration 失败时，rollback 不残留 handler、模块 started 标志或已注册回调

---

## Scope Notes

- 不引入异步 telemetry 队列或后台任务
- 不修改其他业务模块的公共 API
- 不处理 `M4/L4/L5` 等其他 review 项
- 文档的最终依据是当前实现与 host tests，而不是原始草案中的单一 late-registration 方案
