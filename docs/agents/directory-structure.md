# 目录说明

## 文档用途

本文档定义**基于 ESP32-S3 的双模联网智能插座系统**的推荐目录结构和各目录职责。

目录结构服务于以下目标：

1. 让代码按模块职责聚合，避免所有逻辑堆在 `main.c` 或 `app_controller.c`。
2. 让 `Wi-Fi / LTE` 双模网络的继承与多态结构有清晰落点。
3. 让 `BL0942`、继电器、按键、`ThingsBoard`、`LVGL` 等主干能力各自独立。
4. 让 `reference/` 保持只读参考区，不参与主项目构建。

---

## 1. 顶级目录

```text
Smart_Socket/
├── AGENTS.md
├── AGENTS_ZH.md
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── docs/
├── main/
├── reference/
└── test/
```

### `main/`

ESP-IDF 主应用组件目录，放当前项目的全部业务代码、驱动适配代码和模块实现。

本项目是应用工程，不是可复用组件库，因此智能插座业务模块默认放在 `main/` 下，而不是放入 `components/`。

### `docs/`

项目长期文档目录。

AI 编码助手和后续开发会话优先通过 `AGENTS.md` 读取 `docs/agents/` 下的文档。

### `reference/`

只读参考资料目录。

用于存放旧项目、硬件资料、论文、数据手册、课程材料等参考输入。除非任务明确要求整理参考资料，否则不修改该目录内容。

### `test/`

测试目录。

用于存放 host-side 单元测试、模块纯逻辑测试、ESP-IDF 测试工程或测试支撑桩。具体测试组织方式可随实现阶段调整，但测试代码不应混入 `main/` 业务模块目录。

---

## 2. `main/` 推荐结构

```text
main/
├── CMakeLists.txt
├── Kconfig.projbuild
├── idf_component.yml
├── main.c
├── app/
├── platform/
├── bl0942/
├── metering/
├── relay/
├── button/
├── network/
├── thingsboard/
├── display/
└── safety/
```

### `main.c`

系统入口。

职责：

1. 初始化 NVS。
2. 初始化 `esp_netif`。
3. 创建默认事件循环。
4. 调用 `app_controller_start()` 或同等顶层入口。

不承载业务逻辑。

### `app/`

应用编排层。

推荐文件：

```text
app/
├── app_controller.c
└── app_controller.h
```

职责：

1. 创建各模块对象。
2. 装配模块依赖。
3. 注册回调和事件处理函数。
4. 决定启动顺序。
5. 协调云端命令、本地按键、安全动作和显示刷新。

### `platform/`

板级平台适配目录。

第一版只建议放：

```text
platform/
├── board_pinmap.c
└── board_pinmap.h
```

后续如有必要，可增加：

```text
platform/
├── board_config.c
└── board_config.h
```

职责：

1. 定义当前硬件版本的 GPIO 映射。
2. 定义继电器、按键等板级有效电平。
3. 定义 BL0942、LTE、TFT 等板级固定资源。
4. 向上层提供只读 `board_pinmap_t` 或 `board_config_t`。

不放驱动逻辑、业务逻辑或网络逻辑。

### `bl0942/`

BL0942 电能计量芯片驱动适配模块。

推荐文件：

```text
bl0942/
├── bl0942.c
└── bl0942.h
```

职责：

1. 初始化 BL0942 使用的 UART 和 EN GPIO。
2. 周期性读取 BL0942 数据帧。
3. 校验帧头和 checksum。
4. 发布原始测量事件和故障事件。

### `metering/`

电参量业务聚合模块。

推荐文件：

```text
metering/
├── metering_service.c
└── metering_service.h
```

职责：

1. 消费 BL0942 原始测量事件。
2. 将原始值换算成工程量。
3. 维护短窗口聚合状态。
4. 输出电压、电流、功率、电能等遥测快照。

### `relay/`

继电器控制模块。

推荐文件：

```text
relay/
├── relay.c
└── relay.h
```

职责：

1. 初始化继电器 GPIO。
2. 提供开、关、翻转和读取状态 API。
3. 为状态变化附带来源标签。
4. 发布继电器状态变化事件。

### `button/`

本地按键模块。

推荐文件：

```text
button/
├── button.c
└── button.h
```

职责：

1. 初始化按键 GPIO 或 `espressif/button`。
2. 识别单击、双击、长按等事件。
3. 通过回调通知 `app_controller`。

### `network/`

双模网络抽象层。

推荐结构：

```text
network/
├── network_types.h
├── network_link.c
├── network_link.h
├── network_manager.c
├── network_manager.h
├── wifi/
│   ├── wifi_link.c
│   └── wifi_link.h
└── lte/
    ├── lte_link.c
    └── lte_link.h
```

职责：

1. `network_link` 定义统一链路基类接口。
2. `wifi_link` 实现 Wi-Fi + MQTT 链路子类。
3. `lte_link` 实现 LTE + MQTT 链路子类。
4. `network_manager` 只依赖 `network_link_t *`，负责主备选择、切换和统一 publish / subscribe 入口。

### `thingsboard/`

ThingsBoard 云平台语义模块。

推荐文件：

```text
thingsboard/
├── thingsboard_client.c
└── thingsboard_client.h
```

职责：

1. 组织 ThingsBoard telemetry / attributes / RPC topic。
2. 构建上报 JSON。
3. 解析云端 RPC 为内部语义命令。
4. 通过 `network_manager` 发送和接收 MQTT 消息。

### `display/`

本地显示子系统。

推荐结构：

```text
display/
├── lvgl/
│   ├── lvgl_dashboard.c
│   ├── lvgl_dashboard.h
│   ├── lvgl_dashboard_internal.c
│   └── lvgl_dashboard_internal.h
└── tft/
    ├── tft_panel.c
    ├── tft_panel.h
    ├── tft_panel_st7789t.c
    └── tft_panel_st7789t.h
```

职责：

1. `lvgl_dashboard` 直接订阅业务事件，维护显示状态缓存并负责 LVGL 控件树更新。
2. `tft_panel` 负责 LCD 面板初始化、flush 和背光控制。

### `safety/`

本地安全规则模块。

推荐文件：

```text
safety/
├── safety_guard.c
└── safety_guard.h
```

职责：

1. 持有过功率、过流等本地阈值。
2. 根据电参量快照做规则判断。
3. 输出安全状态和建议动作。
4. 不直接操作继电器，不直接访问云平台。

---

## 3. `docs/` 结构

```text
docs/
└── agents/
    ├── architecture.md
    ├── directory-structure.md
    ├── classes.md
    ├── coding-style.md
    ├── oop-design.md
    ├── build-and-debug.md
    ├── err.md
    ├── review-checklist.md
    └── serial_monitor.py
```

### `docs/agents/`

AI 编码助手和开发会话的长期上下文目录。

各文档职责：

| 文档 | 职责 |
|------|------|
| `architecture.md` | 总体架构、分层、模块边界、数据流 |
| `directory-structure.md` | 目录组织和文件归属 |
| `classes.md` | 主要类、句柄、结构体、API 形态 |
| `coding-style.md` | 代码格式、注释模板、Doxygen 规范 |
| `oop-design.md` | C 语言 OOP 设计规则 |
| `build-and-debug.md` | 构建、烧录、串口调试 |
| `err.md` | 错误处理、返回值、cleanup 规则 |
| `review-checklist.md` | 代码 review 流程、检查维度、误报防范、报告模板 |
| `serial_monitor.py` | 非交互串口监视脚本 |

---

## 4. `reference/` 规则

`reference/` 是只读参考区。

允许存放：

1. 旧项目源码。
2. 本科论文。
3. 硬件数据手册。
4. 模块资料。
5. 课程或展示材料。

使用规则：

1. 可以读取、摘录、对照。
2. 不参与主项目构建。
3. 不在其中继续开发新代码。
4. 不把临时分析结果写入该目录。
5. 如需沉淀长期结论，应写入 `docs/`。

---

## 5. `test/` 结构

`test/` 用于存放测试代码。

推荐结构：

```text
test/
├── support/
├── test_metering.c
├── test_safety_guard.c
├── test_network_manager.c
├── test_thingsboard.c
└── test_app_controller.c
```

测试目录职责：

1. `support/` 放 ESP-IDF stub、FreeRTOS stub、测试辅助函数。
2. 模块纯逻辑测试优先放在 host-side C 测试中。
3. 硬件相关行为通过 ESP-IDF 构建、烧录、串口日志验证补充。
4. 测试文件不参与固件主构建。

---

## 6. 命名规则

1. 文件名使用 `snake_case`。
2. 目录名使用模块名或子系统名。
3. 公共头文件只暴露 opaque handle、配置结构体、公共 API 和必要枚举。
4. 内部结构体定义放在 `.c` 或 `_internal.h`。
5. 本项目不是通用组件库，模块 API 不强制加全局项目前缀，但同一模块内部命名前缀应一致。
