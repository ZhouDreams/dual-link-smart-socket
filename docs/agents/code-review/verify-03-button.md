# Verification: button

## ✅ 确认的问题

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

### L3. button.h 未禁止从回调中调用 `button_register_cb(cb=NULL)`——会导致死锁

- **原报告条目**: L3（`button.h:88-92` 对照 `button.c:272-294, 316-369`）
- **验证结论**: 确认。`button_dispatch` 在调用用户回调前会先递增 `active_callbacks[event]`（`button.c:329-334`），并且只有在用户回调返回后才会在尾部递减该计数（`button.c:340-346`）。如果当前回调内部调用 `button_register_cb(me, event, NULL, NULL)` 注销同一事件，代码会进入 `button_wait_event_callbacks_drained`（`button.c:292-293`），随后在 `button_wait_event_callbacks_drained` 中循环等待 `active_callbacks[event] == 0`（`button.c:356-365`）。

  但这次计数归零必须等当前回调先返回到 `button_dispatch` 才会发生；当前回调却正阻塞在自己的 drain 等待里，形成自锁。结合 iot_button 回调运行于 esp_timer 任务这一执行上下文，report 对“从回调中注销当前事件会死锁”的判断成立。文档 note 应显式禁止这一用法。

### L4. `button_iot_adapter.c` 中 `long_press_time=0, short_press_time=0` 缺少注释说明

- **原报告条目**: L4（`button_iot_adapter.c:60-63`）
- **验证结论**: 确认。`button_iot_adapter.c` 在创建 `button_config_t` 时把 `long_press_time` 和 `short_press_time` 都设为 0，但本地没有任何注释解释该语义。对照 iot_button 实现，`TIME_TO_TICKS` 宏明确把 `0` 解释为“使用组件配置默认值”（`iot_button.c:106`）。因此这里的代码行为本身没有错误，但缺少就地说明，属于低严重度的文档/可维护性问题；report 的定性成立。

---

## ⚠️ 部分正确（需调整修复方案）

### M1. `button_destroy` 在 `button_iot_delete` 失败时泄漏 `me` 和 mutex

- **原报告条目**: M1（`button.c:258-262`）
- **调整说明**: 泄漏路径成立，但修复方案需要收敛为“部分正确”。`button_destroy` 在 `button_iot_delete` 失败时确实直接 `return ret`，未执行 `vSemaphoreDelete(me->mutex)` 和 `free(me)`，因此当前实现会泄漏 `me` 和 mutex。

  但现有证据不足以进一步断言 button 层可以在 `button_iot_delete()` 失败后直接释放 `me`。`iot_button_delete` 的失败点在 `btn->driver->del(btn->driver)` 之后立即返回（`iot_button.c:668-681`），而将 `button_dev_t` 从全局链表 `g_head_handle` 脱链并 `free(entry)` 的逻辑位于其后（`iot_button.c:683-688`）。也就是说，失败路径会留下一个“回调存储已清理、但底层对象仍挂在全局链表上”的部分销毁状态；仅凭当前证据，不能证明底层在该失败分支已完成与 button 层对象的全部隔离。

  因此，report 对“当前代码存在泄漏路径”的判断成立；但“button 层应在该分支直接 `free(me)`”这一修复建议证据不足，应先澄清/验证 `button_iot_delete` 失败时的底层契约，再决定资源回收策略。

### L1. `button_dispatch` 在 esp_timer 回调上下文中使用 `portMAX_DELAY` 获取 mutex

- **原报告条目**: L1（`button.c:323, 342`）
- **调整说明**: 验证确认 `button_dispatch` 确实在 esp_timer 任务上下文中用 `portMAX_DELAY` 获取 mutex。经核查 iot_button 源码（`iot_button.c:88` `g_button_timer_handle`、`iot_button.c:660` `esp_timer_start_periodic`），确认回调在 esp_timer 任务中执行，因此 report 对“阻塞会影响全局定时器任务”的风险判断成立。

  **但修复方案需调整**：当前 report 提出的“改为有限超时，获取失败就放弃本次 dispatch”并不合适。原因：
  1. `button_dispatch` 的第二次 take（line 342）发生在用户回调返回之后，用于递减 `active_callbacks`。如果这里改成有限超时且失败后直接放弃，`active_callbacks[event]` 可能永远不归零，后续 `button_destroy` 或 `button_register_cb(..., NULL, NULL)` 的 drain 会卡住。
  2. 如果第一次 take（line 323）也改成有限超时，语义会从“串行化后分发事件”变成“竞争时可能直接丢事件”。这未必一定错误，但已不是 report 所说的局部小修，而是需要重新评估 API/行为契约的设计变更。
  
  因此，现有证据足以否定“有限超时 + 失败即丢弃 dispatch”这一**具体修复建议**，但不足以推出“当前实现方向是唯一合理方案”。后续若修源码，应在保持 `active_callbacks` 引用计数不变式的前提下，再评估更合适的同步或调度方案；就本次验证而言，更稳妥的收敛结论是先补充注释，明确当前设计权衡。

### L2. `button_register_cb` 注册新回调时不等待在途回调 drain

- **原报告条目**: L2（`button.c:287-294`）
- **调整说明**: 验证确认代码行为：`cb!=NULL` 时直接返回，`cb==NULL` 时调用 `button_wait_event_callbacks_drained`。
  
  但将其**直接定性为缺陷**的证据不足，修复建议也需要收敛。`button_dispatch` 会在 mutex 保护下先拷贝当前的 `cb` 和 `user_ctx`，然后释放锁再执行用户回调（`button.c:327-340`）；因此，一旦旧回调已经进入 in-flight 状态，后续 `button_register_cb(me, event, new_cb, new_ctx)` 无法撤销那次已拷贝出去的旧 `user_ctx` 使用。

  这说明 report 指出的“注册路径与注销路径不对称”这一**行为事实**成立，但“因此当前实现就是缺陷”这一结论仍偏强。只有当调用方把 `register_cb(cb!=NULL)` 误当成旧 `user_ctx` 的回收屏障时，才会触发 report 描述的生命周期风险；而现有证据并未证明 button API 已承诺这种对称 drain 语义，也未证明当前代码库存在依赖该语义的调用点。

  **修复方案调整**：更稳妥的结论是收敛为文档/契约清晰度问题。优先在 `button_register_cb` 文档中显式说明“替换回调（`cb!=NULL`）时不等待在途回调完成；若旧回调的 `user_ctx` 可能被回收，调用方需自行建立同步”。是否要把替换路径也改成等待 drain，需基于期望语义和实时性取舍再单独评估。

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
- **`button_destroy` 失败路径**：⚠️ M1 — 当前实现的 `button_iot_delete` 失败分支会直返并泄漏 `me` 和 mutex；但若要修复，不能仅凭现有证据假定该分支可直接 `free(me)`。其他路径（mutex take 失败、drain 超时）虽 practically unreachable（portMAX_DELAY），但代码路径存在且返回错误码。
- **`button_register_cb` 失败路径**：✅ 完备。参数校验使用 `ESP_RETURN_ON_FALSE`，mutex take 使用 `ESP_RETURN_ON_FALSE`，无资源泄漏。
- **`button_dispatch` 失败路径**：⚠️ 需要区分两次 mutex take。第一次 take 失败时，会在调用用户回调前直接返回，因此只会丢弃本次 dispatch，不会留下 `active_callbacks` 残值；第二次 take 失败则发生在用户回调之后，会导致 `active_callbacks[event]` 无法递减，进而卡住后续 drain。后者在当前 `portMAX_DELAY` 设计下 practically unreachable，但不能笼统写成“mutex take 失败时安全返回”。

### Cross-module contract review

- ✅ button 不直接控制继电器或显示（architecture.md §8.2 第 6 条）。回调通过 `button_event_cb_t` 上报 `button_event_t`，由 app_controller 决定动作。
- ✅ iot_button 组件隔离在 `button_iot_adapter` 后面。`button.c` 不直接 include `iot_button.h` 或 `button_gpio.h`，避免 `button_event_t` / `BUTTON_EVENT_MAX` 命名冲突（两套 enum 都有 `BUTTON_EVENT_MAX`，分别在不同 .c 中使用，编译器不会冲突）。
- ✅ 不依赖 `board_pinmap`（GPIO 由 app_controller 从 pinmap 读取后注入 `button_config_t`）。
- ✅ 本地按键数据流不绕过 app_controller（architecture.md §6.3：button → app_controller → relay/display_service）。

### Residual risks

1. **iot_button 组件线程安全**：`iot_button_unregister_cb` 和 `iot_button_delete` 内部无锁，`free(cb_info)` 与 esp_timer 任务的回调分发存在 TOCTOU 竞态。button 模块的 unregister→drain→delete 顺序是最佳缓解策略，但无法完全消除 iot_button 内部的 use-after-free 风险。这是第三方组件问题，需在 iot_button 版本升级时关注。
2. **esp_timer 任务阻塞与后续修复边界**：`button_dispatch` 在 esp_timer 任务中用 `portMAX_DELAY` 获取 mutex（L1），理论上会短暂阻塞全局定时器任务；用户回调若再长时间占用相关锁，也会放大这一影响。另一方面，若后续为降低阻塞而把 dispatch 改成有限超时或 best-effort 取锁，则还必须额外保证 `active_callbacks` 的递增/递减不变式不被破坏，尤其不能让“回调已执行但计数未回收”的状态出现。button 模块至少应先在文档中明确这些约束。
3. **`button_destroy` 从未被调用**：当前代码库中 `button_destroy` 无调用方（`button_create` 在 `main.c:93` 和 `app_controller` 中调用，但 destroy 未被调用）。button 模块通常创建一次、生命周期等于系统生命周期。destroy 的失败路径（M1）在实践中不会触发；若后续决定修复，也需要先明确 `button_iot_delete` 失败分支的底层契约，不能直接假定 button 层可释放 `me`。
