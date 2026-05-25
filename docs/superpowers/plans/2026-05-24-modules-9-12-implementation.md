# Modules 9-12 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `network_manager`, `safety_guard`, `tft_panel`, and `lvgl_dashboard` from `docs/agents/classes.md` using the approved design in `docs/superpowers/specs/2026-05-24-modules-9-12-design.md`.

**Architecture:** `network_manager` is an opaque-handle coordinator over borrowed `network_link_t *` links with a polling monitor task for failover/failback. `safety_guard` consumes metering events and emits local rule snapshots. `tft_panel` owns the ESP-IDF SPI/ST7789T hardware resources, while `lvgl_dashboard` owns LVGL objects and borrows the panel and network manager handles.

**Tech Stack:** ESP-IDF v6.0, C, FreeRTOS mutexes/tasks/semaphores, ESP-IDF event loop, `esp_timer`, `esp_lcd`, SPI master, LVGL 9 via `lvgl/lvgl ^9.0.0`.

---

## File Structure

| Path | Responsibility |
|---|---|
| `docs/agents/classes.md` | Update API contract for callback clearing, TFT flush callback registration, and borrowed dashboard network manager dependency. |
| `main/idf_component.yml` | Add `lvgl/lvgl ^9.0.0`. |
| `main/CMakeLists.txt` | Register all new sources and include directories. |
| `main/network/network_link.c` | Allow `network_link_register_rx_cb(link, NULL, NULL)` to clear callbacks. |
| `main/network/network_manager.h` | Public manager opaque handle, config, status, and unified network API. |
| `main/network/network_manager.c` | Failover/failback monitor, subscription table, publish delegation, RX bridge. |
| `main/safety/safety_guard.h` | Public safety guard config, enums, snapshot, events, and API. |
| `main/safety/safety_guard.c` | Overcurrent/overpower rule evaluation and event publishing. |
| `main/display/tft/tft_panel.h` | Public TFT handle, config, callback registration, draw/backlight/size API. |
| `main/display/tft/tft_panel.c` | Handle-based ESP-IDF `esp_lcd` ST7789T panel setup and cleanup. |
| `main/display/tft/tft_panel_st7789t.h` | Private custom ST7789T panel driver factory. |
| `main/display/tft/tft_panel_st7789t.c` | Board-proven ST7789T `esp_lcd_panel_t` implementation. |
| `main/display/lvgl/lvgl_dashboard.h` | Public dashboard handle, state, config, and lifecycle API. |
| `main/display/lvgl/lvgl_dashboard_internal.h` | Pure layout and formatting helper declarations. |
| `main/display/lvgl/lvgl_dashboard_internal.c` | Pure formatting, stale detection, label text, and state comparison helpers. |
| `main/display/lvgl/lvgl_dashboard.c` | LVGL display, buffers, widget tree, event handlers, network polling, and render task. |

Scope check: this batch includes four modules because display depends on network and safety status types. The tasks are split by module and each task has its own build checkpoint.

## Task 1: Contract And Build Updates

**Files:**
- Modify: `docs/agents/classes.md`
- Modify: `main/idf_component.yml`
- Modify: `main/CMakeLists.txt`
- Modify: `main/network/network_link.c`

- [ ] **Step 1: Document callback clearing**

In `docs/agents/classes.md` section 6.3, add this note below `network_link_register_rx_cb()`:

```markdown
**回调清除语义**：`network_link_register_rx_cb(me, NULL, NULL)` 清除当前回调并返回 `ESP_OK`。这允许借用链路的 `network_manager` 在销毁时解除桥接回调，避免链路后续复用时回调到已释放对象。
```

- [ ] **Step 2: Document TFT flush callback API**

In section 11, add this typedef and method declaration:

```c
typedef void (*tft_panel_flush_done_cb_t)(void *user_ctx);

esp_err_t tft_panel_register_flush_done_cb(tft_panel_t *me,
                                           tft_panel_flush_done_cb_t cb,
                                           void *user_ctx);
```

Keep `tft_panel_config_t` hardware-only with GPIO and dimensions, and add `flush_done_cb` plus `flush_done_ctx` to the internal `struct tft_panel` snippet.

- [ ] **Step 3: Document dashboard borrowed network manager**

In section 12.3, update `lvgl_dashboard_config_t` to:

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

- [ ] **Step 4: Allow callback clearing in `network_link.c`**

Replace `network_link_register_rx_cb()` with:

```c
esp_err_t network_link_register_rx_cb(network_link_t *me,
                                      network_rx_cb_t cb, void *ctx)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");
    ESP_RETURN_ON_FALSE(me->ops != NULL && me->ops->register_rx_cb != NULL,
                        ESP_ERR_NOT_SUPPORTED, TAG,
                        "register rx callback not supported");
    return me->ops->register_rx_cb(me, cb, ctx);
}
```

- [ ] **Step 5: Update `main/idf_component.yml`**

Replace the file content with:

```yaml
## IDF Component Manager Manifest File
## Managed dependencies for the Smart_Socket main component.

dependencies:
  idf:
    version: ">=5.0"
  espressif/button: "^4.1"
  espressif/mqtt: "^1.0.0"
  lvgl/lvgl: "^9.0.0"
```

- [ ] **Step 6: Update `main/CMakeLists.txt`**

Replace the file content with:

```cmake
idf_component_register(
    SRCS
        "main.c"
        "platform/board_pinmap.c"
        "network/network_link.c"
        "network/network_manager.c"
        "network/wifi/wifi_link.c"
        "relay/relay.c"
        "button/button_iot_adapter.c"
        "button/button.c"
        "bl0942/bl0942.c"
        "metering/metering_service.c"
        "safety/safety_guard.c"
        "display/tft/tft_panel.c"
        "display/tft/tft_panel_st7789t.c"
        "display/lvgl/lvgl_dashboard_internal.c"
        "display/lvgl/lvgl_dashboard.c"
    INCLUDE_DIRS
        "."
        "platform"
        "network"
        "network/wifi"
        "relay"
        "button"
        "bl0942"
        "metering"
        "safety"
        "display/tft"
        "display/lvgl"
)
```

- [ ] **Step 7: Build to verify expected missing-file failure**

Run: `idf.py build`

Expected: FAIL naming one of the newly listed missing source files, such as `network/network_manager.c` or `safety/safety_guard.c`.

- [ ] **Step 8: Checkpoint without committing**

Run: `git status --short`

Expected: the four files in this task are modified. Do not run `git commit` unless the user explicitly requests commits.

## Task 2: Network Manager Header

**Files:**
- Create: `main/network/network_manager.h`

- [ ] **Step 1: Create public types**

Create the header with the project template, include `<stdbool.h>`, `<stdint.h>`, `esp_err.h`, `network_link.h`, and `network_types.h`, then add:

```c
typedef struct network_manager network_manager_t;

typedef struct {
    network_link_t *primary;
    network_link_t *backup;
    network_link_type_t preferred_primary;
    uint32_t failover_recheck_ms;
    uint32_t failback_delay_ms;
    int max_subscriptions;
} network_manager_config_t;

typedef struct {
    network_link_type_t active_link;
    bool ready;
    network_link_status_t primary_status;
    network_link_status_t backup_status;
} network_manager_status_t;
```

Add Doxygen comments for the handle, structs, and every field.

- [ ] **Step 2: Add public functions**

Add these declarations with Doxygen comments:

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
                                     const char *topic,
                                     network_mqtt_qos_t qos);
esp_err_t network_manager_unsubscribe(network_manager_t *me,
                                       const char *topic);
esp_err_t network_manager_register_rx_cb(network_manager_t *me,
                                          network_rx_cb_t cb, void *ctx);
```

- [ ] **Step 3: Build checkpoint**

Run: `idf.py build`

Expected: FAIL because `main/network/network_manager.c` still does not exist. If the error is in the header, fix the include or declaration mismatch first.

## Task 3: Network Manager Implementation

**Files:**
- Create: `main/network/network_manager.c`

- [ ] **Step 1: Create skeleton, constants, and object state**

Create the source with includes:

```c
#include "network_manager.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
```

Add constants and internal structs:

```c
#define TAG "network_manager"
#define NETWORK_MANAGER_DEFAULT_RECHECK_MS        (5000U)
#define NETWORK_MANAGER_DEFAULT_FAILBACK_DELAY_MS (30000U)
#define NETWORK_MANAGER_DEFAULT_MAX_SUBS          (8)
#define NETWORK_MANAGER_MAX_TOPIC_LEN             (128)
#define NETWORK_MANAGER_TASK_NAME                 "net_mgr"
#define NETWORK_MANAGER_TASK_STACK                (4096)
#define NETWORK_MANAGER_TASK_PRIORITY             (4)
#define NETWORK_MANAGER_STOP_TIMEOUT_MS           (3000U)

typedef struct {
    char *topic;
    network_mqtt_qos_t qos;
    bool in_use;
} network_manager_sub_entry_t;

typedef struct {
    network_manager_t *manager;
    network_link_t *link;
} network_manager_rx_bridge_ctx_t;

struct network_manager {
    network_link_t *primary;
    network_link_t *backup;
    network_link_t *active;
    network_link_type_t preferred_primary;
    uint32_t failover_recheck_ms;
    uint32_t failback_delay_ms;
    network_manager_sub_entry_t *sub_table;
    int sub_table_size;
    SemaphoreHandle_t mutex;
    network_rx_cb_t rx_cb;
    void *rx_ctx;
    uint64_t failback_since_us;
    TaskHandle_t monitor_task;
    SemaphoreHandle_t monitor_task_done_sema;
    network_manager_rx_bridge_ctx_t primary_rx_ctx;
    network_manager_rx_bridge_ctx_t backup_rx_ctx;
    bool monitor_task_running;
    bool started;
    bool destroying;
};
```

- [ ] **Step 2: Implement validation and defaults**

Add helpers:

```c
static bool network_manager_is_valid_qos(network_mqtt_qos_t qos)
{
    return qos == NETWORK_MQTT_QOS0 || qos == NETWORK_MQTT_QOS1 ||
           qos == NETWORK_MQTT_QOS2;
}

static bool network_manager_status_is_usable(network_link_status_t status)
{
    return status == NETWORK_LINK_STATUS_READY ||
           status == NETWORK_LINK_STATUS_DEGRADED;
}

static esp_err_t network_manager_validate_config(const network_manager_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");
    ESP_RETURN_ON_FALSE(config->primary != NULL, ESP_ERR_INVALID_ARG, TAG, "primary is null");
    ESP_RETURN_ON_FALSE(config->backup == NULL || config->backup != config->primary,
                        ESP_ERR_INVALID_ARG, TAG, "backup duplicates primary");
    return ESP_OK;
}
```

When applying defaults, use primary link type if `preferred_primary == NETWORK_LINK_TYPE_NONE`, `5000 ms` recheck if zero, `30000 ms` failback delay if zero, and `8` subscriptions if `max_subscriptions <= 0`.

- [ ] **Step 3: Implement create and destroy**

`network_manager_create()` must validate, allocate, create mutex and done semaphore, allocate `sub_table`, initialize bridge contexts, and register `network_manager_on_link_rx` on primary and backup with separate bridge context objects. On failure, clear any registered callback with `network_link_register_rx_cb(link, NULL, NULL)` and release all created resources.

`network_manager_destroy(NULL)` returns `ESP_OK`. Non-null destroy calls `network_manager_stop(me)`, clears callbacks on borrowed links with `network_link_register_rx_cb(link, NULL, NULL)`, frees subscription topic strings, frees `sub_table`, deletes synchronization primitives, and frees `me`.

- [ ] **Step 4: Implement subscription table helpers**

Implement helpers that provide these exact behaviors:

```text
find: set existing index and first free index; return ESP_OK when absent.
store: reject empty topic, invalid QoS, and topic length >= 128; update existing entry or allocate a copied topic in a free slot.
remove: reject empty topic; return ESP_ERR_NOT_FOUND when absent; free and clear the entry when present.
replay: iterate active entries and call network_link_subscribe(link, topic, qos); return first non-OK error.
```

Use `malloc(strlen(topic) + 1U)` and `memcpy()` for topic copies.

- [ ] **Step 5: Implement lifecycle and monitor task**

`network_manager_start()` starts primary first, falls back to backup only when primary start returns an error, sets `active`, starts the monitor task, and returns `ESP_OK` when at least one link starts. Repeated start returns `ESP_OK`.

`network_manager_stop()` stops the monitor task using `monitor_task_running=false` plus `monitor_task_done_sema`, stops primary and backup links, clears `active`, and returns the first non-OK stop error. Repeated stop returns `ESP_OK`.

The monitor task loops every `failover_recheck_ms`, reads primary and backup status, best-effort starts backup when active primary is not usable, switches active to a usable backup on primary failure, switches back to primary only after `failback_delay_ms` of continuous primary `READY`, replays subscriptions after each active switch, signals the done semaphore on exit, and deletes itself.

- [ ] **Step 6: Implement status, publish, subscribe, unsubscribe, and RX bridge**

`network_manager_get_status()` fills active link type, `ready`, primary status, and backup status under mutex.

`network_manager_is_ready()` calls `network_manager_get_status()` and returns `status.ready`.

`network_manager_publish()` validates topic, payload pointer/length, and QoS, then copies `active` under mutex. It returns `ESP_ERR_INVALID_STATE` unless the active link status is `NETWORK_LINK_STATUS_READY`, otherwise it calls `network_link_publish(active, req)`.

`network_manager_subscribe()` stores the intent under mutex and then calls `network_link_subscribe(active, topic, qos)` when active exists.

`network_manager_unsubscribe()` removes the intent and best-effort calls `network_link_unsubscribe()` on primary and backup; ignore `ESP_ERR_NOT_FOUND` from borrowed links.

`network_manager_on_link_rx()` uses the bridge context to identify the source link, copies `active`, `rx_cb`, and `rx_ctx` under mutex, releases the mutex, and forwards only when `source_link == active` and `rx_cb != NULL`.

- [ ] **Step 7: Build checkpoint**

Run: `idf.py build`

Expected: FAIL because safety or display sources are still missing. If the error is in `network_manager`, fix the exact type, include, or lifecycle mismatch before continuing.

## Task 4: Safety Guard Module

**Files:**
- Create: `main/safety/safety_guard.h`
- Create: `main/safety/safety_guard.c`

- [ ] **Step 1: Create public header**

Create `safety_guard.h` with `<stdbool.h>`, `<stdint.h>`, `esp_err.h`, and `esp_event.h`. Add the public handle, config, enums, snapshot, event base, event ID, and functions exactly as specified in `docs/agents/classes.md` section 10.

- [ ] **Step 2: Create source state and defaults**

Create `safety_guard.c` with includes for `safety_guard.h`, `metering_service.h`, `esp_check.h`, `esp_log.h`, `esp_timer.h`, FreeRTOS, and semaphores. Add:

```c
#define TAG "safety_guard"
#define SAFETY_GUARD_DEFAULT_OVERCURRENT_A (10.0f)
#define SAFETY_GUARD_DEFAULT_OVERPOWER_W   (2200.0f)
#define SAFETY_GUARD_DEFAULT_PERSISTENCE   (3)
#define SAFETY_GUARD_EVENT_POST_TIMEOUT_MS (10)

ESP_EVENT_DEFINE_BASE(SAFETY_GUARD_EVENT_BASE);

struct safety_guard {
    safety_guard_config_t config;
    SemaphoreHandle_t mutex;
    safety_guard_snapshot_t latest;
    bool has_latest;
    int overcurrent_persistence;
    int overpower_persistence;
    esp_event_handler_instance_t metering_handler;
    bool started;
    bool initialized;
};
```

- [ ] **Step 3: Implement rule evaluation**

Implement evaluation so valid snapshots increment/reset persistence counters, choose overcurrent when both are active and its count is greater than or equal to overpower, emit `WARNING` before the persistence threshold, and emit `DANGER` plus `SAFETY_GUARD_ACTION_RELAY_OFF` when the winning count reaches `persistence_samples`. Invalid metering snapshots emit `valid=false`, `level=SAFETY_GUARD_LEVEL_WARNING`, `event=SAFETY_GUARD_EVENT_NONE`, and `suggested_action=SAFETY_GUARD_ACTION_NONE`.

- [ ] **Step 4: Implement lifecycle and event handler**

`safety_guard_create()` applies defaults, allocates, creates mutex, marks initialized, and returns the handle.

`safety_guard_start()` registers `METERING_EVENT_BASE` / `METERING_EVENT_SNAPSHOT` with `esp_event_handler_instance_register()` and stores the handler instance. Repeated start returns `ESP_OK`.

`safety_guard_stop()` unregisters the stored handler instance and clears state. Repeated stop returns `ESP_OK`.

The metering event handler evaluates, updates latest under mutex, releases the mutex, and posts `SAFETY_GUARD_EVENT_SNAPSHOT` with `esp_event_post()`.

`safety_guard_get_latest()` copies latest under mutex or returns `ESP_ERR_INVALID_STATE` when no latest snapshot exists.

`safety_guard_set_thresholds()` rejects non-positive thresholds, updates config under mutex, resets both persistence counters, and returns `ESP_OK`.

- [ ] **Step 5: Build checkpoint**

Run: `idf.py build`

Expected: FAIL because TFT or LVGL files are missing. If the failure is in `safety_guard`, fix the exact include, event, or type mismatch.

## Task 5: TFT Panel And ST7789T Driver

**Files:**
- Create: `main/display/tft/tft_panel.h`
- Create: `main/display/tft/tft_panel_st7789t.h`
- Create: `main/display/tft/tft_panel_st7789t.c`
- Create: `main/display/tft/tft_panel.c`

- [ ] **Step 1: Create `tft_panel.h`**

Create the public header with the type and function declarations from Task 1 Step 2. Include `driver/gpio.h` so `tft_panel_config_t` can expose `gpio_num_t`.

- [ ] **Step 2: Port ST7789T header and driver**

Create `tft_panel_st7789t.h` and `tft_panel_st7789t.c` from `reference/EEE532-Project/main/display/tft/tft_panel_st7789t.h` and `.c` with these constraints:

```text
Keep factory tft_panel_new_st7789t().
Keep internal method names tft_panel_st7789t_del/reset/init/draw_bitmap/invert_color/mirror/swap_xy/set_gap/disp_on_off.
Keep the old project's init command sequence unchanged: SLPOUT, MADCTL, COLMOD, RAMCTRL, porch, power, gamma, inversion-on, display-on, RAM write.
Keep x_gap/y_gap handling in draw_bitmap before CASET and RASET.
Use the current project's file header and static-prototype comment style.
```

- [ ] **Step 3: Implement `tft_panel.c` object and create path**

Use `SPI3_HOST`, `12 MHz`, backlight high-active, X gap `34`, Y gap `0`, BGR, 16 bpp, mirror X true, color inversion true, and 20 buffer lines. The object owns `esp_lcd_panel_io_handle_t`, `esp_lcd_panel_handle_t`, mutex, flush callback, backlight state, and config.

`tft_panel_create()` validates GPIO and dimensions, configures backlight off, calls `spi_bus_initialize()`, `esp_lcd_new_panel_io_spi()`, `tft_panel_new_st7789t()`, `esp_lcd_panel_reset()`, `esp_lcd_panel_init()`, `esp_lcd_panel_mirror()`, `esp_lcd_panel_invert_color()`, `esp_lcd_panel_set_gap()`, `esp_lcd_panel_disp_on_off()`, then enables backlight.

- [ ] **Step 4: Implement runtime APIs and cleanup**

`tft_panel_register_flush_done_cb()` stores or clears the callback under mutex. The SPI `on_color_trans_done` callback reads the stored callback and context without taking a mutex and returns `false`.

`tft_panel_draw_bitmap()` validates bounds and calls `esp_lcd_panel_draw_bitmap()`.

`tft_panel_set_backlight()` drives the configured backlight GPIO and updates state.

`tft_panel_get_width(NULL)` and `tft_panel_get_height(NULL)` return `0`.

`tft_panel_destroy(NULL)` returns `ESP_OK`; non-null destroy disables backlight, turns display off, deletes panel, deletes IO, frees SPI bus, resets backlight GPIO, deletes mutex, and frees the handle.

- [ ] **Step 5: Build checkpoint**

Run: `idf.py build`

Expected: FAIL because LVGL dashboard files are missing. If the failure is in TFT code, fix the exact ESP-IDF API or include mismatch before continuing.

## Task 6: LVGL Dashboard Header And Pure Helpers

**Files:**
- Create: `main/display/lvgl/lvgl_dashboard.h`
- Create: `main/display/lvgl/lvgl_dashboard_internal.h`
- Create: `main/display/lvgl/lvgl_dashboard_internal.c`

- [ ] **Step 1: Create `lvgl_dashboard.h`**

Create the public header with `lvgl_dashboard_t`, `dashboard_network_t`, `dashboard_state_t`, `lvgl_dashboard_config_t`, and the lifecycle API from `docs/agents/classes.md` section 12, including `network_manager_t *network_manager` in config.

- [ ] **Step 2: Create internal helper header**

Create constants for the board-verified layout and colors:

```c
#define LVGL_DASHBOARD_STALE_TIMEOUT_US     (3000000ULL)
#define LVGL_DASHBOARD_DRAW_BUF_LINES       (20U)
#define LVGL_DASHBOARD_STATUS_PILL_WIDTH    (148)
#define LVGL_DASHBOARD_STATUS_PILL_HEIGHT   (30)
#define LVGL_DASHBOARD_NETWORK_BOX_Y        (8)
#define LVGL_DASHBOARD_RELAY_BOX_Y          (42)
#define LVGL_DASHBOARD_POWER_BOX_Y          (82)
#define LVGL_DASHBOARD_POWER_BOX_HEIGHT     (106)
#define LVGL_DASHBOARD_METRIC_CARD_Y        (216)
#define LVGL_DASHBOARD_SCREEN_BG_HEX        (0xFFFFFFU)
#define LVGL_DASHBOARD_SCREEN_TEXT_HEX      (0x000000U)
#define LVGL_DASHBOARD_POWER_BG_HEX         (0xFFD54FU)
#define LVGL_DASHBOARD_VOLTAGE_BG_HEX       (0x4DD0E1U)
#define LVGL_DASHBOARD_CURRENT_BG_HEX       (0x4DD0E1U)
#define LVGL_DASHBOARD_RELAY_ON_BG_HEX      (0xA5D6A7U)
#define LVGL_DASHBOARD_RELAY_OFF_BG_HEX     (0xEF5350U)
#define LVGL_DASHBOARD_NETWORK_WIFI_BG_HEX  (0xCE93D8U)
#define LVGL_DASHBOARD_NETWORK_LTE_BG_HEX   (0x00897BU)
```

Declare helpers for stale detection, network text, safety text, bottom status text, state comparison, and formatting power/voltage/current/energy.

- [ ] **Step 3: Implement internal helpers**

Implement formatting with `snprintf()` and return `ESP_ERR_INVALID_ARG` for null or zero-length output buffers and `ESP_ERR_INVALID_SIZE` when output is truncated. Use formats `"%.2f W"`, `"%.1f V"`, `"%.3f A"`, and `"%.2f Wh"`.

Map network labels to `OFFLINE`, `CONNECTING`, `WIFI`, and `LTE`. Map safety labels to `SAFE`, `WARN`, `DANGER`, and `SAFETY ?` when invalid. `lvgl_dashboard_internal_should_apply_state()` returns true when there is no rendered state, stale state changed, or `memcmp()` finds a state difference.

- [ ] **Step 4: Build checkpoint**

Run: `idf.py build`

Expected: FAIL because `lvgl_dashboard.c` still does not exist. If the failure is in helper code, fix the exact type or include mismatch.

## Task 7: LVGL Dashboard Implementation

**Files:**
- Create: `main/display/lvgl/lvgl_dashboard.c`

- [ ] **Step 1: Create source skeleton and object state**

Include `lvgl_dashboard.h`, `lvgl_dashboard_internal.h`, `metering_service.h`, `relay.h`, `esp_check.h`, `esp_heap_caps.h`, `esp_log.h`, `esp_timer.h`, FreeRTOS headers, and `lvgl.h`.

Use defaults: task stack `6144`, priority `4`, tick period `10 ms`, update period `20 ms`. The object stores config, mutex, `dashboard_state_t state_cache`, LVGL display, task handle, done semaphore, tick timer, two draw buffers, widget pointers for voltage/current/power/energy/relay/network/safety, lifecycle flags, and whether this object called `lv_init()`.

- [ ] **Step 2: Implement create and cleanup**

`lvgl_dashboard_create()` validates `config` and `config->panel`, applies defaults, creates mutex and done semaphore, initializes LVGL if needed, creates an LVGL display sized from `tft_panel_get_width()` and `tft_panel_get_height()`, registers `tft_panel_register_flush_done_cb(panel, lvgl_dashboard_flush_ready, me)`, allocates two DMA-capable RGB565 buffers with `heap_caps_malloc(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)`, sets color format RGB565, sets display flush callback, creates the widget tree, creates an ESP timer for `lv_tick_inc()`, initializes `screen_enabled=true`, and returns the handle.

On failure, clear the TFT flush callback, delete timer/display, free buffers, delete semaphores/mutex, deinit LVGL only if this object initialized it, and free the handle.

- [ ] **Step 3: Implement event handlers and state updates**

`lvgl_dashboard_start()` registers handlers for `METERING_EVENT_SNAPSHOT`, `RELAY_EVENT_STATE_CHANGED`, and `SAFETY_GUARD_EVENT_SNAPSHOT`, starts the LVGL tick timer, sets `started=true`, sets `lvgl_task_running=true`, and creates the LVGL task. Repeated start returns `ESP_OK`.

Each event handler takes the dashboard mutex, updates only `state_cache`, releases the mutex, and returns. It must not call LVGL widget APIs.

Metering updates voltage/current/power/total energy/metering validity/last update. Relay updates relay state and known flag. Safety updates safety level and validity.

- [ ] **Step 4: Implement network polling and render task**

The LVGL task loop copies `state_cache` under mutex, polls `network_manager_get_status()` when `config.network_manager != NULL`, maps status to `DASHBOARD_NET_*`, applies widgets when state or stale flag changed, calls `lv_timer_handler()`, and delays by `update_period_ms`.

Network mapping:

```text
ready=true and active=WIFI -> DASHBOARD_NET_WIFI
ready=true and active=LTE -> DASHBOARD_NET_LTE
ready=false and any link STARTING/CONNECTING -> DASHBOARD_NET_CONNECTING
otherwise -> DASHBOARD_NET_OFFLINE
```

On task exit, signal the done semaphore and call `vTaskDelete(NULL)`.

- [ ] **Step 5: Implement widget tree and apply-state function**

Use one screen with the old project light theme: network pill, relay pill, large power card, voltage card, current card, and bottom energy/safety label. Use `lv_obj_create()`, `lv_label_create()`, `lv_obj_set_style_bg_color()`, `lv_obj_align()`, and `lv_label_set_text()` as in `reference/EEE532-Project/main/display/lvgl/lvgl_app.c`.

`lvgl_dashboard_apply_state()` formats power, voltage, current, and energy with internal helpers, sets network and relay text, colors pills by state, sets safety/bottom text, and reduces opacity when metering data is stale.

- [ ] **Step 6: Implement stop, destroy, and screen control**

`lvgl_dashboard_stop()` clears `lvgl_task_running`, waits for task done, stops the tick timer, unregisters event handlers, and sets `started=false`. Repeated stop returns `ESP_OK`.

`lvgl_dashboard_destroy(NULL)` returns `ESP_OK`. Non-null destroy calls stop, clears the TFT flush callback, deletes timer/display, frees buffers, deletes semaphores/mutex, deinitializes LVGL only if this object initialized it, and frees the handle. It does not destroy the borrowed `tft_panel_t` or `network_manager_t`.

`lvgl_dashboard_set_screen_enabled()` calls `tft_panel_set_backlight(panel, enabled)`, updates `state_cache.screen_enabled`, and returns the panel result.

- [ ] **Step 7: Build checkpoint**

Run: `idf.py build`

Expected: PASS, or fail with a concrete compiler/linker error in modules 9-12. Fix the first concrete error and rerun until the build passes.

## Task 8: Full Verification

**Files:**
- Inspect: all files created or modified in Tasks 1-7

- [ ] **Step 1: Static contract searches**

Run:

```bash
rg "relay_control_|local_button_|bl0942_service_" main
rg "#include \"board_pinmap.h\"" main/network main/safety main/display
rg "network_link_register_rx_cb\(.*NULL" main
```

Expected:

```text
No matches for old singleton API names.
No accidental board_pinmap dependency in modules 9-12.
Matches for callback clearing in network_manager cleanup are present.
```

- [ ] **Step 2: Run ESP-IDF build through MCP**

Use the ESP-IDF MCP build tool for the project.

Expected: PASS. If MCP cannot run dependency resolution or returns an environment error unrelated to C code, use Step 3.

- [ ] **Step 3: Fallback shell build if MCP cannot complete**

Run:

```bash
source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build
```

Expected: PASS. If it fails, capture the first compiler or linker error and fix that concrete error before rerunning.

- [ ] **Step 4: Final status check**

Run: `git status --short`

Expected: only intended project files, dependency lock files, and generated managed component files are changed. Do not revert unrelated user changes. Do not commit unless the user explicitly requests commits.

- [ ] **Step 5: Report verification scope**

Report one of these build-verification lines, followed by the hardware-verification line:

```text
Implemented modules 9-12 from docs/agents/classes.md.
Build verification: MCP ESP-IDF build passed.
Build verification: shell idf.py build passed.
Hardware verification: not flashed and not serial-validated in this pass.
```

## Self-Review Notes

Spec coverage:

1. `network_manager` is covered in Tasks 2 and 3, including monitor task, subscriptions, failover/failback, and RX bridge cleanup.
2. `safety_guard` is covered in Task 4, including thresholds, persistence, snapshots, and event publishing.
3. `tft_panel` and the custom ST7789T driver are covered in Task 5.
4. `lvgl_dashboard` is covered in Tasks 6 and 7, including LVGL task ownership, event handlers, network polling, and widget updates.
5. Build files, docs updates, and callback clearing are covered in Task 1.
6. Build and static verification are covered in Task 8.

Plan scan: no unresolved fill-in markers are intentionally present in this plan.

Type consistency: public names match the approved design and `docs/agents/classes.md` after Task 1 updates.
