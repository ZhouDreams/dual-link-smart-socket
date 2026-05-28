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

**回调清除语义**：`network_link_register_rx_cb(me, NULL, NULL)` 清除当前回调并返回 `ESP_OK`。这允许借用链路的 `network_manager` 在销毁时解除桥接回调，避免链路后续复用时回调到已释放对象。

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

---

## 7. Metering Service（电参量业务聚合模块）

Metering Service 是业务服务层的电参量快照模块，消费 1 Hz `BL0942_EVENT_MEASUREMENT` 原始测量事件，换算为工程量，计算上报区间电能增量，并通过 esp_event 输出单次采样快照。

### 7.1 类总览

| 类 | 可见性 | 被谁使用 | OOP 角色 | 说明 |
|----|--------|---------|---------|------|
| `metering_service_t` | 用户 API (opaque) | app_controller | 句柄 | struct 定义在 .c |
| `metering_config_t` | 用户 API | app_controller | 配置结构体 | 工程量转换系数 |
| `metering_snapshot_t` | 用户 API | safety_guard + thingsboard_client + display_service | 值对象 | 单次采样工程量快照 |

### 7.2 `metering_config_t` — 初始化配置

**所属层**：业务服务层
**可见性**：用户 API — app_controller 在创建时传入
**OOP 角色**：配置结构体

```c
typedef struct {
    float v_rms_coeff;             // 电压转换系数 (V = raw * coeff)
    float i_rms_coeff;             // 电流转换系数 (A = raw * coeff)
    float watt_coeff;              // 功率转换系数 (W = raw * coeff)
} metering_config_t;
```

### 7.3 `metering_snapshot_t` — 电参量快照

**所属层**：业务服务层
**可见性**：用户 API — 通过 esp_event 广播，或 `get_latest()` 主动获取
**OOP 角色**：值对象

```c
typedef struct {
    float voltage;                 // 电压 (V)
    float current;                 // 电流 (A)
    float power;                   // 有功功率 (W)
    float energy_delta;            // 上报区间电能增量 (mWh)
    float frequency;               // 电网频率 (Hz)
    uint64_t timestamp_us;         // 快照时间戳
    uint32_t energy_delta_token;   // 电能增量确认令牌
    bool valid;                    // 快照是否有效
} metering_snapshot_t;
```

### 7.4 `metering_service_t` — 句柄

**所属层**：业务服务层
**可见性**：用户 API (opaque) — app_controller 创建，consumers 通过事件订阅消费数据；struct 定义在 `.c` 中
**OOP 角色**：句柄

**公开方法**（`metering_service.h`）：

```c
metering_service_t *metering_service_create(const metering_config_t *config);
esp_err_t metering_service_destroy(metering_service_t *me);

esp_err_t metering_service_start(metering_service_t *me);
esp_err_t metering_service_stop(metering_service_t *me);

esp_err_t metering_service_get_latest(metering_service_t *me,
                                       metering_snapshot_t *out);
esp_err_t metering_service_reset_energy(metering_service_t *me);
esp_err_t metering_service_confirm_energy_delta(
    metering_service_t *me, const metering_snapshot_t *snapshot);
esp_err_t metering_service_discard_energy_delta(
    metering_service_t *me, const metering_snapshot_t *snapshot);
```

**事件声明**：

```c
ESP_EVENT_DECLARE_BASE(METERING_EVENT_BASE);

typedef enum {
    METERING_EVENT_SNAPSHOT = 0,     // 单次快照（载荷: metering_snapshot_t）
} metering_event_id_t;
```

**内部结构**（定义在 `metering_service.c`）：

```c
struct metering_service {
    metering_config_t config;
    SemaphoreHandle_t mutex;
    metering_snapshot_t latest;
    bool has_latest;
    metering_energy_delta_state_t energy_delta_state;
    esp_event_handler_instance_t measurement_handler;
    esp_event_handler_instance_t fault_handler;
    bool started;
    bool starting;
    bool stopping;
    bool initialized;
};
```

**调用方使用模式**：

```c
/* app_controller 创建并启动 */
metering_config_t cfg = {
    .v_rms_coeff       = 0.00012207f,   // 来自 BL0942 datasheet
    .i_rms_coeff       = 0.00006104f,
    .watt_coeff        = 0.00000745f,
};
metering_service_t *ms = metering_service_create(&cfg);
metering_service_start(ms);  // 开始订阅 BL0942 事件并输出 1 Hz 快照

/* safety_guard（后续批次）订阅 */
esp_event_handler_register(METERING_EVENT_BASE, METERING_EVENT_SNAPSHOT,
                           safety_guard_on_snapshot, NULL);

/* 主动查询最新快照 */
metering_snapshot_t snap;
metering_service_get_latest(ms, &snap);
printf("Power: %.2f W, Delta: %.3f mWh\n", snap.power, snap.energy_delta);

/* 云端成功上报后确认电能增量 */
metering_service_confirm_energy_delta(ms, &snap);

/* 云端上报失败时释放 pending 增量，不推进确认基线 */
metering_service_discard_energy_delta(ms, &snap);
```

### 7.5 线程模型

```
bl0942 sample task
    │  BL0942_EVENT_MEASUREMENT（esp_event 广播）
    ▼
metering_service（在 esp_event handler 上下文）
    │  换算 raw → 工程量
    │  CF 计数 → 区间电能增量 mWh + 确认令牌
    │  发布 METERING_EVENT_SNAPSHOT
    ▼
safety_guard / thingsboard_client / display_service
```

### 7.6 关键设计决策

- `start()` 注册 `BL0942_EVENT_MEASUREMENT` 事件处理器，在 handler 中换算并输出单次 1 Hz 快照
- BL0942 采样周期由组合根配置为 1000ms，Metering Service 不再做窗口聚合或均值计算
- `energy_delta` 由内部 `metering_energy_delta_state_t` 根据 CF 计数计算，单位为 mWh
- 云端成功发布快照后调用 `metering_service_confirm_energy_delta()` 确认令牌；未确认时新的电能快照不会覆盖 pending 结果
- 云端发布失败后调用 `metering_service_discard_energy_delta()` 释放 pending 增量；Metering Service 不推进确认基线，下一次快照会包含从最后确认 CF 基线开始的遗漏电能
- `reset_energy()` 清除电能增量基线和 pending 状态，用于重新建立确认起点，但不会复用已分配的确认令牌
- bl0942 故障时发布 `valid=false` 的快照，不停止服务
- 换算系数默认值基于 BL0942 数据手册的参考电路参数，可由调用方覆盖
- `get_latest()` 返回最近一次快照的拷贝，线程安全

---

## 8. Wi-Fi Link（Wi-Fi 链路子类）

Wi-Fi Link 是网络抽象层的 `network_link_t` 具体子类，实现 Wi-Fi STA + MQTT 的完整链路。内部直接使用 `esp_netif` + `esp_wifi` + `esp_mqtt`，通过 `network_link_ops_t` 虚函数表接入 `network_manager` 的多态体系。

### 8.1 类总览

| 类 | 可见性 | 被谁使用 | OOP 角色 | 说明 |
|----|--------|---------|---------|------|
| `wifi_link_config_t` | 用户 API | app_controller | 配置结构体 | Wi-Fi + MQTT 连接参数 |
| `wifi_link_t` | 内部 | wifi_link 自身 | 子类结构体 | 继承 `network_link_t`，内含 Wi-Fi/MQTT 句柄 |

`network_link_ops_t` 和 `network_link_t` 已在 Batch 3 第 6 节定义。

### 8.2 `wifi_link_config_t` — 初始化配置

**所属层**：网络抽象层
**可见性**：用户 API — app_controller 在创建时传入
**OOP 角色**：配置结构体

```c
typedef struct {
    const char *ssid;
    const char *password;
    const char *mqtt_broker_host;
    uint16_t mqtt_broker_port;
    const char *mqtt_client_id;
    const char *mqtt_username;
    const char *mqtt_password;
    uint16_t mqtt_keepalive_s;
    bool mqtt_clean_session;
    bool mqtt_use_tls;
    int max_subscriptions;
    int max_topic_len;
} wifi_link_config_t;
```

### 8.3 `wifi_link_t` — 子类内部结构

**所属层**：网络抽象层
**可见性**：内部 — 定义在 `wifi_link.c`，不暴露给调用方
**OOP 角色**：具体子类 — `network_link_t` 必须在第一个字段

```c
typedef struct {
    char topic[WIFI_LINK_MAX_TOPIC_LEN];
    network_mqtt_qos_t qos;
    bool in_use;
} wifi_link_sub_entry_t;

struct wifi_link {
    network_link_t base;                       // 必须是第一个字段
    wifi_link_config_t config;
    // Wi-Fi
    esp_netif_t *netif;
    bool wifi_connected;
    // MQTT
    esp_mqtt_client_handle_t mqtt_client;
    bool mqtt_connected;
    // 订阅表
    wifi_link_sub_entry_t *sub_table;
    int sub_table_size;
    // 线程安全
    SemaphoreHandle_t mutex;
    // 下行回调
    network_rx_cb_t rx_cb;
    void *rx_ctx;
    // 状态
    bool started;
    bool destroying;
};
```

### 8.4 工厂函数和 ops 表

**工厂函数**（`wifi_link.h`）：

```c
network_link_t *wifi_link_create(const wifi_link_config_t *config);
```

**ops 实现映射**（在 `wifi_link.c` 中为 `static const network_link_ops_t`）：

| ops 方法 | 实现函数 | 要点 |
|----------|----------|------|
| `destroy` | `wifi_link_destroy_impl` | 停止链路 + 释放 netif / mqtt_client / sub_table / 自身 |
| `start` | `wifi_link_start_impl` | 初始化 TCP/IP + netif → 注册 Wi-Fi 事件 → 启动 STA |
| `stop` | `wifi_link_stop_impl` | 断开 MQTT → 断开 Wi-Fi → 保留资源可重 start |
| `get_status` | `wifi_link_get_status_impl` | 返回 `{WIFI, state, link_up, mqtt_ready}` |
| `publish` | `wifi_link_publish_impl` | `esp_mqtt_client_enqueue()` |
| `subscribe` | `wifi_link_subscribe_impl` | 写入订阅表 + `esp_mqtt_client_subscribe()` |
| `unsubscribe` | `wifi_link_unsubscribe_impl` | 移除订阅表 + `esp_mqtt_client_unsubscribe()` |
| `register_rx_cb` | `wifi_link_register_rx_cb_impl` | 保存回调 + 上下文，同一时刻只保留一个 |

### 8.5 状态机和链路推进

```text
IDLE → STARTING → CONNECTING → MQTT_CONNECTING → READY
                       │                │
  ┌──── ERROR ←────────┘                │
  │      │                              │
  └──────┘ (重连 → CONNECTING)           │
                                        │
READY → DEGRADED  (MQTT 断开但 Wi-Fi 仍连接)
DEGRADED → READY  (MQTT 重连成功)
```

Wi-Fi STA 连接成功后自动推进到 MQTT 建链。MQTT 建链成功后遍历 `sub_table` 重放所有有效订阅。

### 8.6 MQTT 下行消息流

```
esp_mqtt MQTT_EVENT_DATA
    │
    ▼
wifi_link mqtt event handler（在 esp_mqtt 内部任务上下文）
    │  构造 network_rx_data_t
    │  调用 rx_cb(rx_data, rx_ctx)（持有 mutex 读取）
    ▼
network_manager（或直接 thingsboard_client）
```

### 8.7 调用方使用模式

```c
/* app_controller 装配 */
wifi_link_config_t wifi_cfg = {
    .ssid                = "MyWiFi",
    .password            = "password",
    .mqtt_broker_host    = "mqtt.thingsboard.cloud",
    .mqtt_broker_port    = 1883,
    .mqtt_client_id      = "smart_socket_001",
    .mqtt_keepalive_s    = 60,
    .mqtt_clean_session  = true,
    .mqtt_use_tls        = false,
    .max_subscriptions   = 8,
    .max_topic_len       = 128,
};
network_link_t *wifi = wifi_link_create(&wifi_cfg);

/* network_manager 通过包装函数操作 */
network_link_start(wifi);
network_link_publish(wifi, &req);
network_link_subscribe(wifi, "v1/devices/me/rpc/request/+", NETWORK_MQTT_QOS0);
```

### 8.8 关键设计决策

- `wifi_link_create()` 返回 `network_link_t *`，调用方不感知 `wifi_link_t` 内部结构
- `wifi_link_config_t` 内嵌 MQTT 配置字段（`mqtt_broker_host` 等）而非引用 `network_mqtt_config_t`，因为 wifi_link 是具体实现不需要基类抽象
- Wi-Fi 事件处理在 ESP-IDF event loop 上下文中执行，保持短小非阻塞；状态变更后立即尝试推进 MQTT 状态
- `mutex` 保护 `wifi_connected`、`mqtt_connected`、`sub_table`、`rx_cb`，保证多线程安全
- 订阅表固定容量，满时 `subscribe` 返回 `ESP_ERR_NO_MEM`
- MQTT 断开 → DEGRADED；MQTT 重连成功 → READY 并重放订阅表。Wi-Fi 断开 → ERROR → 自动重连
- `destroy` 不暴露为独立 API — 通过 `network_link_destroy(base)` 统一释放，`network_manager` 不关心子类类型

---

## 模块依赖关系（Batch 1–4）

```text
board_pinmap      (无依赖)
network_types.h   (无依赖，纯头文件)
relay             (依赖: driver/gpio.h, FreeRTOS, esp_event)
button            (依赖: espressif/button, driver/gpio.h)
bl0942            (依赖: driver/uart.h, driver/gpio.h, FreeRTOS, esp_event)
network_link      (依赖: network_types.h, esp_err.h)
metering_service  (依赖: bl0942 事件类型, esp_event, FreeRTOS)
wifi_link         (依赖: network_link, network_types.h, esp_wifi, esp_netif, esp_mqtt, FreeRTOS)
```

`metering_service` 通过 esp_event 消费 `BL0942_EVENT_MEASUREMENT`，不直接依赖 `bl0942_t` 句柄。`wifi_link` 实现 `network_link_ops_t`，通过 `network_link_create()` 工厂返回 `network_link_t *` 注入 `network_manager`。

---

## 9. Network Manager（双模网络管理器）

Network Manager 是网络抽象层的管理模块，持有主/备两条 `network_link_t *` 链路，负责链路选择、故障切换、回切和订阅意图管理。对上层提供统一的网络入口。

### 9.1 类总览

| 类 | 可见性 | 被谁使用 | OOP 角色 | 说明 |
|----|--------|---------|---------|------|
| `network_manager_t` | 用户 API (opaque) | app_controller + thingsboard_client | 句柄 | struct 定义在 .c |
| `network_manager_config_t` | 用户 API | app_controller | 配置结构体 | 主/备链路指针 + 切换策略参数 |
| `network_manager_status_t` | 用户 API | app_controller + thingsboard_client | 值对象 | 聚合状态快照 |

### 9.2 `network_manager_config_t` — 初始化配置

**所属层**：网络抽象层
**可见性**：用户 API — app_controller 创建时传入
**OOP 角色**：配置结构体

```c
typedef struct {
    network_link_t *primary;              // 主链路（Wi-Fi）
    network_link_t *backup;               // 备链路（LTE，可为 NULL）
    network_link_type_t preferred_primary; // 首选主链路类型
    uint32_t failover_recheck_ms;         // 备链路状态重检周期
    uint32_t failback_delay_ms;           // 主链路恢复后回切延迟
    int max_subscriptions;                // 订阅意图表容量
} network_manager_config_t;
```

### 9.3 `network_manager_status_t` — 聚合状态

**所属层**：网络抽象层
**可见性**：用户 API
**OOP 角色**：值对象

```c
typedef struct {
    network_link_type_t active_link;         // 当前活动链路类型
    bool ready;                              // 当前是否有可用 MQTT 通道
    network_link_status_t primary_status;    // 主链路状态
    network_link_status_t backup_status;     // 备链路状态
} network_manager_status_t;
```

### 9.4 `network_manager_t` — 句柄

**所属层**：网络抽象层
**可见性**：用户 API (opaque) — thingsboard_client 通过此句柄 publish/subscribe
**OOP 角色**：句柄

**公开方法**（`network_manager.h`）：

```c
network_manager_t *network_manager_create(const network_manager_config_t *config);
esp_err_t network_manager_destroy(network_manager_t *me);

esp_err_t network_manager_start(network_manager_t *me);
esp_err_t network_manager_stop(network_manager_t *me);

esp_err_t network_manager_get_status(network_manager_t *me,
                                      network_manager_status_t *out);
esp_err_t network_manager_is_ready(network_manager_t *me, bool *out);

esp_err_t network_manager_publish(network_manager_t *me,
                                   const network_publish_request_t *req);
esp_err_t network_manager_subscribe(network_manager_t *me,
                                     const char *topic, network_mqtt_qos_t qos);
esp_err_t network_manager_unsubscribe(network_manager_t *me, const char *topic);
esp_err_t network_manager_register_rx_cb(network_manager_t *me,
                                          network_rx_cb_t cb, void *ctx);
```

**内部结构**（定义在 `network_manager.c`）：

```c
typedef struct {
    char topic[NETWORK_MANAGER_MAX_TOPIC_LEN];
    network_mqtt_qos_t qos;
    bool in_use;
} network_manager_sub_entry_t;

struct network_manager {
    network_link_t *primary;
    network_link_t *backup;
    network_link_t *active;
    network_link_type_t preferred_primary;
    uint32_t failover_recheck_ms;
    uint32_t failback_delay_ms;
    // 订阅意图表
    network_manager_sub_entry_t *sub_table;
    int sub_table_size;
    // 线程安全
    SemaphoreHandle_t mutex;
    // 下行回调
    network_rx_cb_t rx_cb;
    void *rx_ctx;
    // 内部状态
    uint64_t failback_since_us;
    bool started;
    bool destroying;
};
```

### 9.5 链路切换逻辑

```
活动链路状态 → ERROR
    │
    ├─ backup 存在且状态为 READY 或 DEGRADED?
    │   ├─ 是 → 切换 active = backup
    │   │       → 遍历 sub_table，在新 active 上重放全部订阅
    │   │       → 通知 rx_cb（可选：链路切换事件）
    │   └─ 否 → 保持，周期性检查 backup 状态（failover_recheck_ms）
    │
    │
活动链路 = backup 且 primary 恢复 READY
    │  primary 持续 READY 超过 failback_delay_ms?
    │
    └─ 是 → 切回 active = primary → 重放订阅表
```

### 9.6 关键设计决策

- 持有 `primary` 和 `backup` 两个 `network_link_t *`，通过包装函数操作，不感知子类类型
- `start()` 启动首选主链路，`active` 指向它；若 primary 无法启动则尝试 backup
- 订阅意图表：`subscribe`/`unsubscribe` 写入表 → 立即委托给当前 `active` 链路；活动链路切换后遍历表在新链路上全部重放
- `rx_cb` 统一接收当前活动链路的下行消息，透传给 `thingsboard_client`
- 回切延迟防止 Wi-Fi 抖动导致频繁来回切换
- `backup` 可以为 NULL（单链路模式），此时不启用故障切换

---

## 10. Safety Guard（本地安全规则）

Safety Guard 是业务服务层的纯规则安全模块。订阅 `METERING_EVENT_SNAPSHOT`，做可解释的过流/过功率判定，输出安全快照。不包含 AI 模型或负载分类。

### 10.1 类总览

| 类 | 可见性 | 被谁使用 | OOP 角色 | 说明 |
|----|--------|---------|---------|------|
| `safety_guard_t` | 用户 API (opaque) | app_controller | 句柄 | struct 定义在 .c |
| `safety_guard_config_t` | 用户 API | app_controller | 配置结构体 | 过流/过功率阈值 + 持续判定参数 |
| `safety_guard_level_t` | 用户 API | app_controller + lvgl_dashboard | 枚举 | NORMAL / WARNING / DANGER |
| `safety_guard_event_t` | 用户 API | app_controller | 枚举 | NONE / OVERCURRENT / OVERPOWER |
| `safety_guard_action_t` | 用户 API | app_controller | 枚举 | NONE / RELAY_OFF |
| `safety_guard_snapshot_t` | 用户 API | app_controller + lvgl_dashboard + thingsboard_client | 值对象 | 规则判定结果 |

### 10.2 `safety_guard_config_t` — 初始化配置

**所属层**：业务服务层
**可见性**：用户 API
**OOP 角色**：配置结构体

```c
typedef struct {
    float overcurrent_threshold_a;      // 过流阈值 (A)
    float overpower_threshold_w;        // 过功率阈值 (W)
    int persistence_samples;            // 持续超限次数才触发动作（默认 3）
} safety_guard_config_t;
```

### 10.3 `safety_guard_snapshot_t` — 安全快照

**所属层**：业务服务层
**可见性**：用户 API — 通过 esp_event 广播
**OOP 角色**：值对象

```c
typedef enum {
    SAFETY_GUARD_LEVEL_NORMAL = 0,
    SAFETY_GUARD_LEVEL_WARNING,
    SAFETY_GUARD_LEVEL_DANGER,
} safety_guard_level_t;

typedef enum {
    SAFETY_GUARD_EVENT_NONE = 0,
    SAFETY_GUARD_EVENT_OVERCURRENT,
    SAFETY_GUARD_EVENT_OVERPOWER,
} safety_guard_event_t;

typedef enum {
    SAFETY_GUARD_ACTION_NONE = 0,
    SAFETY_GUARD_ACTION_RELAY_OFF,
} safety_guard_action_t;

typedef struct {
    safety_guard_level_t level;
    safety_guard_event_t event;
    safety_guard_action_t suggested_action;
    uint64_t timestamp_us;
    bool valid;
} safety_guard_snapshot_t;
```

### 10.4 `safety_guard_t` — 句柄

**所属层**：业务服务层
**可见性**：用户 API (opaque)
**OOP 角色**：句柄

**公开方法**（`safety_guard.h`）：

```c
safety_guard_t *safety_guard_create(const safety_guard_config_t *config);
esp_err_t safety_guard_destroy(safety_guard_t *me);

esp_err_t safety_guard_start(safety_guard_t *me);
esp_err_t safety_guard_stop(safety_guard_t *me);

esp_err_t safety_guard_get_latest(safety_guard_t *me,
                                   safety_guard_snapshot_t *out);
esp_err_t safety_guard_set_thresholds(safety_guard_t *me,
                                       float overcurrent_a, float overpower_w);
```

**事件声明**：

```c
ESP_EVENT_DECLARE_BASE(SAFETY_GUARD_EVENT_BASE);

typedef enum {
    SAFETY_GUARD_EVENT_SNAPSHOT = 0,
} safety_guard_event_id_t;
```

**内部结构**（定义在 `safety_guard.c`）：

```c
struct safety_guard {
    safety_guard_config_t config;
    SemaphoreHandle_t mutex;
    safety_guard_snapshot_t latest;
    bool has_latest;
    int overcurrent_persistence;
    int overpower_persistence;
    bool initialized;
};
```

### 10.5 判定逻辑

```
收到 METERING_EVENT_SNAPSHOT（每 1s）
    │
    ├─ current > overcurrent_threshold_a? → overcurrent_persistence++
    │    └─ count >= persistence_samples? → DANGER + OVERCURRENT + RELAY_OFF
    │    └─ 否则 → WARNING + OVERCURRENT + NONE
    │
    ├─ power > overpower_threshold_w? → overpower_persistence++
    │    └─ count >= persistence_samples? → DANGER + OVERPOWER + RELAY_OFF
    │    └─ 否则 → WARNING + OVERPOWER + NONE
    │
    └─ 正常 → 清零计数 → NORMAL + NONE + NONE
```

### 10.6 关键设计决策

- 订阅 `METERING_EVENT_SNAPSHOT`，在每个 1s 快照上做规则判定
- `persistence_samples` 防止瞬态尖峰误触发（默认 3，即持续 3s 才执行断开动作）
- `suggested_action=RELAY_OFF` 时不直接操作 relay — 由 `app_controller` 协调执行
- `set_thresholds()` 支持运行时更新（场景：ThingsBoard RPC 下发新阈值）
- 阈值使用 SI 单位（A、W），与 `metering_snapshot_t` 保持一致

---

## 11. TFT Panel（LCD 硬件面板驱动）

TFT Panel 是驱动适配层的 LCD 硬件封装模块，负责 SPI 初始化、像素位图传输和背光控制。对上层只暴露 `draw_bitmap` 和 `set_backlight`，隐藏 SPI 和面板型号细节。

### 11.1 类总览

| 类 | 可见性 | 被谁使用 | OOP 角色 | 说明 |
|----|--------|---------|---------|------|
| `tft_panel_t` | 用户 API (opaque) | app_controller + lvgl_dashboard | 句柄 | struct 定义在 .c |
| `tft_panel_config_t` | 用户 API | app_controller | 配置结构体 | SPI 引脚 + 面板尺寸 + 背光 GPIO |

### 11.2 `tft_panel_config_t` — 初始化配置

**所属层**：驱动适配层
**可见性**：用户 API — app_controller 从 board_pinmap 读取后填入
**OOP 角色**：配置结构体

```c
typedef struct {
    gpio_num_t sclk_gpio;
    gpio_num_t mosi_gpio;
    gpio_num_t dc_gpio;
    gpio_num_t cs_gpio;
    gpio_num_t rst_gpio;
    gpio_num_t bl_gpio;
    int panel_width;
    int panel_height;
} tft_panel_config_t;
```

### 11.3 `tft_panel_t` — 句柄

**所属层**：驱动适配层
**可见性**：用户 API (opaque)
**OOP 角色**：句柄

**公开方法**（`tft_panel.h`）：

```c
typedef void (*tft_panel_flush_done_cb_t)(void *user_ctx);

tft_panel_t *tft_panel_create(const tft_panel_config_t *config);
esp_err_t tft_panel_destroy(tft_panel_t *me);

esp_err_t tft_panel_register_flush_done_cb(tft_panel_t *me,
                                           tft_panel_flush_done_cb_t cb,
                                           void *user_ctx);
esp_err_t tft_panel_draw_bitmap(tft_panel_t *me, int x1, int y1, int x2, int y2,
                                 const void *color_data);
esp_err_t tft_panel_set_backlight(tft_panel_t *me, bool enabled);
int tft_panel_get_width(const tft_panel_t *me);
int tft_panel_get_height(const tft_panel_t *me);
```

**内部结构**（定义在 `tft_panel.c`）：

```c
struct tft_panel {
    tft_panel_config_t config;
    spi_device_handle_t spi;
    tft_panel_flush_done_cb_t flush_done_cb;
    void *flush_done_ctx;
    bool backlight_on;
    bool initialized;
};
```

### 11.4 关键设计决策

- 封装 SPI host 初始化和 LCD 初始化序列（ST7789 等），对上层只暴露 `draw_bitmap` 和背光控制
- LVGL 的 `flush_cb` 由 `lvgl_dashboard` 注册，内部调用 `tft_panel_draw_bitmap`
- `tft_panel_config_t` 从 `board_pinmap` 读取 SPI 引脚后注入，不内部依赖 pinmap
- `panel_width`/`panel_height` 由调用方配置（ESP32-S3-LCD-1.47B 为 172×320）

---

## 12. LVGL Dashboard（LVGL 本地看板）

LVGL Dashboard 是驱动适配层的 UI 模块，管理 LVGL 控件树、业务状态缓存和屏幕开关。直接订阅业务 esp_event（`METERING_EVENT_SNAPSHOT`、`RELAY_EVENT_STATE_CHANGED`、`SAFETY_GUARD_EVENT_SNAPSHOT`），在 LVGL task 内更新控件。

### 12.1 类总览

| 类 | 可见性 | 被谁使用 | OOP 角色 | 说明 |
|----|--------|---------|---------|------|
| `lvgl_dashboard_t` | 用户 API (opaque) | app_controller | 句柄 | struct 定义在 .c |
| `lvgl_dashboard_config_t` | 用户 API | app_controller | 配置结构体 | tft_panel 句柄 + LVGL 任务参数 |
| `dashboard_state_t` | 用户 API | lvgl_dashboard 内部缓存 | 值对象 | 聚合的看板业务状态 |
| `dashboard_network_t` | 用户 API | lvgl_dashboard | 枚举 | OFFLINE / CONNECTING / WIFI / LTE |

### 12.2 `dashboard_state_t` — 看板状态

**所属层**：驱动适配层
**可见性**：用户 API — lvgl_dashboard 内部缓存，LVGL task 读取
**OOP 角色**：值对象

```c
typedef enum {
    DASHBOARD_NET_OFFLINE = 0,
    DASHBOARD_NET_CONNECTING,
    DASHBOARD_NET_WIFI,
    DASHBOARD_NET_LTE,
} dashboard_network_t;

typedef struct {
    // 电参量
    float voltage;
    float current;
    float power;
    float energy_delta;                 // 上报区间电能增量 (mWh)
    bool metering_valid;
    // 继电器
    bool relay_on;
    bool relay_known;
    // 网络
    dashboard_network_t network;
    bool network_ready;
    // 安全
    safety_guard_level_t safety_level;
    bool safety_valid;
    // 屏幕
    bool screen_enabled;
    uint64_t last_update_us;
} dashboard_state_t;
```

### 12.3 `lvgl_dashboard_config_t` — 初始化配置

**所属层**：驱动适配层
**可见性**：用户 API
**OOP 角色**：配置结构体

```c
typedef struct {
    tft_panel_t *panel;
    network_manager_t *network_manager;
    int lvgl_task_stack;
    int lvgl_task_priority;
    uint32_t lvgl_tick_period_ms;
    uint32_t update_period_ms;
} lvgl_dashboard_config_t;
```

### 12.4 `lvgl_dashboard_t` — 句柄

**所属层**：驱动适配层
**可见性**：用户 API (opaque)
**OOP 角色**：句柄

**公开方法**（`lvgl_dashboard.h`）：

```c
lvgl_dashboard_t *lvgl_dashboard_create(const lvgl_dashboard_config_t *config);
esp_err_t lvgl_dashboard_destroy(lvgl_dashboard_t *me);

esp_err_t lvgl_dashboard_start(lvgl_dashboard_t *me);
esp_err_t lvgl_dashboard_stop(lvgl_dashboard_t *me);

esp_err_t lvgl_dashboard_set_screen_enabled(lvgl_dashboard_t *me, bool enabled);
```

**内部结构**（定义在 `lvgl_dashboard.c`）：

```c
struct lvgl_dashboard {
    lvgl_dashboard_config_t config;
    SemaphoreHandle_t mutex;
    dashboard_state_t state_cache;
    // LVGL
    lv_disp_t *display;
    TaskHandle_t lvgl_task;
    SemaphoreHandle_t lvgl_task_done_sema;
    // 控件引用
    lv_obj_t *label_voltage;
    lv_obj_t *label_current;
    lv_obj_t *label_power;
    lv_obj_t *label_energy;
    lv_obj_t *label_relay;
    lv_obj_t *label_network;
    lv_obj_t *label_safety;
    // 状态
    bool screen_enabled;
    bool started;
    bool initialized;
};
```

### 12.5 数据流

```
METERING_EVENT_SNAPSHOT ──→ event_handler ──→ 更新 state_cache (mutex)
RELAY_EVENT_STATE_CHANGED ─→ event_handler ──→ 更新 state_cache (mutex)
SAFETY_GUARD_EVENT_SNAPSHOT → event_handler ──→ 更新 state_cache (mutex)
                                                     │
                              LVGL task loop ────────┘
                              │ 每 update_period_ms:
                              │   mutex 读 state_cache
                              │   更新 label_voltage/current/power/...
                              │   lv_task_handler()
```

事件回调中只做 `mutex lock → 更新 state_cache → mutex unlock`，不操作 widget。widget 更新只在 LVGL task 内完成。

### 12.6 关键设计决策

- 直接订阅业务事件的 esp_event，不需要 `display_service` 中间层
- 网络状态通过 `network_manager_get_status()` 在 LVGL task loop 中轮询
- `set_screen_enabled(false)` → 背光关闭。`set_screen_enabled(true)` → 背光打开
- `lvgl_dashboard_create()` 内部初始化 LVGL 库、创建显示对象、构建控件树
- `lvgl_dashboard_destroy()` 清理控件树、释放 LVGL 资源、停止 LVGL task

---

## 模块依赖关系（Batch 1–5）

```text
board_pinmap       (无依赖)
network_types.h    (无依赖，纯头文件)
relay              (依赖: driver/gpio.h, FreeRTOS, esp_event)
button             (依赖: espressif/button, driver/gpio.h)
bl0942             (依赖: driver/uart.h, driver/gpio.h, FreeRTOS, esp_event)
network_link       (依赖: network_types.h, esp_err.h)
metering_service   (依赖: bl0942 事件类型, esp_event, FreeRTOS)
wifi_link          (依赖: network_link, network_types.h, esp_wifi, esp_netif, esp_mqtt, FreeRTOS)
network_manager    (依赖: network_link, network_types.h, FreeRTOS)
safety_guard       (依赖: metering_service 事件类型, esp_event, FreeRTOS)
tft_panel          (依赖: driver/spi_master.h, driver/gpio.h)
lvgl_dashboard     (依赖: tft_panel, lvgl, esp_event, FreeRTOS)
```

各模块通过 esp_event 松耦合：`bl0942 → metering_service → safety_guard` / `lvgl_dashboard` 均通过事件串联，不直接持有对方句柄。

---

## 13. ThingsBoard Client（云平台语义模块）

ThingsBoard Client 是业务服务层的云平台语义模块，负责构建 telemetry JSON、发布属性、解析 RPC 命令。通过 `network_manager` 收发 MQTT，不直接依赖 `wifi_link` 或 `lte_link`。

### 13.1 类总览

| 类 | 可见性 | 被谁使用 | OOP 角色 | 说明 |
|----|--------|---------|---------|------|
| `thingsboard_client_t` | 用户 API (opaque) | app_controller | 句柄 | struct 定义在 .c |
| `tb_client_config_t` | 用户 API | app_controller | 配置结构体 | TB 平台参数 + network_manager 句柄 |
| `tb_telemetry_input_t` | 用户 API | app_controller | 值对象 | 构建 telemetry JSON 的输入聚合 |
| `tb_command_type_t` | 用户 API | app_controller | 枚举 | SET_RELAY / GET_POWER_LIMIT / SET_POWER_LIMIT |
| `tb_command_t` | 用户 API | app_controller | 值对象 | 解析后的下行语义命令 |
| `tb_command_cb_t` | 用户 API | app_controller | 回调类型 | 语义命令回调 |

### 13.2 `tb_client_config_t` — 初始化配置

**所属层**：业务服务层
**可见性**：用户 API
**OOP 角色**：配置结构体

```c
typedef struct {
    network_manager_t *net_mgr;          // 网络管理器句柄
    const char *device_token;            // TB 设备访问令牌
    bool enable_rpc;                     // 是否启用 RPC 订阅
    bool enable_attributes;              // 是否启用属性订阅
    int json_buf_size;                   // JSON 构建缓冲区大小（默认 512）
} tb_client_config_t;
```

### 13.3 `tb_telemetry_input_t` — Telemetry 输入

**所属层**：业务服务层
**可见性**：用户 API
**OOP 角色**：值对象

```c
typedef struct {
    float voltage;                       // 电压 (V)
    float current;                       // 电流 (A)
    float power;                         // 有功功率 (W)
    float energy_delta;                  // 上报区间电能增量 (mWh)
    float frequency;                     // 电网频率 (Hz)
    bool relay_on;                       // 继电器状态
    const char *active_link;             // "wifi" / "lte"
    safety_guard_level_t safety_level;   // 安全风险等级
    bool valid;                          // 数据是否有效
} tb_telemetry_input_t;
```

### 13.4 `tb_command_t` — 下行命令

**所属层**：业务服务层
**可见性**：用户 API
**OOP 角色**：值对象

```c
typedef enum {
    TB_COMMAND_SET_RELAY = 0,
    TB_COMMAND_GET_POWER_LIMIT,
    TB_COMMAND_SET_POWER_LIMIT,
} tb_command_type_t;

typedef struct {
    tb_command_type_t type;
    int32_t request_id;                  // RPC request id
    bool relay_on;                       // SET_RELAY 目标状态
    float power_limit_w;                 // SET_POWER_LIMIT 功率阈值 (W)
} tb_command_t;

typedef void (*tb_command_cb_t)(const tb_command_t *cmd, void *user_ctx);
```

### 13.5 `thingsboard_client_t` — 句柄

**所属层**：业务服务层
**可见性**：用户 API (opaque)
**OOP 角色**：句柄

**公开方法**（`thingsboard_client.h`）：

```c
thingsboard_client_t *thingsboard_client_create(const tb_client_config_t *config);
esp_err_t thingsboard_client_destroy(thingsboard_client_t *me);

esp_err_t thingsboard_client_start(thingsboard_client_t *me);
esp_err_t thingsboard_client_stop(thingsboard_client_t *me);

// 上行
esp_err_t thingsboard_client_publish_telemetry(thingsboard_client_t *me,
                                                const tb_telemetry_input_t *input);
esp_err_t thingsboard_client_report_relay_state(thingsboard_client_t *me, bool on);
esp_err_t thingsboard_client_report_power_limit(thingsboard_client_t *me,
                                                 float power_limit_w);
esp_err_t thingsboard_client_send_rpc_response(thingsboard_client_t *me,
                                                int32_t request_id,
                                                const char *json, size_t json_len);

// 下行
esp_err_t thingsboard_client_register_command_cb(thingsboard_client_t *me,
                                                  tb_command_cb_t cb, void *ctx);
```

**内部结构**（定义在 `thingsboard_client.c`）：

```c
struct thingsboard_client {
    tb_client_config_t config;
    SemaphoreHandle_t mutex;
    char *json_buf;
    int json_buf_size;
    tb_command_cb_t cmd_cb;
    void *cmd_ctx;
    bool started;
    bool destroying;
};
```

### 13.6 TB Topic 约定

```
telemetry:    v1/devices/me/telemetry
attributes:   v1/devices/me/attributes
rpc_request:  v1/devices/me/rpc/request/+
rpc_response: v1/devices/me/rpc/response/{request_id}
```

### 13.7 数据流

```
上行:
app_controller
    │  tb_telemetry_input_t
    ▼
thingsboard_client_publish_telemetry()
    │  构建 JSON: {"voltage":220.1,"current":1.5,"power":330,...}
    ▼
network_manager_publish(net_mgr, &req)  →  WiFi 或 LTE  →  ThingsBoard

下行:
ThingsBoard  →  WiFi/LTE  →  network_manager rx_cb
    │  network_rx_data_t {topic, data}
    ▼
thingsboard_client on_mqtt_data()
    │  匹配 RPC topic pattern
    │  解析 JSON → tb_command_t
    ▼
cmd_cb(cmd, ctx)  →  app_controller  →  relay_set() / safety_guard_set_thresholds()
```

### 13.8 关键设计决策

- `start()` 通过 `network_manager_subscribe()` 注册 RPC 和属性 topic；通过 `network_manager_register_rx_cb()` 注册下行回调
- `publish_telemetry()` 内部构建 JSON 后调用 `network_manager_publish()`，不直接依赖 `wifi_link` 或 `lte_link`
- `report_relay_state()` 和 `report_power_limit()` 是快捷方法，内部构建 attribute JSON 并 publish
- RPC 响应 topic 格式为 `v1/devices/me/rpc/response/{request_id}`
- 不依赖旧项目的 `feature_engineering_t`、`load_class_t`、`risk_score`、`risk_model`——纯业务语义
- `json_buf` 是预分配缓冲区，telemetry JSON 在栈上构建后通过 `network_manager_publish` 发出

---

## 14. LTE Link（LTE 链路子类）

LTE Link 是网络抽象层的 `network_link_t` 具体子类，封装自研 esp-lwlte 组件，实现 LTE + MQTT 的完整链路。通过 `lwlte_t` 门面 API 操作 Air780EP 模块，通过 `network_link_ops_t` 虚函数表接入 `network_manager` 的多态体系。

### 14.1 类总览

| 类 | 可见性 | 被谁使用 | OOP 角色 | 说明 |
|----|--------|---------|---------|------|
| `lte_link_config_t` | 用户 API | app_controller | 配置结构体 | UART/GPIO + APN + MQTT 参数 |
| `lte_link_t` | 内部 | lte_link 自身 | 子类结构体 | 继承 `network_link_t`，内含 `lwlte_t *` |

`network_link_ops_t` 和 `network_link_t` 已在 Batch 3 第 6 节定义。

### 14.2 `lte_link_config_t` — 初始化配置

**所属层**：网络抽象层
**可见性**：用户 API — app_controller 从 board_pinmap 读取后填入
**OOP 角色**：配置结构体

```c
typedef struct {
    // UART
    uart_port_t uart_num;
    gpio_num_t tx_gpio;
    gpio_num_t rx_gpio;
    int baud_rate;
    // LTE Module GPIO
    gpio_num_t en_gpio;
    // Network
    const char *apn;
    bool auto_connect;
    // MQTT
    bool mqtt_enabled;
    const char *mqtt_broker_host;
    uint16_t mqtt_broker_port;
    const char *mqtt_client_id;
    const char *mqtt_username;
    const char *mqtt_password;
    uint16_t mqtt_keepalive_s;
    bool mqtt_clean_session;
    // Tuning (0 = use esp-lwlte defaults)
    uint32_t init_ready_timeout_ms;
    uint32_t net_activate_timeout_ms;
    int max_subscriptions;
    int max_topic_len;
} lte_link_config_t;
```

### 14.3 `lte_link_t` — 子类内部结构

**所属层**：网络抽象层
**可见性**：内部 — 定义在 `lte_link.c`
**OOP 角色**：具体子类 — `network_link_t` 必须是第一个字段

```c
typedef struct {
    char topic[LTE_LINK_MAX_TOPIC_LEN];
    network_mqtt_qos_t qos;
    bool in_use;
} lte_link_sub_entry_t;

struct lte_link {
    network_link_t base;
    lte_link_config_t config;
    lwlte_t *lwlte;
    // 订阅表
    lte_link_sub_entry_t *sub_table;
    int sub_table_size;
    // 线程安全
    SemaphoreHandle_t mutex;
    // 下行回调
    network_rx_cb_t rx_cb;
    void *rx_ctx;
    // 状态缓存
    network_link_status_t cached_status;
    // 状态
    bool started;
    bool destroying;
};
```

### 14.4 工厂函数和 ops 实现

**工厂函数**（`lte_link.h`）：

```c
network_link_t *lte_link_create(const lte_link_config_t *config);
```

`lte_link_create()` 内部调用 `lwlte_air780ep_init()`，**阻塞**直到 esp-lwlte ready（AT Engine + Modem + Core 初始化完成），然后返回 `network_link_t *`。

**ops 实现映射**：

| ops 方法 | 实现要点 |
|----------|----------|
| `destroy` | 停止链路 → `lwlte_destroy(lwlte)` → 释放 sub_table → 释放自身 |
| `start` | `lwlte_connect(lwlte)`；若 `mqtt_enabled` → `lwlte_mqtt_start(lwlte)` |
| `stop` | 停止 MQTT → 断开网络 → 保留 lwlte 句柄可重 start |
| `get_status` | 从 `lwlte_get_state()` / `lwlte_mqtt_get_state()` 映射到 `network_link_status_t` |
| `publish` | `lwlte_mqtt_publish(lwlte, topic, payload, len, qos, retain)` |
| `subscribe` | 写入自身订阅表 → `lwlte_mqtt_subscribe(lwlte, topic, qos)` |
| `unsubscribe` | 从订阅表移除 → `lwlte_mqtt_unsubscribe(lwlte, topic)` |
| `register_rx_cb` | 保存回调 → lwlte 事件 `LWLTE_EVENT_MQTT_DATA` 时调用 |

### 14.5 状态映射

```
lwlte_state_t              → network_link_status_t.state
─────────────────────────────────────────────────────────
STOPPED                    → IDLE
STARTING                   → STARTING
READY / NET_ACTIVATING     → CONNECTING
ONLINE + MQTT_STOPPED      → DEGRADED
ONLINE + MQTT_CONNECTED    → READY
ERROR                      → ERROR
```

### 14.6 lwlte 事件处理流

```
lwlte event callback (在 esp-lwlte 内部任务上下文)
    │  LWLTE_EVENT_NET_ONLINE    → 更新状态
    │  LWLTE_EVENT_NET_OFFLINE   → 更新状态 → 尝试重连
    │  LWLTE_EVENT_MQTT_CONNECTED → 遍历 sub_table 重放订阅
    │  LWLTE_EVENT_MQTT_DATA     → 构造 network_rx_data_t → 调用 rx_cb
    │  LWLTE_EVENT_ERROR         → 更新状态为 ERROR
```

### 14.7 关键设计决策

- `lte_link_create()` 阻塞直到 esp-lwlte 初始化完成（AT Engine + Modem + Core ready），失败返回 NULL
- `lte_link` 维护自己的订阅表，因为 esp-lwlte 的 MQTT subscribe 不记住订阅意图，重连后需 lte_link 重放
- 配置中大量 esp-lwlte 调优参数（`at_rx_buf_size`、`modem_event_queue_size` 等）不暴露，内部填 0 使用 esp-lwlte 默认值
- `auto_connect=true` 时 `create` 即提交联网请求；`false` 时由 `start()` 触发
- esp-lwlte 通过 `EXTRA_COMPONENT_DIRS` 指向 `/Users/jovisdreams/Projects/esp-lwlte`

---

## 15. App Controller（应用编排层）

App Controller 是应用编排层的唯一模块，持有全部底层模块句柄，负责装配依赖、注册回调、协调启动顺序和业务流程。不创建底层对象——所有模块由 `main.c` 创建后注入。

### 15.1 类总览

| 类 | 可见性 | 被谁使用 | OOP 角色 | 说明 |
|----|--------|---------|---------|------|
| `app_controller_t` | 用户 API (opaque) | main.c | 句柄 | struct 定义在 .c |
| `app_controller_config_t` | 用户 API | main.c | 配置结构体 | 所有模块句柄 + 回调 |

### 15.2 `app_controller_config_t` — 装配配置

**所属层**：应用编排层
**可见性**：用户 API — main.c 填入所有已创建的模块句柄
**OOP 角色**：配置结构体

```c
typedef struct {
    // 基础设施
    esp_event_loop_handle_t event_loop;
    const board_pinmap_t *pinmap;
    // 驱动层
    relay_t *relay;
    button_t *button;
    bl0942_t *bl0942;
    tft_panel_t *tft_panel;
    // 服务层
    metering_service_t *metering;
    safety_guard_t *safety;
    thingsboard_client_t *tb;
    // 网络层
    network_manager_t *net_mgr;
    // 显示层
    lvgl_dashboard_t *dashboard;
} app_controller_config_t;
```

### 15.3 `app_controller_t` — 句柄

**所属层**：应用编排层
**可见性**：用户 API (opaque)
**OOP 角色**：句柄

**公开方法**（`app_controller.h`）：

```c
app_controller_t *app_controller_create(const app_controller_config_t *config);
esp_err_t app_controller_destroy(app_controller_t *me);

esp_err_t app_controller_start(app_controller_t *me);
esp_err_t app_controller_stop(app_controller_t *me);
```

**内部结构**（定义在 `app_controller.c`）：

```c
struct app_controller {
    app_controller_config_t cfg;
    bool started;
};
```

### 15.4 `start()` 编排序列

```
1. 注册按键回调
   button_register_cb(btn, SINGLE_CLICK, on_button_single_click, relay);
   button_register_cb(btn, LONG_PRESS_START, on_button_long_press, dashboard);

2. 注册 safety_guard 事件 → 继电器关断
   esp_event_handler_register(SAFETY_GUARD_EVENT_BASE,
       SAFETY_GUARD_EVENT_SNAPSHOT, on_safety_snapshot, relay);

3. 注册 metering 事件 → TB telemetry 上报
   esp_event_handler_register(METERING_EVENT_BASE,
       METERING_EVENT_SNAPSHOT, on_metering_snapshot, tb);

4. 注册 relay 事件 → TB 状态上报
   esp_event_handler_register(RELAY_EVENT_BASE,
       RELAY_EVENT_STATE_CHANGED, on_relay_changed, tb);

5. 注册 TB 命令回调
   thingsboard_client_register_command_cb(tb, on_tb_command, app);

6. 启动各模块（按依赖顺序）:
   bl0942_start()
   metering_service_start()
   safety_guard_start()
   network_manager_start()
   thingsboard_client_start()
   lvgl_dashboard_start()
```

### 15.5 核心回调实现

**本地按键 → 继电器**：
```c
static void on_button_single_click(button_event_t event, void *ctx) {
    relay_t *relay = (relay_t *)ctx;
    relay_toggle(relay, RELAY_SOURCE_LOCAL_BUTTON);
}

static void on_button_long_press(button_event_t event, void *ctx) {
    lvgl_dashboard_t *dash = (lvgl_dashboard_t *)ctx;
    lvgl_dashboard_set_screen_enabled(dash, !currently_enabled);
}
```

**安全规则 → 继电器**：
```c
static void on_safety_snapshot(void *arg, esp_event_base_t base,
                                int32_t id, void *data) {
    safety_guard_snapshot_t *snap = (safety_guard_snapshot_t *)data;
    relay_t *relay = (relay_t *)arg;
    if (snap->suggested_action == SAFETY_GUARD_ACTION_RELAY_OFF) {
        relay_set(relay, RELAY_SOURCE_SAFETY, false);
    }
}
```

**TB 命令 → 继电器/安全阈值**：
```c
static void on_tb_command(const tb_command_t *cmd, void *ctx) {
    app_controller_t *app = (app_controller_t *)ctx;
    switch (cmd->type) {
    case TB_COMMAND_SET_RELAY:
        relay_set(app->cfg.relay, RELAY_SOURCE_CLOUD, cmd->relay_on);
        thingsboard_client_report_relay_state(app->cfg.tb, cmd->relay_on);
        break;
    case TB_COMMAND_GET_POWER_LIMIT: {
        float threshold = /* 从 safety_guard 读取当前阈值 */;
        thingsboard_client_send_rpc_response(app->cfg.tb,
            cmd->request_id, response_json, len);
        break;
    }
    case TB_COMMAND_SET_POWER_LIMIT:
        safety_guard_set_thresholds(app->cfg.safety, 0, cmd->power_limit_w);
        break;
    }
}
```

### 15.6 `main.c` 装配示例

```c
void app_main(void) {
    esp_event_loop_create_default();
    const board_pinmap_t *pm = board_pinmap_get();

    // 驱动层
    relay_t *relay = relay_create(&(relay_config_t){
        .ctrl_gpio = pm->relay_ctrl_gpio,
        .active_level = (relay_active_level_t)pm->relay_active_level,
    });
    button_t *btn = button_create(&(button_config_t){
        .input_gpio = pm->button_gpio,
        .active_level = (button_active_level_t)pm->button_active_level,
    });
    bl0942_t *bl = bl0942_create(&(bl0942_config_t){...});
    tft_panel_t *tft = tft_panel_create(&(tft_panel_config_t){...});

    // 服务层
    metering_service_t *ms = metering_service_create(&(metering_config_t){...});
    safety_guard_t *sg = safety_guard_create(&(safety_guard_config_t){...});

    // 网络层
    network_link_t *wifi = wifi_link_create(&wifi_cfg);
    network_link_t *lte = lte_link_create(&lte_cfg);
    network_manager_t *nm = network_manager_create(&(network_manager_config_t){
        .primary = wifi, .backup = lte,
        .preferred_primary = NETWORK_LINK_TYPE_WIFI,
        ...
    });

    // 云端
    thingsboard_client_t *tb = thingsboard_client_create(&(tb_client_config_t){
        .net_mgr = nm, .device_token = "...", ...
    });

    // 显示
    lvgl_dashboard_t *dash = lvgl_dashboard_create(&(lvgl_dashboard_config_t){
        .panel = tft, ...
    });

    // 装配并启动
    app_controller_t *app = app_controller_create(&(app_controller_config_t){
        .event_loop = NULL,  // use default
        .pinmap = pm,
        .relay = relay, .button = btn, .bl0942 = bl, .tft_panel = tft,
        .metering = ms, .safety = sg, .tb = tb,
        .net_mgr = nm, .dashboard = dash,
    });
    app_controller_start(app);
}
```

### 15.7 关键设计决策

- `app_controller` 不创建任何底层模块，只接收句柄并编排
- `start()` 按依赖顺序注册所有回调 + 启动子模块
- 自身不在 esp_event 上发事件，只做订阅和协调
- 每条数据流（按键→继电器、安全→继电器、电参量→TB、TB命令→继电器）都在 `app_controller.c` 中以回调函数实现，集中可见
- `main.c` 是唯一的对象创建点，所有装配关系一目了然

---

## 全部模块依赖关系（Batch 1–6）

```text
board_pinmap       (无依赖)
network_types.h    (无依赖，纯头文件)
relay              (依赖: driver/gpio.h, FreeRTOS, esp_event)
button             (依赖: espressif/button, driver/gpio.h)
bl0942             (依赖: driver/uart.h, driver/gpio.h, FreeRTOS, esp_event)
network_link       (依赖: network_types.h, esp_err.h)
metering_service   (依赖: bl0942 事件类型, esp_event, FreeRTOS)
wifi_link          (依赖: network_link, network_types.h, esp_wifi, esp_netif, esp_mqtt, FreeRTOS)
network_manager    (依赖: network_link, network_types.h, FreeRTOS)
safety_guard       (依赖: metering_service 事件类型, esp_event, FreeRTOS)
tft_panel          (依赖: driver/spi_master.h, driver/gpio.h)
lvgl_dashboard     (依赖: tft_panel, lvgl, esp_event, FreeRTOS)
thingsboard_client (依赖: network_manager, network_types.h, FreeRTOS, esp_event)
lte_link           (依赖: network_link, network_types.h, esp-lwlte, FreeRTOS)
app_controller     (依赖: 全部模块公共头文件, esp_event, FreeRTOS)
```

全部 15 个模块定义完毕。`thingsboard_client` 和 `lte_link` 通过 `network_manager` 间接使用 MQTT，不直接依赖 `wifi_link` 或彼此。`app_controller` 是唯一知道所有模块存在的模块。
