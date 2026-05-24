# Modules 1-6 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first six Smart_Socket modules from `docs/agents/classes.md`: board pinmap, network shared types, relay, button, BL0942, and network link base class.

**Architecture:** Keep the new project API authoritative and use the old `reference/EEE532-Project` only for proven pin assignments and hardware protocol details. Hardware modules use opaque handles and injected GPIO/UART config; only `network_link` uses C OOP inheritance with a private ops table.

**Tech Stack:** ESP-IDF v6.0, C, FreeRTOS mutex/tasks/semaphores, ESP-IDF GPIO/UART/event loop/timer APIs, `espressif/button ^4.1`.

---

## File Structure

Create and modify these files:

| Path | Responsibility |
|---|---|
| `main/idf_component.yml` | Declare component-manager dependency on `espressif/button ^4.1`. |
| `main/CMakeLists.txt` | Compile all module sources and expose module include directories. |
| `main/platform/board_pinmap.h` | Public board active-level enum, pinmap struct, and `board_pinmap_get()`. |
| `main/platform/board_pinmap.c` | Static read-only pinmap singleton with GPIO values from the old working project and board TFT reference. |
| `main/network/network_types.h` | ESP-IDF-free shared network enums, publish request, RX payload, and RX callback type. |
| `main/network/network_link.h` | Public opaque network link base handle and wrapper API. |
| `main/network/network_link_priv.h` | Private base struct and ops table for future `wifi_link`/`lte_link` subclasses. |
| `main/network/network_link.c` | Wrapper validation and ops delegation. |
| `main/relay/relay.h` | Public relay opaque handle, config, source enum, event payload, and API. |
| `main/relay/relay.c` | GPIO relay driver with mutex-protected state and `esp_event` state-change publishing. |
| `main/button/button_iot_adapter.h` | Internal adapter API that hides Espressif button component names from `button.c`. |
| `main/button/button_iot_adapter.c` | Internal bridge to `iot_button_new_gpio_device()`, callback registration, and delete. |
| `main/button/button.h` | Public button opaque handle, config, event enum, callback type, and API. |
| `main/button/button.c` | Opaque button wrapper with per-event callback slots and internal iot-button dispatch. |
| `main/bl0942/bl0942.h` | Public BL0942 opaque handle, config, measurement/fault payloads, events, and API. |
| `main/bl0942/bl0942.c` | UART BL0942 reader, periodic sampling task, latest-cache, fault events, and EN-pin hard reset. |

The internal `button_iot_adapter` is required because this project must expose a public `button_config_t`, while the `espressif/button` component also defines `button_config_t`. The adapter includes Espressif headers in a separate translation unit that does not include `main/button/button.h`.

## Task 1: Component Manifest And Build Registration

**Files:**
- Create: `main/idf_component.yml`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Write the dependency manifest**

Create `main/idf_component.yml` with this exact content:

```yaml
## IDF Component Manager Manifest File
## Managed dependencies for the Smart_Socket main component.

dependencies:
  idf:
    version: ">=5.0"
  espressif/button: "^4.1"
```

- [ ] **Step 2: Replace the component registration**

Replace `main/CMakeLists.txt` with this exact content:

```cmake
idf_component_register(
    SRCS
        "main.c"
        "platform/board_pinmap.c"
        "network/network_link.c"
        "relay/relay.c"
        "button/button_iot_adapter.c"
        "button/button.c"
        "bl0942/bl0942.c"
    INCLUDE_DIRS
        "."
        "platform"
        "network"
        "relay"
        "button"
        "bl0942"
)
```

- [ ] **Step 3: Run build to verify the intended failure**

Run: `idf.py build`

Expected: FAIL because the source files referenced in `main/CMakeLists.txt` do not exist yet. The useful failure is a CMake or build-system message naming one of the missing module source files.

Do not attempt to fix this task by removing sources from CMake. The following tasks add the referenced files.

- [ ] **Step 4: Checkpoint without committing**

Run: `git status --short`

Expected: `main/CMakeLists.txt` modified and `main/idf_component.yml` untracked. Do not run `git commit` unless the user explicitly requests commits.

## Task 2: Board Pinmap And Network Types

**Files:**
- Create: `main/platform/board_pinmap.h`
- Create: `main/platform/board_pinmap.c`
- Create: `main/network/network_types.h`

- [ ] **Step 1: Create `board_pinmap.h`**

The public API must contain these exact type and function names:

```c
typedef enum {
    BOARD_ACTIVE_LOW = 0,
    BOARD_ACTIVE_HIGH = 1,
} board_active_level_t;

typedef struct {
    gpio_num_t button_gpio;
    board_active_level_t button_active_level;
    gpio_num_t relay_ctrl_gpio;
    board_active_level_t relay_active_level;
    gpio_num_t bl0942_en_gpio;
    gpio_num_t bl0942_tx_gpio;
    gpio_num_t bl0942_rx_gpio;
    gpio_num_t lte_en_gpio;
    gpio_num_t lte_tx_gpio;
    gpio_num_t lte_rx_gpio;
    gpio_num_t tft_sclk_gpio;
    gpio_num_t tft_mosi_gpio;
    gpio_num_t tft_dc_gpio;
    gpio_num_t tft_cs_gpio;
    gpio_num_t tft_rst_gpio;
    gpio_num_t tft_bl_gpio;
} board_pinmap_t;

const board_pinmap_t *board_pinmap_get(void);
```

Use the header template from `docs/agents/coding-style.md`, include `driver/gpio.h`, and add Doxygen comments for the enum, struct, fields, and function.

- [ ] **Step 2: Create `board_pinmap.c`**

Implement a file-scope read-only singleton with these exact values:

```c
static const board_pinmap_t s_pinmap = {
    .button_gpio = GPIO_NUM_2,
    .button_active_level = BOARD_ACTIVE_LOW,
    .relay_ctrl_gpio = GPIO_NUM_4,
    .relay_active_level = BOARD_ACTIVE_HIGH,
    .bl0942_en_gpio = GPIO_NUM_8,
    .bl0942_tx_gpio = GPIO_NUM_10,
    .bl0942_rx_gpio = GPIO_NUM_11,
    .lte_en_gpio = GPIO_NUM_5,
    .lte_tx_gpio = GPIO_NUM_6,
    .lte_rx_gpio = GPIO_NUM_7,
    .tft_sclk_gpio = GPIO_NUM_40,
    .tft_mosi_gpio = GPIO_NUM_45,
    .tft_dc_gpio = GPIO_NUM_41,
    .tft_cs_gpio = GPIO_NUM_42,
    .tft_rst_gpio = GPIO_NUM_39,
    .tft_bl_gpio = GPIO_NUM_46,
};

const board_pinmap_t *board_pinmap_get(void)
{
    return &s_pinmap;
}
```

- [ ] **Step 3: Create `network_types.h`**

Implement this pure-header API with no ESP-IDF includes:

```c
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    NETWORK_LINK_TYPE_NONE = 0,
    NETWORK_LINK_TYPE_WIFI,
    NETWORK_LINK_TYPE_LTE,
} network_link_type_t;

typedef enum {
    NETWORK_LINK_STATUS_IDLE = 0,
    NETWORK_LINK_STATUS_STARTING,
    NETWORK_LINK_STATUS_CONNECTING,
    NETWORK_LINK_STATUS_DEGRADED,
    NETWORK_LINK_STATUS_READY,
    NETWORK_LINK_STATUS_ERROR,
} network_link_status_t;

typedef enum {
    NETWORK_MQTT_QOS0 = 0,
    NETWORK_MQTT_QOS1 = 1,
    NETWORK_MQTT_QOS2 = 2,
} network_mqtt_qos_t;

typedef struct {
    const char *topic;
    const void *payload;
    size_t payload_len;
    network_mqtt_qos_t qos;
    bool retain;
} network_publish_request_t;

typedef struct {
    const char *topic;
    const char *data;
    int data_len;
} network_rx_data_t;

typedef void (*network_rx_cb_t)(const network_rx_data_t *rx_data, void *user_ctx);
```

Use Doxygen comments and enum value comments following `docs/agents/coding-style.md`.

- [ ] **Step 4: Build checkpoint**

Run: `idf.py build`

Expected: FAIL naming the next missing CMake source, such as `network/network_link.c`, `relay/relay.c`, `button/button_iot_adapter.c`, or `bl0942/bl0942.c`. The failure confirms Task 2 files are no longer the blocker.

- [ ] **Step 5: Checkpoint without committing**

Run: `git status --short`

Expected: new `main/platform/*` and `main/network/network_types.h` files appear. Do not run `git commit` unless the user explicitly requests commits.

## Task 3: Network Link Base Class

**Files:**
- Create: `main/network/network_link.h`
- Create: `main/network/network_link_priv.h`
- Create: `main/network/network_link.c`

- [ ] **Step 1: Create public `network_link.h`**

Expose exactly these public declarations:

```c
typedef struct network_link network_link_t;

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

Include `esp_err.h` and `network_types.h`. Do not expose ops or struct fields in this header.

- [ ] **Step 2: Create private `network_link_priv.h`**

Expose this internal contract to future subclasses:

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

struct network_link {
    const network_link_ops_t *ops;
    network_link_type_t type;
};
```

Include `network_link.h` in this private header.

- [ ] **Step 3: Implement `network_link.c` wrappers**

Implement each wrapper with these exact return semantics:

```c
esp_err_t network_link_start(network_link_t *me)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");
    ESP_RETURN_ON_FALSE(me->ops != NULL && me->ops->start != NULL,
                        ESP_ERR_NOT_SUPPORTED, TAG, "start not supported");
    return me->ops->start(me);
}
```

Use the same validation pattern for `stop`, `get_status`, `publish`, `subscribe`, `unsubscribe`, and `register_rx_cb`. `network_link_destroy(NULL)` returns `ESP_OK`. `network_link_get_type(NULL)` returns `NETWORK_LINK_TYPE_NONE`.

`network_link_get_status()` must cast away const only at the delegation point:

```c
return me->ops->get_status((network_link_t *)me, out);
```

- [ ] **Step 4: Build checkpoint**

Run: `idf.py build`

Expected: FAIL naming a remaining missing source from `relay`, `button`, or `bl0942`. If the error is in `network_link`, fix the exact wrapper or header mismatch before continuing.

- [ ] **Step 5: Checkpoint without committing**

Run: `git status --short`

Expected: new network link files appear. Do not run `git commit` unless the user explicitly requests commits.

## Task 4: Relay Module

**Files:**
- Create: `main/relay/relay.h`
- Create: `main/relay/relay.c`

- [ ] **Step 1: Create `relay.h` public API**

Expose exactly these public types and functions from `docs/agents/classes.md`:

```c
typedef enum {
    RELAY_ACTIVE_LOW = 0,
    RELAY_ACTIVE_HIGH = 1,
} relay_active_level_t;

typedef enum {
    RELAY_SOURCE_INTERNAL = 0,
    RELAY_SOURCE_LOCAL_BUTTON,
    RELAY_SOURCE_CLOUD,
    RELAY_SOURCE_SAFETY,
    RELAY_SOURCE_MAX,
} relay_source_t;

typedef struct {
    gpio_num_t ctrl_gpio;
    relay_active_level_t active_level;
} relay_config_t;

typedef struct {
    bool on;
    relay_source_t source;
} relay_state_changed_event_t;

typedef struct relay relay_t;

ESP_EVENT_DECLARE_BASE(RELAY_EVENT_BASE);

typedef enum {
    RELAY_EVENT_STATE_CHANGED = 0,
} relay_event_id_t;

relay_t *relay_create(const relay_config_t *config);
esp_err_t relay_destroy(relay_t *me);
esp_err_t relay_set(relay_t *me, relay_source_t source, bool on);
esp_err_t relay_toggle(relay_t *me, relay_source_t source);
esp_err_t relay_get(const relay_t *me, bool *out_on);
```

Include `<stdbool.h>`, `driver/gpio.h`, `esp_err.h`, and `esp_event.h`.

- [ ] **Step 2: Implement `relay.c` state and helpers**

Use this internal struct:

```c
struct relay {
    gpio_num_t ctrl_gpio;
    relay_active_level_t active_level;
    bool on;
    SemaphoreHandle_t mutex;
    bool initialized;
};
```

Implement these helpers:

```c
static esp_err_t relay_validate_config(const relay_config_t *config);
static uint32_t relay_logical_to_level(const relay_t *me, bool on);
static void relay_post_state_changed(bool on, relay_source_t source);
```

`relay_validate_config()` must reject `NULL`, invalid output GPIO, and invalid active level. `relay_logical_to_level()` returns `1` when `on` matches active-high state, otherwise `0`. `relay_post_state_changed()` posts to `RELAY_EVENT_BASE` with id `RELAY_EVENT_STATE_CHANGED` and logs warning on failure.

- [ ] **Step 3: Implement relay lifecycle and API behavior**

`relay_create()` must allocate with `calloc(1, sizeof(*me))`, create a mutex, configure GPIO output with no pulls and no interrupt, drive logical off, set `initialized = true`, and return the handle.

`relay_set()` must validate `me`, `source < RELAY_SOURCE_MAX`, and initialized state. It takes the mutex, writes the GPIO level, updates `me->on`, releases the mutex, and posts an event only when the previous logical state differed from `on`.

`relay_toggle()` must take the mutex and compute `new_on = !me->on` inside the locked section so read-modify-write is atomic.

`relay_get()` must copy `me->on` under the mutex.

`relay_destroy(NULL)` returns `ESP_OK`. Non-null destroy drives the relay off when initialized, deletes the mutex, frees the handle, and returns the GPIO-off error if that final drive failed.

- [ ] **Step 4: Build checkpoint**

Run: `idf.py build`

Expected: FAIL naming a remaining missing source from `button` or `bl0942`. If the error is in `relay`, fix the exact signature, event base name, include, or mutex usage before continuing.

- [ ] **Step 5: Checkpoint without committing**

Run: `git status --short`

Expected: new relay files appear. Do not run `git commit` unless the user explicitly requests commits.

## Task 5: Button Module With Internal Iot Adapter

**Files:**
- Create: `main/button/button_iot_adapter.h`
- Create: `main/button/button_iot_adapter.c`
- Create: `main/button/button.h`
- Create: `main/button/button.c`

- [ ] **Step 1: Create internal adapter header**

Create `main/button/button_iot_adapter.h` with an internal API that does not include `iot_button.h`:

```c
typedef void *button_iot_handle_t;
typedef void (*button_iot_event_cb_t)(void *button_handle, void *user_ctx);

typedef enum {
    BUTTON_IOT_EVENT_SINGLE_CLICK = 0,
    BUTTON_IOT_EVENT_DOUBLE_CLICK,
    BUTTON_IOT_EVENT_LONG_PRESS_START,
    BUTTON_IOT_EVENT_LONG_PRESS_HOLD,
    BUTTON_IOT_EVENT_MAX,
} button_iot_event_t;

esp_err_t button_iot_create_gpio(gpio_num_t input_gpio, uint8_t active_level,
                                 button_iot_handle_t *out_handle);
esp_err_t button_iot_register_cb(button_iot_handle_t handle,
                                 button_iot_event_t event,
                                 button_iot_event_cb_t cb,
                                 void *user_ctx);
esp_err_t button_iot_delete(button_iot_handle_t handle);
```

Include `<stdint.h>`, `driver/gpio.h`, and `esp_err.h`.

- [ ] **Step 2: Create internal adapter source**

Create `main/button/button_iot_adapter.c`. Include only `button_iot_adapter.h`, `<stdbool.h>`, `button_gpio.h`, and `iot_button.h`. Do not include `button.h` in this file.

Use this event mapping:

```c
static button_event_t button_iot_to_native_event(button_iot_event_t event)
{
    switch (event) {
    case BUTTON_IOT_EVENT_SINGLE_CLICK:
        return BUTTON_SINGLE_CLICK;
    case BUTTON_IOT_EVENT_DOUBLE_CLICK:
        return BUTTON_DOUBLE_CLICK;
    case BUTTON_IOT_EVENT_LONG_PRESS_START:
        return BUTTON_LONG_PRESS_START;
    case BUTTON_IOT_EVENT_LONG_PRESS_HOLD:
        return BUTTON_LONG_PRESS_HOLD;
    default:
        return BUTTON_EVENT_MAX;
    }
}
```

`button_iot_create_gpio()` must call `iot_button_new_gpio_device()` with:

```c
const button_config_t button_config = {
    .long_press_time = 0,
    .short_press_time = 0,
};
const button_gpio_config_t gpio_config = {
    .gpio_num = input_gpio,
    .active_level = active_level,
    .enable_power_save = false,
    .disable_pull = false,
};
```

`button_iot_register_cb()` must call `iot_button_register_cb((button_handle_t)handle, native_event, NULL, cb, user_ctx)`. `button_iot_delete(NULL)` returns `ESP_OK`; non-null calls `iot_button_delete()`.

- [ ] **Step 3: Create `button.h` public API**

Expose exactly these public declarations:

```c
typedef enum {
    BUTTON_ACTIVE_LOW = 0,
    BUTTON_ACTIVE_HIGH = 1,
} button_active_level_t;

typedef enum {
    BUTTON_EVENT_SINGLE_CLICK = 0,
    BUTTON_EVENT_DOUBLE_CLICK,
    BUTTON_EVENT_LONG_PRESS_START,
    BUTTON_EVENT_LONG_PRESS_HOLD,
    BUTTON_EVENT_MAX,
} button_event_t;

typedef struct {
    gpio_num_t input_gpio;
    button_active_level_t active_level;
} button_config_t;

typedef void (*button_event_cb_t)(button_event_t event, void *user_ctx);

typedef struct button button_t;

button_t *button_create(const button_config_t *config);
esp_err_t button_destroy(button_t *me);
esp_err_t button_register_cb(button_t *me, button_event_t event,
                             button_event_cb_t cb, void *user_ctx);
```

Include `driver/gpio.h` and `esp_err.h`. Do not include Espressif button component headers in this public header.

- [ ] **Step 4: Implement `button.c`**

Use this internal struct:

```c
struct button {
    gpio_num_t input_gpio;
    button_active_level_t active_level;
    button_iot_handle_t iot_button_handle;
    button_event_cb_t callbacks[BUTTON_EVENT_MAX];
    void *user_ctxs[BUTTON_EVENT_MAX];
    SemaphoreHandle_t mutex;
    bool initialized;
};
```

Implement internal callbacks that receive `usr_data` as `button_t *` and dispatch to project events:

```c
static void button_on_single_click(void *button_handle, void *usr_data)
{
    (void)button_handle;
    button_dispatch((button_t *)usr_data, BUTTON_EVENT_SINGLE_CLICK);
}
```

Repeat the same pattern for double click, long-press start, and long-press hold. `button_dispatch()` copies callback and user context under the mutex, releases the mutex, then invokes the copied callback when non-null.

`button_create()` validates config, allocates the handle, creates the mutex, calls `button_iot_create_gpio()`, registers all four internal callbacks, and returns a fully initialized handle. On any failure, it deletes the iot handle if created, deletes the mutex if created, frees memory, and returns `NULL`.

`button_register_cb()` validates `me`, `event < BUTTON_EVENT_MAX`, and initialized state, then updates the event slot under the mutex. Passing `cb == NULL` clears that event callback and still returns `ESP_OK`.

`button_destroy(NULL)` returns `ESP_OK`. Non-null destroy deletes the iot handle, deletes the mutex, clears `initialized`, and frees memory.

- [ ] **Step 5: Build checkpoint**

Run: `idf.py build`

Expected: the component manager may first resolve `espressif/button`. After dependency resolution, build should FAIL only because `bl0942/bl0942.c` is still missing. If the error mentions `button_config_t` redefinition, confirm `button_iot_adapter.c` does not include `button.h` and `button.c` does not include `iot_button.h` or `button_gpio.h`.

- [ ] **Step 6: Checkpoint without committing**

Run: `git status --short`

Expected: new button files appear, plus generated dependency files may appear after component-manager resolution. Do not run `git commit` unless the user explicitly requests commits.

## Task 6: BL0942 Module

**Files:**
- Create: `main/bl0942/bl0942.h`
- Create: `main/bl0942/bl0942.c`

- [ ] **Step 1: Create `bl0942.h` public API**

Expose exactly the API from `docs/agents/classes.md`:

```c
typedef struct bl0942 bl0942_t;

typedef struct {
    uart_port_t uart_num;
    gpio_num_t en_gpio;
    gpio_num_t tx_gpio;
    gpio_num_t rx_gpio;
    int baud_rate;
    uint8_t device_address;
    int rx_buf_size;
    int read_timeout_ms;
    int sample_period_ms;
    int fault_threshold;
    int hard_reset_max_attempts;
} bl0942_config_t;

typedef struct {
    uint32_t i_rms_raw;
    uint32_t v_rms_raw;
    uint32_t i_fast_rms_raw;
    int32_t watt_raw;
    uint32_t cf_cnt_raw;
    uint16_t freq_raw;
    uint16_t status_raw;
    uint64_t capture_time_us;
    bool valid;
} bl0942_measurement_t;

typedef struct {
    uint32_t consecutive_failures;
    uint32_t fault_cycles;
    bool hard_reset_attempted;
    esp_err_t last_error;
} bl0942_fault_info_t;

ESP_EVENT_DECLARE_BASE(BL0942_EVENT_BASE);

typedef enum {
    BL0942_EVENT_MEASUREMENT = 0,
    BL0942_EVENT_FAULT,
} bl0942_event_id_t;

bl0942_t *bl0942_create(const bl0942_config_t *config);
esp_err_t bl0942_destroy(bl0942_t *me);
esp_err_t bl0942_start(bl0942_t *me);
esp_err_t bl0942_stop(bl0942_t *me);
esp_err_t bl0942_read(bl0942_t *me, bl0942_measurement_t *out);
esp_err_t bl0942_get_latest(bl0942_t *me, bl0942_measurement_t *out);
```

Include `<stdbool.h>`, `<stdint.h>`, `driver/gpio.h`, `driver/uart.h`, `esp_err.h`, and `esp_event.h`.

- [ ] **Step 2: Implement BL0942 defaults and validation**

Use these defaults when config fields are zero or negative where applicable:

```c
#define BL0942_DEFAULT_BAUD_RATE              (9600)
#define BL0942_DEFAULT_RX_BUF_SIZE            (256)
#define BL0942_DEFAULT_READ_TIMEOUT_MS        (500)
#define BL0942_DEFAULT_SAMPLE_PERIOD_MS       (100)
#define BL0942_DEFAULT_FAULT_THRESHOLD        (10)
#define BL0942_DEFAULT_HARD_RESET_ATTEMPTS    (3)
```

Validation rules:

```c
config != NULL
uart_num >= UART_NUM_0 && uart_num < UART_NUM_MAX
GPIO_IS_VALID_OUTPUT_GPIO(tx_gpio)
GPIO_IS_VALID_GPIO(rx_gpio)
tx_gpio != rx_gpio
en_gpio == GPIO_NUM_NC || GPIO_IS_VALID_OUTPUT_GPIO(en_gpio)
device_address <= 3
baud_rate in {4800, 9600, 19200, 38400} after defaults
rx_buf_size >= 128 after defaults
read_timeout_ms >= 10 after defaults
sample_period_ms >= 10 after defaults
fault_threshold >= 1 after defaults
hard_reset_max_attempts >= 0 after defaults
```

- [ ] **Step 3: Implement BL0942 object and protocol helpers**

Use this internal struct:

```c
struct bl0942 {
    bl0942_config_t config;
    SemaphoreHandle_t mutex;
    bl0942_measurement_t latest;
    bool has_latest;
    TaskHandle_t sample_task;
    SemaphoreHandle_t sample_task_done_sema;
    volatile bool sample_task_running;
    uint32_t fault_cycles;
    int consecutive_failures;
    int hard_reset_count;
    bool initialized;
};
```

Use these protocol constants:

```c
#define BL0942_FRAME_SIZE              (23)
#define BL0942_FRAME_HEADER            (0x55U)
#define BL0942_PACKET_READ_ADDR        (0xAAU)
#define BL0942_MAX_DEVICE_ADDRESS      (3U)
#define BL0942_EN_LOW_DELAY_MS         (1000)
#define BL0942_EN_SETTLE_DELAY_MS      (1000)
#define BL0942_SAMPLE_TASK_NAME        "bl0942_sample"
#define BL0942_SAMPLE_TASK_STACK       (4096)
#define BL0942_SAMPLE_TASK_PRIORITY    (5)
```

Implement helpers for command building, checksum, little-endian decoding, parse, event posting, power cycle, UART install, UART delete, and hard reset. Preserve the old project's proven checksum rule:

```c
static uint8_t bl0942_calculate_packet_checksum(uint8_t cmd,
                                                const uint8_t frame[BL0942_FRAME_SIZE])
{
    uint32_t checksum_acc = cmd;

    for (size_t i = 0; i < BL0942_FRAME_SIZE - 1; ++i) {
        checksum_acc += frame[i];
    }

    return (uint8_t)(~(checksum_acc & 0xFFU));
}
```

Parse frame offsets exactly as the old project does:

```c
out->i_rms_raw = bl0942_decode_u24_le(&frame[1]);
out->v_rms_raw = bl0942_decode_u24_le(&frame[4]);
out->i_fast_rms_raw = bl0942_decode_u24_le(&frame[7]);
out->watt_raw = bl0942_decode_s24_le(&frame[10]);
out->cf_cnt_raw = bl0942_decode_u24_le(&frame[13]);
out->freq_raw = (uint16_t)(((uint16_t)frame[17] << 8) | frame[16]);
out->status_raw = (uint16_t)frame[19];
out->capture_time_us = (uint64_t)esp_timer_get_time();
out->valid = true;
```

- [ ] **Step 4: Implement BL0942 lifecycle and read path**

`bl0942_create()` must apply defaults, allocate with `calloc`, create mutex and done semaphore, power-cycle through EN when available, reject an already installed UART port, install UART, configure UART, set pins, initialize cache, mark initialized, and return the handle. On failure it must delete the UART driver if installed, delete semaphores, free memory, and return `NULL`.

`bl0942_read()` must take `me->mutex`, flush UART input, send this two-byte command, wait for TX done, read exactly 23 bytes, validate header/checksum, parse, update `latest`, set `has_latest = true`, release mutex, and copy the measurement to `out`:

```c
uint8_t cmd[2] = {
    (uint8_t)(0x58U | (me->config.device_address & 0x03U)),
    BL0942_PACKET_READ_ADDR,
};
```

`bl0942_get_latest()` must validate arguments and initialized state, copy `latest` under mutex, and return `ESP_ERR_INVALID_STATE` when `has_latest` is false.

- [ ] **Step 5: Implement sampling task and fault path**

`bl0942_start()` must return `ESP_OK` when already running. On first start it clears counters, drains any stale done semaphore signal, sets `sample_task_running = true`, and creates `BL0942_SAMPLE_TASK_NAME` with stack `4096` and priority `5`.

The sample task must:

```c
while (me->sample_task_running) {
    bl0942_measurement_t measurement = {0};
    esp_err_t ret = bl0942_read(me, &measurement);

    if (ret == ESP_OK) {
        me->consecutive_failures = 0;
        me->hard_reset_count = 0;
        bl0942_post_measurement(&measurement);
    } else {
        me->consecutive_failures++;
        if (me->consecutive_failures >= me->config.fault_threshold) {
            me->fault_cycles++;
            bool reset_attempted = false;
            if (me->config.en_gpio != GPIO_NUM_NC &&
                me->hard_reset_count < me->config.hard_reset_max_attempts) {
                reset_attempted = true;
                me->hard_reset_count++;
                (void)bl0942_hard_reset(me);
            }
            bl0942_post_fault(me, ret, reset_attempted);
            me->consecutive_failures = 0;
            if (me->hard_reset_count >= me->config.hard_reset_max_attempts &&
                me->config.hard_reset_max_attempts > 0) {
                me->sample_task_running = false;
            }
        }
    }

    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(me->config.sample_period_ms));
}
```

Before exiting, the task sets `sample_task = NULL`, gives `sample_task_done_sema`, and deletes itself.

`bl0942_stop()` sets `sample_task_running = false` and waits for the done semaphore. The wait timeout is `read_timeout_ms + sample_period_ms + 3500` milliseconds to allow a read timeout and EN-pin hard reset to finish.

`bl0942_destroy(NULL)` returns `ESP_OK`. Non-null destroy stops the task, deletes UART driver, deletes semaphores, deletes mutex, frees memory, and returns the first cleanup error that prevents safe teardown.

- [ ] **Step 6: Build checkpoint**

Run: `idf.py build`

Expected: PASS, or fail with a concrete compiler/linker error in the new modules. Fix any exact error before continuing. The app still only logs from `main.c`; no hardware module is started yet because `app_controller` is outside this plan.

- [ ] **Step 7: Checkpoint without committing**

Run: `git status --short`

Expected: all new module files and build metadata changes appear. Do not run `git commit` unless the user explicitly requests commits.

## Task 7: Full Review And Verification

**Files:**
- Inspect: all files created or modified in Tasks 1-6

- [ ] **Step 1: Static contract review**

Run these searches:

```bash
rg "relay_control_|local_button_|bl0942_service_" main
rg "#include \"board_pinmap.h\"" main/relay main/button main/bl0942
rg "network_link_ops_t" main/network/network_link.h
```

Expected:

```text
No matches for old singleton API names.
No matches for board_pinmap include inside relay/button/bl0942.
No matches for network_link_ops_t in public network_link.h.
```

- [ ] **Step 2: Run ESP-IDF build through MCP**

Use the ESP-IDF MCP build tool for the project.

Expected: PASS. If MCP cannot run dependency resolution or returns an environment error unrelated to C code, use Step 3.

- [ ] **Step 3: Fallback shell build if MCP cannot complete**

Run:

```bash
source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build
```

Expected: PASS. If it fails, capture the first compiler or linker error and fix that concrete error before rerunning the build.

- [ ] **Step 4: Final status check**

Run: `git status --short`

Expected: only intended project files, dependency lock files, and generated managed component files are changed. Do not revert unrelated user changes. Do not commit unless the user explicitly requests commits.

- [ ] **Step 5: Report verification scope**

Report one of these exact build-verification lines, followed by the fixed hardware-verification line:

```text
Implemented modules 1-6 from docs/agents/classes.md.
Build verification: MCP ESP-IDF build passed.
Build verification: shell idf.py build passed.
Hardware verification: not flashed and not serial-validated in this pass.
```

## Self-Review Notes

Spec coverage:

1. `board_pinmap` is covered in Task 2.
2. `network_types` is covered in Task 2.
3. `network_link` is covered in Task 3.
4. `relay` is covered in Task 4.
5. `button` is covered in Task 5, including the adapter needed for the Espressif type-name collision.
6. `bl0942` is covered in Task 6.
7. CMake, dependency manifest, and build verification are covered in Tasks 1 and 7.

Plan scan: no unresolved fill-in markers are intentionally present in this plan.

Type consistency: public names match `docs/agents/classes.md`; internal adapter names are prefixed `button_iot_` and do not leak into public headers.
