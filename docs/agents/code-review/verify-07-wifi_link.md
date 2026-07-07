# Verification: wifi_link

## ✅ 确认的问题

### D-1: `esp_mqtt_client_stop` 返回非 ESP_OK/ESP_FAIL 时 MQTT 客户端资源泄漏

- **原报告条目**: D-1（wifi_link.c:966）
- **验证结论**: 确认。重新阅读行 958-984 上下文：
  - 行 959：`ret = esp_mqtt_client_stop(mqtt_client)`
  - 行 966：`if (ret == ESP_OK || ret == ESP_FAIL)` — 仅这两种返回值才执行 destroy
  - 行 967：`esp_mqtt_client_destroy(mqtt_client)` 在条件内
  - 行 978-979：`me->mqtt_client = NULL` 在 destroy 成功分支内

  ESP-IDF `esp_mqtt_client_stop` 可返回 `ESP_ERR_INVALID_STATE`（客户端未启动）。此时代码跳过 destroy，`me->mqtt_client` 保留指向已 stop 但未 destroy 的客户端。后续 `start_impl` 行 532 检查 `me->mqtt_client != NULL` 返回 `ESP_ERR_INVALID_STATE`，链路永久不可重启。

  **触发概率**：低（正常流程 stop 返回 ESP_OK），但边缘状态（如 client 内部 task 已自行退出）可能触发。影响严重（永久链路失效 + 资源泄漏）。

### H-1: 文档漂移 — `esp_mqtt_client_enqueue` vs `esp_mqtt_client_publish`

- **原报告条目**: H-1（classes.md:1124 vs wifi_link_internal.c:42）
- **验证结论**: 确认。`rg "esp_mqtt_client_enqueue"` 搜索结果为空——全项目未使用 `enqueue` API。`wifi_link_internal.c:42` 使用 `esp_mqtt_client_publish`（同步）。classes.md:1124 明确记载 `esp_mqtt_client_enqueue()`。文档与实现不一致。

### H-2: 文档漂移 — 订阅表条目 `char topic[]` vs `char *topic`

- **原报告条目**: H-2（classes.md:1080 vs wifi_link.c:55-59）
- **验证结论**: 确认。classes.md:1080 显示 `char topic[WIFI_LINK_MAX_TOPIC_LEN]`。wifi_link.c:55-59 实际为 `char *topic`。`rg "WIFI_LINK_MAX_TOPIC_LEN"` 仅在 classes.md:1080 出现，代码中无此符号（代码用 `WIFI_LINK_DEFAULT_MAX_TOPIC_LEN`，行 41）。文档漂移确认。

### H-3: 文档漂移 — stop "保留资源可重 start" vs 完全销毁

- **原报告条目**: H-3（classes.md:1122 vs wifi_link.c:734）
- **验证结论**: 确认。classes.md:1122 记载 stop "保留资源可重 start"。wifi_link.c:734 调用 `wifi_link_cleanup_resources(me)`，后者执行 `esp_mqtt_client_destroy`（行 967）、`esp_wifi_deinit`（行 1044）、`esp_netif_destroy_default_wifi`（行 1053）——完全销毁。文档与实现矛盾。

### H-4: 文档漂移 — rx_cb "持有 mutex 读取" vs 释放后调用

- **原报告条目**: H-4（classes.md:1152 vs wifi_link.c:1649,1676）
- **验证结论**: 确认。classes.md:1152 记载"调用 rx_cb(rx_data, rx_ctx)（持有 mutex 读取）"。实际代码：
  - 行 1649：`xSemaphoreGive(me->mutex)` — 释放 mutex
  - 行 1676：`rx_cb(&rx_data, rx_ctx)` — 在无锁状态调用

  实现正确（不应持锁调外部回调），文档描述错误。

### G-1: `wifi_link_from_base` 直接强转

- **原报告条目**: G-1（wifi_link.c:463-466）
- **验证结论**: 确认。`rg "container_of" main/network/` 搜索结果为空——全网络层未使用 `container_of`。wifi_link.c:465 `(wifi_link_t *)base` 为直接强转。`network_link_t base` 在行 66 确认为第一字段（偏移 0），强转当前安全但违反 oop-design.md §2.4。

### G-2: `wifi_link_start_impl` 圈复杂度

- **原报告条目**: G-2（wifi_link.c:508-702）
- **验证结论**: 确认。函数跨度 195 行，包含 7 个 `ESP_GOTO_ON_FALSE` 调用、2 个 goto 标签（`release_mutex`、`cleanup`）、7 次 mutex take/give 交替。圈复杂度远超 15。

### G-3: subscribe/unsubscribe 手动复制 `end_mqtt_op` 逻辑

- **原报告条目**: G-3（wifi_link.c:841-844, 891-896）
- **验证结论**: 确认。行 841-844 和 891-896 手动执行 `me->mqtt_op_active = false; me->runtime_action_count--;`，与 `wifi_link_end_mqtt_op`（行 1282-1284）逻辑重复。不调用 `end_mqtt_op` 的原因是当前持锁，`end_mqtt_op` 内部会 take mutex 导致死锁。逻辑正确但脆弱。

### C-1: `wait_runtime_actions_drained` 无超时

- **原报告条目**: C-1（wifi_link.c:1244-1257）
- **验证结论**: 确认。行 1244 `while (true)` 无退出条件除 `runtime_action_count == 0`。行 1256 `vTaskDelay(pdMS_TO_TICKS(10))` 轮询。无最大重试或超时。与 lte_link.c:475-490 相同模式（report-08 C-2 已记录）。

### D-2: netif 销毁依赖 `esp_wifi_deinit` 返回值

- **原报告条目**: D-2（wifi_link.c:1052）
- **验证结论**: 确认。行 1052 条件 `wifi_link_is_expected_wifi_cleanup_error(ret)` 依赖行 1044 `esp_wifi_deinit()` 的返回值 `ret`。netif 创建（`esp_netif_create_default_wifi_sta`，行 559）与 Wi-Fi init 独立，其销毁不应依赖 deinit 结果。实际风险极低（deinit 正常只返回预期错误码），但模式不正确。

### D-3: `ESP_RETURN_ON_FALSE` 在 cleanup 中留下悬空指针

- **原报告条目**: D-3（wifi_link.c:975, 997, 1018, 1054）
- **验证结论**: 确认。行 975-977 的 `ESP_RETURN_ON_FALSE` 在 `esp_mqtt_client_destroy` 成功后、`me->mqtt_client = NULL` 前执行。若 `xSemaphoreTake` 失败（portMAX_DELAY 下理论不会），返回 `ESP_ERR_TIMEOUT` 跳过 NULL 赋值，`me->mqtt_client` 成为悬空指针。行 997、1018、1054 同理。模式不正确但实际不触发。

### G-4: classes.md §8.4 未列出 `set_active`

- **原报告条目**: G-4（classes.md:1118-1127 vs wifi_link.c:400-409）
- **验证结论**: 确认。`network_link_priv.h:43` 定义 `set_active` 方法。`wifi_link.c:400-409` 的 `wifi_link_ops` 未设置 `.set_active`（NULL）。`network_link.c:126-128` wrapper 安全处理 NULL（返回 `ESP_OK` no-op）。classes.md §8.4 ops 映射表（行 1118-1127）未提及 `set_active` 的缺失。lte_link.c:127 实现了 `set_active`，对比下 wifi_link 的缺失应文档化。

---

## ❌ 误报

（无）

---

## ⚠️ 部分正确（需调整修复方案）

（无——所有发现均在验证中确认）

---

## 修复记录

- **N/A** — review-only，无代码改动

---

## 模块交付清单

### Change summary
N/A（review-only，无代码改动）

### Resource budget

| 资源 | 计算 | 大小 |
|------|------|------|
| `wifi_link_t` 对象 | `sizeof(wifi_link_t)` | ~120 字节（含 base + config + 7 个 char* + 句柄 + bool 标志 + mutex + 回调） |
| `sub_table` | `max_subscriptions × sizeof(wifi_link_sub_entry_t)` = 8 × ~16 字节 | ~128 字节（默认） |
| sub_table topic | 每条按需 `strdup(topic_len + 1)`，8 条全满约 | ~1 KB（假设平均 128 字节 topic） |
| config 字符串 | ssid(32) + password(64) + host(64) + client_id(32) + username(32) + uri(64) | ~290 字节 |
| MQTT URI | `snprintf` 计算 + `malloc(len + 1)` | ~30 字节 |
| mutex | `xSemaphoreCreateMutex()` | ~80 字节（FreeRTOS 互斥量） |
| **启动总 footprint** | | **~1.6 KB**（不含 ESP-IDF 内部 Wi-Fi/MQTT 栈） |
| ESP-IDF Wi-Fi stack | ESP-IDF 内部，非本模块分配 | ~50 KB（station 模式） |
| esp_mqtt task stack | ESP-IDF 内部 | ~4-6 KB |
| **运行时 heap 峰值** | 上述 + MQTT 发送/接收 buffer | ~60 KB（含 ESP-IDF 内部） |

**结论**：本模块自身分配 ~1.6 KB，资源占用合理。主要 heap 消耗来自 ESP-IDF Wi-Fi/MQTT 内部（~60 KB），不可控。

### Lifecycle / ownership notes

| 数据 | Ownership | 说明 |
|------|-----------|------|
| `me->ssid` 等 config 字符串 | Owned | `wifi_link_copy_config` 中 `strdup`（行 1097-1106），`wifi_link_free_config` 中 `free`（行 1138-1143） |
| `me->mqtt_uri` | Owned | `wifi_link_build_uri` 中 `malloc`（行 1199），`free_config` 中释放 |
| `me->config` 内的 `const char *` 字段 | Owned | 行 1117-1123 repoint 到 owned 副本，调用方释放原始 config 后安全 |
| `me->sub_table[i].topic` | Owned | `wifi_link_strdup_or_null` 分配（行 1372），destroy/free 时释放（行 492、1401） |
| `me->mqtt_client` | Owned | `esp_mqtt_client_init` 创建（行 1521），`cleanup_resources` 中 destroy（行 967） |
| `me->netif` | Owned | `esp_netif_create_default_wifi_sta` 创建（行 559），`cleanup_resources` 中 destroy（行 1053） |
| `rx_data.topic`（回调中） | Owned by wifi_link | `malloc` + `free` 包裹回调（行 1661/1677），回调期间 borrowed |
| `rx_data.data`（回调中） | Borrowed from esp_mqtt | `event->data`，仅回调期间有效 |
| `network_link_t *` 返回值 | Owned by caller | `wifi_link_create` 返回，注入 `network_manager` 后由 manager 管理 |

### Failure-path review

| 失败路径 | 完备性 | 说明 |
|----------|--------|------|
| `calloc(wifi_link_t)` 失败 | ✅ | 返回 NULL，无资源泄漏（行 426-429） |
| `xSemaphoreCreateMutex` 失败 | ✅ | `free(me)` 返回 NULL（行 434-438） |
| `wifi_link_copy_config` 失败 | ✅ | 删 mutex + free_config + free(me) 返回 NULL（行 440-445） |
| `calloc(sub_table)` 失败 | ✅ | 删 mutex + free_config + free(me) 返回 NULL（行 448-454） |
| `start_impl` 中 netif/event/wifi init 失败 | ✅ | cleanup 标签回滚（行 685-701），调 `cleanup_resources` |
| `esp_mqtt_client_init` 失败 | ✅ | 返回 ESP_FAIL，`start_mqtt` 调用方记录 `start_failed`（行 1481） |
| `esp_mqtt_client_start` 失败 | ✅ | destroy client + set NULL（行 1553-1561） |
| `esp_mqtt_client_stop` 返回非预期错误 | ⚠️ | D-1：destroy 被跳过，客户端泄漏 |
| `strdup(topic)` 失败（subscribe） | ✅ | 返回 ESP_ERR_NO_MEM，entry 不标记 in_use（行 1373-1375） |
| `malloc(topic)` 失败（MQTT_EVENT_DATA） | ✅ | 记日志 + end_runtime_action + return（行 1662-1665） |
| `esp_mqtt_client_subscribe` 失败 | ✅ | 返回 ESP_FAIL（行 857），但订阅意图已存入 sub_table，重连时重放 |
| `esp_mqtt_client_publish` 失败 | ✅ | 返回 ESP_FAIL（wifi_link_internal.c:45），end_mqtt_op 正常执行 |
| netif destroy 条件 | ⚠️ | D-2：依赖 deinit 返回值，理论可跳过 |

### Cross-module contract review

- ✅ `wifi_link_create` 返回 `network_link_t *`，调用方（`main.c:133`）不感知子类类型
- ✅ config 字符串 owned（strdup），调用方释放原始 config 后安全
- ✅ `set_active` 未实现，wrapper 安全处理 NULL（`network_link.c:126-128`）
- ✅ wifi_link 不理解业务遥测字段，只做 MQTT 透传
- ✅ rx_cb 数据契约清晰：topic 为 wifi_link owned（回调期间 borrowed），data 为 esp_mqtt borrowed
- ✅ 不直接依赖 thingsboard_client 或上层业务模块
- ⚠️ publish 使用同步 `esp_mqtt_client_publish`（阻塞），可能影响 thingsboard_client 任务实时性（文档记载为非阻塞 enqueue）

### Residual risks

1. **MQTT 客户端泄漏（D-1）**：`esp_mqtt_client_stop` 返回非预期错误时客户端未 destroy，链路永久失效。实际触发概率低，但上机长时间运行可能暴露。
2. **`portMAX_DELAY` mutex 获取**：全模块 mutex 操作使用 `portMAX_DELAY`，若出现未预见的死锁，系统永久挂起无恢复机制。当前分析未发现死锁路径，但无法排除所有可能。
3. **MQTT 事件回调与 destroy 的竞态**：`esp_mqtt_client_destroy` 是否等待所有 pending 事件回调完成取决于 ESP-IDF 内部实现。若不等，destroy 后回调可能访问已释放的 `me`。当前代码通过 `runtime_action_count` 保护，但 `MQTT_EVENT_DISCONNECTED` 处理（行 1622-1631）不使用 runtime_action_count，仅依赖 mutex 序列化——若 destroy 在回调 take mutex 前释放了 `me`，存在 use-after-free 理论风险。
4. **Wi-Fi 事件回调阻塞 event loop**：Wi-Fi 事件处理器使用 `xSemaphoreTake(mutex, portMAX_DELAY)`（行 1421、1435），若 mutex 被长时间持有，ESP-IDF event loop 任务被阻塞，影响其他系统事件处理。当前 mutex 持有时间短（微秒级），实际风险低。
5. **同步 publish 阻塞调用方**：`esp_mqtt_client_publish` 在 QoS > 0 时阻塞至 PUBACK，若 broker 不可达，调用方任务（thingsboard_client）被阻塞直至 MQTT 内部超时。`runtime_action_count > 0` 期间 stop/destroy 无法推进。
