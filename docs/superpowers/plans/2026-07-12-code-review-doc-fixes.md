# Code Review 文档修复实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复 `docs/agents/code-review/` 中已确认有误的验证与汇总文档，使 verdict、统计和跨模块结论与源码证据一致。

**Architecture:** 先修 5 份 `verify-*.md`，把它们恢复为可信的单模块事实源；再基于修正后的 verdict 统一更新 `summary-2026-07-07.md` 和 `review-list.md`。全程只改 Markdown 文档，不修改 `main/` 下任何源码。

**Tech Stack:** Markdown / ripgrep (`rg`) / 仓库内源码与第三方头文件引用。

**关联设计文档:** `docs/superpowers/specs/2026-07-12-code-review-doc-fixes-design.md`

**提交策略:** 用户 AGENTS.md 规定未经明确许可不得提交。本计划不包含 git commit 步骤；执行时只修改文件并做一致性验证。

---

## 文件结构

| 文件 | 操作 | 职责 |
|------|------|------|
| `docs/agents/code-review/verify-03-button.md` | 修改 | 补齐 `L3/L4` 验证结论，收敛 `M1` 结论强度 |
| `docs/agents/code-review/verify-04-bl0942.md` | 修改 | 将 `BL0942-FAULT-STOP` 从完全确认改为部分正确 |
| `docs/agents/code-review/verify-07-wifi_link.md` | 修改 | 将 `D-1` 改判为误报，收敛 `H-1` 阻塞语义表述 |
| `docs/agents/code-review/verify-10-safety_guard.md` | 修改 | 保留丢事件风险，修正 `esp_event_post` 失败机制描述 |
| `docs/agents/code-review/verify-14-app_controller.md` | 修改 | 收敛 `M1/M3/M5/M8` 的结论与论据 |
| `docs/agents/code-review/summary-2026-07-07.md` | 修改 | 更新统计、跨模块模式、Top 发现与方法说明 |
| `docs/agents/code-review/review-list.md` | 修改 | 使阶段完成状态与修正后的验证覆盖一致 |

---

### Task 1: 修复 `verify-03-button.md`

**Files:**
- Modify: `docs/agents/code-review/verify-03-button.md`
- Reference: `docs/agents/code-review/report-03-button.md`
- Reference: `main/button/button.c`
- Reference: `managed_components/espressif__button/iot_button.c`

- [ ] **Step 1: 先确认当前 verify 的覆盖缺口**

Run:

```bash
rg -n "^### (M1|M2|M3|L1|L2|L3|L4)" \
  "docs/agents/code-review/report-03-button.md" \
  "docs/agents/code-review/verify-03-button.md"
```

Expected:
- `report-03-button.md` 中出现 `M1 M2 M3 L1 L2 L3 L4` 共 7 条。
- `verify-03-button.md` 中当前只出现 `M1 M2 M3 L1 L2`，缺少 `L3/L4`。

- [ ] **Step 2: 把 `M1` 从“完全确认”改为“部分正确”**

在 `verify-03-button.md` 的 `## ⚠️ 部分正确（需调整修复方案）` 小节中加入并使用下面这段替换原来的 `M1` 结论，不再把“可直接 free `me`”写成确定结论：

```markdown
### M1. `button_destroy` 在 `button_iot_delete` 失败时泄漏 `me` 和 mutex

- **原报告条目**: M1（`button.c:258-262`）
- **调整说明**: 重新阅读 `button.c:229-270`，可确认 `button_iot_delete()` 失败时函数直接 `return ret`，导致 `me` 和 `me->mutex` 泄漏，这一泄漏路径成立。

  但原报告中“调用方无法恢复”和“此时可安全释放 `me`”的结论说得过满。`button_unregister_iot_callbacks()` 会吞掉重复注销得到的 `ESP_ERR_INVALID_STATE`（`button.c:397-410`）；第三方 `iot_button_delete()` 只有在 `btn->driver->del()` 成功后才会把对象从全局链表移除并 `free(entry)`（`managed_components/espressif__button/iot_button.c:680-689`）。因此，更准确的结论是：这里会留下一个部分拆毁且泄漏的对象，但不能仅凭现有证据断言 button 层可以在 `button_iot_delete()` 失败后继续 `vSemaphoreDelete()/free(me)` 而不破坏底层契约。
```

- [ ] **Step 3: 补齐 `L3` 和 `L4` 的验证结论**

在 `## ✅ 确认的问题` 小节末尾追加下面两段：

```markdown
### L3. button.h 未禁止从回调中调用 `button_register_cb(cb=NULL)`——会导致死锁

- **原报告条目**: L3（`button.h:88-92` 对照 `button.c:351-369`）
- **验证结论**: 确认。`button_dispatch()` 在回调前递增 `active_callbacks[event]`（`button.c:329-334`），回调返回后才在 `button.c:342-347` 递减。若当前回调内部调用 `button_register_cb(me, event, NULL, NULL)`，函数会在 `button.c:292-293` 进入 `button_wait_event_callbacks_drained()`，其循环在 `button.c:356-365` 等待 `active_callbacks[event]` 归零；但当前回调因为被 drain 阻塞而无法返回，形成自锁。原报告结论成立。

### L4. `button_iot_adapter.c` 中 `long_press_time=0, short_press_time=0` 缺少注释说明

- **原报告条目**: L4（`button_iot_adapter.c:60-63`）
- **验证结论**: 确认。`button_iot_adapter.c:60-63` 将 `long_press_time` 和 `short_press_time` 置为 0，依赖第三方 iot_button 的“0 表示使用 Kconfig 默认值”约定。代码行为本身正确，但本地代码中没有注释说明这一语义，原报告将其归为低严重度的文档/可维护性问题成立。
```

- [ ] **Step 4: 重新整理本文件的判定分布并验证没有遗漏**

修改后，`verify-03-button.md` 应满足：

- `✅ 确认`: `M2 / M3 / L3 / L4`
- `⚠️ 部分正确`: `M1 / L1 / L2`
- `❌ 误报`: 无

Run:

```bash
rg -n "^### (M1|M2|M3|L1|L2|L3|L4)" "docs/agents/code-review/verify-03-button.md"
```

Expected: 7 条 heading 全部出现，无遗漏。

---

### Task 2: 修复 `verify-04-bl0942.md`

**Files:**
- Modify: `docs/agents/code-review/verify-04-bl0942.md`
- Reference: `main/bl0942/bl0942.c`
- Reference: `main/bl0942/bl0942.h`

- [ ] **Step 1: 确认当前 `FAULT-STOP` 被写成“完全确认”**

Run:

```bash
rg -n "BL0942-FAULT-STOP|## ✅|## ⚠️" "docs/agents/code-review/verify-04-bl0942.md"
```

Expected: 当前 `BL0942-FAULT-STOP` 位于 `## ✅ 确认的问题` 下。

- [ ] **Step 2: 将 `BL0942-FAULT-STOP` 改判为“部分正确”**

把该条目从 `## ✅ 确认的问题` 挪到 `## ⚠️ 部分正确（需调整修复方案）` 下，并替换为：

```markdown
### BL0942-FAULT-STOP — 故障停止条件在多数配置下失效

- **原报告条目**: `main/bl0942/bl0942.c:1130-1131, 1141-1144` — 故障停止条件失效
- **调整说明**: 重新阅读 `bl0942.c:1116-1154` 可确认，`hard_reset_count` 只会在 `en_gpio != GPIO_NUM_NC && hard_reset_count < hard_reset_max_attempts` 的分支内递增，而停止条件又要求 `hard_reset_count >= hard_reset_max_attempts && hard_reset_max_attempts > 0`。因此，当前实现的自停行为确实只覆盖“有 EN 且达到硬复位上限”的情形。

  但原报告把这一点直接定性为“已确认缺陷”证据不足。`bl0942_config_t.hard_reset_max_attempts` 的公开契约只声明为“硬复位最大次数”（`bl0942.h:57-58`），并未明确定义 `en_gpio == GPIO_NUM_NC` 或 `hard_reset_max_attempts == 0` 时系统是否也必须在若干 fault 周期后自停。更准确的结论是：当前实现的自停语义边界没有在公开配置契约中说清楚，属于设计/文档缺口；只有在项目明确期望“无 EN 或 max=0 也要有限容忍后停机”时，这里才构成真实缺陷。
```

- [ ] **Step 3: 验证本文件最终判定分布**

修改后，`verify-04-bl0942.md` 应满足：

- `✅ 确认`: `BL0942-MUTEX-DELAY / BL0942-DOC-DRIFT / BL0942-DESTROY-FAULT-NOISE / BL0942-TASK-COMPLEXITY`
- `⚠️ 部分正确`: `BL0942-FAULT-STOP`
- `❌ 误报`: 无

Run:

```bash
rg -n "BL0942-FAULT-STOP|BL0942-MUTEX-DELAY|BL0942-DOC-DRIFT|BL0942-DESTROY-FAULT-NOISE|BL0942-TASK-COMPLEXITY|^## ✅|^## ⚠️" \
  "docs/agents/code-review/verify-04-bl0942.md"
```

Expected: `BL0942-FAULT-STOP` 只出现在 `⚠️` 小节中。

---

### Task 3: 修复 `verify-07-wifi_link.md`

**Files:**
- Modify: `docs/agents/code-review/verify-07-wifi_link.md`
- Reference: `main/network/wifi/wifi_link.c`
- Reference: `main/network/wifi/wifi_link_internal.c`
- Reference: `/Users/jovisdreams/Library/Caches/Espressif/ComponentManager/service_d92d8f1e/espressif__mqtt_1.0.0_ffdad565/include/mqtt_client.h`

- [ ] **Step 1: 先确认 `D-1` 目前被错误归为“确认的问题”**

Run:

```bash
rg -n "D-1|H-1|^## ✅|^## ❌|^## ⚠️" "docs/agents/code-review/verify-07-wifi_link.md"
```

Expected: `D-1` 当前位于 `## ✅ 确认的问题` 中，`## ❌ 误报` 为空。

- [ ] **Step 2: 把 `D-1` 改判为误报**

在 `## ❌ 误报` 小节中加入下列内容，并从 `## ✅ 确认的问题` 中删除原 `D-1` 块：

```markdown
### D-1: `esp_mqtt_client_stop` 返回非 ESP_OK/ESP_FAIL 时 MQTT 客户端资源泄漏

- **原报告条目**: D-1（`wifi_link.c:966`）
- **驳回理由**: 当前项目依赖的 `esp_mqtt_client_stop()` 公开契约只声明 `ESP_OK`、`ESP_ERR_INVALID_ARG`、`ESP_FAIL` 三种返回值（`mqtt_client.h:453-456`）。`wifi_link_cleanup_resources()` 已在 `ret == ESP_OK || ret == ESP_FAIL` 两种文档化运行态结果下继续执行 `esp_mqtt_client_destroy()`（`wifi_link.c:959-967`）。原报告以 `ESP_ERR_INVALID_STATE` 作为既定返回值建立泄漏链条，缺少当前版本 ESP-MQTT 证据支撑，因此这一条应改判为误报。若后续想增强健壮性，可把它单独记为“对未来未文档化返回码不够防御”的稳健性建议，但不是当前已确认缺陷。
```

- [ ] **Step 3: 收敛 `H-1` 的阻塞语义表述，但保留文档漂移结论**

把 `H-1` 的“验证结论”替换为下面这段：

```markdown
- **验证结论**: 确认文档漂移。`rg "esp_mqtt_client_enqueue"` 搜索结果为空，全项目未使用 `enqueue` API；`wifi_link_internal.c:42-45` 实际调用的是 `esp_mqtt_client_publish()`。因此，文档记录的“非阻塞入队”与实现使用的“在调用方上下文中直接 publish”并不一致。

  需要收敛的是原报告对阻塞阶段的细化描述。基于当前仓库证据，可以确认 `publish()` 可能阻塞，且其语义不同于 `enqueue()`；但不应在这里把行为写死为“QoS 1/2 阻塞到某个 ACK 阶段”，因为这一点超出了当前代码与头文件能够直接证明的范围。
```

- [ ] **Step 4: 验证本文件最终判定分布**

修改后，`verify-07-wifi_link.md` 应满足：

- `✅ 确认`: `H-1 / H-2 / H-3 / H-4 / G-1 / G-2 / G-3 / C-1 / D-2 / D-3 / G-4`
- `❌ 误报`: `D-1`
- `⚠️ 部分正确`: 无

Run:

```bash
rg -n "D-1|H-1|H-2|H-3|H-4|G-1|G-2|G-3|C-1|D-2|D-3|G-4|^## ✅|^## ❌|^## ⚠️" \
  "docs/agents/code-review/verify-07-wifi_link.md"
```

Expected: `D-1` 只出现在 `## ❌ 误报`，文件中不再出现“所有发现均确认”的说法。

---

### Task 4: 修复 `verify-10-safety_guard.md`

**Files:**
- Modify: `docs/agents/code-review/verify-10-safety_guard.md`
- Reference: `main/safety/safety_guard.c`
- Reference: `main/app/app_controller.c`
- Reference: `/Users/jovisdreams/.espressif/v5.5.3/esp-idf/components/esp_event/esp_event.c`

- [ ] **Step 1: 把第一条从“完全确认”改为“部分正确”**

将当前第一条从 `## ✅ 确认的问题` 挪到 `## ⚠️ 部分正确（需调整修复方案）`，并替换为：

```markdown
- **原报告条目**: 🟡 safety_guard.c:520-534 — `esp_event_post` 失败时安全快照被静默丢弃
  - **调整说明**: 核心结论成立：`safety_guard_post_snapshot()` 在 `esp_event_post()` 失败时仅记录 `ESP_LOGW`；`app_controller_on_safety_snapshot()`（`app_controller.c:466-486`）又是唯一会执行 `relay_set(RELAY_SOURCE_SAFETY, false)` 的订阅者，因此当前周期的断开建议确实可能丢失。

    但原报告中“同一 default loop 中 10ms 超时必然到期”的机制说明不准确。当前 ESP-IDF `esp_event_post_to()` 在 dedicated loop task 向自身 post 时走的是 `xQueueSendToBack(..., 0)`（`esp_event.c:963-966`），因此如果事件队列已满会立即失败，而不是等待传入的 10ms 超时。更准确的结论是：这里存在同 loop post 的丢事件风险，但触发机制是“队列满时立即失败”，不是“10ms 必然超时”。
```

- [ ] **Step 2: 验证本文件最终判定分布**

修改后，`verify-10-safety_guard.md` 应满足：

- `✅ 确认`: 文档漂移 2 条 + 低严重度 3 条，共 5 条
- `⚠️ 部分正确`: `esp_event_post` 丢快照机制说明 1 条
- `❌ 误报`: 无

Run:

```bash
rg -n "safety_guard.c:520-534|^## ✅|^## ❌|^## ⚠️" "docs/agents/code-review/verify-10-safety_guard.md"
```

Expected: 第一条出现在 `⚠️` 小节，不再声称“10ms 超时必然到期”。

---

### Task 5: 修复 `verify-14-app_controller.md`

**Files:**
- Modify: `docs/agents/code-review/verify-14-app_controller.md`
- Reference: `main/app/app_controller.c`
- Reference: `main/network/wifi/wifi_link_internal.c`
- Reference: `main/safety/safety_guard.c`
- Reference: `docs/agents/err.md`
- Reference: `docs/agents/classes.md`

- [ ] **Step 1: 将 `M1/M3/M5/M8` 从“确认的问题”改为“部分正确”**

把这 4 条从 `## ✅ 确认的问题` 挪到 `## ⚠️ 部分正确（需调整修复方案）`，并分别替换为以下内容。

`M1` 用：

```markdown
### M1. `app_controller_destroy` 在 `stop` 失败时泄漏对象和 mutex
- **原报告条目**: M1
- **调整说明**: 重新阅读 `app_controller.c:255-273`，可确认 `app_controller_stop(me)` 失败时函数直接返回，跳过 `vSemaphoreDelete(me->mutex)` 和 `free(me)`，因此存在 leak-on-stop-failure 路径。

  但原验证把 `err.md §3.2` 当作“stop 失败时也必须强制 free”的规范依据不准确。`err.md §3.2` 只明确 `destroy(NULL)` 要安全返回，并没有直接规定“stop 失败后必须无条件释放对象”。更准确的结论是：泄漏路径成立；至于 stop 失败后是否还能继续强制回收 `me`，需要先明确生命周期契约及是否仍有 in-flight handler / module teardown。
```

`M3` 用：

```markdown
### M3. 上行遥测 publish 在 esp_event 任务中执行，可能延迟安全保护判定
- **原报告条目**: M3
- **调整说明**: handler 注册顺序与同步 publish 路径的事实成立：`app_controller` 的 `METERING_EVENT_SNAPSHOT` handler 先注册，且 `app_controller_on_metering_snapshot()` 会在当前事件回调中直接调用 `thingsboard_client_publish_telemetry()`，因此 `safety_guard` 的规则判定确实可能被延后。

  但原验证中“Wi-Fi 路径可忽略，因为使用 `esp_mqtt_client_enqueue()` 非阻塞”的论据不成立。当前 Wi-Fi 实现调用的是 `esp_mqtt_client_publish()`（`wifi_link_internal.c:42-45`），不是 `enqueue()`。更准确的结论是：该时延风险并非 LTE 独有，Wi-Fi/LTE 都会在当前回调上下文中直接 publish，只是 LTE 路径更可能出现更长阻塞。
```

`M5` 用：

```markdown
### M5. `on_tb_command` 命令处理失败时不发送 RPC 错误响应
- **原报告条目**: M5
- **调整说明**: `GET_POWER_LIMIT` 查询类 RPC 在本地失败时不发送 response，这一点成立：`safety_guard_get_thresholds()`、格式化或 `send_rpc_response()` 任一失败都会让 ThingsBoard 侧等待到超时。

  但原验证把 `SET_RELAY` 和 `SET_POWER_LIMIT` 也一并写成“缺少错误响应 bug”证据不足。当前仓库内示例只明确 `GET_POWER_LIMIT` 需要 response（`classes.md:2138-2146`），而 `SET_*` 当前实现更接近 fire-and-forget。更准确的结论应聚焦在查询类 RPC 的 response 契约，而不是把所有命令失败都定性为相同缺陷。
```

`M8` 用：

```markdown
### M8. classes.md §15.5 `SET_POWER_LIMIT` 示例传 0 作 overcurrent 阈值
- **原报告条目**: M8
- **调整说明**: 文档示例与实际实现不一致，这一点成立；`classes.md:2145` 把 `overcurrent` 直接写成 0，而实际代码会先读当前阈值再只更新功率阈值。

  但原验证把后果写成“危险错误模式”过重。`safety_guard_set_thresholds()` 在 `safety_guard.c:324-325` 明确拒绝 `overcurrent_a <= 0` 或 `overpower_w <= 0`，因此照抄该示例的真实结果是返回 `ESP_ERR_INVALID_ARG`，而不是静默把过流阈值改成 0 生效。
```

- [ ] **Step 2: 保留其余条目为确认问题，不改动它们的 verdict**

保留在 `## ✅ 确认的问题` 中的应为：

- `M2 / M4 / M6 / M7`
- `L1 / L2 / L3 / L4 / L5 / L6`

不要把这些条目一起改写成 `⚠️`。

- [ ] **Step 3: 验证本文件最终判定分布**

修改后，`verify-14-app_controller.md` 应满足：

- `✅ 确认`: 10 条
- `⚠️ 部分正确`: `M1 / M3 / M5 / M8`
- `❌ 误报`: 无

Run:

```bash
rg -n "^### (M1|M2|M3|M4|M5|M6|M7|M8|L1|L2|L3|L4|L5|L6)|^## ✅|^## ❌|^## ⚠️" \
  "docs/agents/code-review/verify-14-app_controller.md"
```

Expected: `M1/M3/M5/M8` 仅出现在 `⚠️` 小节。

---

### Task 6: 重建 `summary-2026-07-07.md` 与 `review-list.md`

**Files:**
- Modify: `docs/agents/code-review/summary-2026-07-07.md`
- Modify: `docs/agents/code-review/review-list.md`
- Reference: `docs/agents/code-review/verify-03-button.md`
- Reference: `docs/agents/code-review/verify-04-bl0942.md`
- Reference: `docs/agents/code-review/verify-07-wifi_link.md`
- Reference: `docs/agents/code-review/verify-10-safety_guard.md`
- Reference: `docs/agents/code-review/verify-11-thingsboard_client.md`
- Reference: `docs/agents/code-review/verify-14-app_controller.md`

- [ ] **Step 1: 先更新 summary 顶部统计表与模块分布表**

把 `summary-2026-07-07.md` 顶部统计替换为：

```markdown
| 指标 | 数值 |
|------|------|
| 审查模块数 | 14 |
| 报告文件数 | 14 report + 14 verify = 28 |
| 🔴 高严重度 | 0 |
| 🟡 中严重度 | 46 |
| 🟢 低严重度 | 53 |
| **发现总数** | **99** |
| ✅ 已确认 | 86 |
| ❌ 误报 | 1 |
| ⚠️ 部分正确 | 12 |

**说明**：无 🔴 高严重度发现。大多数 🟡 中严重度发现仍为真实问题，但并非全部都能维持“已确认”结论；阶段 3 中有若干条目被修正为 `⚠️ 部分正确`，另有 1 条被改判为 `❌ 误报`。
```

并把模块分布表中的 `✅确认` 一列替换为以下 6 行：

```markdown
| 03 | button | 0 | 3 | 4 | 4(+3⚠️) | 驱动 |
| 04 | bl0942 | 0 | 3 | 2 | 4(+1⚠️) | 驱动 |
| 07 | wifi_link | 0 | 5 | 7 | 10(+1❌,1⚠️) | 网络 |
| 10 | safety_guard | 0 | 3 | 3 | 5(+1⚠️) | 业务 |
| 11 | thingsboard_client | 0 | 1 | 4 | 3(+2⚠️) | 业务 |
| 14 | app_controller | 0 | 8 | 6 | 10(+4⚠️) | 应用 |
```

- [ ] **Step 2: 修正模式 1、模式 2、模式 4 的表述**

把 `summary-2026-07-07.md` 中以下三处替换为对应文本：

1. `模式 1` 开头段落改为：

```markdown
`destroy()` 调用 `stop()`，若 `stop()` 失败则直接 return 不释放 `me`/mutex，形成泄漏路径。该模式本身真实存在，但 `err.md` §3.2 只明确 `destroy(NULL)` 要安全返回，不能把它直接当作“stop 失败后也必须强制 free”的规范依据。
```

2. `模式 2` 的标题和说明改为：

```markdown
### 模式 2：`esp_event_post` 失败处理不完整（2 模块，含安全影响）

`esp_event_post` 可能因事件队列满而失败。当前部分模块在失败后仅记日志继续，导致状态不一致或本周期事件丢失。
```

并删除 `relay` 那一行，把表格保留为 `metering_service` 与 `safety_guard` 两个模块；表格下补一句：

```markdown
`relay` 的问题应归类为“事件发布时序 / TOCTOU 竞态”，而不是“post 失败处理不完整”。
```

3. `模式 4` 的最后一条说明改为：

```markdown
- 存在安全路径时延风险：本地闭环仍然成立，但 telemetry publish 插入在 safety 判定之前，削弱了时延可预测性
```

- [ ] **Step 3: 重写“Top 已确认发现”区块，避免再把部分正确或误报写成已确认**

用下面这整段替换 `## 三、Top 已确认发现（按影响排序）` 到下一条 `---` 之间的内容：

```markdown
## 三、Top 发现（按影响排序）

以下为本次审查中影响最大的 10 条发现，混合列出 `✅ 已确认` 与 `⚠️ 部分正确` 条目；`❌ 误报` 不进入本节。

### 安全相关（最高优先级）

1. **✅ `metering_service.c:700-712` (MS-01)** — `esp_event_post` 失败后 pending 电能增量未回滚 → 服务静默卡死 → `safety_guard` 失去过流/过功率检测输入
2. **⚠️ `app_controller.c:488-509, 697-747` (M3)** — metering handler 注册早于 safety_guard，遥测 publish 会延后安全判定；风险成立，但并非仅 LTE 路径存在，Wi-Fi 实现同样在当前回调上下文中直接 publish
3. **⚠️ `safety_guard.c:520-534`** — `esp_event_post` 失败会丢掉本周期 `DANGER/RELAY_OFF` 快照；核心风险成立，但触发机制是“同 loop 队列满时立即失败”，不是“10ms 超时必然到期”
4. **⚠️ `bl0942.c:1130-1144`** — 故障自停逻辑只覆盖“有 EN 且达到硬复位上限”的配置；源码事实成立，但是否构成缺陷取决于 `hard_reset_max_attempts` 的期望语义

### 资源泄漏 / 生命周期

5. **✅ `network_manager.c:391-393` (D-1)** — destroy 在 stop 失败时不调 `free_resources` → manager 泄漏且 `destroying=true` 导致系统不可恢复
6. **⚠️ `app_controller.c:255-273` (M1)** — destroy 在 stop 失败时泄漏 `me`/mutex；泄漏路径成立，但“应无条件强制 free”不是 `err.md §3.2` 的直接要求
7. **✅ `lvgl_dashboard.c:830-837` (M1)** — `tick_ctx` 在 destroy 中置 NULL 不释放 → 每次 `create→destroy` 泄漏约 20 字节

### 并发 / 时序

8. **✅ `lte_link.c:456`** — `set_active_impl` 无锁读取 `destroying/lwlte` → 与并发 `destroy_impl` 存在 TOCTOU 竞态
9. **✅ `relay.c:185,191-193`** — `relay_set` / `relay_toggle` 在释放 mutex 后发布状态变化事件 → TOCTOU 竞态，可导致事件乱序
10. **✅ `bl0942.c:1071-1107`** — `bl0942_hard_reset` 持有 mutex 跨两次 `vTaskDelay`（共 2s）→ 阻塞并发 `bl0942_read/get_latest`
```

- [ ] **Step 4: 修正文末“审查方法说明”和 `review-list.md` 的完成状态**

把 `summary-2026-07-07.md` 的方法说明里两条改成：

```markdown
- 当前汇总口径：99 条发现 = 86 条确认 + 12 条部分正确 + 1 条误报
- `⚠️ 部分正确` 主要集中在 button / bl0942 / wifi_link / safety_guard / thingsboard_client / app_controller
```

并把 `review-list.md:29` 改成：

```markdown
- 全部 14 个模块现已完成阶段 2（report-*.md）+ 阶段 3（verify-*.md）；其中 `button` 的阶段 3 覆盖缺口已在本次文档修复中补齐
```

- [ ] **Step 5: 做最终一致性检查**

Run:

```bash
rg -n "\| ✅ 已确认 \| 86 \||\| ❌ 误报 \| 1 \||\| ⚠️ 部分正确 \| 12 \|" \
  "docs/agents/code-review/summary-2026-07-07.md"

rg -n "4\(\+3⚠️\)|4\(\+1⚠️\)|10\(\+1❌,1⚠️\)|5\(\+1⚠️\)|10\(\+4⚠️\)" \
  "docs/agents/code-review/summary-2026-07-07.md"

rg -n "button.*阶段 3.*补齐" "docs/agents/code-review/review-list.md"
```

Expected:
- summary 顶部出现 `86 / 1 / 12` 新统计。
- 模块分布表出现新的 `button / bl0942 / wifi_link / safety_guard / app_controller` verdict 计数。
- review-list 明确写出 `button` 的 verify 覆盖缺口已补齐。

---

## 计划自检

### Spec 覆盖

- `verify-03-button.md` 的 `M1` 收敛与 `L3/L4` 补齐：Task 1 覆盖。
- `verify-04-bl0942.md` 的 `FAULT-STOP` 改判：Task 2 覆盖。
- `verify-07-wifi_link.md` 的 `D-1` 误报与 `H-1` 收敛：Task 3 覆盖。
- `verify-10-safety_guard.md` 的机制修正：Task 4 覆盖。
- `verify-14-app_controller.md` 的 `M1/M3/M5/M8` 收敛：Task 5 覆盖。
- `summary-2026-07-07.md` 的统计、跨模块模式、Top 发现与方法说明修正：Task 6 覆盖。
- `review-list.md` 的阶段完成状态修正：Task 6 覆盖。

### Placeholder Scan

本计划未使用 `TODO` / `TBD` / “类似 Task N” 这类占位表达；每个任务都给出具体文件、具体替换文本和验证命令。

### 命名一致性

本计划统一使用以下最终口径：

- `button`: `4 确认 + 3 部分正确`
- `bl0942`: `4 确认 + 1 部分正确`
- `wifi_link`: `10 确认 + 1 误报`
- `safety_guard`: `5 确认 + 1 部分正确`
- `app_controller`: `10 确认 + 4 部分正确`
- `summary` 总计：`86 确认 + 12 部分正确 + 1 误报 = 99`
