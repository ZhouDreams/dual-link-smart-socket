# Verification: lte_link

## 验证方法

对报告中的每条发现，重新阅读源码前后 ≥30 行上下文，使用 `rg` 搜索调用点和条件分支，检查遗漏的已有防护逻辑。

---

## ✅ 确认的问题

### C-1: `lte_link_set_active_impl` 无锁读取 `destroying` / `lwlte`

- **原报告条目**: C-1
- **验证结论**: 确认。

  重新阅读 `lte_link.c:442-473`（`lte_link_set_active_impl` 完整函数）：`me->destroying`（行 456）和 `me->lwlte`（行 461、466）均在未持有 `me->mutex` 的情况下读取。

  对比 `lte_link_destroy_impl`（`lte_link.c:192-246`）：`me->destroying = true` 在 mutex 保护下写入（行 203）；`lwlte_destroy(lwlte)` 在行 226 释放 lwlte 对象，`me->lwlte = NULL` 在行 230。

  `rg` 搜索确认 `network_link_destroy` 在 `main/` 下除声明外**从未被调用**（仅 `network_link.h:47` 声明、`network_link.c:43` 定义），因此当前代码库中该竞态不会被触发。但 `network_manager.c:443` 和 `network_manager.c:1249,1257` 确认 `set_active` 在 `network_manager` mutex 保护下调用，而 `destroy` 若被调用则不经过 `network_manager` mutex（`network_link_destroy` 是基类直调）。模块自身缺少防护。

  **严重度维持 🟡 中**：真实数据竞争 + 潜在 use-after-free，但当前无触发路径，且 network_manager 的正常停机流程（先停 monitor → 再停链路 → 后 destroy）在实践中间接串行化。

### C-2: `lte_link_wait_rx_callbacks_drained` 无超时上限

- **原报告条目**: C-2
- **验证结论**: 确认。

  重新阅读 `lte_link.c:475-490`：循环条件为 `while (true)`，退出条件仅 `active_rx_callbacks == 0`（行 485-486），无最大迭代次数或总超时。`vTaskDelay(pdMS_TO_TICKS(10))`（行 488）是唯一的时间相关调用，仅控制轮询间隔。

  搜索确认无其他调用点传入超时参数（函数签名 `lte_link_wait_rx_callbacks_drained(lte_link_t *me)` 无 timeout 参数）。该函数被 `destroy_impl`（行 210）和 `register_rx_cb_impl`（行 437，cb==NULL 时）调用。若 rx_cb 阻塞，destroy 永久挂起。

  **严重度维持 🟡 中**：非热路径，但系统级影响（挂死）。

### H-1: 文档漂移 — create 阻塞语义

- **原报告条目**: H-1
- **验证结论**: 确认。

  `classes.md:1959` 原文："`lte_link_create()` 内部调用 `lwlte_air780ep_init()`，**阻塞**直到 esp-lwlte ready"。

  `lte_link.c:643-644` 注释："Creates facade only — does not start module, wait for AT ready, or activate PDP."

  `lwlte.h:532`（`lwlte_air780ep_init` 的 `@note`）："该函数只创建 LTE 用户门面及内部对象，不启动模块、不等待 AT ready、不激活 PDP。"

  三方交叉验证：classes.md 的"阻塞"描述与实现注释和 esp-lwlte API 文档均矛盾。文档错误。

### H-2: 文档漂移 — start 语义

- **原报告条目**: H-2
- **验证结论**: 确认。

  `classes.md:1966` 原文："`lwlte_connect(lwlte)`；若 `mqtt_enabled` → `lwlte_mqtt_start(lwlte)`"。

  `rg` 搜索 `lwlte_connect` 在 `main/network/lte/` 和 `lwlte.h` 中均无结果——该 API 不存在。实际 API 是 `lwlte_start`（`lwlte.h:595`）。

  `lte_link.c:279`：`ret = lwlte_start(me->lwlte)` — 仅启动 LTE 网络，不启动 MQTT。

  `lte_link.c:460-464`：`lwlte_mqtt_start(me->lwlte)` 在 `set_active_impl(true)` 中调用。

  `network_manager.c:443`：`network_link_set_active(selected, true)` 在 `network_manager_start` 中于 `network_link_start` 之后调用。

  确认 start 不启动 MQTT，MQTT 由 set_active 控制。文档描述的 API 名（`lwlte_connect`）和行为（start 即启动 MQTT）均错误。

### H-3: 文档漂移 — 结构体定义

- **原报告条目**: H-3
- **验证结论**: 确认。

  `rg -n "cached_status" main/network/lte/` 返回空——实现中无此字段。
  `lte_link.c:49-69` 的 `lte_link_t` 定义中无 `cached_status`，状态通过 `lte_link_query_status`（行 703-726）实时查询 `lwlte_get_state` / `lwlte_mqtt_get_state`。

  `lte_link.c:57`：`lwlte_handle_t *lwlte`（`lwlte_handle_t` = `struct lwlte_t *`，`lwlte.h:38`）。classes.md §14.3 写 `lwlte_t *lwlte`——项目中无 `lwlte_t` typedef，只有 `struct lwlte_t` 和 `lwlte_handle_t`。

  `lte_link_internal.h:36-40`：`lte_link_sub_entry_t` 使用 `char *topic`（堆指针）。classes.md §14.3 写 `char topic[LTE_LINK_MAX_TOPIC_LEN]`（固定数组）。

  全部漂移项确认。

### H-4: 文档漂移 — `auto_connect` 字段不存在

- **原报告条目**: H-4
- **验证结论**: 确认。

  `rg -n "auto_connect" main/network/lte/` 返回空。`lte_link.h:37-56` 的 `lte_link_config_t` 中无此字段。`main.c:155-174` 的配置初始化中也未设置 `auto_connect`。

  `classes.md:1900` 列有 `bool auto_connect`，`classes.md:2003` 描述其行为。文档过时。

### H-5: 文档漂移 — ops 表未记录 `set_active`

- **原报告条目**: H-5
- **验证结论**: 确认。

  `rg -n "set_active" docs/agents/classes.md` 返回空——classes.md 中无 `set_active` 的任何提及。

  `lte_link.c:118-128`：`lte_link_ops` 包含 `.set_active = lte_link_set_active_impl`。
  `network_link_priv.h:43`：`network_link_ops_t` 定义了 `set_active` 函数指针。
  `network_link.h:137-147`：`network_link_set_active` 包装 API 有完整 Doxygen 文档。
  `network_manager.c:443,1249,1257`：network_manager 实际调用 `network_link_set_active`。

  确认 `set_active` 是实现中存在且被使用的 ops 方法，但 classes.md §14.4 的 ops 映射表遗漏。

### G-1: `lte_link_from_base` 直接强转

- **原报告条目**: G-1
- **验证结论**: 确认。

  `lte_link.c:187-190`：
  ```c
  static lte_link_t *lte_link_from_base(network_link_t *base)
  {
      return (lte_link_t *)base;
  }
  ```
  直接强转，无 `container_of`。`wifi_link.c:463-466` 使用相同模式。oop-design.md §2.4 推荐 `container_of`。项目范围统一约定，技术正确（base 在偏移 0）。

### F-1: `LWLTE_STATE_DESTROYING` 未显式处理

- **原报告条目**: F-1
- **验证结论**: 确认。

  `lte_link_internal.c:65-81`：`switch (lte_state)` 有 `case` for STOPPED、STARTING、READY、NET_ACTIVATING、ONLINE，`default` 返回 ERROR。`LWLTE_STATE_ERROR` 和 `LWLTE_STATE_DESTROYING`（`lwlte.h:56-57`）均落入 default。功能正确但不够显式。

### D-1: `destroy_impl` 吞掉 `lwlte_destroy` 错误

- **原报告条目**: D-1
- **验证结论**: 确认。

  `lte_link.c:226`：`ret = lwlte_destroy(lwlte)` 赋值给 `ret`。
  `lte_link.c:228`：`ESP_LOGW` 记录错误。
  `lte_link.c:245`：`return ESP_OK` — 返回字面量，丢弃 `ret`。

  `ret` 在行 210 被 `wait_rx_callbacks_drained` 赋值后可能为非 OK，但行 212 `if (ret != ESP_OK) return ret` 已处理该路径。行 226 之后 `ret` 仅被 `lwlte_destroy` 赋值，但最终未使用。

### G-2: 缺少 MACROS section

- **原报告条目**: G-2
- **验证结论**: 确认。

  `lte_link.c` 行 114-128 为 STATIC VARIABLES section（含 ops 表），行 130 直接开始 GLOBAL FUNCTIONS section，中间无 MACROS section。`lte_link_internal.c:42-44` 有 MACROS section（空）。coding-style.md §源文件模板（行 73-79）要求该 section。

---

## ❌ 误报

无。

---

## ⚠️ 部分正确（需调整修复方案）

无。所有发现均经交叉验证确认为真实问题。

---

## 修复记录

本模块为 review-only，无代码改动。

---

## 模块交付清单

### Change summary

N/A（review-only，无代码改动）。

### Resource budget

| 资源 | 计算 | 大小 |
|------|------|------|
| `lte_link_t` 结构体 | `calloc(1, sizeof(*me))` | ~200 字节（ESP32-S3 32 位：base 8 + config ~80 + 5×char* 20 + lwlte* 4 + sub_table* 4 + int×3 12 + mutex 4 + rx_cb+rx_ctx 8 + int 4 + 2×handler 8 + 2×bool 2，padding 后 ~180-200） |
| `sub_table` | `calloc(max_subscriptions, sizeof(lte_link_sub_entry_t))` = `8 × (char* 4 + qos 4 + bool 1, padded 12)` | 96 字节（默认 max_subscriptions=8） |
| `sub_table[i].topic` | 按需 `malloc(strlen(topic)+1)` | 每个 ~几十字节，按实际 topic 长度 |
| lwlte 内部资源 | esp-lwlte 默认（config 传 0） | 由 esp-lwlte 组件决定，不在本模块控制范围 |
| mutex | `xSemaphoreCreateMutex()` | ~80 字节（FreeRTOS 互斥量对象） |

**峰值 heap**：`lte_link_t`(~200) + sub_table(~96) + mutex(~80) + 5×strdup(apn/broker_host/client_id/username/password, 各 ~20-40) + esp-lwlte 内部 ≈ ~1-2 KB（不含 esp-lwlte 内部）。远低于几十 KB 阈值。

**task stack**：本模块不创建任务。所有执行在调用方任务上下文（network_manager monitor task / esp_event task / app_controller task）。

**queue size**：无队列（esp-lwlte 内部队列由其自身管理，config 传 0 用默认）。

**buffer size**：无大 buffer。`lte_link_handle_mqtt_data` 中 `malloc(topic_len+1)` 为临时小 buffer，回调后立即 `free`。

### Lifecycle / ownership notes

| 数据 | 归属 | 说明 |
|------|------|------|
| `me->config` 中的 `const char *` 字段（apn, mqtt_broker_host, client_id, username, password） | **owned** by lte_link | `lte_link_copy_config` 通过 `strdup` 深拷贝；`me->config.apn` 等指向 owned 副本；`lte_link_free_config` 释放 |
| `lwlte_mqtt_config_t` 中的字符串（host, client_id, username, password） | **borrowed** by esp-lwlte，仅 `lwlte_mqtt_init` 期间 | `lwlte.h:834` 确认："config 及其字符串字段由调用方拥有，仅在该函数执行期间被借用；函数返回后调用方可释放或复用"。esp-lwlte 在 init 内部复制 |
| `me->lwlte` | **owned** by lte_link | `lwlte_air780ep_init` 创建，`lwlte_destroy` 释放；destroy 中先 `lwlte_destroy` 再 `free(me)`（逆序正确） |
| `sub_table` | **owned** by lte_link | `calloc` 创建，`free` 释放；topic 指针按需 malloc/free |
| `network_rx_data_t.topic`（回调中） | **owned** by lte_link，**borrowed** by rx_cb | `lte_link_handle_mqtt_data` 中 `malloc` → 传入 rx_cb → `free`；rx_cb 不得保留指针 |
| `network_rx_data_t.data`（payload） | **owned** by esp-lwlte | 指向 `data->msg.payload`；`lwlte_mqtt_event_data_release(data)` 在 rx_cb 返回后释放 |
| `lwlte_mqtt_event_data_t` | **owned** by esp-lwlte 事件系统 | `lte_link_handle_mqtt_data` 必须在返回前调 `lwlte_mqtt_event_data_release`（`lwlte.h:702-706`），已正确实现（行 850、859、870、884、904） |

### Failure-path review

| 失败场景 | 处理 | 评价 |
|----------|------|------|
| `calloc(lte_link_t)` 失败 | 返回 NULL | ✅ 正确 |
| `xSemaphoreCreateMutex` 失败 | `free(me)` 返回 NULL | ✅ 正确 |
| `lte_link_copy_config` 中 strdup 失败 | `lte_link_free_config` 清理已分配字符串，释放 mutex 和 me | ✅ 正确，逆序清理 |
| `calloc(sub_table)` 失败 | 释放 mutex + config + me | ✅ 正确 |
| `lte_link_init_lwlte` 中 `lwlte_air780ep_init` 失败 | 清理 sub_table + mutex + config + me | ✅ 正确 |
| `lte_link_init_lwlte` 中 `lwlte_mqtt_init` 失败 | `lwlte_destroy` + 清理全部 | ✅ 正确 |
| `lte_link_start_impl` 中 `register_events` 失败 | goto revert_started，恢复 started=false | ✅ 正确 |
| `lte_link_start_impl` 中 `lwlte_start` 失败 | unregister events + revert_started | ✅ 正确 |
| `lte_link_handle_mqtt_data` 中 `malloc(topic)` 失败 | 释放 data buffer + 减少 active_rx_callbacks | ✅ 正确，计数准确 |
| `lte_link_destroy_impl` 中 `lwlte_destroy` 失败 | `ESP_LOGW` 记录，继续清理，返回 ESP_OK | ⚠️ 见 D-1：错误被吞 |
| `lte_link_wait_rx_callbacks_drained` 超时 | 无超时 | ⚠️ 见 C-2：无界等待 |
| `esp_event_post` / queue send 失败 | 不适用 | 本模块不 post event / send queue |

### Cross-module contract review

| 契约 | 遵守情况 |
|------|----------|
| lte_link 只依赖 `network_link` + esp-lwlte | ✅ `lte_link.c` include `network_link_priv.h`（基类私有接口）+ `lwlte.h`；不 include `wifi_link.h`/`thingsboard_client.h` |
| lte_link 不解析业务遥测字段 | ✅ publish/subscribe 只透传 topic+payload，不解析内容 |
| lte_link 不直接操作继电器/BL0942/LVGL | ✅ 无相关 include 或调用 |
| rx_cb 回调不持 lte_link mutex | ✅ 行 855-857 注释明确记录不变量；行 867 释放 mutex 后行 895 调用 rx_cb |
| `network_manager` 只依赖基类 `network_link_t *` | ✅ network_manager 通过 `network_link_*` 包装 API 调用，不直接访问 `lte_link_t` |
| esp-lwlte 依赖通过 `EXTRA_COMPONENT_DIRS` | ✅ `classes.md:2004` 确认指向 `/Users/jovisdreams/Projects/esp-lwlte`；无版本锁定机制（见 Residual risks） |

### Residual risks

1. **esp-lwlte 版本/兼容性风险**：esp-lwlte 通过 `EXTRA_COMPONENT_DIRS` 指向本地路径，无 git submodule 锁定或版本号。esp-lwlte API 变更（如 `lwlte_state_t` 新增状态、`lwlte_mqtt_event_data_t` 结构调整）会直接破坏 lte_link 编译或运行时行为。建议通过 git submodule 或 CMake 版本检查锁定。

2. **`set_active` 与 `destroy` 的 TOCTOU 竞态**（见 C-1）：当前代码库中 `network_link_destroy` 从未被调用，竞态不会触发。但若未来增加停机/重启逻辑（如 OTA 重启前清理），需确保 `network_manager_stop`（停 monitor → 停链路）在 `network_link_destroy` 之前完成，或为 lte_link 增加操作计数 drain 机制。

3. **`destroy` 期间事件回调访问 `me` 的理论竞态**：`lte_link_destroy_impl` 在 `wait_rx_callbacks_drained` 后 `unregister_events`（行 217），但 `esp_event_handler_instance_unregister` 不保证等待正在执行的事件 handler 完成。若 handler 在 unregister 后仍执行（esp_event 内部竞争窗口），它访问 `me->destroying`（行 789/814）时 `me` 可能已被 `free`（行 243）。实际风险极低：handler 在 destroying=true 时立即返回，且 `free(me)` 在大量清理操作之后。但缺少 `esp_event_loop_run` 级别的同步保证。

4. **`portMAX_DELAY` mutex 等待**：多处使用 `portMAX_DELAY`（行 202、258、305、379、408、430、481、751、858、898），若任何 mutex 持有者阻塞，这些调用将永久等待。属 ESP-IDF 常规实践，但在安全关键路径上应考虑超时。

5. **`LWLTE_STATE_DESTROYING` 映射为 ERROR**：destroy 期间 `get_status` 返回 ERROR，可能导致 network_manager 误判链路故障并触发切换。但 destroy 通常在系统停机时调用，network_manager 自身也在停止，影响有限。
