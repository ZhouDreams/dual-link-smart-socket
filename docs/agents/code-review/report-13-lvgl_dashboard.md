# Code Review: lvgl_dashboard

**日期**: 2026-07-07
**文件**:
- main/display/lvgl/lvgl_dashboard.c (1687 行)
- main/display/lvgl/lvgl_dashboard.h
- main/display/lvgl/lvgl_dashboard_internal.c
- main/display/lvgl/lvgl_dashboard_internal.h

## 🔴 高严重度

（无）

## 🟡 中严重度

### M1. tick_ctx 内存泄漏——`esp_timer_stop` 不等待 in-flight callback

- **lvgl_dashboard.c:830-837** — `lvgl_dashboard_release_resources()` 中 `me->tick_ctx` 被置 NULL 但从未 `free()`，每次 create→destroy 周期泄漏 `sizeof(lvgl_dashboard_tick_ctx_t)`（约 16-20 字节）。
- **根因**：代码使用 `esp_timer_stop()`（非阻塞版本），该函数仅解除定时器武装，不保证已派发但尚未进入 `lvgl_dashboard_tick_ctx_begin()` 的回调已完成。`wait_for_tick_callbacks()` 依赖 `active_callbacks` 计数器，而计数器在回调进入 `begin` 时才递增——存在"回调已派发但未进入 begin"的窗口，此时 `active_callbacks == 0`，drain 立即返回，释放 ctx 将导致 UAF。
- **代码注释**（lvgl_dashboard.c:832-835）已正确识别此问题并选择泄漏作为 workaround。
- **建议修复**：使用 `esp_timer_stop_blocking(timer, portMAX_DELAY)` 替代 `esp_timer_stop()` + 手动 drain。该 API 在 ESP-IDF 5.x 中可用，保证返回后无回调运行，随后可安全 `free(tick_ctx)`。若 ESP-IDF 版本不支持 `stop_blocking`，则在 `esp_timer_delete()` 之后增加足够长的 `vTaskDelay` 作为保守 drain，然后释放 ctx。

### M2. esp_event handler 中使用 `portMAX_DELAY` 阻塞取锁

- **lvgl_dashboard.c:1457, 1491, 1519** — `lvgl_dashboard_on_metering_snapshot`、`lvgl_dashboard_on_relay_state_changed`、`lvgl_dashboard_on_safety_snapshot` 三个事件处理器均在 esp_event 任务上下文中以 `xSemaphoreTake(me->mutex, portMAX_DELAY)` 无限等待。
- **风险**：esp_event 默认使用单个事件处理任务（`sys_evt`），所有 `ESP_EVENT_ANY_BASE` 的事件共享该任务。若 LVGL task 恰好持有 mutex（虽然持锁时间极短——仅拷贝 `state_cache`），事件处理器会阻塞，进而阻塞整个 esp_event loop，使其他事件消费者（thingsboard_client、safety_guard 等）被饿死。
- **实际影响**：LVGL task 持锁时间极短（仅 `state = me->state_cache` 结构体拷贝），正常情况下阻塞时间可忽略。但在极端情况（如高优先级任务抢占 LVGL task 恰在持锁期间）可能放大延迟。
- **建议修复**：将事件处理器中的 `portMAX_DELAY` 改为有限超时（如 `pdMS_TO_TICKS(100)`），超时则跳过本次状态更新并记录 `ESP_LOGW`。状态快照是周期性的，跳过一次不影响最终一致性。

### M3. classes.md §12.4 内部结构体文档漂移

- **classes.md:1638-1658** vs **lvgl_dashboard.c:100-139** — classes.md 记录的 `struct lvgl_dashboard` 与实际实现严重不符：
  - 文档缺少 `flush_lock`、`tick_timer`、`tick_ctx`、`draw_buf_1/2`、`screen`、`network_pill`、`relay_pill`、`power_card`、`voltage_card`、`current_card`、`metering_handler`、`relay_handler`、`safety_handler`、`rendered_state`、`pending_flushes`、`rendered_stale`、`has_rendered_state`、`lvgl_task_running`、`destroying`、`lvgl_initialized_by_me`、`flush_cb_registered`、`tick_timer_started` 等字段。
  - 文档中有 `bool screen_enabled` 直接字段，实际代码中 `screen_enabled` 在 `state_cache.screen_enabled`。
  - 文档使用 `lv_disp_t *display`（LVGL v8 命名），实际代码使用 `lv_display_t *display`（LVGL v9 命名）。
- **影响**：reviewer 据文档误判代码结构，阻碍 review 有效性。
- **建议修复**：更新 classes.md §12.4 内部结构体以反映实际实现，或标注"简化表示，详见源码"。

### M4. architecture.md 仍引用不存在的 display_service 中间层

- **architecture.md:531-533**（§5.5）和 **architecture.md:641-655**（§6.5）描述数据流为 `display_service → lvgl_dashboard → tft_panel`，且 **architecture.md:223-236**（§3.2）列出 `display_service` 为业务服务层模块。
- **实际**：`display_service` 在 `main/` 下不存在（`rg "display_service" main/` 零命中）。classes.md §12.6 明确写道"直接订阅业务事件的 esp_event，不需要 `display_service` 中间层"。lvgl_dashboard 实现直接订阅 `METERING_EVENT_SNAPSHOT`、`RELAY_EVENT_STATE_CHANGED`、`SAFETY_GUARD_EVENT_SNAPSHOT`。
- **影响**：架构文档与设计文档、实现三方不一致，对新人产生严重误导。
- **建议修复**：更新 architecture.md §3.2、§5.5、§6.5，移除 display_service 相关描述，改为 lvgl_dashboard 直接订阅业务事件的架构。

## 🟢 低严重度

### L1. `set_screen_enabled` 在 mutex 之前操作背光

- **lvgl_dashboard.c:749** — `tft_panel_set_backlight(me->config.panel, enabled)` 在获取 mutex 之前调用，此时未检查 `me->destroying`。
- **实际影响**：`tft_panel_set_backlight` 有自己的 mutex（tft_panel 内部保护），线程安全。背光操作在 destroy 期间无害（panel 是借用句柄，lvgl_dashboard 不销毁 panel）。仅是逻辑顺序不一致——先改硬件再更新缓存。
- **建议修复**：将 `tft_panel_set_backlight` 移到 mutex 内 `!me->destroying` 检查之后，或保持现状但在注释中说明"背光操作先于缓存更新是可接受的"。

### L2. LVGL task 每轮轮询 `network_manager_get_status`（portMAX_DELAY）

- **lvgl_dashboard.c:1418** → **lvgl_dashboard.c:1547** — LVGL task 每 `update_period_ms`（配置为 50ms，即 20 Hz）调用 `lvgl_dashboard_poll_network`，内部调用 `network_manager_get_status`，该函数以 `portMAX_DELAY` 获取 `network_manager` 的 mutex（network_manager.c:564）。
- **风险**：当 network_manager 进行链路切换等长操作时持锁，LVGL task 会阻塞，导致显示卡顿。
- **实际影响**：network_manager 的操作通常很快，50ms 轮询频率可接受。但链路切换期间可能出现短暂显示冻结。
- **建议修复**：考虑改用事件驱动（订阅 network_manager 状态变化事件）而非轮询，或在 `network_manager_get_status` 中使用有限超时。若保持轮询，可接受当前行为。

### L3. `lv_display_delete` 前未清除 user_data

- **lvgl_dashboard.c:839-841** — `lv_display_delete(me->display)` 之前未调用 `lv_display_set_user_data(me->display, NULL)`。
- **参考**：Espressif 官方代码（`lua_lvgl_runtime.c:283`）在 `lv_display_delete` 前显式清除 user_data，防止 delete 期间 flush 回调访问已失效的上下文。
- **实际影响**：当前代码在 `destroy` 中已先停止 LVGL task 并 drain pending flushes，`lv_display_delete` 期间不会有 flush 回调触发。但清除 user_data 是防御性编程最佳实践。
- **建议修复**：在 `lv_display_delete(me->display)` 前加 `lv_display_set_user_data(me->display, NULL);`。

### L4. `state_cache.screen_enabled` 变更触发无效重渲染

- **lvgl_dashboard.c:757** + **lvgl_dashboard.c:1411** + **lvgl_dashboard.c:1423-1430** — `set_screen_enabled` 更新 `state_cache.screen_enabled`，LVGL task 拷贝 `state_cache` 后通过 `memcmp`（在 `should_apply_state` 中）检测到变化并调用 `apply_state`。但 `apply_state`（lvgl_dashboard.c:1572-1642）从不读取 `state->screen_enabled`——背光控制由 `tft_panel_set_backlight` 直接完成，与渲染无关。
- **影响**：每次切换背光都会触发一次全控件重绘（`lv_label_set_text` 等），造成不必要的 LVGL invalidation 和 flush。
- **建议修复**：将 `screen_enabled` 从 `state_cache` 中移出，作为独立字段（如 `me->screen_enabled`），或让 `should_apply_state` 在比较时忽略 `screen_enabled` 字段。

## 无问题维度

- **A. 资源账本**：draw buffer 大小 = `width * 20 * 2`（RGB565），对 172×320 面板约 6.8 KB/buffer × 2 = 13.6 KB，使用 `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL` 分配，合理。struct 分配为单次 `calloc`。无乘法型分配风险。
- **B. 内存安全**（除 M1 外）：无 VLA、无指针偏移下溢、无 payload 泄漏。事件载荷为 borrowed 指针（`const metering_snapshot_t *`），handler 中仅拷贝字段不持有指针。
- **C. 并发**（除 M2 外）：LVGL task 停止遵循推荐模式（stop 标志 → 等待 done semaphore，3s 超时）。flush_lock 使用 `portENTER_CRITICAL_SAFE`（ISR 安全）。事件 handler 只做 `lock → 更新 state_cache → unlock`，不操作 widget。widget 操作仅在 LVGL task 内。
- **D. 失败路径**：`create` 失败走 `err:` → `cleanup_create_failure` → `release_resources`，反序释放。`start` 失败有完整的 tick timer 停止 + handler 注销 + 状态回滚。`destroy` 在 `stop`/`wait_for_pending_flushes` 失败时提前返回不释放资源。
- **E. 跨模块契约**（除 M4 外）：lvgl_dashboard 在驱动适配层，不直接操作 relay/safety/metering，只通过 esp_event 消费数据。非 LVGL 任务不操作 widget。`tft_panel` 为借用句柄。`network_manager` 为借用句柄。
- **F. 类型与边界**：`draw_buf_size` 计算正确使用 `size_t` 强制转换防溢出。面板尺寸有 `<= 0` 检查。`snprintf` 返回值正确检查。
- **G. 代码质量**：section 组织符合 coding-style.md（INCLUDES → DEFINES → TYPEDEFS → STATIC PROTOTYPES → ... → GLOBAL FUNCTIONS → STATIC FUNCTIONS）。函数圈复杂度合理（`create_widgets` 较长但为重复性代码）。错误处理遵循 err.md（`ESP_GOTO_ON_FALSE`、`err:` 标签、`ret` 变量）。
- **H. 文档一致性**（除 M3/M4 外）：公共 API（`create`/`destroy`/`start`/`stop`/`set_screen_enabled`）与 classes.md §12.4 签名一致。`dashboard_state_t` / `dashboard_network_t` / `lvgl_dashboard_config_t` 与 classes.md 一致。Doxygen 注释完整且双语。
