# 代码 Review 检查清单与流程

本文档规定 Smart_Socket 项目的日常代码 review 流程与检查维度。在功能开发与迭代过程中，review 用于系统性找 bug、优化代码，所有 review 工作必须遵循本文档。

本文档内容源自：

- esp-lwlte 项目的 `docs/agents/review-checklist.md`（已移植并适配到本项目）
- AT Engine 内存爆掉事件（`response_pool = max_response_lines * rx_line_buf_size` 在 ESP32-C3 上撑爆 heap）的教训——这是 esp-lwlte 项目的真实案例，同样的乘法型分配模式在本项目的 LVGL framebuffer、BL0942 RX buffer、network queue 等大块占用上同样存在风险
- 嵌入式 review 的两条主线：**内存泄漏** 与 **死锁**
- 通用嵌入式 review checklist（`embedded-code-review` skill）的技术盲点补充：ISR/DMA/cache 一致性、实时性风险、圈复杂度等
- Smart_Socket 全量 review 整改的教训：正确性补丁也会累积重复状态转换、cleanup/retry/drain 骨架和一次性 helper，必须对修复后的 diff 再做结构收敛

---

## 一、Review 流程（必须按顺序执行）

Review 不是"让 AI 直接改 bug"，而是一条**先理解、再列单、再验证、最后才动手**的流水线。阶段 0–3 是审查核心，不得跳步；完整多模块审查还必须执行阶段 4。阶段 5–7 只在用户任务范围包含整改，或用户在报告后授权整改时执行。用户只要求审查时，在完成约定范围的阶段 3/4 产物后停止，不得擅自修改代码。

### 阶段 0：前置理解（每次会话开始时）

在找任何 bug 之前，reviewer 必须先建立对项目的准确理解：

1. 读 `AGENTS.md` 与 `docs/agents/` 下的架构、目录、类设计文档。
2. 搞清楚分层契约：**应用编排层（app_controller）→ 业务服务层（metering_service / safety_guard / thingsboard_client）→ 网络抽象层（network_manager / network_link / wifi_link / lte_link）→ 驱动适配层（bl0942 / relay / button / tft_panel / lvgl_dashboard / board_pinmap）**。
3. 搞清楚每层数据的生命周期：metering snapshot、relay event payload、dashboard state、network MQTT message 是 borrowed 还是 owned。
4. 建立**硬件上下文**：MCU 型号与资源上限（ESP32-S3 的 heap / 可用 task stack）、UART/外设资源、编译配置（Kconfig 默认值）。建立"软件逻辑 ↔ 硬件约束"双向映射——某段代码用了多少 RAM、在哪个任务栈上运行（如 LVGL task / BL0942 task / metering task）、是否运行在 ISR 上下文。
5. 明确本次 review 的基线 commit、目标 commit 和工作区已有改动。报告必须区分基线遗留问题与本次 patch 新引入的问题，不得把用户未提交改动误算为 reviewer 的修复。
6. 生成唯一 `review_run_id`，格式为 `<YYYYMMDD-HHMM>-<baseline短SHA>`；若名称已存在则追加 `-02`、`-03`。同一轮 report、verify、summary 和 remediation 都使用该 ID，任何历史文件不得覆盖。

> 不理解调用链就找 bug，等于在猜。

### 阶段 1：建立 / 更新审查清单

1. 扫描 `main/` 下所有源文件，按功能模块分组，每组的粒度是 **1–3 个密切相关的源文件**。
2. 维护 `docs/agents/code-review/review-list.md`（格式见下文"模板"一节），每个模块一行，记录审查基线并标注状态：`⬜ Pending` / `🔎 Reviewing` / `📝 Reported` / `✅ Verified` / `⏭️ Skipped`。`Skipped` 必须写明用户范围或技术原因。
3. 单次审查范围既不能过大（一整层），也不能过小（单个函数）。一个模块一次审完。
4. 已审过的模块在后续迭代中如果被改动，需重新标记为 `⬜ Pending`（回归 review）。
5. review-list 由主 reviewer/调度者维护：派发模块前标记 `🔎 Reviewing`，subagent 返回 report 后标记 `📝 Reported`，verify 覆盖全部 finding 后标记 `✅ Verified`。只允许创建报告的 subagent 不得修改该清单。

### 阶段 2：逐模块审查（只产出报告，不动代码）

对调度者分配且已标记为 `🔎 Reviewing` 的模块，按"二、Review 维度"逐条检查，把发现写入 `docs/agents/code-review/report-<review_run_id>-<编号>-<模块名>.md`。回归审查创建新文件，不覆盖历史报告：

1. 每条发现分配 canonical ID，格式为 `<模块缩写>-<类别缩写>-<两位序号>`，类别建议使用 `RES/MEM/LIFE/CONC/FAIL/CONTRACT/BOUND/QUALITY/DOC/REDUND/OBS/HW`。后续 report、verify、remediation 和测试都沿用该 ID；ID 在仓库内不得重排或复用。
2. 旧报告中 `C-1`、`M1` 等仅在单份报告内唯一的 legacy ID 不是全局主键。首次迁入新台账时分配 canonical ID，并记录 `<历史报告文件>:<legacy ID>`；不得修改历史报告。回归审查同一问题沿用 canonical ID，新问题使用该模块下一个可用序号。
3. 每条发现必须给出 **文件:行号、触发条件、证据、影响和建议方向**，不能只写代码气味。
4. 按影响严重度分三档：🔴 高 / 🟡 中 / 🟢 低；另行标注问题类型：`行为缺陷` / `契约缺口` / `可维护性` / `文档漂移` / `可选优化`。严重度、证据可信度和修复优先级不得混为一项。
5. 同一根因在多个位置出现时写成一条系统性 finding，并列出全部位置；不得为凑数量逐行重复报告。
6. **本阶段禁止修改源代码**——审查与修复必须分离，避免边找边改导致漏审。
7. subagent 返回后由调度者更新 `review-list.md` 状态为 `📝 Reported`，并填入报告文件名。
8. 输出本次审查摘要（问题数 + 最严重的 1–2 个发现）。

### 阶段 3：验证（绝不盲信报告）

这是最容易省略、但最关键的一步。审查报告会出错，reviewer 也会编造调用点或误判语义。修复前必须对每条发现**独立验证**：

1. 打开完整函数，继续读取相关状态字段、直接调用者、被调用者、callback 注册点和 cleanup 路径。固定的“前后 N 行”不能替代调用链理解。
2. 追踪条件分支、锁域、ownership 和已有防护逻辑，确认触发条件在当前代码中真实可达。
3. **双重校验**：逻辑推理 + 工具交叉验证——用 `rg` 实际搜索调用点、查编译器/`idf.py` 警告、必要时跑 host test。不靠单一推理路径就下结论。
4. 涉及 ESP-IDF、第三方组件、芯片寄存器或外设协议时，记录所依据的具体版本、官方文档、datasheet 或第三方源码位置。证据不足时标记“待外部验证”，不得把推测写成已确认事实。
5. 按三类输出验证结论，写入 `docs/agents/code-review/verify-<review_run_id>-<编号>-<模块名>.md`，与对应 report 使用同一 run ID、编号和模块名：
   - ✅ **确认的问题**：简述确认理由。
   - ❌ **误报**：说明为什么不是真问题（参见"三、误报防范"）。
   - ⚠️ **部分正确**：真正的问题是什么，修复方案应如何调整。
6. 验证结论必须保留原 finding ID，并记录证据缺口和剩余不确定性。
7. 约定范围内所有 finding 验证完成后，由调度者把模块状态更新为 `✅ Verified` 并记录 verify 文件名。验证完成前，不得开始修复。

### 阶段 4：跨模块归并与整改计划

完整多模块审查完成逐模块验证后，不得立即逐条打补丁。先把 finding 汇总到 `summary-<review_run_id>.md` 和 `remediation-status-<review_run_id>.md`。单模块只读审查可以在完成 verify 后停止，但必须把跨模块线索和未处理 finding 明确交给调度者：

1. 归并跨模块重复模式，例如 stop/destroy、event post、callback drain、锁顺序、状态提交和文档漂移。
2. 为系统性模式定义统一契约、权威实现位置和修复边界，再决定集中修复还是局部修复。
3. remediation 台账必须逐项列出所有 finding，不得用“53 条 P4”之类的聚合行代替逐项状态。
4. 每条 finding 分别记录整改处置和验证状态，不得压成一个字段：
   - **Disposition**：`Pending` / `Fix in progress` / `Fixed` / `Accepted risk` / `Deferred` / `False positive` / `Superseded`。
   - **Verification**：分别记录 `Static` / `Host` / `Build` / `Hardware` 四层；每层取值为 `Verified` / `Failed` / `Not run` / `N/A`，Hardware 还可取 `Pending`。
5. `Accepted risk` 必须写接受理由和适用边界；`Deferred` 必须写触发重审的条件；`Superseded` 必须指向替代 finding；只有 `Fixed` 可以表述为已修复。
6. 修复优先级根据影响、可达性、恢复能力和验证成本单独确定，不直接照搬严重度。默认边界为：P0 安全/数据损坏/凭据泄露；P1 不可恢复生命周期或资源故障；P2 功能、并发和有界恢复问题；P3 契约或文档错误；P4 可维护性和可选优化。实际上下文可以升降级，但必须解释。
7. 低严重度项一次性批量请用户决策，不逐条打断。

### 阶段 5：修复与聚焦验证

1. 只修复阶段 3 中确认或部分确认且已进入整改计划的问题，**不修误报**。
2. 修改前先写清要保持的不变量、失败后的句柄语义、重试入口和资源 ownership。生命周期修复不得以“强制 free”掩盖潜在 callback UAF。
3. 对行为缺陷，优先补能复现问题的 host test 或故障注入；无法自动化时写明原因和人工验证步骤。
4. 按逻辑修复批次实施修改。每批运行相关聚焦测试；涉及目标头文件、组件 API、链接关系或 Kconfig 时运行 ESP-IDF 全量构建（构建/烧录必须遵循 [build-and-debug.md](build-and-debug.md)）。
5. 某个修复若影响其他模块，先告知风险并重新检查对应模块契约。
6. 每个修复完成后，在对应 `verify-*.md` 和 remediation 台账中追加修复记录、Disposition 和四层 Verification。代码已改但仍待实机验证时记录 `Disposition=Fixed`、`Verification.Hardware=Pending`。

### 阶段 6：补丁最小性与结构收敛复审

正确性测试通过后，对“审查基线 → 当前修复结果”的 diff 做一次只读复审，禁止跳过：

1. 同时检查 `git diff <baseline>` 和 `git status --short`；逐个读取未跟踪文件，因为普通 diff 不包含尚未暂存的新文件。
2. 统计新增的 `static` 函数、状态字段、mutex/semaphore/timer/task、公共 API、配置项和测试 fixture；这些增量是审查触发器，不是机械评分指标。
3. 每个新增 helper 至少应承担一项明确职责：锁/ownership 不变量、callback/ops/task ABI、复杂生命周期阶段、模块边界或独立测试接缝。仅转发参数且没有边界价值的单调用 helper 应评估内联。
4. 搜索同一状态转换是否存在两套权威路径，尤其关注 `state commit → lower-layer call → replay/notify`、startup/rollback、stop/destroy 和 retry/drain 序列。
5. 同一模块出现第二份 cleanup、rollback、retry 或 drain 循环时，比较 deadline、锁顺序、错误优先级和完成谓词；骨架和语义相同则共享内核，谓词不同则保留独立入口。
6. 每个新增状态字段必须说明 source of truth、合法组合和全部写入点。能从 enum、指针或计数可靠推导的 bool 不应单独存储；handoff、in-flight 和 partial-cleanup checkpoint 可保留，但必须有失败重试测试。
7. 检查旧路径、旧字段、死分支、兼容包装和过时测试是否已随新实现删除，禁止新旧语义长期并存。
8. 三个以上测试重复同一参数簇时评估聚合 fixture；只有行为和故障注入契约一致的 stub 才允许共享。
9. 对每个冗余候选输出 `保留` / `合并` / `内联` / `删除` 及理由。callback、task entry、ops 实现、语义边界 API 和承载复杂不变量的 helper 不得仅因调用次数少而合并。
10. 结构收敛修改必须重新运行对应聚焦测试，不得借“清理冗余”改变已验证行为。

### 阶段 7：收尾与闭环

整轮 review + 修复收尾时，必须完成适用的全量 host tests、ESP-IDF 构建、`git diff --check` 和实机验证，并在各模块 verify 文件末尾回答以下八项。纯文档或无需目标构建的变更可以标记 N/A，但必须写明理由；当前环境无法进行实机验证时，必须记录 `Verification.Hardware=Pending`：

- **Change summary**：本次改动概述。
- **Resource budget**：启动 heap / 运行 heap / 峰值 heap / task stack / queue size / buffer size，所有 `count*size` 显式计算；区分静态计算、实测值和待实机测量，禁止编造未知数据。
- **Lifecycle / ownership notes**：关键数据 borrowed vs owned 标注。
- **Failure-path review**：malloc / queue / event / UART / SPI 传输失败 的失败路径是否完备。
- **Cross-module contract review**：是否破坏 应用编排 / 业务服务 / 网络抽象 / 驱动适配 之间的分层契约。
- **Structural delta**：新增 helper、状态字段、同步原语和公共 API 是否均有保留理由，冗余候选如何处置。
- **Verification evidence**：聚焦测试、故障注入、全量构建和实机验证分别覆盖什么。
- **Residual risks**：已知但未解决的问题、上机才可能暴露的风险。

最终 remediation 台账中的每条 finding 都必须有处置状态和证据：

- **审查完成**：约定范围内不存在未分类 verdict 的 finding；允许 disposition 为 `Pending`，表示尚未进入整改。
- **整改计划已处置**：Disposition 不存在 `Pending` 或 `Fix in progress`；`Accepted risk`、`Deferred` 可以保留理由和边界，`Verification.Hardware=Pending` 可以保留验证步骤，但这些均不表示问题已完整修复并验证。
- **问题已修复并验证**：所有 `Confirmed` 以及 `Partial` 中需要改动的 finding 均为 `Fixed`，且所有适用 Verification 层均为 `Verified` 或 `N/A`。`Accepted risk`、`Deferred`，以及任何 `Pending` / `Failed` / `Not run` 不得计入“已修复并验证”。

---

## 二、Review 维度（找什么问题）

### A. 资源账本与乘法型分配（最高优先级）

本项目继承自 esp-lwlte 的最深教训：一个配置项被跨模块复用后，在底层被乘以一个上限值，启动时一次性撑爆 heap。资源风险必须高优先级检查，但不得因此忽略生命周期、并发和行为正确性。LVGL framebuffer、BL0942 RX buffer、network 订阅表与队列都遵循同一模式。

- **所有 `count * size` 的乘法型占用必须显式计算**：`width * height * bytes_per_pixel`（LVGL framebuffer）、`rx_buf_len * buf_count`（UART RX 池）、`queue_len * payload_size`、`sub_count * topic_len`、`window_size * snapshot_size`（metering 窗口）。
- **一次性预分配审查**：`calloc(max_count, max_size)`、二维静态数组、大 payload 队列——问"所有槽位真的都需要按最大值常驻吗？"优先采用"指针槽位常驻 + 内容按需 malloc"。
- **配置传播半径**：新增或调大配置项时（如屏幕分辨率、BL0942 采集窗口、MQTT 订阅上限），追踪它在哪些模块被使用——是单个 buffer 大小，还是被当成池中每个元素的大小？默认值变化是否改变启动 footprint？
- **超过几十 KB 的一次性分配必须解释理由**（ESP32-S3 内部 SRAM 仍有限，尤其要关注 PSRAM 未启用时的内部 RAM 占用）。

### B. 内存安全与生命周期（嵌入式主线之一）

- **指针偏移前的长度校验**：`remaining = len - N` 类计算，当 `len < N` 时 uint 下溢成巨大值，使后续 `<` 检查失效，导致越界读（BL0942 帧解析、JSON 拼接均高危）。
- **VLA / 大块栈分配**：`char buf[config_value]` 在受限任务栈上极易栈溢出，且 VLA 无错误恢复机制。改用堆或固定小 buffer。
- **失败路径的内存泄漏**：`esp_event_post` / queue send / callback 触发失败后，已分配的 payload / snapshot 是否被释放。对照同模块其他路径（如 blocking vs async）看是否一致释放。
- **ownership 与 borrowed/owned**：调用方是否可能在下一次事件/回调后仍持有旧指针（如 metering snapshot、relay event、MQTT message）。
- **半初始化失败的反序销毁**：init 链路中途失败（如 app_controller 启动顺序中某模块 init 失败），已初始化的子系统是否按反序清理。
- **DMA / cache 一致性**：若 RX/TX buffer 走 DMA（如 UART DMA 收发、SPI DMA 刷 TFT），CPU 访问前后是否做了 cache invalidate/writeback；DMA buffer 是否落在兼容内存区域、对齐是否满足要求。
- **生命周期状态机**：为 `create/start/stop/destroy` 列出合法状态转换、幂等性、重入行为和每个失败出口。首次 stop/destroy 失败后，对象必须保持可识别、可重试或明确不可恢复的状态。
- **非消费式失败语义**：若 callback、task 或底层组件仍可能引用对象，cleanup 失败时不得释放句柄；必须保留 pending 状态和重试入口，并明确 first-error 如何保存。
- **callback drain 完整性**：不仅检查已经进入 callback 的计数，还要考虑已排队但尚未进入用户 callback 的事件、timer 和第三方任务。超时后不得释放仍可能被访问的 context。
- **状态 source of truth**：同一生命周期事实是否同时由 bool、句柄和计数表达；检查所有组合是否合法，失败路径是否会使它们分叉。

### C. 并发、竞态、死锁与实时性（嵌入式主线之二）

**并发与同步：**
- **死锁**：锁/信号量的获取顺序、跨链路切换（Wi-Fi↔LTE）时持锁、回调中持锁调用外部代码。
- **竞态**：共享状态（如 app_controller 上下文、当前活动链路、safety 阈值）的读写保护、多个任务（metering task / LVGL task / network task）的访问顺序。
- **重入安全**：callback 是否可能在持锁状态下被递归触发（如 relay event 回调中又触发 relay 操作）。
- **中断上下文误用**：真正的 ISR（如 button GPIO ISR）不得调用阻塞、休眠、普通 malloc 或重量级日志 API，只应使用对应的 ISR-safe API 并把重活 defer 到任务。
- **任务上下文 callback**：ESP event loop、MQTT event、UART queue consumer 和 esp_timer callback 通常运行在任务上下文，不按 ISR 规则机械判定；必须先确认具体 dispatch context，再按阻塞预算、优先级和重入契约审查。
- **被忽略的返回值**：`esp_event_post` / `xQueueSend` 失败仅记日志就 `return OK`，会欺骗调用方并泄漏资源。
- **锁图与顺序**：列出模块内 mutex、control lock、callback drain semaphore 的固定获取顺序；跨模块持锁链必须进入跨模块模式汇总。
- **锁内外部调用**：状态锁内原则上不得调用链路 API、用户 callback、event post、第三方 stop/destroy 或其他可能阻塞/重入的操作；确有必要时必须记录时延上限和重入证明。
- **控制锁与状态锁分工**：长控制操作应由 control mutex 串行化，短状态锁只保护内存状态，不能为了避免竞态把未知耗时的 I/O 包进状态锁。
- **callback 自注销**：检查 callback 在自身执行期间注销、替换或触发 destroy 时是否会等待自己的 active count，从而形成自死锁。

**实时性：**
- **优先级反转**：低优先级任务持锁阻塞高优先级任务，是否需要优先级继承互斥量（mutex）而非二值信号量。
- **关键路径不可预测执行时间**：关键路径上是否有无界循环、无超时的阻塞调用（尤其安全保护路径：metering snapshot → safety_guard → relay 必须本地闭环且时延可控）。
- **忙等 busy-wait**：是否有 `while(wait_hw)` 类忙等而无超时 / 退出条件。
- **ISR / 高优先级回调长度**：是否足够短，重活是否 defer 到普通任务。
- **超时后的安全状态**：给阻塞调用加超时不等于修复完成；必须检查超时后资源、状态标志、callback context 和下次重试是否仍一致。

### D. 失败路径完整性

- `malloc` 失败后对象是否仍可继续使用、是否设 `ESP_ERR_NO_MEM` 并清理上下文。
- UART / SPI 传输失败或超时后 RX 是否清理，旧数据是否污染下一次采集（BL0942 帧同步、AT 响应均高危）。
- queue/event send 失败后 payload 的 ownership 归属。
- **`abort()` 类宏是否被用在可恢复的 init 路径里**（旧项目真实案例：URC 表填满后 `LWLTE_ERROR_CHECK` 直接 abort 整个程序）。本项目应避免在模块 init 失败时直接 abort，应返回错误码让 app_controller 决定降级策略。
- **状态提交与通知失败**：先修改内部状态再发布 event/queue 时，发布失败后是回滚、保留 pending 后重试、由读取接口兜底，还是接受丢失；必须明确唯一策略。
- **错误优先级**：主操作和 cleanup 同时失败时，明确返回 first error、cleanup error 或组合诊断；不得静默覆盖导致调用方误判资源状态。
- **故障注入矩阵**：对 create/start/stop/destroy 中每个外部可失败调用，检查测试是否覆盖首次失败、部分成功、超时和第二次重试。

### E. 跨模块契约

- 应用编排 / 业务服务 / 网络抽象 / 驱动适配 之间的数据生命周期是否清楚、是否有 architecture.md 第 8 节"模块依赖规则"锁定关键契约（如 `thingsboard_client` 不得直接依赖 `wifi_link`/`lte_link`；`safety_guard` 不得直接操作继电器）。
- 配置项被多个模块复用时的语义是否一致（A 模块改默认值会不会撑爆 B 模块的池）。
- 非 LVGL 任务不得直接操作 widget；上行遥测、下行 RPC、本地按键、安全保护四条数据流是否各自不绕过 `app_controller`。
- 上层模块不得 include 下层模块的 internal/private 头文件；跨层调用应表达业务语义，不应泄漏 topic、JSON、寄存器或底层句柄格式。
- 每个公共 API 必须核对：nullability、ownership、线程/ISR 安全、callback 上下文与重入、是否阻塞及超时、参数单位/范围、buffer 长度、错误是否消费句柄和重试语义。
- 对关键事件链记录生产者、状态提交点、发布点、消费者顺序、失败补偿和 source of truth。安全动作不能只依赖可能丢失或阻塞的通知事件。

### F. 类型与边界

- 类型不一致导致的静默截断（`uint32_t` → `uint16_t`、`int` vs `uint32_t` 的 size 字段、BL0942 寄存器宽度转换）。
- **整数溢出**：乘法/加法（如 `len * elem_size`、`offset + n`、`width * bytes_per_pixel`）是否可能溢出；长度字段是否先验证再用（uint 下溢见维度 B）。
- 死代码、不可达分支、重复赋值、从未读取的配置字段和无法触发的兼容路径。
- 魔数应提为命名常量（尤其长度偏移、BL0942 帧字段这类与协议耦合的值）。

### G. 代码质量与一致性（低严重度但低成本）

- **圈复杂度**：10–15 可作为人工复核触发区间，不是自动 finding 阈值；只有复杂度造成路径难以证明、重复 cleanup 或测试不可达时才建议拆分。
- 命名一致性、误导性函数名（如 `_act` 后缀实际处理全部类型）。
- 与 [coding-style.md](coding-style.md) 的一致性：双语注释格式、section 组织（DEFINES vs MACROS）、include 风格。
- 传递性头文件依赖（宏用了 `LOGE` 但头文件未 include，靠间接依赖才编译过）。
- 废弃别名有无编译器级 deprecation 警告。
- 不以函数数量作为质量目标：拆分应降低真实复杂度或隔离不变量，合并也不能把多个锁域、callback ABI 或失败语义塞回一个大函数。

### H. 文档与注释一致性

聚焦文档与代码的**内容一致性**，与维度 G 分工：G 管**格式**（命名、双语注释格式、include 风格），H 管**内容准确性**（文档说得对不对、是否过时、变更是否同步）。

- **公共 API 文档完整性**：该模块对外暴露的 API 是否在 [classes.md](classes.md) 中有对应类/句柄/结构体定义；新增公开 API 是否已补进 classes.md。
- **文档与实现一致性**：classes.md 记录的 API 签名、配置结构体字段、[architecture.md](architecture.md) 中该模块的"职责/不负责"描述与当前实现是否一致——不一致即文档漂移。
- **注释准确性**：代码注释（含 Doxygen）描述的行为与代码实际行为是否一致；改了实现没改注释属缺陷。
- **过时引用**：文档/注释是否引用已删除的文件、已改名的 API、已废弃的配置项；[directory-structure.md](directory-structure.md) 的目录清单是否仍覆盖该模块。
- **变更同步**：本次改动若改了公开接口/分层契约/模块依赖/数据流，是否同步更新了 AGENTS.md 索引、architecture.md（含 §8 模块依赖规则表）、classes.md、directory-structure.md。

严重度按实际影响判定：纯格式或无行为影响的漂移通常为 🟢 低；公共 API、ownership、并发或安全契约错误通常为 🟡 中；已导致高风险错误实现或安全误判时才升 🔴 高。

### I. 补丁最小性与结构熵

本维度检查“修复是否正确且保持最小”，必须同时阅读当前代码和相对审查基线的 diff：

- **重复状态转换**：startup 与 runtime、set 与 toggle、normal cleanup 与 rollback 是否重复推进相同状态机；相同序列应只有一个权威 transition kernel。
- **重复控制骨架**：cleanup、retry、drain、deadline polling 是否只更换了谓词或资源名；先评估模块内复用，避免为了复用制造跨模块通用框架。
- **薄包装与单调用 helper**：仅转发参数、没有命名价值和不变量的 helper 应内联；callback、ops、task entry、模块语义边界和独立 test seam 不按调用次数判定。
- **状态字段膨胀**：新增 bool/enum/count 是否表达独立时态，还是可从已有 source of truth 推导；为失败重试保存的 checkpoint 必须有合法状态组合和测试。
- **新旧路径并存**：新实现落地后，旧 helper、旧字段、旧注释、兼容分支和重复测试是否删除。
- **测试代码冗余**：fixture、stub 和故障注入脚手架是否出现大段复制；不同并发或失败语义的 stub 不得为了减少行数而错误合并。
- **抽象收益证明**：新增抽象必须减少真实重复、降低复杂度或强化边界。只减少代码行数、只隐藏分支或只有假想复用价值，不足以证明抽象成立。

### J. 可观测性与敏感信息

- 高频循环、1 Hz 遥测和正常成功路径不得持续使用 `ESP_LOGI`；根据诊断价值降为 debug、抽样或状态变化时记录。
- 失败日志必须包含可行动上下文，例如模块状态、操作阶段、错误码和是否允许重试，但不得在高频路径制造日志风暴。
- Wi-Fi 密码、ThingsBoard token、设备密钥、完整 payload 和个人标识不得进入源码、构建告警、测试快照或运行日志。
- 日志本身不得位于持锁关键区、ISR 或安全实时路径中形成不可预测阻塞；必要日志应延后或限频。

### K. 外部证据与硬件正确性

- 协议字段、寄存器位、字节序、颜色顺序、时序参数和厂商命令必须引用匹配硬件版本的 datasheet、官方文档或已验证抓包。
- ESP-IDF 和第三方组件行为必须绑定实际使用版本；不能用另一个版本的 API 契约推导当前代码。
- 静态分析、host test、目标构建和实机测试的证明边界必须分别记录。host stub 通过不能证明 ISR 时序、DMA/cache、真实 callback 排队或外设电气行为。
- 无法在当前环境验证的硬件结论必须记录 `Verification.Hardware=Pending`，并写清测试步骤、观测指标和通过标准。

---

## 三、误报防范（什么不该报）

验证阶段驳回的典型误报，提示 reviewer 守住技术严谨：

- **把设计意图当缺陷**：如 fire-and-forget 语义的"首条响应即完成"被误读为漏洞；又如 `network_manager` 重放订阅意图、safety_guard 短暂持续判定（防瞬态误触发）属设计行为。先确认接口语义文档，再下结论。
- **报告里编造调用点**：声称某宏在某行使用，实际该行用的是另一个宏。验证时必须实际搜索调用点。
- **"用户可能移除 include"等不现实场景**不应作为设计考量。
- **空壳函数**（如无堆分配时仍加空 `deinit`）属风格偏好，非缺陷。
- **把非消费式 cleanup 失败直接判为泄漏**：若底层 callback 或 task 仍可能引用对象，保留句柄并允许重试可能是唯一安全语义。必须先证明对象已不可达，才能建议强制释放。
- **把有限超时等同于安全释放**：drain 超时只能限制等待时间，不能证明已排队 callback 不再访问 context；不得在没有派发屏障或底层保证时建议超时后 free。
- **按调用次数判定 helper 冗余**：单调用 callback、ops、task entry、语义边界 API 和复杂生命周期 helper 都可能合理。必须证明合并后不损失边界或不变量。
- **把所有 `portMAX_DELAY` 都判为缺陷**：必须结合执行上下文、锁持有上限、优先级、回调重入和系统恢复要求证明存在无界阻塞风险。
- **猜测第三方实现**：不知道 ESP-IDF 或组件内部是否同步 drain、复制参数或保留 callback 时，应读取匹配版本源码/文档或标记待验证，不得补全想象中的契约。

---

## 四、模板

### 4.1 review-list.md

```markdown
# Code Review List

**Review run ID**: <review_run_id>
**Review baseline**: <commit>
**Review target**: <commit / working tree>

| # | Module | Path | Last reviewed | Status | Report / Verify | Notes |
|---|--------|------|---------------|--------|-----------------|-------|
| 1 | board_pinmap | main/platform/board_pinmap.{c,h} | <commit> | ⬜ Pending | — | — |
| 2 | relay | main/relay/relay.{c,h} | <commit> | ⬜ Pending | — | — |
| 3 | button | main/button/button.{c,h}, main/button/button_iot_adapter.{c,h} | <commit> | ⬜ Pending | — | — |
| 4 | BL0942 驱动 | main/bl0942/bl0942.{c,h} | <commit> | ⬜ Pending | — | — |
| 5 | network_link | main/network/network_link.{c,h}, main/network/network_link_priv.h, main/network/network_types.h | <commit> | ⬜ Pending | — | — |
| 6 | network_manager | main/network/network_manager.{c,h} | <commit> | ⬜ Pending | — | — |
| 7 | wifi_link | main/network/wifi/wifi_link.{c,h}, main/network/wifi/wifi_link_internal.{c,h} | <commit> | ⬜ Pending | — | — |
| 8 | lte_link | main/network/lte/lte_link.{c,h}, main/network/lte/lte_link_internal.{c,h} | <commit> | ⬜ Pending | — | — |
| 9 | metering_service | main/metering/metering_service.{c,h}, main/metering/metering_service_internal.{c,h} | <commit> | ⬜ Pending | — | — |
| 10 | safety_guard | main/safety/safety_guard.{c,h} | <commit> | ⬜ Pending | — | — |
| 11 | thingsboard_client | main/thingsboard/thingsboard_client.{c,h}, main/thingsboard/thingsboard_client_internal.{c,h} | <commit> | ⬜ Pending | — | — |
| 12 | tft_panel | main/display/tft/tft_panel.{c,h}, main/display/tft/tft_panel_st7789t.{c,h} | <commit> | ⬜ Pending | — | — |
| 13 | lvgl_dashboard | main/display/lvgl/lvgl_dashboard.{c,h}, main/display/lvgl/lvgl_dashboard_internal.{c,h}, main/display/lvgl/lvgl_dashboard_timer_barrier.{c,h} | <commit> | ⬜ Pending | — | — |
| 14 | app_controller | main/app/app_controller.{c,h}, main/app/app_controller_internal.{c,h}, main/main.c | <commit> | ⬜ Pending | — | — |
```

### 4.2 report-<review_run_id>-<编号>-<模块名>.md

```markdown
# Code Review: <模块名>

**日期**: <今天>
**Review run ID**: <review_run_id>
**文件**: <文件列表>
**审查基线**: <commit>
**审查目标**: <commit / working tree>

## 🔴 高严重度

### <canonical finding ID>：<标题>
- Legacy reference：`<历史报告>:<旧 ID>` / N/A
- 类型：行为缺陷 / 契约缺口 / 可维护性 / 文档漂移 / 可选优化
- 证据可信度：High / Medium / Low
- 位置：`文件:行号`（同一根因的其他位置全部列出）
- 触发条件：...
- 证据：...
- 影响：...
- 建议方向：...

## 🟡 中严重度
- 使用同一 finding 格式。

## 🟢 低严重度
- 使用同一 finding 格式。

## 无问题维度
- 列出无显著问题的维度
```

### 4.3 verify-<review_run_id>-<编号>-<模块名>.md

```markdown
# Verification: <对应报告的模块名>

## ✅ 确认的问题
- **Finding ID**: ...
- 验证结论：简述确认理由
- 代码证据：调用链、状态字段和可达路径
- 工具证据：`rg` / host test / 编译器 / 目标构建
- 外部证据：ESP-IDF/组件版本、官方文档或 datasheet；不适用写 N/A
- 剩余不确定性：...

## ❌ 误报
- **Finding ID**: ...
- 驳回理由：说明为什么这不是真正的问题

## ⚠️ 部分正确（需调整修复方案）
- **Finding ID**: ...
- 调整说明：真正的问题是什么，应如何修正

## 修复记录
- **Finding ID**: ...
- 改动：...
- 不变量与失败语义：...
- 聚焦测试：命令 + 结果
- ESP-IDF 构建：命令 + 结果 / N/A
- 实机验证：步骤 + 结果 / Hardware=Pending / N/A
- Disposition：Pending / Fix in progress / Fixed / Accepted risk / Deferred / False positive / Superseded
- Verification：Static=<状态>; Host=<状态>; Build=<状态>; Hardware=<状态>

## 模块交付清单
- Change summary: ...
- Resource budget: 静态计算 / 实测值 / 待实机测量
- Lifecycle / ownership notes: ...
- Failure-path review: ...
- Cross-module contract review: ...
- Structural delta: 新增 helper / 状态字段 / 同步原语 / 公共 API 及保留理由
- Verification evidence: 聚焦测试 / 故障注入 / 全量构建 / 实机验证边界
- Residual risks: ...
```

### 4.4 summary-<review_run_id>.md

完整多模块审查必须生成汇总，不能只留下分散的模块报告：

```markdown
# Code Review Summary

**Review baseline**: <commit>
**Review target**: <commit / working tree>
**Review run ID**: <review_run_id>
**Scope**: <模块与排除项>

## Coverage
| Module | Report | Verify | Status |
|--------|--------|--------|--------|
| <module> | <report path> | <verify path> | Verified / Skipped with reason |

## Finding totals
- Severity: 🔴 N / 🟡 N / 🟢 N
- Verdict: Confirmed N / Partial N / False positive N
- Type: 行为缺陷 N / 契约缺口 N / 可维护性 N / 文档漂移 N / 可选优化 N

## Cross-module patterns
- <模式 ID>：涉及 finding、统一契约、权威实现位置和建议修复边界。

## Remediation ledger
- `remediation-status-<review_run_id>.md`

## Verification boundary
- Static analysis：...
- Host tests：...
- ESP-IDF build：...
- Hardware：...
```

### 4.5 remediation-status-<review_run_id>.md

所有 report/verify 中的 finding 必须逐项进入该表，不得聚合省略：

```markdown
# Code Review Remediation Status

**Review baseline**: <commit>
**Current target**: <commit / working tree>
**Review run ID**: <review_run_id>

| Finding ID | Legacy reference | Type | Severity | Verdict | Priority | Disposition | Verification | Evidence / rationale |
|------------|------------------|------|----------|---------|----------|-------------|--------------|----------------------|
| MS-EVENT-01 | report-09:MS-01 | 行为缺陷 | 🟡 | Confirmed | P0 | Fixed | Static=Verified; Host=Verified; Build=Verified; Hardware=N/A | <commit> + <test> |
| BTN-LIFE-02 | report-03:L2 | 契约缺口 | 🟢 | Partial | P4 | Accepted risk | Static=Verified; Host=Not run; Build=N/A; Hardware=N/A | 接受理由、适用边界和重审条件 |
| TFT-HW-01 | report-12:#1 | 行为缺陷 | 🟡 | Confirmed | P2 | Fixed | Static=Verified; Host=Verified; Build=Verified; Hardware=Pending | 实机步骤、指标和通过标准 |

## Cross-module patterns
- <模式 ID>：涉及 finding、统一契约、权威实现位置和修复策略。

## Verification boundary
- Host tests：...
- ESP-IDF build：...
- Hardware：...
```

### 4.6 补丁结构收敛记录

阶段 6 的完整结果追加到 summary；受影响模块的结论同时写入对应 verify 的 `Structural delta`：

```markdown
## Structural convergence review

**Baseline**: <commit>
**Target**: <commit / working tree>
**Diff command**: commit 对 commit 使用 `git diff <baseline>..<target>`；working tree 同时使用 `git diff <baseline>` 和 `git status --short`，逐个读取未跟踪文件

| Candidate | Added by | Decision | Rationale | Verification |
|-----------|----------|----------|-----------|--------------|
| 重复 cleanup/retry 骨架 | <finding/commit> | 合并 / 保留 / 内联 / 删除 | 锁顺序、谓词和错误优先级比较 | <test> |

### Structural delta
- New static helpers: ...
- New state fields: ...
- New synchronization primitives: ...
- New public APIs/config: ...
- Removed legacy paths: ...
```
