# 类定义

在编码前先确定每个模块有哪些类（struct + 配套函数），是理解项目架构最重要的文档。每个类的定义包含：**所属层、职责、可见性、关键字段、关键方法、OOP 角色**。

## 可见性三级定义

| 可见性 | 落入哪个头文件 | 谁能看到 | 命名前缀 |
|--------|-------------|---------|---------|
| **用户 API** | `main/<module>/<module>.h` | 所有模块 | `<module>_` |
| **内部** | `.c` 或 `_priv.h` | 当前模块自身 | 无限制 |

**说明**：Smart_Socket 是应用工程而非组件库，所有模块公共头文件放在 `main/<module>/` 下，模块间可以直接 include。没有"层间 API"这一级——每个模块的公共头文件对所有其他模块可见。

---

## 1. Board Pinmap（板级引脚映射）

Board Pinmap 是驱动适配层的基础模块，为所有硬件模块提供统一的 GPIO 映射。

### 1.1 类总览

| 类 | 可见性 | 被谁使用 | OOP 角色 | 说明 |
|----|--------|---------|---------|------|
| `board_active_level_t` | 用户 API | 所有读取 pinmap 的模块 | 枚举 | 板级有效电平（高/低有效） |
| `board_pinmap_t` | 用户 API | app_controller + 各驱动模块 | 值对象 | 只读引脚映射结构体，编译期确定 |

### 1.2 `board_active_level_t` — 有效电平

**所属层**：驱动适配层
**可见性**：用户 API
**OOP 角色**：枚举

```c
typedef enum {
    BOARD_ACTIVE_LOW = 0,
    BOARD_ACTIVE_HIGH = 1,
} board_active_level_t;
```

### 1.3 `board_pinmap_t` — 引脚映射

**所属层**：驱动适配层
**可见性**：用户 API — 所有模块通过 `board_pinmap_get()` 获取只读指针
**OOP 角色**：值对象（只读单例）

```c
typedef struct {
    /* 按键 */
    gpio_num_t button_gpio;
    board_active_level_t button_active_level;

    /* 继电器 */
    gpio_num_t relay_ctrl_gpio;
    board_active_level_t relay_active_level;

    /* BL0942 */
    gpio_num_t bl0942_en_gpio;
    gpio_num_t bl0942_tx_gpio;
    gpio_num_t bl0942_rx_gpio;

    /* LTE (Air780EP) */
    gpio_num_t lte_en_gpio;
    gpio_num_t lte_tx_gpio;
    gpio_num_t lte_rx_gpio;

    /* TFT LCD (SPI) */
    gpio_num_t tft_sclk_gpio;
    gpio_num_t tft_mosi_gpio;
    gpio_num_t tft_dc_gpio;
    gpio_num_t tft_cs_gpio;
    gpio_num_t tft_rst_gpio;
    gpio_num_t tft_bl_gpio;
} board_pinmap_t;
```

**唯一 API**：

```c
const board_pinmap_t *board_pinmap_get(void);
```

**调用模式**：

```c
const board_pinmap_t *pinmap = board_pinmap_get();

relay_config_t relay_cfg = {
    .ctrl_gpio    = pinmap->relay_ctrl_gpio,
    .active_level = (relay_active_level_t)pinmap->relay_active_level,
};
relay_t *relay = relay_create(&relay_cfg);
```

**关键设计决策**：
- 单例，定义在 `board_pinmap.c` 的 `static const board_pinmap_t s_pinmap`，编译期确定
- 不暴露 mutable 接口，物理引脚不会在运行时改变
- `gpio_num_t` 使用 ESP-IDF 原生类型，不重新封装
- 未使用的引脚填 `GPIO_NUM_NC`
- 不包含 UART port number、SPI host 等非 GPIO 资源，这些由各驱动模块自己管理

---

## 2. Network Types（网络公共类型）

Network Types 是网络抽象层的公共类型定义，为所有网络模块提供共享的枚举和结构体。纯头文件，无 .c，不引入任何编译依赖。

### 2.1 类总览

| 类 | 可见性 | 被谁使用 | OOP 角色 | 说明 |
|----|--------|---------|---------|------|
| `network_link_type_t` | 用户 API | network_manager + 上层 | 枚举 | Wi-Fi / LTE / None |
| `network_link_status_t` | 用户 API | network_manager + 上层 | 枚举 | 链路生命周期状态 |
| `network_mqtt_qos_t` | 用户 API | 所有 MQTT 调用方 | 枚举 | MQTT QoS 等级 |
| `network_publish_request_t` | 用户 API | 所有 MQTT 发布方 | 值对象 | 一次 MQTT publish 的完整参数 |
| `network_rx_data_t` | 用户 API | 下行消息消费者 | 值对象 | 收到的 MQTT 消息 |
| `network_rx_cb_t` | 用户 API | thingsboard_client + app_controller | 回调类型 | 下行消息回调签名 |

### 2.2 `network_link_type_t` — 链路类型

**所属层**：网络抽象层
**可见性**：用户 API
**OOP 角色**：枚举

```c
typedef enum {
    NETWORK_LINK_TYPE_NONE = 0,
    NETWORK_LINK_TYPE_WIFI,
    NETWORK_LINK_TYPE_LTE,
} network_link_type_t;
```

### 2.3 `network_link_status_t` — 链路状态

**所属层**：网络抽象层
**可见性**：用户 API
**OOP 角色**：枚举

```c
typedef enum {
    NETWORK_LINK_STATUS_IDLE = 0,
    NETWORK_LINK_STATUS_STARTING,
    NETWORK_LINK_STATUS_CONNECTING,
    NETWORK_LINK_STATUS_DEGRADED,
    NETWORK_LINK_STATUS_READY,
    NETWORK_LINK_STATUS_ERROR,
} network_link_status_t;
```

**状态语义**：

| 状态 | 含义 |
|------|------|
| `IDLE` | 链路未启动 |
| `STARTING` | 链路启动中 |
| `CONNECTING` | 物理网络连接中 |
| `DEGRADED` | 物理链路可用但 MQTT 不可用 |
| `READY` | 物理链路与 MQTT 均可用 |
| `ERROR` | 链路故障，需重试或降级 |

### 2.4 `network_mqtt_qos_t` — MQTT QoS

**所属层**：网络抽象层
**可见性**：用户 API
**OOP 角色**：枚举

```c
typedef enum {
    NETWORK_MQTT_QOS0 = 0,
    NETWORK_MQTT_QOS1 = 1,
    NETWORK_MQTT_QOS2 = 2,
} network_mqtt_qos_t;
```

### 2.5 `network_publish_request_t` — 发布请求

**所属层**：网络抽象层
**可见性**：用户 API — thingsboard_client 等模块栈上构造后传入 network_manager
**OOP 角色**：值对象

```c
typedef struct {
    const char *topic;
    const void *payload;
    size_t payload_len;
    network_mqtt_qos_t qos;
    bool retain;
} network_publish_request_t;
```

### 2.6 `network_rx_data_t` — 下行消息

**所属层**：网络抽象层
**可见性**：用户 API — network_manager 回调时传入
**OOP 角色**：值对象

```c
typedef struct {
    const char *topic;
    const char *data;
    int data_len;
} network_rx_data_t;
```

### 2.7 `network_rx_cb_t` — 下行消息回调

**所属层**：网络抽象层
**可见性**：用户 API
**OOP 角色**：回调类型

```c
typedef void (*network_rx_cb_t)(const network_rx_data_t *rx_data, void *user_ctx);
```

**关键设计决策**：
- 纯头文件，只依赖 `<stdint.h>`、`<stddef.h>`、`<stdbool.h>`
- 不声明任何函数，不 include ESP-IDF 头文件
- `network_publish_request_t` 中 `payload` 用 `const void *`，不限定上层传 JSON 还是二进制
- `network_link_t` opaque handle 和 `network_link_ops_t` 虚函数表不放在这里——它们属于 Batch 3 的 `network_link.h`
- 枚举值以 `NETWORK_` 为前缀，与模块名保持一致

---

## 3. Relay（继电器控制）

Relay 是驱动适配层的执行器模块，负责继电器 GPIO 控制和状态管理。

### 3.1 类总览

| 类 | 可见性 | 被谁使用 | OOP 角色 | 说明 |
|----|--------|---------|---------|------|
| `relay_active_level_t` | 用户 API | app_controller | 枚举 | 继电器有效电平 |
| `relay_source_t` | 用户 API | 所有调用 set/toggle 的模块 | 枚举 | 操作来源标签 |
| `relay_config_t` | 用户 API | app_controller | 配置结构体 | 创建时的 GPIO + 电平参数 |
| `relay_t` | 用户 API (opaque) | app_controller + safety_guard | 句柄 | 继电器实例，struct 定义在 .c |
| `relay_state_changed_event_t` | 用户 API | 订阅 esp_event 的模块 | 值对象 | 状态变化事件载荷 |

### 3.2 `relay_active_level_t` — 有效电平

**所属层**：驱动适配层
**可见性**：用户 API
**OOP 角色**：枚举

```c
typedef enum {
    RELAY_ACTIVE_LOW = 0,
    RELAY_ACTIVE_HIGH = 1,
} relay_active_level_t;
```

### 3.3 `relay_source_t` — 操作来源

**所属层**：驱动适配层
**可见性**：用户 API
**OOP 角色**：枚举

```c
typedef enum {
    RELAY_SOURCE_INTERNAL = 0,
    RELAY_SOURCE_LOCAL_BUTTON,
    RELAY_SOURCE_CLOUD,
    RELAY_SOURCE_SAFETY,
    RELAY_SOURCE_MAX,
} relay_source_t;
```

### 3.4 `relay_config_t` — 初始化配置

**所属层**：驱动适配层
**可见性**：用户 API — app_controller 从 board_pinmap 读取后填入
**OOP 角色**：配置结构体

```c
typedef struct {
    gpio_num_t ctrl_gpio;
    relay_active_level_t active_level;
} relay_config_t;
```

### 3.5 `relay_state_changed_event_t` — 状态变化事件

**所属层**：驱动适配层
**可见性**：用户 API — 通过 esp_event 广播
**OOP 角色**：值对象

```c
typedef struct {
    bool on;
    relay_source_t source;
} relay_state_changed_event_t;
```

### 3.6 `relay_t` — 继电器句柄

**所属层**：驱动适配层
**可见性**：用户 API (opaque) — app_controller 创建，safety_guard 通过句柄操作；struct 定义在 `.c` 中
**OOP 角色**：句柄

**公开方法**（`relay.h`）：

```c
relay_t *relay_create(const relay_config_t *config);
esp_err_t relay_destroy(relay_t *me);

esp_err_t relay_set(relay_t *me, relay_source_t source, bool on);
esp_err_t relay_toggle(relay_t *me, relay_source_t source);
esp_err_t relay_get(const relay_t *me, bool *out_on);
```

**事件声明**：

```c
ESP_EVENT_DECLARE_BASE(RELAY_EVENT_BASE);

typedef enum {
    RELAY_EVENT_STATE_CHANGED = 0,
} relay_event_id_t;
```

**内部结构**（定义在 `relay.c`）：

```c
struct relay {
    gpio_num_t ctrl_gpio;
    relay_active_level_t active_level;
    bool on;
    SemaphoreHandle_t mutex;
    bool initialized;
};
```

**调用方使用模式**：

```c
/* app_controller 创建 */
const board_pinmap_t *pm = board_pinmap_get();
relay_config_t cfg = {
    .ctrl_gpio    = pm->relay_ctrl_gpio,
    .active_level = (relay_active_level_t)pm->relay_active_level,
};
relay_t *relay = relay_create(&cfg);

/* 本地按键触发 */
relay_toggle(relay, RELAY_SOURCE_LOCAL_BUTTON);

/* 安全模块关断 */
relay_set(relay, RELAY_SOURCE_SAFETY, false);

/* 订阅状态变化事件 */
esp_event_handler_register(RELAY_EVENT_BASE, RELAY_EVENT_STATE_CHANGED,
                           my_handler, NULL);
```

**关键设计决策**：
- 配置接受 `gpio_num_t`，不直接依赖 `board_pinmap` — 由 `app_controller` 从 pinmap 读取后填入，保持 relay 的可移植性和可测试性
- `mutex` 用 `xSemaphoreCreateMutex()` 保护 `on` 字段和 GPIO 操作
- `relay_set` 仅在状态实际变化时发布 `RELAY_EVENT_STATE_CHANGED`，避免无意义的事件风暴；载荷含新状态和来源，订阅方可按来源过滤（例如云端下发导致的变更不再回传云端）
- `relay_toggle` 原子完成"读-改-写"，不在 get 和 set 之间被其他来源插入
- 只有 `create` 和 `destroy` 分配/释放资源，`set`/`toggle`/`get` 不涉及堆操作

---

## 4. Button（本地按键）

Button 是驱动适配层的输入模块，封装 `espressif/button`（iot_button）组件，负责去抖、时序识别和事件回调。

### 4.1 类总览

| 类 | 可见性 | 被谁使用 | OOP 角色 | 说明 |
|----|--------|---------|---------|------|
| `button_active_level_t` | 用户 API | app_controller | 枚举 | 按键有效电平 |
| `button_event_t` | 用户 API | app_controller 回调 | 枚举 | 按键事件类型 |
| `button_config_t` | 用户 API | app_controller | 配置结构体 | 创建时的 GPIO + 电平参数 |
| `button_event_cb_t` | 用户 API | app_controller | 回调类型 | 单个事件的回调签名 |
| `button_t` | 用户 API (opaque) | app_controller | 句柄 | 按键实例，struct 定义在 .c |

### 4.2 `button_active_level_t` — 有效电平

**所属层**：驱动适配层
**可见性**：用户 API
**OOP 角色**：枚举

```c
typedef enum {
    BUTTON_ACTIVE_LOW = 0,
    BUTTON_ACTIVE_HIGH = 1,
} button_active_level_t;
```

### 4.3 `button_event_t` — 按键事件

**所属层**：驱动适配层
**可见性**：用户 API
**OOP 角色**：枚举

```c
typedef enum {
    BUTTON_EVENT_SINGLE_CLICK = 0,
    BUTTON_EVENT_DOUBLE_CLICK,
    BUTTON_EVENT_LONG_PRESS_START,
    BUTTON_EVENT_LONG_PRESS_HOLD,
    BUTTON_EVENT_MAX,
} button_event_t;
```

### 4.4 `button_config_t` — 初始化配置

**所属层**：驱动适配层
**可见性**：用户 API — app_controller 从 board_pinmap 读取后填入
**OOP 角色**：配置结构体

```c
typedef struct {
    gpio_num_t input_gpio;
    button_active_level_t active_level;
} button_config_t;
```

### 4.5 `button_event_cb_t` — 事件回调

**所属层**：驱动适配层
**可见性**：用户 API
**OOP 角色**：回调类型

```c
typedef void (*button_event_cb_t)(button_event_t event, void *user_ctx);
```

**回调约束**：回调在 iot_button 内部任务上下文中执行，必须短小非阻塞。不得在回调中调用 `button_destroy()`。

### 4.6 `button_t` — 按键句柄

**所属层**：驱动适配层
**可见性**：用户 API (opaque) — app_controller 创建，struct 定义在 `.c` 中
**OOP 角色**：句柄

**公开方法**（`button.h`）：

```c
button_t *button_create(const button_config_t *config);
esp_err_t button_destroy(button_t *me);

esp_err_t button_register_cb(button_t *me, button_event_t event,
                              button_event_cb_t cb, void *user_ctx);
```

**内部结构**（定义在 `button.c`）：

```c
struct button {
    gpio_num_t input_gpio;
    button_active_level_t active_level;
    void *iot_button_handle;
    button_event_cb_t callbacks[BUTTON_EVENT_MAX];
    void *user_ctxs[BUTTON_EVENT_MAX];
    bool initialized;
};
```

**调用方使用模式**：

```c
/* app_controller 创建 */
const board_pinmap_t *pm = board_pinmap_get();
button_config_t cfg = {
    .input_gpio   = pm->button_gpio,
    .active_level = (button_active_level_t)pm->button_active_level,
};
button_t *btn = button_create(&cfg);

/* 注册单击回调 */
static void on_single_click(button_event_t event, void *user_ctx) {
    relay_t *relay = (relay_t *)user_ctx;
    relay_toggle(relay, RELAY_SOURCE_LOCAL_BUTTON);
}
button_register_cb(btn, BUTTON_EVENT_SINGLE_CLICK, on_single_click, relay);

/* 注册长按回调 */
static void on_long_press(button_event_t event, void *user_ctx) {
    display_service_t *ds = (display_service_t *)user_ctx;
    display_service_toggle_screen(ds);
}
button_register_cb(btn, BUTTON_EVENT_LONG_PRESS_START, on_long_press, display_svc);
```

**关键设计决策**：
- 内部封装 `espressif/button`（iot_button），不对外暴露 iot_button 类型
- `button_config_t` 只包含板级配置（GPIO + 电平），按键时序参数（长按阈值、双击间隔等）使用 iot_button 默认值；后续如需可配再扩展
- 每个事件类型独立注册回调，`app_controller` 只注册自己关心的事件
- 同一事件的重复注册覆盖旧回调
- 回调传递 `button_event_t`，让只注册一个总回调的用户也能区分事件类型
- 不依赖 `board_pinmap` — GPIO 和电平由 `app_controller` 从 pinmap 读取后填入 `button_config_t`
- `button_destroy` 内部注销所有 iot_button 回调并释放 iot_button 对象

---

## 模块依赖关系（Batch 1 & 2）

```text
board_pinmap (无依赖)
network_types.h (无依赖，纯头文件)
relay (依赖: driver/gpio.h, FreeRTOS, esp_event, 不依赖 board_pinmap)
button (依赖: espressif/button, driver/gpio.h, 不依赖 board_pinmap)
```

`relay` 和 `button` 接受 `gpio_num_t` 配置，由 `app_controller`（Batch 7）从 `board_pinmap` 读取后注入，保持各模块的独立可测试性。

---

## 5. BL0942（电能计量芯片驱动）

BL0942 是驱动适配层的电参量采集模块，负责 UART 通信、数据帧校验、周期性采样和故障检测。

### 5.1 类总览

| 类 | 可见性 | 被谁使用 | OOP 角色 | 说明 |
|----|--------|---------|---------|------|
| `bl0942_t` | 用户 API (opaque) | app_controller | 句柄 | BL0942 实例，struct 定义在 .c |
| `bl0942_config_t` | 用户 API | app_controller | 配置结构体 | UART + GPIO + 波特率 + 采样参数 |
| `bl0942_measurement_t` | 用户 API | metering_service | 值对象 | 单次全电参数测量快照（原始寄存器值） |
| `bl0942_fault_info_t` | 用户 API | metering_service + app_controller | 值对象 | 故障事件载荷 |

### 5.2 `bl0942_config_t` — 初始化配置

**所属层**：驱动适配层
**可见性**：用户 API — app_controller 从 board_pinmap 读取 UART/GPIO 后填入
**OOP 角色**：配置结构体

```c
typedef struct {
    uart_port_t uart_num;             // UART 端口号
    gpio_num_t en_gpio;               // 模块电源使能引脚（不使用填 GPIO_NUM_NC）
    gpio_num_t tx_gpio;               // ESP TX → BL0942 RX/SDI
    gpio_num_t rx_gpio;               // ESP RX ← BL0942 TX/SDO
    int baud_rate;                    // UART 波特率（默认 9600）
    uint8_t device_address;           // 器件地址 0~3
    int rx_buf_size;                  // UART RX 环形缓冲区大小
    int read_timeout_ms;              // 单次读取超时
    int sample_period_ms;             // 内部采样任务周期（默认 100ms）
    int fault_threshold;              // 连续失败触发 FAULT 事件的阈值
    int hard_reset_max_attempts;      // 硬复位最大尝试次数
} bl0942_config_t;
```

### 5.3 `bl0942_measurement_t` — 测量快照

**所属层**：驱动适配层
**可见性**：用户 API — 通过 esp_event 广播或 get_latest 获取
**OOP 角色**：值对象

```c
typedef struct {
    uint32_t i_rms_raw;               // 电流有效值原始寄存器
    uint32_t v_rms_raw;               // 电压有效值原始寄存器
    uint32_t i_fast_rms_raw;          // 快速电流有效值原始寄存器
    int32_t watt_raw;                 // 有功功率原始寄存器
    uint32_t cf_cnt_raw;              // 电能脉冲计数原始寄存器
    uint16_t freq_raw;                // 频率原始寄存器
    uint16_t status_raw;              // 状态原始寄存器
    uint64_t capture_time_us;         // 采集时间戳（微秒）
    bool valid;                       // 快照是否有效
} bl0942_measurement_t;
```

**关键设计决策**：只保留原始寄存器值，不在此层做工程量换算。电压 mV、电流 mA、功率 mW、电能 Wh 的换算由 `metering_service` 负责。

### 5.4 `bl0942_fault_info_t` — 故障信息

**所属层**：驱动适配层
**可见性**：用户 API — `BL0942_EVENT_FAULT` 事件载荷
**OOP 角色**：值对象

```c
typedef struct {
    uint32_t consecutive_failures;    // 触发本次事件的连续失败次数
    uint32_t fault_cycles;            // 启动后累计 FAULT 事件次数
    bool hard_reset_attempted;        // 是否已尝试 EN-pin 硬复位
    esp_err_t last_error;             // 最近一次读取返回值
} bl0942_fault_info_t;
```

### 5.5 `bl0942_t` — BL0942 句柄

**所属层**：驱动适配层
**可见性**：用户 API (opaque) — app_controller 创建，metering_service 通过事件消费数据；struct 定义在 `.c` 中
**OOP 角色**：句柄

**公开方法**（`bl0942.h`）：

```c
bl0942_t *bl0942_create(const bl0942_config_t *config);
esp_err_t bl0942_destroy(bl0942_t *me);

esp_err_t bl0942_start(bl0942_t *me);
esp_err_t bl0942_stop(bl0942_t *me);

esp_err_t bl0942_read(bl0942_t *me, bl0942_measurement_t *out);
esp_err_t bl0942_get_latest(bl0942_t *me, bl0942_measurement_t *out);
```

**事件声明**：

```c
ESP_EVENT_DECLARE_BASE(BL0942_EVENT_BASE);

typedef enum {
    BL0942_EVENT_MEASUREMENT = 0,    // 单次测量成功（载荷: bl0942_measurement_t）
    BL0942_EVENT_FAULT,              // 连续失败达阈值（载荷: bl0942_fault_info_t）
} bl0942_event_id_t;
```

**内部结构**（定义在 `bl0942.c`）：

```c
struct bl0942 {
    bl0942_config_t config;
    SemaphoreHandle_t mutex;             // 保护 latest 和硬件访问
    bl0942_measurement_t latest;         // 最近一次成功的测量缓存
    bool has_latest;
    TaskHandle_t sample_task;            // 内部采样任务句柄
    SemaphoreHandle_t sample_task_done_sema;
    bool sample_task_running;
    uint32_t fault_cycles;               // 累计 FAULT 次数
    int consecutive_failures;            // 当前连续失败计数
    int hard_reset_count;                // 当前连续硬复位次数
    bool initialized;
};
```

**调用方使用模式**：

```c
/* app_controller 创建 */
const board_pinmap_t *pm = board_pinmap_get();
bl0942_config_t cfg = {
    .uart_num        = UART_NUM_1,
    .en_gpio         = pm->bl0942_en_gpio,
    .tx_gpio         = pm->bl0942_tx_gpio,
    .rx_gpio         = pm->bl0942_rx_gpio,
    .baud_rate       = 9600,
    .device_address  = 0,
    .rx_buf_size     = 256,
    .read_timeout_ms = 500,
    .sample_period_ms = 100,
    .fault_threshold = 10,
    .hard_reset_max_attempts = 3,
};
bl0942_t *bl = bl0942_create(&cfg);
bl0942_start(bl);  // 启动 10Hz 采样，事件开始广播

/* metering_service 订阅 */
esp_event_handler_register(BL0942_EVENT_BASE, BL0942_EVENT_MEASUREMENT,
                           my_measurement_handler, NULL);
```

### 5.6 BL0942 线程模型

```
┌─────────────────────────────────────────────────────────────┐
│                     BL0942 线程模型                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  BL0942 采样任务（bl0942 内部）                               │
│  ┌──────────────────┐                                       │
│  │ sample_task      │──→ 循环（每 sample_period_ms）:         │
│  └────────┬─────────┘     ──→ 发送 UART 读取命令              │
│           │               ──→ 校验帧头和 checksum             │
│           │               ──→ 成功: 更新 latest，             │
│           │                      重置连续失败计数，            │
│           │                      发布 BL0942_EVENT_MEASUREMENT│
│           │               ──→ 失败: 递增连续失败计数，        │
│           │                      达阈值 → 发布 FAULT 事件     │
│           │                              → 尝试 EN-pin 硬复位│
│           │               ──→ 硬复位超限: 停止任务            │
│                                                             │
│  调用方线程（同步读取）                                       │
│  ┌──────────────────┐                                       │
│  │ bl0942_read()    │──→ 发送命令 + 阻塞等待响应              │
│  │                  │    （不与采样任务并发，mutex 串行化）     │
│  │ bl0942_get_latest│──→ 返回缓存的 latest（不加锁）          │
│  └──────────────────┘                                       │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 5.7 关键设计决策

- `bl0942_start()` 启动内部采样任务，周期由 `sample_period_ms` 配置（默认 100ms = 10Hz）
- `bl0942_read()` 是同步阻塞接口，与内部采样任务通过 mutex 串行化，供启动前芯片验证或临时单次读取
- `bl0942_get_latest()` 返回缓存的最新测量值，无锁读取
- 内部故障检测：连续失败达 `fault_threshold` → 发布 `BL0942_EVENT_FAULT` → 自动 EN-pin 硬复位 → 硬复位超 `hard_reset_max_attempts` 则停止采样任务
- UART 配置只用单一波特率（`baud_rate`），不做启动波特率→运行波特率切换
- 原始寄存器值不在此层转换工程量，换算逻辑属于 `metering_service`

---

## 6. Network Link（网络链路基类接口）

Network Link 是网络抽象层的核心，定义 Wi-Fi/LTE 双模链路的统一基类接口，通过 ops 表实现继承与多态。`network_manager` 只依赖 `network_link_t *`，不感知当前链路的具体类型。

### 6.1 类总览

| 类 | 可见性 | 被谁使用 | OOP 角色 | 说明 |
|----|--------|---------|---------|------|
| `network_link_t` | 用户 API (opaque) | network_manager | 句柄/基类 | 所有链路的统一句柄，struct 定义在 .c 或 _priv.h |
| `network_link_ops_t` | 内部 | wifi_link + lte_link | 虚函数表 | 子类实现为 `static const`，不暴露给 network_manager |

以下类型已在 Batch 1 的 `network_types.h` 中定义，`network_link` 引用它们：
`network_link_type_t`、`network_link_status_t`、`network_mqtt_qos_t`、`network_publish_request_t`、`network_rx_data_t`、`network_rx_cb_t`。

### 6.2 `network_link_ops_t` — 虚函数表

**所属层**：网络抽象层
**可见性**：内部 — 定义在 `network_link_priv.h`，只给子类实现和包装函数使用。不放入 `network_link.h`。
**OOP 角色**：虚函数表

```c
typedef struct {
    esp_err_t (*destroy)(network_link_t *me);
    esp_err_t (*start)(network_link_t *me);
    esp_err_t (*stop)(network_link_t *me);
    esp_err_t (*get_status)(network_link_t *me, network_link_status_t *out);
    esp_err_t (*publish)(network_link_t *me, const network_publish_request_t *req);
    esp_err_t (*subscribe)(network_link_t *me, const char *topic,
                            network_mqtt_qos_t qos);
    esp_err_t (*unsubscribe)(network_link_t *me, const char *topic);
    esp_err_t (*register_rx_cb)(network_link_t *me, network_rx_cb_t cb,
                                 void *ctx);
} network_link_ops_t;
```

**方法语义**：

| 方法 | 语义 |
|------|------|
| `destroy` | 释放子类所有资源，停止任务/断开连接/注销回调，最后释放自身内存 |
| `start` | 启动物理连接 + MQTT 建链，异步推进，状态通过 `get_status` 观察 |
| `stop` | 断开 MQTT + 物理连接，保留已分配资源和配置，可重新 `start` |
| `get_status` | 返回当前链路快照（link_type、state、link_up、mqtt_ready） |
| `publish` | 通过当前链路的 MQTT 连接发布消息 |
| `subscribe` | 在当前链路上注册主题订阅，支持重连后重放 |
| `unsubscribe` | 撤销订阅 |
| `register_rx_cb` | 注册下行消息回调，同一时刻只保留一个回调 |

### 6.3 `network_link_t` — 基类句柄

**所属层**：网络抽象层
**可见性**：用户 API (opaque) — `network_link.h` 只暴露前置声明；`network_manager` 持有 `network_link_t *`，不直接访问 ops 或内部字段
**OOP 角色**：抽象基类

**公开类型**（`network_link.h`）：

```c
typedef struct network_link network_link_t;
```

**公开方法**（`network_link.h` — 包装函数，内部做参数校验后委托 ops）：

```c
esp_err_t network_link_destroy(network_link_t *me);
esp_err_t network_link_start(network_link_t *me);
esp_err_t network_link_stop(network_link_t *me);
esp_err_t network_link_get_status(const network_link_t *me,
                                   network_link_status_t *out);
esp_err_t network_link_publish(network_link_t *me,
                                const network_publish_request_t *req);
esp_err_t network_link_subscribe(network_link_t *me, const char *topic,
                                  network_mqtt_qos_t qos);
esp_err_t network_link_unsubscribe(network_link_t *me, const char *topic);
esp_err_t network_link_register_rx_cb(network_link_t *me,
                                       network_rx_cb_t cb, void *ctx);
network_link_type_t network_link_get_type(const network_link_t *me);
```

**内部结构**（定义在 `network_link_priv.h`）：

```c
struct network_link {
    const network_link_ops_t *ops;
    network_link_type_t type;
};
```

**包装函数实现模式**：

```c
esp_err_t network_link_start(network_link_t *me)
{
    if (!me || !me->ops || !me->ops->start) {
        return ESP_ERR_INVALID_ARG;
    }
    return me->ops->start(me);
}
```

所有包装函数遵循相同模式：参数校验 → ops 方法存在性检查 → 委托。

### 6.4 子类实现模式（wifi_link / lte_link，后续批次实现）

子类必须把基类作为第一个成员：

```c
struct wifi_link {
    network_link_t base;              // 必须是第一个字段
    wifi_link_config_t config;
    // Wi-Fi STA 句柄、esp-mqtt 句柄、订阅表、运行状态...
};

static const network_link_ops_t wifi_link_ops = {
    .destroy        = wifi_link_destroy_impl,
    .start          = wifi_link_start_impl,
    .stop           = wifi_link_stop_impl,
    .get_status     = wifi_link_get_status_impl,
    .publish        = wifi_link_publish_impl,
    .subscribe      = wifi_link_subscribe_impl,
    .unsubscribe    = wifi_link_unsubscribe_impl,
    .register_rx_cb = wifi_link_register_rx_cb_impl,
};

network_link_t *wifi_link_create(const wifi_link_config_t *config)
{
    wifi_link_t *self = calloc(1, sizeof(*self));
    if (!self) return NULL;
    self->base.ops = &wifi_link_ops;
    self->base.type = NETWORK_LINK_TYPE_WIFI;
    // 拷贝 config、初始化内部状态...
    return &self->base;
}
```

`lte_link` 同理，`base.type` 设为 `NETWORK_LINK_TYPE_LTE`。

`network_manager` 只 include `network_link.h`，通过包装函数操作，不 include `wifi_link.h` 或 `lte_link.h`。

### 6.5 调用方使用模式

```c
/* app_controller 装配（创建子类，转为基类指针注入 network_manager） */
wifi_link_t *wifi = wifi_link_create(&wifi_cfg);
lte_link_t *lte = lte_link_create(&lte_cfg);

network_manager_config_t mgr_cfg = {
    .primary = wifi_link_as_network_link(wifi),   // 等价于 &wifi->base
    .backup  = lte_link_as_network_link(lte),     // 等价于 &lte->base
};
network_manager_t *mgr = network_manager_create(&mgr_cfg);

/* network_manager 内部通过包装函数操作，不感知子类类型 */
network_link_start(mgr->primary);
network_link_publish(mgr->active, &req);
```

### 6.6 关键设计决策

- 基类结构极简 — 只含 `ops` 和 `type`，不预设状态、锁、回调等字段，每个子类管理自己的内部状态
- `network_link_ops_t` 定义在 `network_link_priv.h`，不暴露给 `network_manager` — manager 只通过包装函数调用，不直接访问 ops 表
- 包装函数统一做参数校验（NULL 检查、ops 方法存在性检查），再委托给 ops 方法，保证上层调用安全
- `network_link_get_type()` 直接读 `me->type`，不经过 ops 表（类型查询不需要多态）
- 子类必须把 `network_link_t base` 放第一个字段，保证 `&self->base == (network_link_t *)self`
- `destroy` 进入 ops 表，`network_manager` 切换链路或销毁时可以统一释放，不关心子类类型
- 子类工厂函数（`wifi_link_create` / `lte_link_create`）返回 `network_link_t *`，不暴露子类具体类型

---

## 模块依赖关系（Batch 1–3）

```text
board_pinmap      (无依赖)
network_types.h   (无依赖，纯头文件)
relay             (依赖: driver/gpio.h, FreeRTOS, esp_event)
button            (依赖: espressif/button, driver/gpio.h)
bl0942            (依赖: driver/uart.h, driver/gpio.h, FreeRTOS, esp_event)
network_link      (依赖: network_types.h, esp_err.h)
```

`bl0942` 和 `relay`/`button` 一样不直接依赖 `board_pinmap` — GPIO 和 UART 参数由 `app_controller` 从 pinmap 读取后填入 `bl0942_config_t`。

`network_link` 是唯一进入 C OOP 继承体系的模块。`wifi_link` 和 `lte_link`（后续批次）实现 `network_link_ops_t`，`network_manager` 只依赖 `network_link_t *` 基类指针。
