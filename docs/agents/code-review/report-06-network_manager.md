# Code Review: network_manager

**日期**: 2026-07-07
**文件**:
- `main/network/network_manager.c`
- `main/network/network_manager.h`

## 审查范围说明

本报告审查双模网络管理器实现。该模块持有主/备两条 `network_link_t *` 链路，负责链路选择、故障切换、回切延迟、订阅意图管理和下行消息桥接。审查覆盖并发安全（锁序、持锁调用链路 API）、内存生命周期、失败路径完整性、文档一致性。

审查中交叉阅读了 `network_link.c`、`network_link_priv.h`、`wifi_link.c`、`lte_link.c` 的关键路径，以验证锁序和回调上下文契约。

---

## 🟡 中严重度

### C-1: `switch_active_locked` 持有 manager mutex 调用链路 API（延迟与脆弱性风险）

- **文件:行号**: `network_manager.c:1150-1163`（`monitor_once` 持锁调用 `switch_active_locked`）、`network_manager.c:1231-1270`（`switch_active_locked` 持锁调用 `network_link_set_active` 和 `replay_subscriptions_locked`）、`network_manager.c:1007-1008`（`replay_subscriptions_locked` 持锁调用 `network_link_subscribe`）
- **维度**: C（并发、实时性 — 持锁调用可能阻塞的外部 API）
- **问题描述**:
  `network_manager_monitor_once` 在持有 `me->mutex` 时调用 `network_manager_switch_active_locked`（行 1163 或 1177）。`switch_active_locked` 在持锁状态下依次调用：
  1. `network_link_set_active(link, true)`（行 1249）
  2. `network_link_set_active(old, false)`（行 1257）
  3. `network_manager_replay_subscriptions_locked(me, link)`（行 1265）→ 遍历 `sub_table` 调用 `network_link_subscribe`（行 1007）

  这意味着整个链路切换 + 订阅重放过程期间，manager mutex 被持有，阻塞所有其他 manager 操作：
  - `network_manager_publish` 无法读取 `active`（行 635-639）
  - `network_manager_subscribe` / `unsubscribe` 无法操作订阅表
  - `network_manager_get_status` 无法查询状态（行 564-596）
  - `network_manager_on_link_rx` 无法获取 mutex 处理下行消息（行 1289）—— **在 esp_mqtt / lwlte 事件任务上下文中阻塞**

  **当前缓解因素**（经交叉验证确认）：
  1. `wifi_link_subscribe_impl` 在调用 `esp_mqtt_client_subscribe` 前释放 wifi_link mutex（`wifi_link.c:849-855`），`esp_mqtt_client_subscribe` 为异步提交。
  2. `lte_link_subscribe_impl` 在调用 `lwlte_mqtt_subscribe` 前释放 lte_link mutex（`lte_link.c:384-390`），`lwlte_mqtt_subscribe` 返回 `ESP_OK` 语义为"请求已提交"（`esp-lwlte/src/include/lwlte.h:908`），异步。
  3. wifi_link 和 lte_link 的 RX 回调均在释放自身 mutex 后调用 `on_link_rx`（`wifi_link.c:1649-1676`、`lte_link.c:895` 注释 "INVARIANT: lte_link mutex not held here"），因此不存在 manager_mutex ↔ link_mutex 的 AB-BA 死锁。
  4. `network_link_set_active` 对 LTE 调用 `lwlte_mqtt_start/stop`，同样为异步提交。

  **残余风险**：
  - 虽然 subscribe / set_active 均为异步，但持锁期间仍需遍历 sub_table 并提交 N 次订阅请求（默认 8 次），加上 set_active 的两次调用，总耗时虽短但非零。
  - 若未来新增链路类型使用同步（阻塞）subscribe 或 set_active 实现，将导致 manager mutex 长时间持有，严重情况下 `network_manager_stop_monitor` 的 3 秒超时（`NETWORK_MANAGER_STOP_TIMEOUT_MS`）可能不够。
  - `on_link_rx` 在事件任务中使用 `portMAX_DELAY` 获取 mutex（行 1289），持锁期间事件任务被阻塞，延迟 MQTT 事件处理（包括 QoS ACK、断连检测）。

- **建议修复**:
  在 `switch_active_locked` 中，先在持锁状态下读取 `sub_table` 的快照（topic 指针数组 + qos 数组），释放 mutex，再调用 `network_link_set_active` 和 `network_link_subscribe`，最后重新获取 mutex 更新状态。`start` 函数中的 `replay_subscriptions_locked`（行 462）同理。如果重构成本高，至少在注释中文档化"所有链路 ops 必须为非阻塞"的前置契约。

### C-2: `on_link_rx` 在事件任务上下文中使用 `portMAX_DELAY` 获取 manager mutex

- **文件:行号**: `network_manager.c:1289`
- **维度**: C（实时性 — 事件任务中无超时阻塞）
- **问题描述**:
  `network_manager_on_link_rx` 在 link 的事件处理任务（esp_mqtt 内部任务 / lwlte 内部任务）上下文中执行。行 1289 使用 `xSemaphoreTake(me->mutex, portMAX_DELAY)` 无限等待 manager mutex。

  当 monitor 任务正在执行 `switch_active_locked`（持锁重放订阅）或 `network_manager_start` / `stop` 持锁操作链路时，事件任务在此行无限阻塞。这不会死锁（monitor 任务最终会释放 mutex），但会延迟 MQTT 事件处理，可能导致：
  - esp_mqtt 内部队列积压
  - QoS 1/2 消息的 ACK 延迟
  - MQTT 断连事件处理延迟

  同样的问题存在于行 1305（回调返回后再次获取 mutex 以递减 `active_rx_callbacks`）。

- **建议修复**:
  使用有限超时（如 `pdMS_TO_TICKS(100)`），超时后丢弃当前消息并记录 `ESP_LOGW`。下行 MQTT 消息丢一条不影响系统正确性（QoS 0 本就允许丢包；QoS 1/2 由 broker 重发）。

### F-1: `preferred_primary` 字段被校验和存储但从未使用

- **文件:行号**: `network_manager.c:73`（字段定义）、`network_manager.c:782-793`（`validate_config` 校验）、`network_manager.c:808-811`（`apply_config` 存储）
- **维度**: F（死代码 / 误导性 API）
- **问题描述**:
  `preferred_primary` 在 `validate_config` 中被校验（必须匹配 primary 或 backup 的链路类型），在 `apply_config` 中被存储到 `me->preferred_primary`。但 `rg` 搜索确认，该字段在 `network_manager_start`、`network_manager_monitor_once`、`network_manager_switch_active_locked` 中均未被读取。

  实际行为：
  - `start` 始终先启动 `me->primary`（行 425），失败后才尝试 `me->backup`（行 429）
  - `monitor_once` 的回切逻辑始终切回 `me->primary`（行 1177），而非根据 `preferred_primary` 选择

  用户配置 `preferred_primary = NETWORK_LINK_TYPE_LTE` 不会产生任何效果，但 `validate_config` 会通过校验，给用户造成"已生效"的错觉。

- **建议修复**:
  方案 A：在 `start` 和 `monitor_once` 中根据 `preferred_primary` 选择首选链路（如果 preferred_primary 匹配 backup 类型，则先启动 backup、回切到 backup）。
  方案 B：如果当前设计意图是"primary 指针即首选链路"，则移除 `preferred_primary` 字段和相关校验，减少误导。

### D-1: `destroy` 在 `stop` 或 `clear_link_rx_cb` 失败时泄漏 manager 对象

- **文件:行号**: `network_manager.c:391-393`
- **维度**: D（失败路径完整性 — 失败后资源泄漏）
- **问题描述**:
  `network_manager_destroy` 的失败路径：
  ```c
  if (first_error != ESP_OK) {
      return first_error;
  }
  network_manager_free_resources(me);
  ```

  如果 `network_manager_stop` 或 `clear_link_rx_cb` 返回错误，`destroy` 直接返回错误码，不调用 `free_resources`。此时：
  1. `me->destroying` 已设为 `true`（行 373），manager 处于半销毁状态
  2. manager 对象及其内部资源（mutex、sub_table、semaphore）全部泄漏
  3. 调用方可以重试 `destroy`，但如果 `stop` 持续失败（如链路硬件故障），对象永久泄漏

  另外，`network_manager_stop` 的 `stop_monitor` 超时返回 `ESP_ERR_TIMEOUT`（行 1068）时，monitor 任务可能仍在运行，此时 `destroy` 返回错误但 monitor 任务可能仍在访问 `me`，形成悬空访问风险。

- **建议修复**:
  考虑在 `destroy` 中即使 `stop` 失败也继续清理（接受可能的回调后访问风险，优于永久泄漏）。或者增加 `force` 参数让调用方决定是否强制释放。至少在日志中记录泄漏的资源大小和原因。

---

## 🟢 低严重度

### H-1: 文档漂移 — `sub_entry_t` 使用 `char *topic` 而非 `char topic[]`

- **文件:行号**: `network_manager.c:51` vs `classes.md §9.4`（行 1286-1289）
- **维度**: H（文档与实现一致性）
- **问题描述**:
  classes.md §9.4 记载的内部结构为：
  ```c
  typedef struct {
      char topic[NETWORK_MANAGER_MAX_TOPIC_LEN];  // 固定数组
      network_mqtt_qos_t qos;
      bool in_use;
  } network_manager_sub_entry_t;
  ```

  实际实现（`network_manager.c:50-54`）为：
  ```c
  typedef struct {
      char *topic;  // 堆指针
      network_mqtt_qos_t qos;
      bool in_use;
  } network_manager_sub_entry_t;
  ```

  实现采用"指针槽位常驻 + 内容按需 malloc"模式，比文档的固定数组更节省内存（默认 8 槽 × 128 字节 = 1024 字节 → 8 槽 × 16 字节 ≈ 128 字节 + 每个主题按需分配 strlen+1）。实现更好，但文档未同步。

  此外，classes.md §9.4 的内部结构缺少实现中存在的多个字段：`active_rx_callbacks`、`monitor_task`、`monitor_task_done_sema`、`primary_rx_ctx`、`backup_rx_ctx`、`monitor_task_running`、`stop_pending`。

- **建议修复**: 更新 classes.md §9.4 的 `sub_entry_t` 定义和 `struct network_manager` 定义以匹配实现。

### H-2: `destroy` 和 `stop` 的返回值文档不完整

- **文件:行号**: `network_manager.h:87-90`（`destroy`）、`network_manager.h:107-112`（`stop`）
- **维度**: H（文档完整性）
- **问题描述**:
  `destroy` 的 Doxygen 仅列出 `ESP_OK` 和 `ESP_ERR_INVALID_ARG`，但实现可返回 `ESP_ERR_TIMEOUT`（来自 `stop_monitor` 超时）和 `clear_link_rx_cb` 的错误码。

  `stop` 的 Doxygen 仅列出 `ESP_OK`、`ESP_ERR_INVALID_ARG`、`ESP_ERR_INVALID_STATE`，但实现可返回 `ESP_ERR_TIMEOUT`（mutex 获取失败或 `stop_monitor` 超时）和链路 stop 失败的错误码。

- **建议修复**: 补充 `ESP_ERR_TIMEOUT` 等返回值到 Doxygen 注释。

---

## 无问题维度

- **A（资源账本与乘法型分配）**: `sub_table` 使用 `calloc(max_subscriptions, sizeof(sub_entry_t))`，其中 `sub_entry_t` 含 `char *`（8 字节）+ `enum`（4 字节）+ `bool`（1 字节）≈ 16 字节/槽。默认 8 槽 = 128 字节，远低于阈值。每个 topic 按需 `malloc(strlen+1)`，最大 128 字节。无乘法型分配风险。
- **B（内存安全与生命周期）**: topic 拷贝使用 `malloc(topic_len + 1U)` + `memcpy`（行 956-960），正确处理 null 终止符。`free_resources` 正确释放每个 topic 和 sub_table。primary/backup 链路为借用（header 行 42-44 文档化），manager 不负责销毁。`publish` 的 payload 为 `const void *` 借用，无 ownership 转移。
- **E（跨模块契约）**: network_manager 只依赖 `network_link` 基类 API，不解析 ThingsBoard topic（§8.2 契约遵守），不构建 JSON，不控制继电器。`rx_cb` 透传原始 MQTT 消息给上层，不修改内容。
- **G（代码质量与一致性）**: section 组织（DEFINES → TYPEDEFS → STATIC PROTOTYPES → ...）符合 coding-style.md。`_locked` 后缀命名一致。Doxygen 双语注释格式正确。`monitor_once` 圈复杂度约 10-12，在可接受范围内。
