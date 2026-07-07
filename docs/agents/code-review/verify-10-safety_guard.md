# Verification: safety_guard

## ✅ 确认的问题

- **原报告条目**: 🟡 safety_guard.c:520-534 — `esp_event_post` 失败时安全快照被静默丢弃
  - 验证结论: 确认。重新阅读 `safety_guard_post_snapshot`（`:520-535`）与 handler（`:487-518`）：handler 在 `:515` 释放 `me->mutex` 后于 `:517` 调用 `post_snapshot`；`post_snapshot` 内 `esp_event_post` 失败时仅 `ESP_LOGW`（`:533`）后返回，无重试、无备用通知通道。`rg` 搜索确认 `app_controller_on_safety_snapshot`（`app_controller.c:466-486`）是唯一对 `SAFETY_GUARD_EVENT_SNAPSHOT` 做出 `relay_set` 反应的订阅者（注册于 `:703-709`）。`app_controller.c:1004` 虽调用 `safety_guard_get_latest`，但仅读取 `safety.level` / `safety.valid` 用于遥测组装（`:1015-1016`），不检查 `suggested_action`，不触发继电器动作。因此 post 失败时继电器不会在本周期断开，需等下一秒 metering snapshot 重试。缓解因素：`persistence_samples`（默认 3）要求持续超限才触发 DANGER，存在多周期重试机会。

- **原报告条目**: 🟡 classes.md:1438-1446 vs safety_guard.c:45-56 — 内部结构体文档漂移
  - 验证结论: 确认。classes.md §10.4 内部结构（`:1438-1446`）列出 7 个字段：`config / mutex / latest / has_latest / overcurrent_persistence / overpower_persistence / initialized`。实际 `safety_guard.c:45-56` 定义 10 个字段，额外包含 `metering_handler`（`:52`，`esp_event_handler_instance_t`）、`started`（`:54`，`bool`）、`stopping`（`:55`，`bool`）。这三个字段实现 start/stop 状态机，`stopping` 标志被 handler 在 `:507` 检查以实现优雅停止。

- **原报告条目**: 🟡 classes.md:1412-1423 vs safety_guard.h:191-193 — `safety_guard_get_thresholds` 未列入 classes.md 公开方法清单
  - 验证结论: 确认。classes.md §10.4 公开方法代码块（`:1412-1423`）列出 6 个方法（`create/destroy/start/stop/get_latest/set_thresholds`），不包含 `get_thresholds`。`safety_guard.h:191-193` 声明了 `safety_guard_get_thresholds`，`safety_guard.c:342-366` 实现了它，`app_controller.c:568` 和 `:581` 实际调用了它（用于 `GET_POWER_LIMIT` RPC 响应和 `SET_POWER_LIMIT` 保留过流阈值）。

- **原报告条目**: 🟢 safety_guard.c:503 — event handler 中使用 `portMAX_DELAY` 获取互斥锁
  - 验证结论: 确认为低风险。`safety_guard.c:503` 在 default event loop 任务上下文中以 `portMAX_DELAY` 获取 `me->mutex`。`rg` 验证所有 mutex 持有者（`get_latest:305`、`set_thresholds:330`、`get_thresholds:354`、`start:197`、`stop:242/269`、`destroy:167`、`handler:503`）均为快速操作（标志检查 / 结构体拷贝 / 浮点比较 / 计数器更新），无 I/O 或长循环。`stop()` 的 `esp_event_handler_instance_unregister` 在锁外执行（`:265`）。FreeRTOS mutex 带优先级继承。实际死锁概率极低，但事件循环任务阻塞会影响全局事件分发。

- **原报告条目**: 🟢 safety_guard.c:183 — `destroy` 返回 `stop_ret` 而非 `ESP_OK`
  - 验证结论: 确认。`destroy` 在 `:163` 检查 `stop_ret != ESP_OK` 时提前返回，到达 `:183` 时 `stop_ret` 必为 `ESP_OK`。`return stop_ret;` 等价于 `return ESP_OK;` 但语义不直观。

- **原报告条目**: 🟢 classes.md:1449-1462 vs safety_guard.c:468-484 — 过流+过功率同时超限时的"胜出"选择逻辑未在文档中说明
  - 验证结论: 确认。classes.md §10.5（`:1451-1462`）以两条独立分支展示过流和过功率的判定，每条独立产出 `DANGER/WARNING + event + action`。实际 `safety_guard_evaluate_locked`（`:468-484`）在两者同时超限时选择 persistence 计数较高者（`overcurrent_persistence >= overpower_persistence`）作为 `event`，并以该计数判断 DANGER。单事件快照（`safety_guard_snapshot_t.event` 为单值）决定了只能报告一个事件，此合并行为合理但未在文档中描述。

## ❌ 误报

（无）

## ⚠️ 部分正确（需调整修复方案）

（无）

## 修复记录

（review-only，无代码改动）

## 模块交付清单

- **Change summary**: N/A（review-only，无代码改动）

- **Resource budget**:
  - 启动 heap：`safety_guard_create` 分配 `sizeof(struct safety_guard)` ≈ 64 字节（10 个字段：3 float + 1 SemaphoreHandle_t + 1 snapshot(24B) + 2 int + 1 handler_ptr + 3 bool，含对齐填充）。`xSemaphoreCreateMutex` 分配 mutex 对象（FreeRTOS recursive mutex ≈ 80 字节，但此处用 `xSemaphoreCreateMutex` 即二值 mutex 带 priority inheritance ≈ 80 字节）。总计 ≈ 144 字节。
  - 运行 heap：无动态分配。`esp_event_post` 内部 `calloc` 拷贝 `sizeof(safety_guard_snapshot_t)` ≈ 24 字节，event loop 消费后 `free`。
  - 峰值 heap：≈ 168 字节（create + 1 post in-flight）。
  - task stack：handler 在 default event loop 任务栈上执行（默认 3072 字节）。handler 调用栈：`safety_guard_on_metering_snapshot` → `safety_guard_evaluate_locked` / `safety_guard_post_snapshot`，局部变量 ≈ `safety_guard_snapshot_t`(24B) + 几个 bool/int，栈占用 < 100 字节。
  - queue size：无自有 queue。使用 default event loop 的 queue（默认 32 slots，每 slot `sizeof(esp_event_post_instance_t)` + payload copy）。
  - buffer size：无 buffer。
  - 无乘法型分配。

- **Lifecycle / ownership notes**:
  - `safety_guard_t *me`：owned by `app_controller`，通过 `safety_guard_create` 创建、`safety_guard_destroy` 释放。
  - `me->latest`：owned by `safety_guard_t`，mutex 保护写（handler `:513`）与读（`get_latest:313`）。
  - handler 中的 `snapshot`（`:494`）：栈变量，值拷贝。`esp_event_post` 再次拷贝（ESP-IDF 内部 `calloc` + `memcpy`），event loop 消费后释放。无跨层 borrowed 指针。
  - `event_data`（handler 参数）：borrowed from esp_event loop，仅在 handler 执行期间有效，handler 内不保存该指针。
  - `me->config`：owned by `safety_guard_t`，`create` 时值拷贝，`set_thresholds` 原地更新。

- **Failure-path review**:
  - `calloc` 失败：返回 NULL，无资源泄漏 ✓
  - `xSemaphoreCreateMutex` 失败：`free(me)` 返回 NULL ✓
  - `esp_event_handler_instance_register` 失败：释放 mutex 返回错误码 ✓
  - `esp_event_handler_instance_unregister` 失败：保留 handler 注册状态，`metering_handler` 不清空，`started` 不重置，`stopping` 复位为 false，返回错误码；`destroy` 在 `stop` 失败时不释放 `me` ✓
  - `esp_event_post` 失败：仅记日志，快照丢失（见中严重度 #1）⚠️
  - `xSemaphoreTake` 失败（handler `:503`）：记日志跳过本次评估 ✓

- **Cross-module contract review**:
  - safety_guard 不直接操作继电器：仅设置 `suggested_action = RELAY_OFF`，由 `app_controller_on_safety_snapshot`（`app_controller.c:480-485`）调用 `relay_set(RELAY_SOURCE_SAFETY, false)` ✓（符合 architecture.md §3.2 / §6.4 / §8.2）
  - safety_guard 不直接访问 ThingsBoard：代码中无 `thingsboard` / `network` 依赖 ✓（符合 §8.2）
  - safety_guard 仅依赖 `metering_service` 输出类型（`metering_snapshot_t`、`METERING_EVENT_BASE`、`METERING_EVENT_SNAPSHOT`）✓（符合 §8.1）
  - 安全保护为本地闭环：metering → safety_guard → app_controller → relay，全程不依赖网络 ✓（符合 §6.4）
  - `app_controller_stop_modules` 停止顺序：dashboard → tb → network → safety_guard → metering → bl0942，消费方先于生产方停止 ✓

- **Residual risks**:
  - `esp_event_post` 失败导致安全快照丢失（中严重度 #1）：在事件循环队列拥塞时可能延迟继电器断开 1 s+。需上机压力测试验证队列是否会在正常运行中填满。
  - handler 中 `portMAX_DELAY` 获取 mutex（低严重度 #4）：若未来有高优先级任务长时间持有 `me->mutex`（如新增的 API），可能阻塞事件循环。当前所有持有者均为快速操作。
  - ESP-IDF `esp_event_handler_instance_unregister` 在 Path B（loop 正在分发）中调用 `esp_event_post_to(..., portMAX_DELAY)` 投递 cleanup 事件：若事件队列恰好已满且其他任务持续 post，理论上可能阻塞 unregister 调用者。实际概率极低（需队列满 + 持续 post），但属于 ESP-IDF 内部行为，safety_guard 无法控制。
