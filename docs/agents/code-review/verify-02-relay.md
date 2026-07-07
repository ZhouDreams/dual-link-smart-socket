# Verification: relay

**对应报告**: report-02-relay.md

## ✅ 确认的问题

### 1. relay_set / relay_toggle 事件发布 TOCTOU 竞态

- **原报告条目**: relay.c:185 + 191-193（relay_set）/ relay.c:219 + 225（relay_toggle）— 状态变化事件在释放 mutex 之后才发布
- **验证结论**: 确认。

  **验证过程**：

  1. **重新读源码（30 行上下文）**：relay.c:164-196（relay_set）和 relay.c:198-227（relay_toggle）。确认两者均在 `xSemaphoreGive(me->mutex)` 之后才调用 `relay_post_state_changed`。事件载荷使用局部变量 `on`/`new_on`（非 `me->on`），无法反映 mutex 释放后的状态变更。

  2. **`rg` 搜索调用点**：确认三个不同任务上下文的调用方：
     - `app_controller.c:431` — `relay_toggle(RELAY_SOURCE_LOCAL_BUTTON)`，在 `app_controller_on_button_single_click`（line 422）中，button 回调运行在 **iot_button task**。
     - `app_controller.c:481` — `relay_set(RELAY_SOURCE_SAFETY, false)`，在 `app_controller_on_safety_snapshot`（line 466）中，esp_event handler 运行在 **event loop task**。
     - `app_controller.c:561` — `relay_set(RELAY_SOURCE_CLOUD, ...)`，在 `app_controller_on_tb_command`（line 545）中，ThingsBoard 回调运行在 **MQTT / event loop task**。

     三个不同任务上下文确认竞态条件可实际触发。

  3. **事件订阅方影响确认**：
     - `app_controller.c:531` — `me->relay_on = changed->on`（直接用事件载荷覆盖缓存状态）
     - `app_controller.c:536-538` — `source != CLOUD` 时上报 ThingsBoard（乱序事件会触发错误上报）
     - `lvgl_dashboard.c:1485-1486` — 订阅同一事件更新显示

  4. **修复方案可行性确认**：`esp_event_post`（relay.c:279）使用 `ticks_to_wait=0`，是异步操作——将事件数据拷贝到事件循环队列后立即返回，handler 在 event loop task 中执行。在持锁状态下调用 `esp_event_post` 不会导致死锁（handler 不在调用方上下文同步执行）。因此将 `relay_post_state_changed` 移入 mutex 临界区是安全的修复方案。

  5. **mutex 类型确认**：`xSemaphoreCreateMutex()`（relay.c:101）创建的是带优先级继承的 mutex（非二值信号量），优先级反转风险已缓解。

  6. **无遗漏的防护逻辑**：`relay_set` 的 `previous_on != on` 检查仅避免无变化时发布事件，不防止事件乱序——`previous_on` 是 mutex 内捕获的局部变量，mutex 释放后已过时。

### 2. relay_create 失败路径重复清理代码

- **原报告条目**: relay.c:119-120 + relay.c:127-128 — `vSemaphoreDelete(me->mutex); free(me);` 出现两次
- **验证结论**: 确认。

  **验证过程**：重新读 relay.c:86-136（relay_create 完整实现）。确认：
  - line 119-120：`gpio_config` 失败 → `vSemaphoreDelete(me->mutex); free(me); return NULL;`
  - line 127-128：`gpio_set_level` 失败 → `vSemaphoreDelete(me->mutex); free(me); return NULL;`

  两处清理代码完全相同。当前 create 仅 3 步资源分配（calloc → mutex → gpio_config → gpio_set_level），重复尚可管理。但若后续新增资源（如 NVS 持久化、debounce timer），手动清理的维护风险会增加。

  err.md §2.4 推荐 `ESP_GOTO_ON_*` + `err:` 标签模式，但该宏要求函数返回 `esp_err_t` 并使用 `ret` 变量。relay_create 返回 `relay_t *`（风格 B），无法直接套用宏，但仍可使用手写 `goto cleanup`。此为 🟢 低严重度风格改进建议。

### 3. relay_destroy 缺少线程安全文档

- **原报告条目**: relay.h:99-107 — Doxygen 注释未说明 destroy 不得与其他操作并发调用
- **验证结论**: 确认。

  **验证过程**：
  1. 重新读 relay.c:138-162（relay_destroy）。确认 destroy 不获取 mutex 就直接 `vSemaphoreDelete(me->mutex)`（line 157）。若另一任务正持锁执行 set/toggle/get，`vSemaphoreDelete` 删除被持有的信号量，行为未定义。
  2. `rg` 搜索确认 `relay_destroy` 当前未被任何模块调用（无外部调用点）。但作为公共 API，契约应文档化。
  3. 这是嵌入式驱动 destroy 操作的通用契约（"不要在使用中销毁"），文档化即可，无需代码改动。err.md §3.2 要求 destroy 对 NULL 安全（已满足），但未要求 destroy 线程安全——这是合理的，因为 destroy 本质上无法与并发使用安全共存。

## ❌ 误报

无。

## ⚠️ 部分正确（需调整修复方案）

无。

## 修复记录

N/A（review-only，无代码改动）。

## 模块交付清单

- **Change summary**: N/A（review-only，无代码改动）
- **Resource budget**:
  - `relay_create` 分配：
    - `calloc(1, sizeof(struct relay))` ≈ 20 bytes（gpio_num_t 4B + relay_active_level_t 4B + bool 1B + padding 3B + SemaphoreHandle_t 4B + bool 1B + padding 3B = 20 bytes）
    - `xSemaphoreCreateMutex()` ≈ 80-100 bytes（FreeRTOS mutex struct，含优先级继承）
    - 合计约 100-120 bytes 堆分配
  - 无 `count * size` 乘法型占用，无队列、无缓冲区、无池化。
  - 启动 heap: ~100-120 bytes（create 后常驻）
  - 运行 heap: ~100-120 bytes（set/toggle/get 不涉及堆操作）
  - 峰值 heap: ~100-120 bytes
  - task stack: 无内部 task（所有操作在调用方任务上下文执行）
  - queue size: 无
  - buffer size: 无
- **Lifecycle / ownership notes**:
  - `relay_create` 返回 `relay_t *` — **owned**（调用方拥有），由 `app_controller` 持有，须通过 `relay_destroy` 释放。
  - `relay_config_t` — 调用方栈上构造，值传入 `relay_create`，create 内部拷贝字段到 `me`。调用方原始 config 可释放。
  - `relay_state_changed_event_t` — `relay_post_state_changed` 中栈局部构造，`esp_event_post` **拷贝**到事件循环队列（调用方不保留 ownership），post 返回后栈变量即可销毁。订阅方通过 `event_data` 获得的是 event loop 内部拷贝的 **borrowed** 指针。
  - `board_pinmap_t` — `app_controller` 从 `board_pinmap_get()` 借用 `const *`，读取字段填入 `relay_config_t`（值拷贝），relay 模块不持有 pinmap 指针。
- **Failure-path review**:
  - `calloc` 失败 → 返回 NULL ✅
  - `xSemaphoreCreateMutex` 失败 → `free(me)` 返回 NULL ✅
  - `gpio_config` 失败 → `vSemaphoreDelete(mutex) + free(me)` 返回 NULL ✅
  - `gpio_set_level` 失败（create 中）→ `vSemaphoreDelete(mutex) + free(me)` 返回 NULL ✅
  - `gpio_set_level` 失败（set/toggle 中）→ 不更新 `me->on`，返回错误，状态与 GPIO 一致 ✅
  - `esp_event_post` 失败 → 仅 `ESP_LOGW`，fire-and-forget 语义（与 bl0942/metering_service/safety_guard 一致的设计模式）✅
  - `gpio_set_level` 失败（destroy 中）→ 仅 `ESP_LOGW`，继续清理（mutex delete + free），返回错误码 ✅
  - 无 `ESP_ERROR_CHECK` / `abort()` 在可恢复路径 ✅
- **Cross-module contract review**:
  - 不依赖 `board_pinmap`（`rg` 确认 include 列表：relay.h, stdint.h, stdlib.h, esp_check.h, esp_log.h, freertos/FreeRTOS.h, freertos/semphr.h）✅
  - 通过 `esp_event` 广播状态变化（architecture.md §7.1 适合事件循环的场景）✅
  - 事件含 `source` 标签，订阅方可按来源过滤（`app_controller.c:536` `source != CLOUD` 时才上报，避免回声）✅
  - 不直接操作 LVGL / 网络 / 业务服务模块 ✅
  - `relay_t` opaque handle（struct 定义在 .c），符合 oop-design.md §1.2 句柄模式 ✅
  - 分层契约完整，无违规 ✅
- **Residual risks**:
  - **TOCTOU 事件竞态（已报告，未修复）**：`relay_set`/`relay_toggle` 在 mutex 释放后发布事件，多任务并发调用可导致事件乱序，订阅方（app_controller、lvgl_dashboard）可能缓存错误状态并向 ThingsBoard 上报错误状态。修复方案：将 `relay_post_state_changed` 移入 mutex 临界区。
  - **destroy 非线程安全（已记录文档缺失）**：`relay_destroy` 不获取 mutex 即删除信号量，与并发 set/toggle/get 不安全。当前无调用方，但公共 API 契约应文档化。
  - **上机才可能暴露的风险**：TOCTOU 竞态需要两个任务几乎同时调用 relay 操作才能触发，概率取决于任务调度时机。在安全保护（safety_guard → relay_set SAFETY false）与本地按键（button → relay_toggle LOCAL_BUTTON）同时发生时最可能暴露。
