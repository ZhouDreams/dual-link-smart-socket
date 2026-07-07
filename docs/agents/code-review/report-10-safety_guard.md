# Code Review: safety_guard

**日期**: 2026-07-07
**文件**: main/safety/safety_guard.c, main/safety/safety_guard.h

## 🔴 高严重度

（无）

## 🟡 中严重度

- **safety_guard.c:520-534** — `esp_event_post` 失败时安全快照被静默丢弃
  - 问题：`safety_guard_post_snapshot` 在 `esp_event_post` 返回非 `ESP_OK` 时仅记录 `ESP_LOGW`，不向上层传递错误。若事件循环队列已满，一条 `DANGER + RELAY_OFF` 快照将丢失，`app_controller` 不会收到此次断开建议。
  - 背景：handler 运行在 default event loop 任务上下文中（`esp_event_loop_run` 持有 `loop->mutex` 期间调用 handler）。handler 内调用 `esp_event_post` 向同一 default loop 投递 `SAFETY_GUARD_EVENT_SNAPSHOT`。由于 event loop 任务正忙于执行当前 handler，队列无法在本任务内排空；若队列恰好已满，10 ms 超时必然到期，post 必然失败。
  - 缓解：`persistence_samples` 机制保证持续 3 s 才触发 DANGER，下一秒的 metering snapshot 会再次评估并 post，存在重试机会。但快速上升的过流场景下，1 s 延迟仍有风险。
  - 建议修复：对 `level == DANGER` 的快照，post 失败时通过备用通道通知 `app_controller`（例如在 `me->latest` 中保留该快照并标记 `pending_delivery`，由 `app_controller` 在其他周期路径中 poll `safety_guard_get_latest`）；或在 post 失败时增大超时 / 重试一次。

- **classes.md:1438-1446 vs safety_guard.c:45-56** — 内部结构体文档漂移
  - 问题：classes.md §10.4 记录的 `struct safety_guard` 缺少 `metering_handler`、`started`、`stopping` 三个字段。这些字段实现了 start/stop 状态机，是理解并发行为的关键。reviewer 依据文档审查时会遗漏该状态机的存在。
  - 建议修复：在 classes.md §10.4 的内部结构代码块中补齐这三个字段并添加注释说明用途。

- **classes.md:1410-1422 vs safety_guard.h:191-193** — `safety_guard_get_thresholds` 未列入 classes.md 公开方法清单
  - 问题：classes.md §10.4 列出的公开方法（`create/destroy/start/stop/get_latest/set_thresholds`）不包含 `safety_guard_get_thresholds`，但该函数已在 `.h` 中声明并在 `.c` 中实现，`app_controller.c:568/581` 也实际调用了它。
  - 建议修复：在 classes.md §10.4 的公开方法列表中补充 `safety_guard_get_thresholds` 的签名。

## 🟢 低严重度

- **safety_guard.c:503** — event handler 中使用 `portMAX_DELAY` 获取互斥锁
  - 问题：handler 在 default event loop 任务中执行，`xSemaphoreTake(me->mutex, portMAX_DELAY)` 无超时阻塞。若另一任务（如 `app_controller` 调用 `set_thresholds`）持有锁，event loop 任务会被阻塞，暂停所有事件分发。所有临界区都很短且 FreeRTOS mutex 带优先级继承，实际风险低，但关键安全路径上使用有界超时更稳健。
  - 建议修复：将 `portMAX_DELAY` 替换为有限超时（如 `pdMS_TO_TICKS(100)`），超时后记录告警并跳过本次评估（下一秒重试）。

- **safety_guard.c:183** — `destroy` 返回 `stop_ret` 而非 `ESP_OK`
  - 问题：函数在 line 163 已检查 `stop_ret != ESP_OK` 时提前返回，到达 line 183 时 `stop_ret` 必为 `ESP_OK`。直接 `return ESP_OK;` 更清晰。
  - 建议修复：将 `return stop_ret;` 改为 `return ESP_OK;`。

- **classes.md:1451-1462 vs safety_guard.c:468-484** — 过流+过功率同时超限时的"胜出"选择逻辑未在文档中说明
  - 问题：classes.md §10.5 的判定逻辑展示了两条独立分支（过流 / 过功率），但实现中当两者同时超限时，选择 persistence 计数较高者作为 `event`，并以该计数判断是否进入 DANGER。文档未描述此合并行为，reviewer 可能误以为两个条件独立评估。
  - 建议修复：在 classes.md §10.5 中补充"当过流与过功率同时超限时，取 persistence 计数较高者作为报告事件"的说明。

## 无问题维度

- **A. 资源账本与乘法型分配** — 无乘法型分配。`safety_guard_t` 为单次 `calloc(1, sizeof(*me))`，结构体仅含配置 / mutex / 快照 / 计数器 / 状态标志，占用极小（< 100 字节）。`esp_event_post` 按值拷贝快照（`sizeof(safety_guard_snapshot_t)` ≈ 24 字节），无堆分配。

- **B. 内存安全与生命周期** — 无指针偏移长度校验需求。无 VLA。`esp_event_post` 失败后快照为栈变量，无泄漏。`create` 失败路径完整回滚（calloc 失败返回 NULL；mutex 失败 free(me)）。ownership 清晰：handler 中 `snapshot` 为栈变量，`me->latest` 为值拷贝，`esp_event_post` 内部复制数据。

- **C. 并发、竞态、死锁与实时性**（经 ESP-IDF 源码验证后的结论）：
  - **死锁**：handler 在释放 `me->mutex` 后才调用 `esp_event_post`（line 515 释放 → line 517 post），不存在持锁 post 的死锁风险。
  - **竞态**：`overcurrent_persistence` / `overpower_persistence` 的读改写全部在 `me->mutex` 保护下（handler 的 `evaluate_locked` 和 `set_thresholds` / `clear_rule_state_locked` 互斥）。`set_thresholds` 运行时更新阈值也在锁内完成，与 handler 无竞态。
  - **stop/destroy 与 handler 的竞态**：经查阅 ESP-IDF `esp_event.c` 源码，`esp_event_loop_run` 在调用 handler 期间持有 `loop->mutex`（recursive）。`esp_event_handler_instance_unregister` 先尝试 `xSemaphoreTake(loop->mutex, 0)`：若成功（loop 空闲）直接移除 handler；若失败（loop 正在分发）则 `xSemaphoreTakeRecursive(loop->mutex, portMAX_DELAY)` 阻塞，直到 handler 返回、loop 释放 mutex。**因此 unregister 返回后 handler 必然已执行完毕**，`destroy` 随后释放 `me` 不存在 use-after-free。
  - **实时性**：安全保护路径（metering snapshot → safety_guard → relay）为纯本地闭环，不依赖 ThingsBoard 在线状态，符合 architecture.md §6.4。

- **D. 失败路径完整性** — `calloc` 失败设 NULL 返回。`xSemaphoreCreateMutex` 失败 free(me) 返回 NULL。`esp_event_handler_instance_register` 失败释放锁返回错误。`esp_event_handler_instance_unregister` 失败时保留 handler 注册状态、不清理 `metering_handler`，`destroy` 在 `stop` 失败时直接返回不释放。`esp_event_post` 失败仅记日志（见中严重度 #1）。

- **E. 跨模块契约** — safety_guard 不直接操作继电器（仅设置 `suggested_action`，由 `app_controller` 协调执行，符合 architecture.md §3.2/§6.4）。不直接访问 ThingsBoard（符合 §8.2）。仅依赖 `metering_service` 输出类型（符合 §8.1）。`app_controller_stop_modules` 先停 safety_guard 再停 metering_service（app_controller.c:911 vs 919），消费方先于生产方停止，顺序正确。

- **F. 类型与边界** — persistence 计数为 `int`，每秒 +1，理论溢出需 ~68 年，无实际风险。`winning_count >= persistence_samples` 使用 `>=`，persistence_samples 默认 3（`apply_defaults` 保证 > 0）。无整数溢出 / 下溢风险。无静默截断。

- **G. 代码质量与一致性** — 圈复杂度：`safety_guard_evaluate_locked` ≈ 8，`safety_guard_stop` ≈ 7，均在 10-15 范围内。命名一致（`_locked` 后缀表示持锁调用）。section 组织符合 coding-style.md 模板。static 函数 Doxygen 注释放在 STATIC PROTOTYPES 区域，符合规范。
