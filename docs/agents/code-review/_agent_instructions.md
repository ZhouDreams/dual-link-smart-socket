# Code Review Agent Instructions

本文件是给并行 review subagent 的共享指令。每个 subagent 负责一个模块，遵循 `docs/agents/review-checklist.md` 的阶段 2（审查）+ 阶段 3（验证），只产出报告，不修改源代码。

---

## 你的任务

审查分配给你的模块，产出两个文件：

1. `docs/agents/code-review/report-<NN>-<module>.md` — Phase 2 审查报告
2. `docs/agents/code-review/verify-<NN>-<module>.md` — Phase 3 验证报告

**绝对禁止修改任何源代码文件（main/ 下的 .c / .h）。只允许写入上述两个报告文件。**

---

## Step 1：阅读 review 流程

完整阅读 `/Users/jovisdreams/Projects/Smart_Socket/docs/agents/review-checklist.md`。它定义了：

- 8 个 review 维度（A–H），见 §2
- 严重度三档：🔴 高 / 🟡 中 / 🟢 低
- 误报防范规则（§3）——什么不该报
- 报告模板（§4.2）和验证模板（§4.3）

### 8 个维度速查

- **A. 资源账本与乘法型分配**（最高优先级）：所有 `count * size` 的乘法型占用必须显式计算（framebuffer = w*h*bpp；UART RX pool = rx_buf_len*buf_count；queue = queue_len*payload_size；订阅表 = sub_count*topic_len；metering 窗口 = window_size*snapshot_size）。一次性 `calloc(max_count, max_size)` 要质疑。配置传播半径。超过几十 KB 的一次性分配必须解释。
- **B. 内存安全与生命周期**：指针偏移前长度校验（`remaining = len - N` 当 `len < N` 时 uint 下溢 → 越界读）；VLA / 大块栈分配；失败路径内存泄漏（`esp_event_post` / queue send / callback 失败后 payload 是否释放）；ownership 与 borrowed/owned；半初始化失败的反序销毁；DMA / cache 一致性。
- **C. 并发、竞态、死锁与实时性**：死锁（锁获取顺序、跨链路切换持锁、回调查调持锁）；竞态（共享状态读写保护）；重入安全；中断上下文误用（ISR / 事件回调中调用阻塞 API — mutex take / malloc / vTaskDelay / 重量级日志）；被忽略的返回值（`esp_event_post` / `xQueueSend` 失败仅记日志就 return OK）；优先级反转；关键路径无界循环 / 无超时阻塞；忙等；ISR 长度。
- **D. 失败路径完整性**：malloc 失败后对象是否仍可用、是否设 `ESP_ERR_NO_MEM` 并清理；UART/SPI 传输失败或超时后 RX 是否清理、旧数据是否污染下一次采集；queue/event send 失败后 payload ownership；`abort()` 类宏是否被用在可恢复 init 路径。
- **E. 跨模块契约**：分层（应用编排 / 业务服务 / 网络抽象 / 驱动适配）；数据生命周期（borrowed vs owned 跨层）；配置项被多模块复用时语义是否一致；非 LVGL 任务不得直接操作 widget；上行遥测 / 下行 RPC / 本地按键 / 安全保护四条数据流是否各自不绕过 `app_controller`。
- **F. 类型与边界**：类型不一致导致静默截断（uint32_t→uint16_t；int vs uint32_t size 字段）；整数溢出（len*elem_size；offset+n；width*bpp）；长度字段先验证再用；死代码 / 不可达分支 / 重复赋值；魔数应提为命名常量。
- **G. 代码质量与一致性**（低严重度但低成本）：圈复杂度 ≤ 10–15；命名一致性 / 误导性函数名；coding-style.md 合规（双语注释格式、section 组织 DEFINES vs MACROS、include 风格）；传递性头文件依赖；废弃别名。
- **H. 文档与注释一致性**：公共 API 文档完整性（classes.md 有对应定义？）；文档与实现一致性（classes.md 的 API 签名 / 配置字段 / architecture.md 的"职责/不负责"与实现是否一致）；注释准确性；过时引用；变更同步。

### 误报防范（§3）——不要报这些

- 把设计意图当缺陷（fire-and-forget 语义、network_manager 重放订阅、safety_guard 持续判定——都是设计行为）
- 报告里编造调用点（声称某宏在某行使用，实际是另一个宏——必须用 `rg` 实际搜索）
- "用户可能移除 include"等不现实场景
- 空壳函数（无堆分配时仍加空 deinit）属风格偏好

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

## Step 4：审查（Phase 2）——写 report-<NN>-<module>.md

按维度 A–H 逐条检查。每条发现必须包含：

- **文件:行号**（精确定位）
- 严重度：🔴 高 / 🟡 中 / 🟢 低
- 问题描述
- 建议修复

使用 review-checklist.md §4.2 的模板。报告用中文写（代码 / API / 文件路径保持原文）。

---

## Step 5：验证（Phase 3）——写 verify-<NN>-<module>.md

对报告中的**每一条**发现：

1. 重新读源码，前后至少 30 行上下文
2. 用 `rg` 实际搜索调用点、追踪条件分支
3. 检查是否有你遗漏的已有防护逻辑
4. 分类：
   - ✅ **确认的问题**：简述确认理由
   - ❌ **误报**：说明为什么不是真问题
   - ⚠️ **部分正确**：真正的问题是什么，修复方案应如何调整

使用 review-checklist.md §4.3 的模板。**必须包含"模块交付清单"**：

- **Change summary**：N/A（review-only，无代码改动）
- **Resource budget**：启动 heap / 运行 heap / 峰值 heap / task stack / queue size / buffer size，所有 `count*size` 显式计算
- **Lifecycle / ownership notes**：关键数据 borrowed vs owned 标注
- **Failure-path review**：malloc / queue / event / UART / SPI 传输失败 的失败路径是否完备
- **Cross-module contract review**：是否破坏分层契约
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
- 注意到的跨模块问题（供维度 E 汇总）

---

## 关键规则

1. **工作目录**：`/Users/jovisdreams/Projects/Smart_Socket`
2. **绝对禁止修改源代码**（main/ 下的 .c / .h）
3. **绝对禁止修改现有文档**（docs/ 下的已有文件）——只写入新的 report-*.md 和 verify-*.md
4. 每条发现必须有**文件:行号**
5. 用 `rg` 实际搜索验证调用点——不要猜
6. 应用误报防范（§3）
7. 技术严谨——先验证再下结论
