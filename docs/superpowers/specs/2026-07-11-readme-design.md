# GitHub README 设计文档

## 目标

为 `dual-link-smart-socket` 仓库编写一份**中文 GitHub README**，面向**求职面试官 / HR**，
使其在 30 秒内抓住项目核心价值，并能向下滚动查看技术深度。

README 的根本任务：证明作者具备"嵌入式 + AI 开发"岗位所需的两项核心能力——

1. **用 AI 做严谨的工程闭环**（设计规格 → 实施计划 → 单元测试 → 代码审查，全程留痕）。
2. **从底层驱动到云端集成的完整 IoT 能力栈**（双模联网 OOP 架构为差异化亮点）。

## 设计约束

| 约束 | 说明 |
|------|------|
| 语言 | 中文为主，技术术语保留英文（ESP-IDF、MQTT、LVGL 等） |
| 视觉资产 | 暂无实物照片/截图；使用 **Mermaid 图表**（GitHub 原生渲染）+ 表格构建视觉层次 |
| 受众 | 面试官 / HR，停留时间短，必须"开屏即亮"（倒金字塔结构） |
| 归属 | Smart_Socket 与 esp-lwlte 均为作者独立完成，可强调完整能力栈 |
| Emoji | 不使用 emoji，视觉层次靠徽章 / 表格 / Mermaid / 分隔线 |
| 篇幅 | 约 350-450 行 Markdown，覆盖完整但分层呈现 |

## 整体结构（倒金字塔）

```
README.md
├── ① 英雄区（标题 + 一句话定位 + 徽章行）
├── ② 核心亮点（4 张卡片，AI 工作流置顶）
├── ③ 系统架构图（Mermaid flowchart）
├── ④ 模块全景表
├── ⑤ 深入：AI 驱动的工程工作流（Mermaid 流程图 + 留痕证据表）
├── ⑥ 深入：双模联网 C OOP 设计（Mermaid 类图 + 故障切换时序图）
├── ⑦ 深入：工程化体系（代码审查 / 单元测试 / 设计文档）
├── ⑧ 深入：云端 + 显示全栈集成
├── ⑨ 技术栈 & 硬件清单
├── ⑩ 快速开始（前置条件 / 构建 / 烧录 / 配置）
└── ⑪ 项目演化 & 相关仓库
```

设计原则：**前 4 节（①-④）让面试官在 30 秒内掌握全貌**；⑤-⑧ 为深度章节，供技术面试深挖；
⑨-⑪ 为工程可信度背书（可复现、可追溯）。

---

## 各节详细设计

### ① 英雄区

**标题**：用仓库标准名 + 中文副标题。

```markdown
# dual-link-smart-socket

> 基于 ESP32-S3 的双模联网智能插座系统 —— 全程以 AI 驱动的工程工作流开发
```

**一句话定位**（紧跟标题，2-3 行）：浓缩项目体量与差异化，让面试官一眼读到关键数字。

要点：
- 平台 ESP32-S3，双模联网 Wi-Fi + LTE（热备份 + 自动故障切换）。
- 14 个模块 / 23 个源文件，覆盖驱动层到云端集成完整能力栈。
- 全程 AI 辅助工程闭环：6 份设计规格 + 7 份实施计划 + 28 份代码审查报告。

**徽章行**（shields.io 静态徽章，一行排列）：

| 徽章 | 样式 |
|------|------|
| 平台 | `ESP32-S3` |
| 框架 | `ESP-IDF v6.0` |
| 语言 | `C` |
| 显示 | `LVGL 9` |
| 云平台 | `ThingsBoard` |
| 网络 | `Wi-Fi + LTE` |
| 开发方式 | `AI-Assisted` |
| 许可证 | `MIT` |

格式统一用 `https://img.shields.io/badge/<label>-<value>-<color>`，颜色用蓝/绿系传递专业感。
License 徽章使用动态徽章指向 LICENSE 文件。

**LICENSE 文件**：仓库根目录新增 `LICENSE`（MIT，版权人 ZhouDreams，年份 2026）。

---

### ② 核心亮点（4 张卡片）

用 4 个引用块或表格行，每张卡片 = 标题 + 一句话价值 + 关键证据数字。
**第 1 张必须是 AI 工作流**（直击岗位）。

| 卡片 | 标题 | 价值句 | 证据数字 |
|------|------|--------|----------|
| 1 | AI 驱动的工程工作流 | 不是"AI 写代码我复制"，而是用 AI 跑通设计→计划→测试→审查的完整闭环 | 6 spec / 7 plan / 28 review |
| 2 | 双模联网 + C OOP 架构 | Wi-Fi/LTE 热备份 + 自动故障切换/回切，用 C 继承多态实现统一抽象 | 故障切换 ~1-3s MQTT 重建 |
| 3 | 工程化 / 代码质量 | 逐模块代码审查 + 复核、单元测试、错误处理规范 | 14 模块审查 / 5 套主机测试 |
| 4 | 云端 + 显示全栈 | ThingsBoard RPC 远程控制/遥测 + LVGL 本地仪表盘 | TLS / 双向 RPC |

---

### ③ 系统架构图（Mermaid）

一张 `flowchart LR` 或 `TD`，按分层展示 14 模块协作关系。分层：

```
应用层:    app_controller
业务层:    metering_service / safety_guard / thingsboard_client
网络层:    network_manager ─ (network_link 抽象) → wifi_link / lte_link
驱动层:    bl0942 / relay / button
平台层:    board_pinmap
显示层:    lvgl_dashboard ← tft_panel
```

设计要点：
- 用 subgraph 划分 5 层（应用 / 业务 / 网络 / 驱动 / 显示）。
- 标注关键数据流：bl0942 → metering → safety/thingsboard/display。
- network_manager 用虚线连 wifi_link / lte_link 表示多态。
- 此图是 README 的"视觉锚点"，面试官最常停留的图。

---

### ④ 模块全景表

一张表覆盖全部 14 模块，让面试官快速确认覆盖面。

| 模块 | 目录 | 职责（一句话） | 关键句柄 |
|------|------|----------------|----------|
| 应用编排 | `main/app` | 组合根，注入 9 个模块句柄，协调启动顺序 | `app_controller_t` |
| 电能计量芯片 | `main/bl0942` | BL0942 UART 驱动，采集电压/电流/功率/电能 | `bl0942_t` |
| 计量服务 | `main/metering` | 寄存器值→工程单位，窗口聚合，电能增量确认/丢弃协议 | `metering_service_t` |
| 安全保护 | `main/safety` | 本地过流/过功率规则，持久采样去抖，输出 NORMAL/WARNING/DANGER | `safety_guard_t` |
| 继电器 | `main/relay` | 通断控制，每次状态变更打来源标签防云端回声 | `relay_t` |
| 按键 | `main/button` | 本地 GPIO 输入，单击/双击/长按，不直接控继电器 | `button_t` |
| 网络链路基类 | `main/network` | 抽象基类，定义 vtable（start/stop/publish/subscribe…） | `network_link_t` |
| Wi-Fi 链路 | `main/network/wifi` | 子类：Wi-Fi STA + esp-mqtt，支持 TLS | （返回 `network_link_t*`） |
| LTE 链路 | `main/network/lte` | 子类：UART→Air780EP，经 esp-lwlte，热备 MQTT-on-active | （返回 `network_link_t*`） |
| 网络管理器 | `main/network` | 主备选择 + 故障切换/回切 + 订阅意图重放表 | `network_manager_t` |
| ThingsBoard 客户端 | `main/thingsboard` | 遥测 JSON / RPC 解析 / 属性上报，只依赖 network_manager | `thingsboard_client_t` |
| LVGL 仪表盘 | `main/display/lvgl` | 拥有 LVGL 任务，构建控件树，周期读快照刷新 | `lvgl_dashboard_t` |
| TFT 面板 | `main/display/tft` | ST7789T SPI 驱动（172×320），esp_lcd 初始化 | `tft_panel_t` |
| 板级引脚映射 | `main/platform` | 只读单例，返回 ESP32-S3-LCD-1.47B 全部 GPIO 分配 | `board_pinmap_t` |

---

### ⑤ 深入：AI 驱动的工程工作流（核心章节）

这是面向"嵌入式 AI 开发"岗位的**第一卖点**。目标是证明作者建立了**可复现、有纪律的 AI 协作工程方法**。

**Mermaid 流程图**：展示完整闭环。

```
需求 → 头脑风暴(brainstorming) → 设计规格(spec)
     → 实施计划(plan) → TDD 单元测试 → 实现 → 代码审查 → 复核验证
```

用 `flowchart LR`，标注每个阶段的产出物和仓库路径。

**留痕证据表**：

| 工作流阶段 | 产出物 | 仓库位置 | 数量 |
|-----------|--------|---------|------|
| 头脑风暴 + 设计规格 | 模块设计文档 | `docs/superpowers/specs/` | 6 份 |
| 实施计划 | 逐步实施计划 | `docs/superpowers/plans/` | 7 份 |
| 代码审查 | 模块审查报告 | `docs/agents/code-review/report-*.md` | 14 份 |
| 审查复核 | 修复验证报告 | `docs/agents/code-review/verify-*.md` | 14 份 |
| 单元测试 | 主机端测试 | `test/host/` | 5 套 |
| AI 协作规范 | 编码风格 / OOP 准则 / 错误处理 / 审查清单 | `docs/agents/` | 7 篇 |

**方法论叙述**（3-4 句）：强调三点——
1. **人主导设计**：架构决策、模块边界、错误处理策略由人定义，AI 执行。
2. **AI 不绕过质量门**：每段代码都经过 spec→plan→test→review，不是一次性生成。
3. **全程可追溯**：每个模块的设计、计划、审查、复核都在仓库留痕，可回查。

---

### ⑥ 深入：双模联网 C OOP 设计（差异化章节）

本章是项目的**技术差异化核心**，也是 C OOP 能力的直接证明。配两张 Mermaid 图。

**图 1：类继承图（Mermaid classDiagram）**

```
classDiagram
    class network_link_t {
        <<abstract>>
        +ops: network_link_ops_t*
        +start() / stop()
        +publish() / subscribe()
        +get_status()
    }
    class wifi_link_t {
        Wi-Fi STA + esp-mqtt
    }
    class lte_link_t {
        UART → Air780EP + esp-lwlte
    }
    network_link_t <|-- wifi_link_t
    network_link_t <|-- lte_link_t
    network_manager_t o-- network_link_t : primary + backup
```

设计要点：说明 C 单继承手法（`network_link_t base` 作为子类结构体首成员）、
vtable 分发、`create()` 返回基类指针让管理器看不见具体类型。

**图 2：故障切换时序图（Mermaid sequenceDiagram）**

展示 Wi-Fi 故障 → LTE 接管 → Wi-Fi 恢复 → 回切的完整流程：

```
Wi-Fi(主) --断开--> network_manager --5s 检测--> 切 LTE(备)
LTE --set_active(true)--> MQTT 重建(~1-3s) --> 业务恢复
... Wi-Fi 恢复 --> 延迟 30s 防抖 --> 回切 Wi-Fi --> LTE set_active(false)
```

**关键参数表**：

| 参数 | 值 | 说明 |
|------|----|------|
| 故障切换检测周期 | 5 s | 主链路状态轮询 |
| 回切防抖延迟 | 30 s | 防止主链路抖动导致频繁切换 |
| LTE MQTT 重建耗时 | ~1-3 s | 热备网络策略：开机即注册驻网，切换只重建 MQTT |
| 订阅意图表容量 | 8 | 链路切换后自动重放订阅 |

**热备策略说明**（2-3 句）：LTE 开机即完成网络注册 + PDP 激活（~15-30s 昂贵阶段只付一次），
MQTT 连接仅在 LTE 成为活跃链路时才建立 —— 既保证快速故障切换，又避免 Wi-Fi 健康时
白白消耗 LTE 流量。

---

### ⑦ 深入：工程化体系

证明项目不是"能跑就行"，而是有工业级工程素养。三个子板块。

**代码审查**：
- 每个模块独立审查（14 份报告）+ 复核验证（14 份）。
- 审查清单覆盖：内存安全、错误处理、线程安全、API 边界、命名规范。
- 引用 `docs/agents/review-checklist.md`。

**单元测试**：
- 主机端测试（`test/host/`），用 `_internal.c` 纯逻辑分离模式。
- 编译参数 `-Wall -Wextra -Werror`。
- 覆盖 5 个模块的纯逻辑层，硬件 API 用 `test/support/` 桩隔离。

**设计文档体系**：
- `docs/agents/` 7 篇规范：架构 / 目录结构 / 类设计 / 编码风格 / OOP 准则 / 错误处理 / 审查清单。
- 每个模块实现前先在 `classes.md` 定义类，遵循 esp-lwlte 同款格式。

---

### ⑧ 深入：云端 + 显示全栈集成

**ThingsBoard**：
- MQTT over TLS（端口 8883）。
- 上行：遥测（电压/电流/功率/电能）+ 属性上报。
- 下行：RPC 远程控制（开/关继电器、读取/设置功率阈值）。
- 只依赖 `network_manager`，不感知当前是 Wi-Fi 还是 LTE。

**LVGL 仪表盘**：
- ST7789T 172×320 SPI 屏。
- 独立 LVGL 任务（6144 栈，优先级 4，10ms tick，50ms 刷新）。
- 从聚合快照 `dashboard_state_t` 读取，仅在 LVGL 任务上下文刷新控件。

**本地安全闭环**（简述，作为加分项）：
- BL0942 采集 → metering 聚合 → safety_guard 规则评估（过流 10A / 过功率 2200W，3 次持久去抖）→ 建议动作。
- 安全规则可解释、可验证，适合面试讲解。

---

### ⑨ 技术栈 & 硬件清单

**软件栈表**：

| 类别 | 技术 | 版本 |
|------|------|------|
| 框架 | ESP-IDF | v6.0.0 |
| 语言 | C (C11) | — |
| 实时系统 | FreeRTOS（ESP-IDF 内置） | — |
| 显示框架 | LVGL | 9.5.0 |
| MQTT | esp-mqtt（espressif/mqtt 组件） | 1.0.0 |
| 按键 | espressif/button | 4.1.6 |
| LTE 驱动 | esp-lwlte（独立仓库，Air780EP） | — |
| 云平台 | ThingsBoard | — |

**硬件清单表**：

| 部件 | 型号 | 角色 |
|------|------|------|
| 主控板 | ESP32-S3-LCD-1.47B | 主控 + 显示 |
| 电能计量 | BL0942 | 电压/电流/功率/电能采集 |
| LTE 模组 | Air780EP | LTE 备用链路 |
| 显示屏 | ST7789T 172×320 | 本地仪表盘 |
| 继电器 | — | 负载通断 |
| Flash | 8 MB | 分区：factory 7MB + nvs 24KB |

---

### ⑩ 快速开始

**前置条件**：
- ESP-IDF v6.0.0 已安装并激活。
- esp-lwlte 仓库克隆为同级目录（`../esp-lwlte/src`），否则 LTE 构建路径不可用（Wi-Fi-only 模式可用）。

**步骤**（代码块）：
```bash
# 1. 克隆（注意 esp-lwlte 需放在同级目录）
git clone git@github.com:ZhouDreams/dual-link-smart-socket.git
git clone https://github.com/ZhouDreams/esp-lwlte.git ../esp-lwlte

# 2. 设置目标
idf.py set-target esp32s3

# 3. 配置（Wi-Fi SSID/密码、ThingsBoard broker/token、LTE 开关）
idf.py menuconfig

# 4. 构建并烧录
idf.py build flash monitor
```

**LTE 开关说明**：`CONFIG_SMART_SOCKET_LTE_ENABLED`（默认 `n`）。关闭时 network_manager
以单链路 Wi-Fi-only 运行，backup=NULL，无需 esp-lwlte。

---

### ⑪ 项目演化 & 相关仓库

**项目演化**（简述，2-3 句）：
- 本项目从 EEE532 毕业设计重构而来，原项目以"电动自行车电池充电异常 AI 模型"为主线。
- 重构后收敛为聚焦的、可解释的、面试级的双模智能插座系统，移除了不可验证的 AI 风险模型，
  安全保护改用可解释的规则逻辑。

**相关仓库**：
- [esp-lwlte](https://github.com/ZhouDreams/esp-lwlte)：独立的 ESP32 LTE（Air780EP）驱动组件，本项目通过 `EXTRA_COMPONENT_DIRS` 引入。

---

## 已确认的决策

| 项目 | 决策 |
|--------|---------|
| License | MIT — 新增 `LICENSE` 文件 + License 徽章 |
| 联系方式 | 不放邮箱 / 微信 |
| esp-lwlte 仓库地址 | `https://github.com/ZhouDreams/esp-lwlte`（已公开） |
| 作者 GitHub 链接 | 不放 |

## 不写入 README 的内容

| 排除项 | 理由 |
|--------|------|
| `reference/谢师宴发言稿.docx` | 与项目无关的私人文档 |
| EEE532 旧项目 AI 电池模型细节 | 已弃用，仅在演化段一句话提及 |
| 完整 API 清单 / 类字段表 | 太长，链接到 `docs/agents/classes.md` |
| 逐模块代码审查报告全文 | 太长，只放统计数字 + 链接到 `docs/agents/code-review/` |
| sdkconfig 完整配置 | 链接到 `sdkconfig.defaults` 即可 |

## 成功标准

1. 面试官打开仓库 30 秒内能说出"这是个 ESP32 双模智能插座，用 AI 工程方法做的"。
2. 能看到至少 2 张 Mermaid 图（架构图 + 类图/时序图），无照片也不显空洞。
3. AI 工作流章节有可点击的留痕证据（仓库内真实存在的文件）。
4. 快速开始章节可复现（任何人照着能 build）。
5. 整体视觉层次清晰：徽章 → 表格 → 图 → 深入章节，无明显文字墙。
