# Verification: network_manager

## 验证方法

对报告中的每条发现，重新阅读源码前后 ≥30 行上下文，使用 `rg` 搜索调用点和条件分支，检查遗漏的已有防护逻辑。交叉阅读 `wifi_link.c`、`lte_link.c`、`network_link.c`、`network_link_priv.h` 验证锁序和回调上下文契约。

---

## ✅ 确认的问题

### C-1: `switch_active_locked` 持有 manager mutex 调用链路 API

- **原报告条目**: C-1
- **验证结论**: 确认。

  重新阅读 `network_manager.c:1101-1185`（`monitor_once` 完整函数）和 `network_manager.c:1231-1270`（`switch_active_locked` 完整函数）：

  `monitor_once` 在行 1150 获取 mutex，行 1158 读取 `active`，然后根据条件在行 1163 或 1177 调用 `switch_active_locked`——此时 mutex 仍被持有。`switch_active_locked` 在持锁状态下调用 `network_link_set_active(link, true)`（行 1249）、`network_link_set_active(old, false)`（行 1257）、`network_manager_replay_subscriptions_locked(me, link)`（行 1265）。`replay_subscriptions_locked`（行 996-1015）在循环中调用 `network_link_subscribe`（行 1007）。

  `rg` 确认 `network_manager_start`（行 462）也在持锁状态下调用 `replay_subscriptions_locked`。

  **交叉验证锁序安全**：
  - `wifi_link.c:826-849`：`subscribe_impl` 在行 849 释放 wifi_link mutex 后，行 855 调用 `esp_mqtt_client_subscribe`（异步）。
  - `lte_link.c:384-390`：`subscribe_impl` 在行 384 释放 lte_link mutex 后，行 390 调用 `lwlte_mqtt_subscribe`。
  - `esp-lwlte/src/include/lwlte.h:908`：返回值 `ESP_OK: 请求已提交`，确认异步。
  - `wifi_link.c:1638-1676`：MQTT 事件处理在行 1649 释放 wifi_link mutex 后，行 1676 调用 `rx_cb`（即 `on_link_rx`）。
  - `lte_link.c:895`：注释 `INVARIANT: lte_link mutex not held here`。

  锁序分析：
  - Thread A（monitor）：manager_mutex → wifi_link_mutex（subscribe_impl 行 826 获取，行 849 释放）→ esp_mqtt_client_subscribe（无锁）
  - Thread B（mqtt event）：wifi_link_mutex（行 1638 获取，行 1649 释放）→ manager_mutex（on_link_rx 行 1289 获取）

  Thread B 在获取 manager_mutex 前已释放 wifi_link_mutex，不存在循环等待。**无死锁**。

  **确认延迟风险**：持锁期间遍历 sub_table（默认 8 项）并逐个调用 `network_link_subscribe` + 2 次 `network_link_set_active`。虽然各 API 均为异步提交，但仍阻塞 manager 操作和事件任务处理。

  **严重度维持 🟡 中**：当前无死锁，但设计脆弱——未来同步链路实现可导致严重延迟或 stop_monitor 超时。

### C-2: `on_link_rx` 在事件任务中使用 `portMAX_DELAY` 获取 mutex

- **原报告条目**: C-2
- **验证结论**: 确认。

  重新阅读 `network_manager.c:1272-1310`（`on_link_rx` 完整函数）：

  行 1289：`xSemaphoreTake(me->mutex, portMAX_DELAY)` — 在 link 事件任务上下文中无限等待。
  行 1305：`xSemaphoreTake(me->mutex, portMAX_DELAY)` — 回调返回后再次无限等待以递减计数器。

  交叉确认 `on_link_rx` 的执行上下文：
  - `wifi_link.c:1676`：在 `mqtt_event_handler`（esp_mqtt 内部任务）中调用 `rx_cb(&rx_data, rx_ctx)`，此 `rx_cb` 即 `network_manager_on_link_rx`（通过 `network_link_register_rx_cb` 注册）。
  - `lte_link.c:895`：在 lwlte 事件处理中调用 `rx_cb`。

  确认 `on_link_rx` 运行在 link 的事件任务中，`portMAX_DELAY` 在 manager mutex 被 monitor 任务持有时会阻塞事件任务。

  **严重度维持 🟡 中**：不致死锁，但延迟 MQTT 事件处理。

### F-1: `preferred_primary` 字段存储但从未使用

- **原报告条目**: F-1
- **验证结论**: 确认。

  `rg -n "preferred_primary" main/network/network_manager.c` 输出：
  ```
  73:    network_link_type_t preferred_primary;
  782:    if (config->preferred_primary != NETWORK_LINK_TYPE_NONE) {
  789:        ESP_RETURN_ON_FALSE(config->preferred_primary == primary_type ||
  791:                                 config->preferred_primary == backup_type),
  808:    me->preferred_primary = (config->preferred_primary ==
  811:                            config->preferred_primary;
  ```

  仅在 `validate_config`（行 782-793）和 `apply_config`（行 808-811）中出现。在 `start`（行 399-481）、`monitor_once`（行 1101-1185）、`switch_active_locked`（行 1231-1270）中均未读取 `me->preferred_primary`。

  `start` 始终先启动 `me->primary`（行 425），`monitor_once` 回切始终指向 `me->primary`（行 1177）。字段为死代码。

### D-1: `destroy` 失败时泄漏 manager 对象

- **原报告条目**: D-1
- **验证结论**: 确认。

  重新阅读 `network_manager.c:362-397`（`destroy` 完整函数）：

  行 373：`me->destroying = true`（在 mutex 保护下设置）
  行 377：`first_error = network_manager_stop(me)` — 若返回错误，`first_error` 非 OK
  行 379-389：`clear_link_rx_cb` 错误累积到 `first_error`
  行 391-393：`if (first_error != ESP_OK) { return first_error; }` — 直接返回，不调用 `free_resources`

  确认失败路径不释放资源。调用方可重试 `destroy`（`me` 仍有效），但 `destroying` 已为 true，`start` 会返回 `ESP_ERR_INVALID_STATE`（行 420-423），系统进入不可恢复状态。

  `network_manager_stop` 可能返回 `ESP_ERR_TIMEOUT`（`stop_monitor` 行 1068 超时，或 mutex 获取失败），此时 monitor 任务可能仍在运行并访问 `me`。

### H-1: 文档漂移 — `sub_entry_t` 结构体定义

- **原报告条目**: H-1
- **验证结论**: 确认。

  `network_manager.c:50-54` 实际定义：
  ```c
  typedef struct {
      char *topic;
      network_mqtt_qos_t qos;
      bool in_use;
  } network_manager_sub_entry_t;
  ```

  `classes.md` §9.4（行 1286-1289）记载：
  ```c
  typedef struct {
      char topic[NETWORK_MANAGER_MAX_TOPIC_LEN];
      network_mqtt_qos_t qos;
      bool in_use;
  } network_manager_sub_entry_t;
  ```

  `char *topic`（指针 + 按需 malloc）vs `char topic[128]`（固定数组），内存模型完全不同。实现更优，但文档未同步。

  `NETWORK_MANAGER_MAX_TOPIC_LEN`（行 34，值 128）在实现中仅用作长度校验阈值（行 939：`strlen(topic) < NETWORK_MANAGER_MAX_TOPIC_LEN`），不参与 sub_table 的内存分配。

### H-2: 返回值文档不完整

- **原报告条目**: H-2
- **验证结论**: 确认。

  `network_manager.h:87-90`（`destroy`）Doxygen 仅列 `ESP_OK` 和 `ESP_ERR_INVALID_ARG`。
  实现可返回：`ESP_ERR_TIMEOUT`（`stop_monitor` 行 1068、mutex 获取失败行 539）、`clear_link_rx_cb` 的错误码、`network_manager_stop` 的链路 stop 错误码。

  `network_manager.h:107-112`（`stop`）Doxygen 仅列 `ESP_OK`、`ESP_ERR_INVALID_ARG`、`ESP_ERR_INVALID_STATE`。
  实现可返回：`ESP_ERR_TIMEOUT`（mutex 获取失败行 494/511/539、`stop_monitor` 行 507/1068）、链路 stop 失败的错误码。

---

## ❌ 误报

无。

---

## ⚠️ 部分正确（需调整修复方案）

无。

---

## 模块交付清单

### Change summary
N/A（review-only，无代码改动）

### Resource budget

| 资源 | 计算 | 大小 |
|------|------|------|
| manager 对象 | `sizeof(struct network_manager)` | ~120 字节（12 字段 × ~8-10 字节） |
| sub_table | `calloc(max_subscriptions, sizeof(sub_entry_t))` = `8 × sizeof(char* + enum + bool)` ≈ `8 × 16` | 128 字节（默认） |
| 每个 topic | `malloc(strlen(topic) + 1)` | ≤ 128 字节/主题（MAX_TOPIC_LEN 限制） |
| mutex | `xSemaphoreCreateMutex()` | ~80 字节（FreeRTOS mutex） |
| monitor_done_sema | `xSemaphoreCreateBinary()` | ~80 字节（FreeRTOS binary semaphore） |
| monitor task stack | `NETWORK_MANAGER_TASK_STACK` = 4096 | 4096 字节 |
| primary_rx_ctx / backup_rx_ctx | 2 × `sizeof(bridge_ctx)` = `2 × 16` | 32 字节 |

**总静态占用**（不含 task stack）：~440 字节 + N × topic_size（N = 活跃订阅数，通常 2-4 个 ThingsBoard topic，每个 ~40 字节）≈ ~600 字节

**峰值 heap**：~600 字节 + 4096 字节 task stack ≈ 4.7 KB

远低于 ESP32-S3 内部 SRAM 上限。无乘法型分配风险。

### Lifecycle / ownership notes

| 数据 | ownership | 说明 |
|------|-----------|------|
| `primary` / `backup` 链路指针 | **borrowed** | 调用方创建和销毁，manager 生命周期内可 start/stop 链路并注册/清除 RX 回调 |
| `sub_table` | **owned** | manager create 时 calloc，destroy 时 free |
| `sub_table[i].topic` | **owned** | subscribe 时 malloc，unsubscribe/free_resources 时 free |
| `rx_cb` / `rx_ctx` | **borrowed** | 上层注册的回调指针和上下文，manager 不拥有 |
| `primary_rx_ctx` / `backup_rx_ctx` | **owned** | manager 内部桥接上下文，随 manager 生命周期存在 |
| `publish` 的 `req->payload` | **borrowed** | 调用方拥有，publish 仅透传给 link，失败后 ownership 不转移 |
| `on_link_rx` 的 `rx_data` | **borrowed** | link 拥有，回调期间有效，回调返回后可能失效 |

### Failure-path review

| 失败点 | 处理 | 评价 |
|--------|------|------|
| `calloc(manager)` 失败 | 返回 NULL | ✅ 正确 |
| `xSemaphoreCreateMutex` 失败 | `cleanup_create_failure(false, false)` → free + 返回 NULL | ✅ 正确 |
| `xSemaphoreCreateBinary` 失败 | 同上 | ✅ 正确 |
| `calloc(sub_table)` 失败 | 同上 | ✅ 正确 |
| `register_rx_cb(primary)` 失败 | `cleanup_create_failure(false, false)` → free + 返回 NULL | ✅ 正确（primary cb 未注册成功） |
| `register_rx_cb(backup)` 失败 | `cleanup_create_failure(primary_cb_registered, false)` → 清除 primary cb → free + 返回 NULL | ✅ 正确 |
| `cleanup_create_failure` 中 `clear_link_rx_cb` 失败 | 设 `destroying=true`，保留对象（泄漏）返回 false | ⚠️ 有意泄漏以避免 use-after-free，`ESP_LOGE` 记录原因。create 返回 NULL 但对象泄漏。设计权衡合理但非理想 |
| `store_subscription` malloc 失败 | 返回 `ESP_ERR_NO_MEM`，不修改 sub_table | ✅ 正确 |
| `start_monitor` xTaskCreate 失败 | 设 `monitor_task_running=false`，返回 `ESP_ERR_NO_MEM`，`start` 回滚（stop links） | ✅ 正确 |
| `stop_monitor` 超时 | 返回 `ESP_ERR_TIMEOUT`，`stop` 返回此错误 | ⚠️ monitor 任务可能仍在运行 |
| `destroy` 中 `stop`/`clear_link_rx_cb` 失败 | 返回错误，不 free_resources | ⚠️ 对象泄漏（见 D-1） |
| `publish` 时 active=NULL | 返回 `ESP_ERR_INVALID_STATE` | ✅ 正确 |
| `publish` 时 status ≠ READY | 返回 `ESP_ERR_INVALID_STATE` | ✅ 正确 |
| `on_link_rx` mutex 获取失败 | 静默返回（丢弃消息） | ✅ 可接受（下行消息可丢可重发） |

### Cross-module contract review

| 契约 | 遵守情况 |
|------|----------|
| network_manager 只依赖 `network_link` 基类 API（§8.1） | ✅ 仅 include `network_link.h`，不 include `wifi_link.h` / `lte_link.h` |
| network_manager 禁止解析 ThingsBoard topic（§8.2） | ✅ topic 字符串原样透传给 subscribe / publish / rx_cb |
| network_manager 禁止构建 telemetry JSON（§4.4） | ✅ payload 为 `const void *`，不解析内容 |
| network_manager 禁止直接控制继电器（§4.4） | ✅ 无 relay 依赖 |
| 下行 RPC 流不绕过 app_controller（§6.2） | ✅ `rx_cb` 透传给上层（thingsboard_client），由上层解析后回调 app_controller |
| primary/backup 为借用链路（header @note） | ✅ manager 不 destroy 链路，只 start/stop/register_rx_cb |

### Residual risks

1. **C-1 残余**：当前 link 实现的 subscribe/set_active 均为异步，持锁时间短。但设计未在代码或注释中强制"链路 ops 必须非阻塞"的前置契约。未来新增同步链路实现时，`switch_active_locked` 和 `start` 持锁调用链路 API 可导致严重延迟或 `stop_monitor` 超时。

2. **D-1 残余**：`destroy` 失败路径泄漏对象。若 `stop` 持续失败（如 LTE 硬件故障），对象永久泄漏，且 `destroying=true` 导致系统不可恢复。

3. **destroy 回调安全依赖 link 实现**：`destroy` 的 use-after-free 防护依赖 link 的 `register_rx_cb(NULL)` 实现 callback draining（wifi_link: `wait_runtime_actions_drained`，lte_link: `wait_rx_callbacks_drained`）。两者均无超时上限（lte_link 报告 C-2 已记录）。若 `rx_cb`（上层回调）阻塞，`destroy` 永久挂起。network_manager 自身的 `active_rx_callbacks` 机制仅在 `register_rx_cb` 中使用，`destroy` 未使用，设计不对称。

4. **`preferred_primary` 死代码**（F-1）：用户可配置但无效。若后续按方案 A 实现，需修改 `start` 和 `monitor_once` 的链路选择逻辑，需回归测试。

5. **上机验证项**：Wi-Fi → LTE 切换时 subscribe 重放的实际延迟（需上机测量 esp_mqtt_client_subscribe 的返回时间）；LTE 链路 set_active(true) 触发 `lwlte_mqtt_start` 的实际耗时。
