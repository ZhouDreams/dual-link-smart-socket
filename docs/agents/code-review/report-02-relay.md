# Code Review: relay

**日期**: 2026-07-07
**文件**: main/relay/relay.c, main/relay/relay.h

## 审查范围

relay 是驱动适配层的执行器模块，负责继电器 GPIO 控制、状态管理和状态变化事件广播。通过 opaque handle 封装，使用 mutex 保护内部状态，通过 esp_event 发布状态变化事件（含来源标签）。

对应文档：classes.md §3（Relay）、architecture.md §3.4 + §5.2（继电器控制路径）。

## 🔴 高严重度

无。

## 🟡 中严重度

- **relay.c:185 + relay.c:191-193（relay_set）/ relay.c:219 + relay.c:225（relay_toggle）** — 状态变化事件在释放 mutex 之后才发布，存在 TOCTOU 竞态窗口

  `relay_set` 和 `relay_toggle` 均在 `xSemaphoreGive(me->mutex)` 之后才调用 `relay_post_state_changed(on, source)`。如果在释放 mutex 与发布事件之间，另一任务调用了 `relay_set`/`relay_toggle` 改变了状态，则两个事件的发布顺序会与实际状态变更顺序不一致。

  具体竞态场景（已通过 `rg` 确认存在三个不同任务上下文的调用方）：
  - 任务 A（button 回调，iot_button task）：`relay_toggle(LOCAL_BUTTON)` — relay 从 ON→OFF，释放 mutex
  - 任务 B（cloud RPC handler，MQTT/event loop task）抢占：`relay_set(CLOUD, true)` — relay 从 OFF→ON，释放 mutex，发布 event(on=true, CLOUD)
  - 任务 A 恢复：发布 event(on=false, LOCAL_BUTTON)

  订阅方（`app_controller.c:531` `me->relay_on = changed->on`、`lvgl_dashboard.c:1485`）收到的事件序列为 (on=true, CLOUD) → (on=false, LOCAL_BUTTON)，最终认为 relay=OFF，但实际 relay=ON。`app_controller.c:536-538` 在 `source != CLOUD` 时会上报 ThingsBoard，导致云端收到错误的 relay=OFF 状态。

  - 建议修复：将 `relay_post_state_changed` 调用移到 `xSemaphoreGive` 之前（mutex 临界区内）。`esp_event_post` 是异步的（拷贝 event 数据到事件循环队列，不在调用方上下文执行 handler），在持锁状态下调用不会导致死锁。这样事件发布顺序与状态变更顺序严格一致。

## 🟢 低严重度

- **relay.c:119-120 + relay.c:127-128（relay_create）** — 失败路径存在重复清理代码

  `gpio_config` 失败（line 119-120）和 `gpio_set_level` 失败（line 127-128）的清理代码完全相同：`vSemaphoreDelete(me->mutex); free(me); return NULL;`。若后续在 gpio_config 与 gpio_set_level 之间新增资源分配，两处清理均需同步修改，存在维护遗漏风险。

  - 建议修复：使用 `goto cleanup` 模式集中清理（err.md §2.4 推荐模式），或在 relay_create 返回 `relay_t *` 的风格下用统一 cleanup 标签。
  - 注意：err.md §2.4 的 `ESP_GOTO_ON_*` 宏要求函数返回 `esp_err_t` 并使用 `ret` 变量。relay_create 返回指针（风格 B），无法直接套用该宏，但仍可使用手写 `goto cleanup`。

- **relay.h:99-107（relay_destroy）** — 缺少线程安全约束文档

  `relay_destroy` 的 Doxygen 注释未说明该函数不得与其他 relay 操作（set/toggle/get）并发调用。`relay_destroy`（relay.c:138-162）不获取 mutex 就直接 `vSemaphoreDelete(me->mutex)`，若另一任务正持锁执行 `relay_set`/`relay_toggle`/`relay_get`，会导致 `vSemaphoreDelete` 删除被持有的信号量，行为未定义。

  当前代码中 `relay_destroy` 尚未被任何模块调用（`rg` 搜索确认），但作为公共 API 应文档化其线程安全前提。

  - 建议修复：在 `relay.h` 的 `relay_destroy` Doxygen 注释中增加 `@note`："`destroy` 非线程安全，调用方须确保无其他任务正在调用 `set`/`toggle`/`get`。" 这是嵌入式驱动 destroy 操作的通用契约，文档化即可，无需代码改动。

## 无问题维度

- **A. 资源账本与乘法型分配**：`relay_create` 分配 `calloc(1, sizeof(struct relay))` ≈ 20 bytes + `xSemaphoreCreateMutex()` ≈ 80 bytes，总计约 100 bytes。无 `count * size` 乘法型占用，无队列、无缓冲区、无池化。`set`/`toggle`/`get` 不涉及堆操作（classes.md §3.6 设计决策）。
- **B. 内存安全与生命周期**：无指针运算、无 VLA、无缓冲区。create 失败路径按逆序清理（mutex → free）。event payload 为栈局部变量，`esp_event_post` 拷贝数据后即可安全释放。`relay_destroy` 对 NULL 安全返回 ESP_OK（err.md §3.2）。
- **D. 失败路径完整性**：calloc 失败 → 返回 NULL；mutex 创建失败 → free(me) 返回 NULL；gpio_config 失败 → 删 mutex + free 返回 NULL；gpio_set_level 失败（create 中）→ 删 mutex + free 返回 NULL；gpio_set_level 失败（set/toggle 中）→ 不更新 `me->on`，返回错误，状态与 GPIO 一致；esp_event_post 失败 → 仅日志（fire-and-forget 设计语义，与 bl0942/metering_service/safety_guard 一致）；destroy 中 gpio_set_level 失败 → 仅日志，继续清理。无 `ESP_ERROR_CHECK`/`abort()` 在可恢复路径。
- **E. 跨模块契约**：不依赖 `board_pinmap`（`rg` 确认 include 列表无 board_pinmap.h）。通过 `esp_event` 广播状态变化（architecture.md §7.1 适合事件循环的场景）。事件含 `source` 标签，订阅方可按来源过滤（如云端下发不回声，`app_controller.c:536`）。不直接操作 LVGL、网络或业务服务模块。分层契约完整。
- **F. 类型与边界**：`relay_logical_to_level` 返回 `uint32_t`，与 `gpio_set_level` 参数类型匹配。`source` 范围校验 `source >= RELAY_SOURCE_INTERNAL && source < RELAY_SOURCE_MAX`。`1ULL << (uint32_t)me->ctrl_gpio` 使用 64 位移位避免截断。`active_level` 显式校验两个有效枚举值。无整数溢出风险。
- **G. 代码质量与一致性**：`.c` 和 `.h` 遵循 `coding-style.md` section 组织。static 函数 Doxygen 注释放在 STATIC PROTOTYPES 区域（coding-style.md 要求）。include 风格：自有头文件优先 + 分组（系统 / ESP-IDF）。TAG = "relay"（err.md §6）。所有函数圈复杂度 ≤ 6。
- **H. 文档与注释一致性**：`relay_active_level_t`、`relay_source_t`、`relay_config_t`、`relay_state_changed_event_t`、`relay_t` opaque handle、`relay_event_id_t`、`ESP_EVENT_DECLARE_BASE` 均与 classes.md §3.2-§3.6 完全一致。内部 `struct relay` 字段与 classes.md §3.6 一致。5 个公开方法签名与 classes.md §3.6 一致。classes.md §3.6 关键设计决策（不依赖 board_pinmap、mutex 保护 on+GPIO、set 仅在状态变化时发事件、toggle 原子读改写、create/destroy 管资源）均与实现吻合。
