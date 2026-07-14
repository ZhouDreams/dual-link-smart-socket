# Code Review Agent Instructions

本文件是给并行 review subagent 的共享指令。每个 subagent 负责一个模块，遵循 `docs/agents/review-checklist.md` 的阶段 2（审查）+ 阶段 3（验证），只产出报告，不修改源代码。

---

## 你的任务

审查分配给你的模块，产出两个文件：

1. `docs/agents/code-review/report-<review_run_id>-<NN>-<module>.md` — Phase 2 审查报告
2. `docs/agents/code-review/verify-<review_run_id>-<NN>-<module>.md` — Phase 3 验证报告

**绝对禁止修改任何源代码文件（main/ 下的 .c / .h）。只允许写入上述两个报告文件。**

---

## Step 1：阅读 review 流程

完整阅读 `/Users/jovisdreams/Projects/Smart_Socket/docs/agents/review-checklist.md`。该文件是唯一规范来源；本指令不复制维度内容，避免两份清单漂移。必须遵循：

- 阶段 2 和阶段 3 的完整流程
- 11 个 review 维度（A–K）
- 稳定 finding ID、问题类型、严重度和证据可信度规则
- §3 的全部误报防范规则
- §4.2 报告模板和 §4.3 验证模板

开始审查前记录调度者给出的 `review_run_id`、review mode（`snapshot` / `patch`）、审查基线和目标。缺少 `review_run_id` 时停止并向调度者报告 blocker，避免文件名碰撞。`patch` review 缺少基线或目标时同样停止，因为无法执行维度 I 和变更归因；`snapshot` review 可以只指定当前源码为目标，但必须在报告中明确没有 diff 基线。

---

## Step 2：阅读项目规则（按需）

根据你的检查维度，阅读以下文档：

- `docs/agents/architecture.md` — 分层（§3）、依赖规则（§8）、数据流（§6）
- `docs/agents/classes.md` — 你的模块的期望 API（搜索模块名）
- `docs/agents/err.md` — 错误处理规则（esp_err_t、ESP_RETURN_ON_*、ESP_GOTO_ON_*、cleanup 标签 `err`、变量 `ret`）
- `docs/agents/coding-style.md` — 代码格式（section 组织、Doxygen、双语注释）
- `docs/agents/oop-design.md` — C OOP（opaque handle、ops 表、继承——网络模块必读）

---

## Step 3：阅读源文件

阅读分配给你的模块的所有源文件（.c 和 .h）。

---

## Step 4：审查（Phase 2）——写 report-<review_run_id>-<NN>-<module>.md

按维度 A–K 逐条检查。每条发现必须包含：

- canonical finding ID；先搜索历史报告和 remediation 台账，已有 canonical ID 的同一问题必须沿用。旧 `C-1`/`M1` 等局部 ID 首次迁移时分配 canonical ID，并另记 `<历史报告>:<legacy ID>`
- **文件:行号**（精确定位）
- 严重度：🔴 高 / 🟡 中 / 🟢 低
- 类型：行为缺陷 / 契约缺口 / 可维护性 / 文档漂移 / 可选优化
- 证据可信度：High / Medium / Low
- 触发条件、证据、影响和建议方向

使用 review-checklist.md §4.2 的模板。报告用中文写（代码 / API / 文件路径保持原文）。

---

## Step 5：验证（Phase 3）——写 verify-<review_run_id>-<NN>-<module>.md

对报告中的**每一条**发现：

1. 读取完整函数、相关状态字段、直接调用者、被调用者、callback 注册点和 cleanup 路径
2. 用 `rg` 实际搜索调用点，追踪条件分支、锁域、ownership 和可达性
3. 检查是否有遗漏的已有防护逻辑；涉及第三方或硬件契约时记录匹配版本的官方证据，证据不足则标记待验证
4. 分类：
   - ✅ **确认的问题**：简述确认理由
   - ❌ **误报**：说明为什么不是真问题
   - ⚠️ **部分正确**：真正的问题是什么，修复方案应如何调整

使用 review-checklist.md §4.3 的模板。**必须包含"模块交付清单"**：

review-only 阶段的 confirmed/partial finding 默认记录 `Disposition=Pending`；Verification 分别填写 `Static` / `Host` / `Build` / `Hardware` 四层状态，不得把静态验证写成 host、build 或 hardware verified。

- **Change summary**：N/A（review-only，无代码改动）
- **Resource budget**：启动 heap / 运行 heap / 峰值 heap / task stack / queue size / buffer size；所有 `count*size` 显式计算，并区分静态计算、实测值和待实机测量，禁止编造未知数据
- **Lifecycle / ownership notes**：关键数据 borrowed vs owned 标注
- **Failure-path review**：malloc / queue / event / UART / SPI 传输失败 的失败路径是否完备
- **Cross-module contract review**：是否破坏分层契约
- **Structural delta**：N/A（review-only）；若审查目标本身是 patch，列出新增 helper / 状态字段 / 同步原语 / 公共 API 及冗余候选
- **Verification evidence**：静态推理、`rg`、host test、目标构建和外部证据分别覆盖什么；每层分别记录 `Verified` / `Failed` / `Not run` / `N/A`，Hardware 还可记录 `Pending`
- **Residual risks**：已知但未解决的问题、上机才可能暴露的风险

---

## Step 6：返回摘要

返回一条**简洁**的摘要消息（这是你返回给调度者的唯一消息）：

- 审查的模块名
- report 文件路径
- verify 文件路径
- 发现数量：🔴 N / 🟡 N / 🟢 N
- 验证结果：✅ N 确认 / ❌ N 误报 / ⚠️ N 部分
- 最严重的 1–2 个**已确认**发现（文件:行号 + 一句话描述）
- 注意到的跨模块问题和补丁结构熵增候选（供阶段 4/6 汇总）

---

## 关键规则

1. **工作目录**：`/Users/jovisdreams/Projects/Smart_Socket`
2. **绝对禁止修改源代码**（main/ 下的 .c / .h）
3. **绝对禁止修改现有文档**（docs/ 下的已有文件，包括 review-list.md）——只写入新的 run-ID report-*.md 和 verify-*.md，不覆盖历史审查证据；清单状态由调度者更新
4. 每条发现必须有 canonical finding ID 和**文件:行号**
5. 用 `rg` 实际搜索验证调用点——不要猜
6. 应用误报防范（§3）
7. 技术严谨——先验证再下结论
