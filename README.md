# dual-link-smart-socket

> 基于 ESP32-S3 的双模联网智能插座系统

[![Platform](https://img.shields.io/badge/Platform-ESP32--S3-blue)]()
[![Framework](https://img.shields.io/badge/Framework-ESP--IDF_v6.0-orange)]()
[![Language](https://img.shields.io/badge/Language-C_(C11)-lightgrey)]()
[![Display](https://img.shields.io/badge/Display-LVGL_9-3498db)]()
[![Cloud](https://img.shields.io/badge/Cloud-ThingsBoard-2ecc71)]()
[![Network](https://img.shields.io/badge/Network-Wi--Fi_%2B_LTE-9b59b6)]()
[![Dev](https://img.shields.io/badge/Workflow-AI--Assisted-1abc9c)]()
[![License](https://img.shields.io/badge/License-MIT-green)](LICENSE)

基于 ESP32-S3 的双模联网（Wi-Fi + LTE）智能插座系统。14 个模块、23 个源文件，
覆盖从底层驱动到云端集成的完整 IoT 能力栈。全程采用 AI 驱动的工程工作流开发，
留有 6 份设计规格、7 份实施计划、28 份代码审查报告。

---

## 核心亮点

### AI 驱动的工程工作流

不是"AI 写代码、我复制"，而是用 AI 跑通**设计规格 → 实施计划 → 单元测试 → 代码审查**
的完整工程闭环，每个决策与产出都在仓库留痕、可回查。

### 双模联网 + C OOP 架构

Wi-Fi / LTE 热备份，主链路故障自动切换到 LTE（MQTT 重建 ~1-3s），主链路恢复后自动回切。
用 C 语言继承与多态（vtable）实现统一网络抽象，上层不感知当前是 Wi-Fi 还是 LTE。

### 工程化 / 代码质量

14 个模块逐个代码审查 + 复核验证，主机端单元测试隔离纯逻辑层，配套错误处理规范与编码风格指南。

### 云端 + 显示全栈集成

ThingsBoard RPC 远程控制 / 遥测上报（MQTT over TLS），LVGL 本地仪表盘实时显示电参量。

---

## 系统架构

```mermaid
flowchart TD
    subgraph 应用层
        APP[app_controller<br/>组合根]
    end
    subgraph 业务层
        METER[metering_service<br/>计量聚合]
        SAFETY[safety_guard<br/>安全规则]
        TB[thingsboard_client<br/>云端客户端]
    end
    subgraph 网络层
        NM[network_manager<br/>主备切换]
        NL{{network_link_t 抽象基类}}
        WIFI[wifi_link]
        LTE[lte_link]
    end
    subgraph 驱动层
        BL[bl0942<br/>电能计量]
        RELAY[relay<br/>继电器]
        BTN[button<br/>按键]
    end
    subgraph 显示层
        LVGL[lvgl_dashboard]
        TFT[tft_panel ST7789T]
    end

    BL -->|原始电参量| METER
    METER -->|快照| SAFETY
    METER -->|快照| TB
    METER -->|快照| LVGL
    SAFETY -->|建议动作| APP
    BTN -->|按键事件| APP
    APP -->|通断控制| RELAY
    APP -->|RPC 响应| TB
    NM -.->|多态分发| NL
    NL --- WIFI
    NL --- LTE
    NM -->|MQTT 发布/订阅| TB
    LVGL --> TFT
```

---

## 模块全景

| 模块 | 目录 | 职责 | 关键句柄 |
|------|------|------|----------|
| 应用编排 | `main/app` | 组合根，注入 9 个模块句柄，协调启动顺序与数据流 | `app_controller_t` |
| 电能计量芯片 | `main/bl0942` | BL0942 UART 驱动，采集电压/电流/功率/电能 | `bl0942_t` |
| 计量服务 | `main/metering` | 寄存器值转工程单位，窗口聚合，电能增量确认/丢弃协议 | `metering_service_t` |
| 安全保护 | `main/safety` | 过流/过功率规则，持久采样去抖，输出三级告警 | `safety_guard_t` |
| 继电器 | `main/relay` | 通断控制，状态变更打来源标签防云端回声 | `relay_t` |
| 按键 | `main/button` | GPIO 输入，单击/双击/长按检测 | `button_t` |
| 网络链路基类 | `main/network` | 抽象基类，定义 vtable（start/stop/publish/subscribe…） | `network_link_t` |
| Wi-Fi 链路 | `main/network/wifi` | 子类：Wi-Fi STA + esp-mqtt，支持 TLS | `network_link_t*` |
| LTE 链路 | `main/network/lte` | 子类：UART→Air780EP，经 esp-lwlte | `network_link_t*` |
| 网络管理器 | `main/network` | 主备选择 + 故障切换/回切 + 订阅意图重放 | `network_manager_t` |
| ThingsBoard | `main/thingsboard` | 遥测 JSON / RPC 解析 / 属性上报 | `thingsboard_client_t` |
| LVGL 仪表盘 | `main/display/lvgl` | 独立 LVGL 任务，构建控件树，周期刷新 | `lvgl_dashboard_t` |
| TFT 面板 | `main/display/tft` | ST7789T SPI 驱动（172×320） | `tft_panel_t` |
| 引脚映射 | `main/platform` | 只读单例，返回全部 GPIO 分配 | `board_pinmap_t` |

---

## AI 驱动的工程工作流

本项目全程采用结构化的 AI 协作流程。核心原则：**人主导设计决策，AI 执行与验证；AI 不绕过任何质量门。**

```mermaid
flowchart LR
    REQ([需求]) --> BS[头脑风暴<br/>Brainstorming]
    BS --> SPEC[设计规格<br/>Spec]
    SPEC --> PLAN[实施计划<br/>Plan]
    PLAN --> TDD[TDD 单元测试<br/>先写测试]
    TDD --> IMPL[实现<br/>Implementation]
    IMPL --> REVIEW[代码审查<br/>Code Review]
    REVIEW --> VERIFY[复核验证<br/>Verify]
    VERIFY --> DONE([完成])
```

每个阶段的产出物都存档在仓库内，可追溯、可回查：

| 工作流阶段 | 产出物 | 仓库位置 | 数量 |
|-----------|--------|---------|------|
| 头脑风暴 + 设计规格 | 模块设计文档 | `docs/superpowers/specs/` | 6 份 |
| 实施计划 | 逐步实施文档 | `docs/superpowers/plans/` | 7 份 |
| 代码审查 | 模块审查报告 | `docs/agents/code-review/report-*.md` | 14 份 |
| 审查复核 | 修复验证报告 | `docs/agents/code-review/verify-*.md` | 14 份 |
| 单元测试 | 主机端测试套 | `test/host/` | 5 套 |
| AI 协作规范 | 编码风格/OOP/错误处理/审查清单/构建调试 | `docs/agents/` | 8 篇 |

这套方法确保：架构决策与模块边界由人定义，AI 负责执行、生成测试、发现问题；每段代码都经过
规格→计划→测试→审查四道关卡，而非一次性生成。

---

## 双模联网 C OOP 设计

这是项目的技术差异化核心。Wi-Fi 与 LTE 底层实现完全不同，但对上层暴露相同能力。
用 C 语言的继承与多态实现统一抽象，`network_manager` 永远不知道当前链路的具体类型。

### 类继承结构

```mermaid
classDiagram
    class network_link_t {
        <<abstract>>
        +ops : network_link_ops_t*
        +type : network_link_type_t
        +start()
        +stop()
        +publish(req)
        +subscribe(topic, qos)
        +unsubscribe(topic)
        +get_status()
        +register_rx_cb(cb)
    }
    class wifi_link_t {
        Wi-Fi STA + esp-mqtt
        支持 TLS
    }
    class lte_link_t {
        UART → Air780EP
        经 esp-lwlte
        热备 MQTT-on-active
    }
    class network_manager_t {
        +primary : network_link_t*
        +backup : network_link_t*
        +failover()
        +failback()
        +publish() / subscribe()
    }
    network_link_t <|-- wifi_link_t : C 单继承
    network_link_t <|-- lte_link_t : C 单继承
    network_manager_t o-- network_link_t : 持有主备两个基类指针
```

**实现手法**：子类结构体以 `network_link_t base` 作为首成员（C 单继承），各自提供
`static const network_link_ops_t` 虚表；`wifi_link_create()` / `lte_link_create()` 均返回
`network_link_t*`，基类 wrapper API 校验参数后经 `me->ops->` 分发。

### 故障切换与回切

```mermaid
sequenceDiagram
    participant WF as Wi-Fi（主链路）
    participant NM as network_manager
    participant LTE as LTE（备链路）
    participant APP as 业务层

    Note over WF: Wi-Fi 健康运行
    WF-->>NM: 状态 READY（每 5s 轮询）
    Note over WF: Wi-Fi 断开
    WF-->>NM: 状态 ERROR
    NM->>NM: 检测主链路故障（5s）
    NM->>LTE: set_active(true)
    Note over LTE: 热备已驻网，仅重建 MQTT（~1-3s）
    LTE-->>NM: MQTT READY
    NM->>APP: 切换完成，订阅自动重放
    APP->>LTE: 发布/订阅走 LTE

    Note over WF: Wi-Fi 恢复
    WF-->>NM: 状态 READY
    NM->>NM: 防抖等待 30s
    NM->>WF: 切回主链路
    NM->>LTE: set_active(false)
    APP->>WF: 发布/订阅走 Wi-Fi
```

### 关键参数

| 参数 | 值 | 说明 |
|------|----|------|
| 故障切换检测周期 | 5 s | 主链路状态轮询间隔 |
| 回切防抖延迟 | 30 s | 防止主链路抖动导致频繁切换 |
| LTE MQTT 重建耗时 | ~1-3 s | 热备策略：开机即完成网络注册+PDP 激活 |
| 订阅意图表容量 | 8 | 链路切换后自动重放订阅 |

**热备策略**：LTE 开机即完成网络注册与 PDP 激活（~15-30s 昂贵阶段只付一次），MQTT 连接
仅在 LTE 成为活跃链路时才建立——既保证秒级故障切换，又避免 Wi-Fi 健康时白白消耗 LTE 流量。

---

## 工程化体系

### 代码审查

每个模块独立审查，发现问题后修复并复核验证：

| 环节 | 文件 | 数量 |
|------|------|------|
| 模块审查报告 | `docs/agents/code-review/report-*.md` | 14 份 |
| 修复复核验证 | `docs/agents/code-review/verify-*.md` | 14 份 |

审查清单覆盖：内存安全、错误处理、线程安全、API 边界、命名规范、并发访问。
完整清单见 [`docs/agents/review-checklist.md`](docs/agents/review-checklist.md)。

### 单元测试

采用 `_internal.c` 纯逻辑分离模式——把不依赖硬件的业务逻辑抽到单独文件，
用桩替换 ESP-IDF 硬件 API，在主机端用标准 C 编译器运行测试：

| 测试模块 | 测试文件 |
|---------|---------|
| 计量服务 | `test/host/test_metering_service_internal.c` |
| Wi-Fi 链路 | `test/host/test_wifi_link_internal.c` |
| LTE 链路 | `test/host/test_lte_link_internal.c` |
| ThingsBoard | `test/host/test_thingsboard_client_internal.c` |
| 应用控制 | `test/host/test_app_controller_internal.c` |

编译参数 `-Wall -Wextra -Werror`，运行 `test/host/run_host_tests.sh`。

### 设计文档体系

[`docs/agents/`](docs/agents/) 下 8 篇工程规范：架构概览、目录结构、类设计、编码风格、
C OOP 准则、错误处理、构建调试、代码审查清单。每个模块实现前先定义其类设计，遵循统一格式。

---

## 云端 + 显示全栈集成

### ThingsBoard 云平台

- **传输**：MQTT over TLS（端口 8883）
- **上行**：遥测数据（电压/电流/功率/电能）+ 设备属性上报
- **下行**：RPC 远程控制——开关继电器、读取/设置功率阈值
- **解耦**：只依赖 `network_manager`，不感知当前是 Wi-Fi 还是 LTE

### LVGL 本地仪表盘

- **硬件**：ST7789T SPI 屏，172×320 分辨率
- **任务**：独立 LVGL 任务（6144 字节栈，优先级 4，10ms tick，50ms 刷新周期）
- **数据**：从聚合快照 `dashboard_state_t` 读取，仅在 LVGL 任务上下文刷新控件

### 本地安全闭环

BL0942 采集 → metering 聚合 → safety_guard 规则评估 → 建议动作：

- 过流阈值 10 A，过功率阈值 2200 W
- 3 次持久采样去抖，避免瞬时尖峰误触发
- 输出三级告警：NORMAL / WARNING / DANGER
- 安全规则可解释、可验证，适合面试讲解

---

## 技术栈

| 类别 | 技术 | 版本 |
|------|------|------|
| 框架 | ESP-IDF | v6.0.0 |
| 语言 | C（C11） | — |
| 实时系统 | FreeRTOS（ESP-IDF 内置） | — |
| 显示框架 | LVGL | 9.5.0 |
| MQTT 客户端 | esp-mqtt（espressif/mqtt 组件） | 1.0.0 |
| 按键组件 | espressif/button | 4.1.6 |
| LTE 驱动 | [esp-lwlte](https://github.com/ZhouDreams/esp-lwlte)（Air780EP） | — |
| 云平台 | ThingsBoard | — |

### 硬件清单

| 部件 | 型号 | 角色 |
|------|------|------|
| 主控板 | ESP32-S3-LCD-1.47B | 主控 + 显示 |
| 电能计量 | BL0942 | 电压/电流/功率/电能采集 |
| LTE 模组 | Air780EP | LTE 备用链路 |
| 显示屏 | ST7789T 172×320 | 本地仪表盘 |
| 继电器 | — | 负载通断 |
| Flash | 8 MB | factory 7MB + nvs 24KB + phy 4KB |

---

## 快速开始

### 前置条件

- [ESP-IDF v6.0.0](https://docs.espressif.com/projects/esp-idf/zh_CN/v6.0.0/) 已安装并激活
- [esp-lwlte](https://github.com/ZhouDreams/esp-lwlte) 仓库克隆为同级目录（LTE 功能需要；仅用 Wi-Fi 可跳过）

### 构建与烧录

```bash
# 1. 克隆（esp-lwlte 需放在同级目录 ../esp-lwlte）
git clone https://github.com/ZhouDreams/dual-link-smart-socket.git
git clone https://github.com/ZhouDreams/esp-lwlte.git ../esp-lwlte

# 2. 设置目标芯片
idf.py set-target esp32s3

# 3. 配置（Wi-Fi SSID/密码、ThingsBoard broker/token、LTE 开关）
idf.py menuconfig

# 4. 构建并烧录
idf.py build flash monitor
```

### LTE 开关

LTE 功能由 `CONFIG_SMART_SOCKET_LTE_ENABLED`（默认关闭）控制。关闭时 `network_manager`
以单链路 Wi-Fi-only 模式运行，无需 esp-lwlte。在 `menuconfig` 中开启后，需确保 esp-lwlte
已克隆到同级目录。

---

## 项目演化

本项目从 EEE532 毕业设计重构而来。原项目以"电动自行车电池充电异常 AI 模型"为主线，
功能分散且安全逻辑不可验证。重构后收敛为聚焦的双模智能插座系统，移除了 AI 风险模型，
安全保护改用可解释、可验证的规则逻辑，使整个系统适合面试讲解与工程复现。

## 相关仓库

- [esp-lwlte](https://github.com/ZhouDreams/esp-lwlte) —— 独立的 ESP32 LTE（Air780EP）驱动组件，
  本项目通过 `EXTRA_COMPONENT_DIRS` 引入
