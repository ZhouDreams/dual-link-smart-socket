# Verification: button

## ✅ 确认的问题

### M1. `button_destroy` 在 `button_iot_delete` 失败时泄漏 `me` 和 mutex

- **原报告条目**: M1（`button.c:258-262`）
- **验证结论**: 确认。重新阅读 `button.c:229-270`，`button_destroy` 的 `button_iot_delete` 失败路径（line 259-262）：
  ```c
  ret = button_iot_delete(me->iot_button_handle);
  if (ret != ESP_OK) {
      ESP_LOGW(TAG, "delete iot button failed: %s", esp_err_to_name(ret));
      return ret;   /* ← 直接返回，未执行 vSemaphoreDelete / free(me) */
  }
  ```
  此时 `me->initialized` 已为 false（line 241）、callbacks 已清空（line 244-246）、iot_button 回调已注销（line 252 `button_unregister_iot_callbacks`）、drain 已完成（line 253 `button_wait_all_callbacks_drained` 返回 ESP_OK）。`me` 可安全释放（无在途回调），但代码直接 return 导致 `me`（~80B）和 mutex 泄漏。

  **修复方案调整**：`button_iot_delete` 失败时，应跳过 `button_iot_delete` 的清理但继续执行 `vSemaphoreDelete(me->mutex)` + `free(me)`。iot_button 对象本身泄漏是 iot_button 组件问题，button 模块无法回收。建议改为：
  ```c
  ret = button_iot_delete(me->iot_button_handle);
  if (ret != ESP_OK) {
      ESP_LOGW(TAG, "delete iot button failed: %s", esp_err_to_name(ret));
      /* iot_button 对象泄漏，但 me 可安全释放 */
  }
  vSemaphoreDelete(me->mutex);
  free(me);
  return ret;
  ```
  注意：`return ret` 在此结构下会返回 `button_iot_delete` 的错误码（非 ESP_OK），调用方知道 destroy 未完全成功。

### M2. classes.md §4.6 struct button 文档漂移

- **原报告条目**: M2（`classes.md:449-456` 对照 `button.c:37-46`）
- **验证结论**: 确认。用 `rg` 核实 `button.c:37-46` 的实际 struct 定义：
  ```c
  struct button {
      gpio_num_t input_gpio;
      button_active_level_t active_level;
      button_iot_handle_t iot_button_handle;       /* ← classes.md 写 void * */
      button_event_cb_t callbacks[BUTTON_EVENT_MAX];
      void *user_ctxs[BUTTON_EVENT_MAX];
      uint32_t active_callbacks[BUTTON_EVENT_MAX]; /* ← classes.md 未记录 */
      SemaphoreHandle_t mutex;                      /* ← classes.md 未记录 */
      bool initialized;
  };
  ```
  `button_iot_handle_t` 是 `button_iot_adapter.h:35` 定义的 `typedef void *button_iot_handle_t`，语义等价但使用了适配层 typedef（更好的隔离）。`active_callbacks` 和 `mutex` 是 drain 机制和线程安全的核心，classes.md 完全未记录，reviewer 据文档无法理解 `button_destroy` 的 drain 语义。

### M3. `button_destroy` 返回值文档不完整

- **原报告条目**: M3（`button.h:94-97` 对照 `button.c:229-270`）
- **验证结论**: 确认。`button.h:94-97` 列出 `ESP_OK` 和 `ESP_FAIL`，但 `button.c:238` 可返回 `ESP_ERR_TIMEOUT`（mutex take 失败）、`button.c:255` 可返回 `ESP_ERR_TIMEOUT`（drain 超时）、`button.c:261` 返回 `button_iot_delete` 的原始错误码（不限于 `ESP_FAIL`）。
  
  补充说明：由于使用 `portMAX_DELAY`，`ESP_ERR_TIMEOUT` 路径 practically unreachable，但代码路径存在且文档应覆盖。`button_iot_delete` 实际可返回的错误码取决于 `iot_button_delete` → `btn->driver->del()`，GPIO driver 的 `del` 通常返回 `ESP_OK` 或 `ESP_FAIL`，但文档不应假设。

---

## ⚠️ 部分正确（需调整修复方案）

### L1. `button_dispatch` 在 esp_timer 回调上下文中使用 `portMAX_DELAY` 获取 mutex

- **原报告条目**: L1（`button.c:323, 342`）
- **调整说明**: 验证确认 `button_dispatch` 确实在 esp_timer 任务上下文中用 `portMAX_DELAY` 获取 mutex。经核查 iot_button 源码（`iot_button.c:88` `g_button_timer_handle`、`iot_button.c:660` `esp_timer_start_periodic`），确认回调在 esp_timer 任务中执行。

  **但修复方案需调整**：不建议改为有限超时。原因：
  1. `button_dispatch` 的第一次 take（line 323）必须在 `me->initialized` 为 true 时递增 `active_callbacks`，否则 drain 机制失效——如果超时跳过，`button_destroy` 的 drain 会提前认为无在途回调，导致 use-after-free。
  2. 第二次 take（line 342）用于递减 `active_callbacks`，如果超时跳过，`active_callbacks` 永远不归零，`button_destroy` 的 drain 会永久阻塞。
  
  **真正的问题**是：`portMAX_DELAY` 在 esp_timer 任务中阻塞会影响全局定时器。但这是 drain 机制正确性的必要代价。建议的修复方案改为：**维持 `portMAX_DELAY` 但在注释中说明设计理由**（drain 机制要求 callback dispatch 必须成功获取 mutex 以维护引用计数不变式；临界区极短，实际阻塞时间可忽略）。

### L2. `button_register_cb` 注册新回调时不等待在途回调 drain

- **原报告条目**: L2（`button.c:287-294`）
- **调整说明**: 验证确认代码行为：`cb!=NULL` 时直接返回，`cb==NULL` 时调用 `button_wait_event_callbacks_drained`。
  
  但这**不是缺陷，而是合理的设计选择**。原因：
  1. `button_register_cb` 的典型用法是初始化时注册回调（无在途回调）或运行时替换回调（旧回调的 user_ctx 仍有效）。
  2. 如果替换回调时也等待 drain，则每次 `register_cb` 都可能阻塞 10ms+（vTaskDelay 轮询），影响调用方实时性。
  3. button.h:89-90 的 note 已说明"调用方必须在外部串行化 destroy 与 button_register_cb 以及同一句柄的其它访问"，隐式要求调用方确保旧 user_ctx 的生命周期。
  
  **修复方案调整**：不需要改代码，只需在 button.h 的 `button_register_cb` 文档中显式说明"替换回调（cb!=NULL）时不等待在途回调完成"。

---

## ❌ 误报

无。

---

## 修复记录

- **N/A** — review-only，无代码改动。

---

## 模块交付清单

### Change summary
N/A（review-only，无代码改动）。

### Resource budget

- **启动 heap**：`button_create` 分配 `struct button`（~80B：4B gpio + 4B active_level + 4B handle + 4*8B callbacks + 4*8B user_ctxs + 4*4B active_callbacks + 4B mutex + 1B initialized + padding ≈ 88B）+ FreeRTOS mutex（~80B）+ iot_button 内部 `button_dev_t`（~100B）+ cb_info 数组（4 事件 × ~24B = ~96B）。总计 ~360B，远低于 KB 级。
- **运行 heap**：无动态分配。回调 dispatch 全部在栈上操作（局部变量 cb, user_ctx）。
- **峰值 heap**：= 启动 heap（无运行时增长）。
- **task stack**：button 模块无自建 task。回调在 esp_timer 任务栈上执行（默认 4096B）。`button_dispatch` 栈占用极小（2 个指针 + mutex take 返回值）。
- **queue size**：无队列。
- **buffer size**：无 buffer。
- **count*size 计算**：`callbacks[BUTTON_EVENT_MAX]` = 4 × sizeof(function_ptr) = 4 × 4 = 16B；`user_ctxs[BUTTON_EVENT_MAX]` = 4 × 4 = 16B；`active_callbacks[BUTTON_EVENT_MAX]` = 4 × 4 = 16B。无乘法型风险。

### Lifecycle / ownership notes

- **`button_t *me`**：owned by app_controller（通过 `button_create` 创建，`button_destroy` 释放）。
- **`me->iot_button_handle`**：owned by button 模块（通过 `button_iot_create_gpio` 创建，`button_iot_delete` 释放）。
- **`me->mutex`**：owned by button 模块（`xSemaphoreCreateMutex` 创建，`vSemaphoreDelete` 释放）。
- **`user_ctx`**：borrowed — 由 app_controller 传入，button 模块不持有 ownership。`button_dispatch` 在 mutex 保护下拷贝到局部变量后释放锁，回调执行时使用拷贝值。drain 机制确保回调完成后 destroy 才释放 `me`，但 user_ctx 的生命周期由调用方保证（button.h:89-90 note）。
- **`button_config_t *config`**：borrowed — `button_create` 读取后立即使用，不保存指针。

### Failure-path review

- **`button_create` 失败路径**：✅ 完备。calloc 失败→free(NULL) 安全返回 NULL；mutex 创建失败→free(me) 返回 NULL；iot_button 创建失败→goto err 清理 mutex + free(me)；每个 register_cb 失败→goto err，err 标签执行 `button_unregister_iot_callbacks` + `button_iot_delete` + `vSemaphoreDelete` + `free`，逆序释放正确。
- **`button_destroy` 失败路径**：⚠️ M1 — `button_iot_delete` 失败时泄漏 `me` 和 mutex。其他路径（mutex take 失败、drain 超时）虽 practically unreachable（portMAX_DELAY），但代码路径存在且返回错误码。
- **`button_register_cb` 失败路径**：✅ 完备。参数校验使用 `ESP_RETURN_ON_FALSE`，mutex take 使用 `ESP_RETURN_ON_FALSE`，无资源泄漏。
- **`button_dispatch` 失败路径**：✅ mutex take 失败时安全返回（不调用回调、不递增/递减 active_callbacks）。

### Cross-module contract review

- ✅ button 不直接控制继电器或显示（architecture.md §8.2 第 6 条）。回调通过 `button_event_cb_t` 上报 `button_event_t`，由 app_controller 决定动作。
- ✅ iot_button 组件隔离在 `button_iot_adapter` 后面。`button.c` 不直接 include `iot_button.h` 或 `button_gpio.h`，避免 `button_event_t` / `BUTTON_EVENT_MAX` 命名冲突（两套 enum 都有 `BUTTON_EVENT_MAX`，分别在不同 .c 中使用，编译器不会冲突）。
- ✅ 不依赖 `board_pinmap`（GPIO 由 app_controller 从 pinmap 读取后注入 `button_config_t`）。
- ✅ 本地按键数据流不绕过 app_controller（architecture.md §6.3：button → app_controller → relay/display_service）。

### Residual risks

1. **iot_button 组件线程安全**：`iot_button_unregister_cb` 和 `iot_button_delete` 内部无锁，`free(cb_info)` 与 esp_timer 任务的回调分发存在 TOCTOU 竞态。button 模块的 unregister→drain→delete 顺序是最佳缓解策略，但无法完全消除 iot_button 内部的 use-after-free 风险。这是第三方组件问题，需在 iot_button 版本升级时关注。
2. **esp_timer 任务阻塞**：`button_dispatch` 在 esp_timer 任务中用 `portMAX_DELAY` 获取 mutex（L1）。虽然临界区极短 + 优先级继承，但理论上 esp_timer 任务可能被短暂阻塞。用户回调（如 `app_controller_on_button_long_press` 中 `xSemaphoreTake(me->mutex, portMAX_DELAY)`）也可能阻塞 esp_timer 任务——这是调用方的责任，但 button 模块应在文档中强调回调约束。
3. **`button_destroy` 从未被调用**：当前代码库中 `button_destroy` 无调用方（`button_create` 在 `main.c:93` 和 `app_controller` 中调用，但 destroy 未被调用）。button 模块通常创建一次、生命周期等于系统生命周期。destroy 的失败路径（M1）在实践中不会触发，但仍应修复以保证正确性。
