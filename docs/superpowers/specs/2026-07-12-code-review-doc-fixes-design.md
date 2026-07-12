# Code Review 文档修复设计文档

## 目标

修复 `docs/agents/code-review/` 中已经确认存在问题的审查文档，使其满足以下目标：

1. 已确认结论与源码证据一致。
2. `verify-*.md` 的判定类型准确区分为 `确认 / 误报 / 部分正确`。
3. `summary-2026-07-07.md` 的统计、跨模块归类、影响表述与各 `verify` 文件一致。
4. `review-list.md` 的阶段完成状态与实际验证覆盖范围一致。

本次工作只修正文档，不修改 `main/` 下任何源码。

## 背景

现有 code-review 文档总体上有价值，但存在三类问题：

1. 个别条目把“证据不足”写成“已确认问题”。
2. 个别条目把“现象成立”进一步扩展成了不准确的机制解释、影响范围或修复建议。
3. 汇总文档把局部结论进一步放大，导致统计与实际 `verify` 覆盖不完全一致。

这些问题如果不先修正文档，后续按报告直接修源码会带来错误修复风险。

## 修复范围

本次采用最小修复策略，只修改以下 7 个文件：

1. `docs/agents/code-review/verify-03-button.md`
2. `docs/agents/code-review/verify-04-bl0942.md`
3. `docs/agents/code-review/verify-07-wifi_link.md`
4. `docs/agents/code-review/verify-10-safety_guard.md`
5. `docs/agents/code-review/verify-14-app_controller.md`
6. `docs/agents/code-review/summary-2026-07-07.md`
7. `docs/agents/code-review/review-list.md`

## 非目标

本次不做以下工作：

1. 不重写全部 28 份 `report/verify` 文档。
2. 不重新执行整轮 14 模块代码审查。
3. 不修复任何源码问题。
4. 不改动 `docs/agents/review-checklist.md`、`classes.md`、`architecture.md` 等规范文档。

## 修复策略

### 策略 1：只改已确认有误的判定与表述

对每个目标文件，仅修改已经有明确反证或明显不自洽的内容，不顺手做风格重写。

这样做的原因：

1. 可以避免把本来正确的审查内容一并扰动。
2. 修改后的 diff 更容易复核。
3. 后续如果要继续修源码，文档基线更稳定。

### 策略 2：优先修 `verify`，再修 `summary`

单模块 `verify` 是汇总结论的上游依据。先修 `verify`，再根据修正后的判定更新 `summary` 与 `review-list`，避免再次引入统计不一致。

### 策略 3：不凭印象补结论，所有改动都回指源码或公共头文件

每个修正文案都要能回到已核对的证据，例如：

1. `mqtt_client.h` 的返回值契约。
2. `esp_event.c` 的同 loop `post` 行为。
3. 具体模块 `.c/.h` 中的真实实现与文档行为。

## 文件级设计

### 1. `verify-03-button.md`

修复目标：

1. 收敛 `M1` 的修复建议，不再把“`button_iot_delete` 失败后仍可安全 free `me`”写成确定结论。
2. 补齐遗漏的 `L3` 和 `L4` 验证结论，消除“report 有条目但 verify 未覆盖”的缺口。

修正原则：

1. `M1` 保留“泄漏路径成立”的结论。
2. `M1` 改成“修复方案需谨慎，当前不能仅凭现有证据断言可直接 free `me`”。
3. `L3` 明确补为已确认问题或至少给出完整判定。
4. `L4` 明确给出验证结论，而不是遗漏。

### 2. `verify-04-bl0942.md`

修复目标：

1. 将 `BL0942-FAULT-STOP` 从“完全确认的问题”调整为“部分正确”。

修正原则：

1. 保留源码事实：当前停止条件确实只覆盖“有 EN 且达到硬复位上限”的情形。
2. 删除“这已经是确定缺陷”的强结论。
3. 改写为“现有实现的语义边界未在公开配置契约中说明清楚，属于设计/文档缺口，是否视为 bug 取决于期望语义”。

### 3. `verify-07-wifi_link.md`

修复目标：

1. 将 `D-1` 从“确认的问题”调整为 `误报`。
2. 收敛 `H-1` 中对 `esp_mqtt_client_publish()` 阻塞语义的过强断言。

修正原则：

1. `D-1` 需要引用当前依赖头文件里 `esp_mqtt_client_stop()` 的公开返回值契约。
2. `H-1` 保留“文档写 enqueue、实现写 publish”的漂移结论。
3. `H-1` 不再把 `publish()` 细化描述成“QoS 1/2 阻塞到某个 ACK 阶段”，只保留“可能阻塞，且与 enqueue 的非阻塞语义不同”。

### 4. `verify-10-safety_guard.md`

修复目标：

1. 保留“`esp_event_post` 失败会丢这个周期的安全快照”这一核心结论。
2. 修正其失败机制说明。

修正原则：

1. 删除“同一 default loop 中 10ms 超时必然到期”的表述。
2. 改成“同 loop dedicated task 场景下会走 `xQueueSendToBack(..., 0)`，因此若队列已满会立即失败”。
3. 保持对后果的描述聚焦在“本周期断开建议可能丢失”。

### 5. `verify-14-app_controller.md`

修复目标：

1. 收敛 `M1`、`M3`、`M5`、`M8` 的结论强度。

修正原则：

1. `M1`：保留“stop 失败时泄漏”的事实，但删除“`err.md` 已要求必须强制 free”的规范性误引。
2. `M3`：保留“telemetry publish 可能延迟 safety 判定”的结论，但删除“Wi-Fi 可忽略因为 enqueue 非阻塞”的错误论据。
3. `M5`：把问题收敛到 `GET_POWER_LIMIT` 查询类 RPC 的 response 契约，不再把 `SET_RELAY` / `SET_POWER_LIMIT` 一概写成缺陷。
4. `M8`：把“危险错误模式”改成“示例错误且会导致 `ESP_ERR_INVALID_ARG`”，不再写成“会把过流阈值设成 0 生效”。

### 6. `summary-2026-07-07.md`

修复目标：

1. 修正统计口径。
2. 修正跨模块模式 2 与模式 4 的不准确表述。
3. 修正对 `err.md §3.2` 的规范引用过度。

修正原则：

1. 统计必须与修正后的 `verify` 文件一致。
2. summary 需要明确：原先并非 99 条发现都完成了完整验证，`button` 存在补验证前的缺口。
3. `误报率 0%` 不再保留为全量结论。
4. 模式 2 不再把 `relay` 的 TOCTOU 竞态归类为“post 失败处理不完整”。
5. 模式 4 改成“存在安全路径时延风险”，不再写成“违反本地闭环”。
6. 模式 1 中关于 `destroy` 的讨论，不再引用 `err.md §3.2` 作为强规范依据。

### 7. `review-list.md`

修复目标：

1. 使阶段完成状态描述与实际 verify 覆盖一致。

修正原则：

1. 在本次补齐 `button` verify 后，可以恢复“全部完成阶段 2+3”的表述。
2. 若实现过程中决定不补齐 `button` 的 `L3/L4`，则必须把 `review-list.md` 改成更保守的说明。但本设计以“补齐 verify”为目标，因此最终状态应是“阶段 3 已完整补齐”。

## 验证设计

完成文档修改后，按以下顺序验证：

1. 逐个检查 5 份 `verify`：
   - 是否仍保持 `原报告条目 -> 验证结论` 的结构。
   - 是否存在遗漏条目。
   - 判定类型是否与证据匹配。
2. 检查 `summary-2026-07-07.md`：
   - 总数是否等于各模块 `report` 条目总和。
   - `确认 / 误报 / 部分正确` 是否与全部 `verify` 汇总一致。
   - 跨模块模式是否只归纳相同类型的问题。
3. 检查 `review-list.md`：
   - 完成状态描述是否与修正后的 `verify` 覆盖一致。

## 风险与取舍

### 风险 1：局部修复后，summary 仍可能引用未修的 report 原文

应对：summary 只以修正后的 `verify` 结论为准，不直接继承原 `report` 的强表述。

### 风险 2：为了最小改动，部分 report 中仍保留过强措辞

应对：本次明确只修 `verify + summary + review-list`。后续若需要把 `report` 也收敛，可单独开一次文档清理任务。

### 风险 3：统计口径改变后，与历史聊天记录中的数字不再一致

应对：以仓库内修正后的文档为准，不追求与历史对话输出保持一致。

## 完成标准

当以下条件同时满足时，本次文档修复算完成：

1. 目标 7 个文件全部更新。
2. `verify-03-button.md` 不再遗漏 `L3/L4`。
3. `verify-07-wifi_link.md` 不再把 `D-1` 记为确认问题。
4. `summary-2026-07-07.md` 的统计与各 `verify` 文件一致。
5. 全程未修改任何 `main/` 下源码文件。
