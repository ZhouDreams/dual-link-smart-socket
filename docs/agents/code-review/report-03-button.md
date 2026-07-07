# Code Review: button

**日期**: 2026-07-07
**文件**:
- `main/button/button.c`
- `main/button/button.h`
- `main/button/button_iot_adapter.c`
- `main/button/button_iot_adapter.h`

## 审查上下文

- **回调执行上下文**：iot_button 使用 `esp_timer` 周期性调度（`esp_timer_start_periodic`），所有 button 回调（`button_on_single_click` 等）在 **esp_timer 任务**中执行，不是 ISR，但 esp_timer 任务是系统级高优先级任务，阻塞会影响全局定时器。
- **drain 机制**：`button.c` 实现了 `active_callbacks[]` 引用计数 + `button_wait_*_callbacks_drained` 轮询等待，确保 `button_destroy` / `button_register_cb(cb=NULL)` 返回时无在途回调。classes.md 未记录该机制。
- **iot_button 线程安全**：`iot_button_unregister_cb` / `iot_button_delete` 内部无锁，`free(cb_info)` 与 esp_timer 任务的回调分发存在固有竞态——这是第三方组件问题，非 button 模块责任，但 button 模块的 unregister→drain→delete 顺序是其最佳缓解策略。

---

## 🔴 高严重度

无。

---

## 🟡 中严重度

### M1. `button_destroy` 在 `button_iot_delete` 失败时泄漏 `me` 和 mutex

- **文件**: `button.c:258-262`
- **问题描述**: `button_destroy` 调用 `button_iot_delete` 失败时直接 `return ret`，未执行 `vSemaphoreDelete(me->mutex)` 和 `free(me)`。此时 `me->initialized` 已为 false、callbacks 已清空、iot_button 回调已注销，但 `me` 结构体和 mutex 泄漏。调用方收到错误后无法恢复（再次调用 `button_destroy` 会重复注销已注销的回调、且 mutex 仍指向已删除状态）。
- **建议修复**: `button_iot_delete` 失败时仍应释放 `me`（回调已注销，不存在 use-after-free 风险），仅 iot_button 对象本身泄漏（属 iot_button 组件问题）。将 `return ret` 改为跳转到清理 `mutex` + `free(me)` 的标签，或直接在 `return ret` 前执行 `vSemaphoreDelete(me->mutex); free(me);`。

### M2. classes.md §4.6 struct button 文档漂移——缺少 `mutex` 和 `active_callbacks` 字段

- **文件**: `docs/agents/classes.md:449-456`（对照 `button.c:37-46`）
- **问题描述**: classes.md 记录的内部结构为：
  ```c
  struct button {
      gpio_num_t input_gpio;
      button_active_level_t active_level;
      void *iot_button_handle;
      button_event_cb_t callbacks[BUTTON_EVENT_MAX];
      void *user_ctxs[BUTTON_EVENT_MAX];
      bool initialized;
  };
  ```
  实际实现多了两个字段：`uint32_t active_callbacks[BUTTON_EVENT_MAX]`（drain 引用计数）和 `SemaphoreHandle_t mutex`（线程安全）。`iot_button_handle` 的实际类型也从 `void *` 改为 `button_iot_handle_t`（适配层 typedef）。
  
  这两个字段承载了 button 模块的核心并发设计（线程安全 + 回调 drain），reviewer 据文档无法得知 destroy/register_cb 的 drain 语义。
- **建议修复**: 在 classes.md §4.6 更新 struct 定义，补充 `active_callbacks` 和 `mutex` 字段，并增加一段说明 drain 机制的设计意图（destroy 时先注销 iot_button 回调、再等待在途回调退出、最后释放资源）。

### M3. `button_destroy` 返回值文档不完整

- **文件**: `button.h:94-97`（对照 `button.c:229-270`）
- **问题描述**: button.h 文档列出的返回值为 `ESP_OK` 和 `ESP_FAIL`，但实现还可返回：
  - `ESP_ERR_TIMEOUT`（`button.c:238` mutex 获取失败、`button.c:255` drain 等待失败——虽然 `portMAX_DELAY` 使其 practically unreachable，但代码路径存在）
  - `button_iot_delete` 返回的任意错误码（`button.c:261` 直接 `return ret`，不限于 `ESP_FAIL`）
- **建议修复**: 补充 `ESP_ERR_TIMEOUT` 返回值说明，或将 `ESP_FAIL` 改为更准确的"底层删除失败的错误码"描述。

---

## 🟢 低严重度

### L1. `button_dispatch` 在 esp_timer 回调上下文中使用 `portMAX_DELAY` 获取 mutex

- **文件**: `button.c:323`、`button.c:342`
- **问题描述**: `button_dispatch` 由 iot_button 回调（`button_on_single_click` 等）调用，运行在 esp_timer 任务上下文。该函数两次 `xSemaphoreTake(me->mutex, portMAX_DELAY)` —— 如果 mutex 被 `button_register_cb` 或 `button_destroy` 持有，esp_timer 任务会阻塞。虽然临界区极短（几次指针赋值）且 FreeRTOS mutex 支持优先级继承，但 `portMAX_DELAY` 无超时退出，阻塞 esp_timer 任务会影响全局定时器调度。
- **建议修复**: 回调路径中考虑使用有限超时（如 `pdMS_TO_TICKS(5)`），获取失败则放弃本次 dispatch（回调丢失可接受，因为按键事件会再次触发）。或维持现状但在注释中说明此设计选择的理由。

### L2. `button_register_cb` 注册新回调时不等待在途回调 drain——与注销路径不对称

- **文件**: `button.c:287-294`
- **问题描述**: `button_register_cb` 传入 `cb=NULL`（注销）时会调用 `button_wait_event_callbacks_drained` 等待在途回调完成；但传入 `cb!=NULL`（替换回调）时立即返回，不等在途回调。如果调用方替换回调后立即释放旧 `user_ctx`，在途回调可能访问已释放的内存。button.h 的 note（"调用方必须在外部串行化"）隐式覆盖了此场景，但 drain 行为的不对称可能让调用方误以为 `register_cb` 返回后旧回调已停止。
- **建议修复**: 在 button.h 的 `button_register_cb` 文档中补充说明："替换回调（cb!=NULL）时不等待在途回调完成；调用方需确保旧 user_ctx 在回调期间保持有效"。

### L3. button.h 未禁止从回调中调用 `button_register_cb(cb=NULL)`——会导致死锁

- **文件**: `button.h:88-92`（对照 `button.c:351-369`）
- **问题描述**: button.h 的 `button_destroy` note 明确禁止从回调中调用 `button_destroy`。但未提及从回调中调用 `button_register_cb(me, event, NULL, NULL)` 注销**同一事件**的回调也会死锁：`button_wait_event_callbacks_drained` 会在 esp_timer 任务中 `vTaskDelay` 等待 `active_callbacks[event]` 归零，但当前回调无法返回（被 drain 阻塞），形成死锁。
- **建议修复**: 在 button.h 的 `button_register_cb` 或 `button_destroy` 的 note 中补充："不得从按键回调中调用 `button_register_cb` 注销当前正在执行的回调"。

### L4. `button_iot_adapter.c` 中 `long_press_time=0, short_press_time=0` 缺少注释说明

- **文件**: `button_iot_adapter.c:60-63`
- **问题描述**: `button_config_t` 的 `long_press_time` 和 `short_press_time` 设为 0，依赖 iot_button 的"0 表示使用 CONFIG 默认值"语义（`button_types.h:28-29`：`if 0 default to BUTTON_LONG_PRESS_TIME_MS`）。classes.md 也说明"使用 iot_button 默认值"。但代码中无注释说明 0 的含义，维护者需查阅 iot_button 源码才能理解。
- **建议修复**: 在 `button_config` 初始化处补充注释：`/* 0 = 使用 iot_button Kconfig 默认值（CONFIG_BUTTON_LONG_PRESS_TIME_MS / SHORT_PRESS_TIME_MS）*/`。

---

## 无问题维度

- **A. 资源账本与乘法型分配**：`struct button` 约 80 字节，无大块分配，无乘法型占用。`button_config_t` 只含 GPIO + 电平，无配置传播风险。
- **B. 内存安全与生命周期**：drain 机制正确防止 use-after-free（destroy 等待在途回调完成后才释放 `me`）。`button_dispatch` 在 mutex 保护下拷贝 cb 和 user_ctx 后才调用，回调执行时不持锁。`button_create` 失败路径完整回滚（err 标签逆序释放）。
- **E. 跨模块契约**：button 仅通过回调上报事件，不直接控制继电器或显示（符合 architecture.md §8.2 第 6 条）。iot_button 类型隔离在 `button_iot_adapter` 后面，`button.c` 不直接 include `iot_button.h`（避免 `button_event_t` / `BUTTON_EVENT_MAX` 命名冲突）。不依赖 `board_pinmap`（GPIO 由 app_controller 注入）。
- **F. 类型与边界**：枚举值校验完备（`button_validate_config` 检查 GPIO 有效性 + active_level 范围；`button_dispatch` 检查 event 范围）。无整数溢出风险。
- **G. 代码质量**：coding-style.md 合规（section 组织、双语注释、include 风格、TAG 定义）。圈复杂度低（所有函数 ≤ 5）。命名清晰。
