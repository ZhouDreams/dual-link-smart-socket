# Verification: lvgl_dashboard

## ✅ 确认的问题

### M1. tick_ctx 内存泄漏

- **原报告条目**: M1 — tick_ctx 内存泄漏
- **验证结论**: 确认。
  - `lvgl_dashboard_tick_ctx_create`（lvgl_dashboard.c:1155）`calloc` 分配 ctx。
  - `lvgl_dashboard_release_resources`（lvgl_dashboard.c:830-837）将 `me->tick_ctx = NULL` 但不 `free`。
  - 代码注释（lvgl_dashboard.c:832-835）明确承认泄漏并解释原因。
  - ESP-IDF 文档确认 `esp_timer_stop()`（非 `_blocking` 版本）不等待 in-flight 回调；`esp_timer_delete()` 不 drain 回调。`wait_for_tick_callbacks` 依赖 `active_callbacks` 计数器，该计数器在回调进入 `lvgl_dashboard_tick_ctx_begin` 时才递增——存在"已派发未进入"的窗口。
  - 泄漏量小（~20 字节），create/destroy 通常仅一次，实际影响可忽略。但技术上确为泄漏。
  - **修复方向**：使用 `esp_timer_stop_blocking(timer, portMAX_DELAY)` 替代 `esp_timer_stop` + 手动 drain，然后 `free(tick_ctx)`。

### M2. esp_event handler 中 `portMAX_DELAY` 阻塞

- **原报告条目**: M2 — 事件处理器无限等待 mutex
- **验证结论**: 确认为风险，但实际影响低。
  - 三个 handler（lvgl_dashboard.c:1457, 1491, 1519）均使用 `xSemaphoreTake(me->mutex, portMAX_DELAY)`。
  - LVGL task 持锁时间极短（lvgl_dashboard.c:1409-1412：仅 `state = me->state_cache` 结构体赋值 + give），正常情况微秒级。
  - esp_event 默认任务为 `sys_evt`，所有非 ISR 事件共享。若被阻塞，其他消费者（thingsboard_client 等）确实会被延迟。
  - `xSemaphoreCreateMutex()` 在 FreeRTOS 中启用优先级继承，缓解优先级反转。
  - **结论**：设计模式可接受（项目其他模块如 metering_service、safety_guard 也用相同模式），但建议统一改为有限超时以增强鲁棒性。

### M3. classes.md §12.4 内部结构体文档漂移

- **原报告条目**: M3 — 内部 struct 文档与实现不符
- **验证结论**: 确认。
  - classes.md:1638-1658 记录的 struct 有 12 个字段。
  - lvgl_dashboard.c:100-139 实际 struct 有 39 个字段。
  - 关键差异：文档有 `bool screen_enabled` 直接字段→实际在 `state_cache.screen_enabled`；文档用 `lv_disp_t *`→实际用 `lv_display_t *`（v9 命名）；文档缺少所有 flush/tick/rendered/handler 相关字段。
  - classes.md §12.6 的"关键设计决策"与实现一致（直接订阅事件、轮询网络状态、背光控制、create/destroy 职责）。
  - **修复方向**：更新 classes.md §12.4 的内部 struct，或标注"简化表示"。

### M4. architecture.md 引用不存在的 display_service

- **原报告条目**: M4 — 架构文档引用 display_service
- **验证结论**: 确认。
  - `rg "display_service" main/` — 零命中。
  - `glob "main/display/display_service*"` — 无文件。
  - architecture.md §3.2（line 223-236）列出 display_service 为业务服务层模块。
  - architecture.md §5.5（line 531）："display_service → lvgl_dashboard → tft_panel"。
  - architecture.md §6.5（line 648-655）："display_service → lvgl_dashboard"。
  - classes.md §12.6（line 1679）："直接订阅业务事件的 esp_event，不需要 display_service 中间层"。
  - 实现遵循 classes.md，直接订阅事件。
  - **修复方向**：更新 architecture.md §3.2/§5.5/§6.5，移除 display_service 引用。

### L1. `set_screen_enabled` 背光操作在 mutex 之前

- **原报告条目**: L1 — 背光操作顺序
- **验证结论**: 确认，但影响可忽略。
  - lvgl_dashboard.c:749：`tft_panel_set_backlight` 在 mutex take 之前。
  - lvgl_dashboard.c:756-758：`destroying` 检查在 mutex 内，仅保护 `state_cache.screen_enabled` 赋值。
  - `tft_panel_set_backlight` 内部有 mutex（tft_panel.h:124 返回 `ESP_ERR_TIMEOUT`），线程安全。
  - **结论**：逻辑顺序不一致但无安全风险。

### L2. LVGL task 轮询 `network_manager_get_status`

- **原报告条目**: L2 — 高频轮询 network_manager
- **验证结论**: 确认。
  - main.c:206 配置 `update_period_ms = 50`（20 Hz）。
  - lvgl_dashboard.c:1418 每轮调用 `lvgl_dashboard_poll_network`。
  - lvgl_dashboard.c:1547 调用 `network_manager_get_status`。
  - network_manager.c:564 使用 `portMAX_DELAY` 取锁。
  - network_manager 持锁期间调用 `network_link_get_status`（两个链路），通常快速。
  - **结论**：可接受的设计，链路切换期间可能有短暂显示延迟。

### L3. `lv_display_delete` 前未清除 user_data

- **原报告条目**: L3 — 防御性编程缺失
- **验证结论**: 确认为改进建议。
  - lvgl_dashboard.c:839-841 直接 `lv_display_delete`，未先 `lv_display_set_user_data(me->display, NULL)`。
  - Espressif 官方代码 `lua_lvgl_runtime.c:283` 确实在 delete 前清除 user_data。
  - 当前代码在 delete 前已停止 LVGL task + drain flushes，无 flush 回调风险。
  - **结论**：防御性改进，非 bug。

### L4. `screen_enabled` 触发无效重渲染

- **原报告条目**: L4 — 背光切换触发不必要重绘
- **验证结论**: 确认。
  - `set_screen_enabled`（lvgl_dashboard.c:757）更新 `state_cache.screen_enabled`。
  - LVGL task（lvgl_dashboard.c:1411）拷贝 `state_cache` 到局部 `state`。
  - `should_apply_state`（lvgl_dashboard_internal.c:118）用 `memcmp` 比较整个 struct，`screen_enabled` 变化会被检测到。
  - `apply_state`（lvgl_dashboard.c:1572-1642）不读取 `state->screen_enabled`。
  - **结论**：确认存在无效重渲染，影响极小（一次额外的 `lv_label_set_text` + flush）。

## ❌ 误报

（无）

## ⚠️ 部分正确（需调整修复方案）

（无）

## 修复记录

N/A（review-only，无代码改动）

## 模块交付清单

### Change summary
N/A（review-only，无代码改动）

### Resource budget

| 资源 | 计算 | 大小 |
|------|------|------|
| `struct lvgl_dashboard` | `calloc(1, sizeof(*me))` | ~300+ 字节（39 字段，含指针+枚举+bool+uint） |
| draw_buf_1 | `width * 20 * 2` (RGB565, 20 行) | 172×20×2 = 6,880 B (DMA, internal) |
| draw_buf_2 | 同上 | 6,880 B (DMA, internal) |
| **draw buffers 合计** | 2 × 6,880 | **13,760 B** (~13.4 KB) |
| tick_ctx | `calloc(1, sizeof(*ctx))` | ~16-20 字节（portMUX + uint32×2 + bool） |
| mutex | `xSemaphoreCreateMutex()` | ~80 B (FreeRTOS mutex) |
| done_sema | `xSemaphoreCreateBinary()` | ~64 B (FreeRTOS binary semaphore) |
| tick_timer | `esp_timer_create()` | ~80 B (esp_timer handle) |
| LVGL task stack | `xTaskCreate(stack=6144)` | 6,144 B |
| **启动 heap 总估** | struct + 2×buf + sync + timer | ~14.3 KB（不含 task stack） |

- 配置传播：`lvgl_dashboard_config_t` 由 `main.c` 直接传入，无跨模块乘法型放大。
- draw_buf_lines 固定为 20（`LVGL_DASHBOARD_DRAW_BUF_LINES`），不可配置，无撑爆风险。

### Lifecycle / ownership notes

| 资源 | ownership | 说明 |
|------|-----------|------|
| `tft_panel_t *panel` | borrowed | config 传入，lvgl_dashboard 不销毁 |
| `network_manager_t *network_manager` | borrowed | config 传入，可为 NULL |
| `draw_buf_1/2` | owned | create 分配，release_resources 释放 |
| `tick_ctx` | owned | create 分配，release_resources **泄漏**（M1） |
| `tick_timer` | owned | create 创建，release_resources 停止+删除 |
| `display` | owned | create 创建，release_resources 删除 |
| `mutex` / `done_sema` | owned | create 创建，release_resources 删除 |
| event payloads（metering_snapshot_t 等）| borrowed | handler 中仅拷贝字段，不持有指针 |

### Failure-path review

| 失败点 | 处理 | 评估 |
|--------|------|------|
| `calloc(me)` 失败 | 返回 NULL | ✅ |
| `xSemaphoreCreateMutex` 失败 | `ESP_GOTO_ON_FALSE` → `err:` → cleanup | ✅ |
| `xSemaphoreCreateBinary` 失败 | 同上 | ✅ |
| `tick_ctx_create` 失败 | 同上 | ✅ |
| `lv_display_create` 失败 | `ESP_GOTO_ON_FALSE(ESP_FAIL)` → cleanup | ✅ |
| `tft_panel_register_flush_done_cb` 失败 | `ESP_GOTO_ON_ERROR` → cleanup | ✅ |
| `heap_caps_malloc(draw_buf)` 失败 | `ESP_GOTO_ON_FALSE(ESP_ERR_NO_MEM)` → cleanup | ✅ 两个 buffer 一起检查 |
| `create_widgets` 失败 | `ESP_GOTO_ON_ERROR` → cleanup | ✅ |
| `esp_timer_create` 失败 | `ESP_GOTO_ON_ERROR` → cleanup | ✅ |
| `esp_event_handler_register` 失败（start） | `goto err` → 停止 timer + 注销已注册 handler + 状态回滚 | ✅ |
| `esp_timer_start_periodic` 失败（start） | `goto err` → 回滚 accepting + 注销 handler | ✅ |
| `xTaskCreate` 失败（start） | `goto err` → 停止 timer + 注销 handler + 状态回滚 | ✅ |
| `tft_panel_draw_bitmap` 失败（flush_cb） | `lv_display_flush_ready` + `complete_flush` | ✅ |
| `tft_panel_register_flush_done_cb(NULL)` 失败（destroy） | 返回错误，不释放资源 | ✅ 防止 UAF |

### Cross-module contract review

| 契约 | 状态 | 说明 |
|------|------|------|
| 非 LVGL 任务不操作 widget | ✅ | 事件 handler 仅更新 state_cache；widget 操作仅在 LVGL task |
| lvgl_dashboard 在驱动适配层 | ✅ | 不直接操作 relay/safety/metering |
| panel/network_manager 为借用句柄 | ✅ | config 注释明确标注 |
| 事件载荷为 borrowed | ✅ | handler 中仅拷贝字段 |
| display_service 中间层 | ⚠️ M4 | architecture.md 仍引用，但 classes.md 和实现已移除 |
| 不绕过 app_controller | ✅ | lvgl_dashboard 只消费事件，不发起控制动作 |

### Residual risks

1. **tick_ctx 泄漏**（M1）：已知且有意为之，待使用 `esp_timer_stop_blocking` 修复。
2. **esp_event 阻塞**（M2）：项目级模式，影响低但非零。
3. **LVGL task 栈大小**：默认 6144 字节，main.c 配置也是 6144。LVGL 渲染（特别是字体渲染、动画）可能消耗较多栈。若未来增加复杂控件或字体，可能需要增大栈。上机验证时建议监控 `uxTaskGetStackHighWaterMark`。
4. **`lv_display_delete` 行为**：LVGL v9 的 `lv_display_delete` 是否自动清理关联的 screen 和 widgets 未在文档中明确。当前代码不显式删除 widgets，依赖 `lv_display_delete` 的内部清理。若 LVGL 版本行为变化，可能泄漏 widget 内存。
5. **面板分辨率假设**：代码假设面板为 172×320（基于 draw_buf_size 计算），但实际通过 `tft_panel_get_width/height` 动态获取。若面板分辨率大幅增大（如 320×480），draw buffer 仍为 20 行，渲染性能会下降但不会溢出。
6. **`esp_timer_stop_blocking` 可用性**：修复 M1 前需确认项目使用的 ESP-IDF 版本是否支持该 API。
