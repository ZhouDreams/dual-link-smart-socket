# Modules 9-12 Implementation Design

## Scope

Implement modules 9 through 12 from `docs/agents/classes.md`:

1. `network_manager`
2. `safety_guard`
3. `tft_panel`
4. `lvgl_dashboard`

The implementation will add module files under `main/network/`, `main/safety/`, and `main/display/`, update `docs/agents/classes.md` for required interface fields, update `main/CMakeLists.txt`, add the `lvgl/lvgl` dependency to `main/idf_component.yml`, and make one safety fix to `network_link_register_rx_cb()` so `NULL` clears an existing callback.

This work does not implement `lte_link`, `thingsboard_client`, `app_controller`, or full system assembly. `main/main.c` remains a minimal entry point.

## Reference Inputs

The authoritative API contract is `docs/agents/classes.md`, sections 9 through 12. Architecture and dependency constraints come from `docs/agents/architecture.md`, `docs/agents/directory-structure.md`, `docs/agents/coding-style.md`, `docs/agents/oop-design.md`, and `docs/agents/err.md`.

The old project under `reference/EEE532-Project` is used as a validated display and implementation reference. Its TFT/LVGL implementation has been run successfully on the target board. The relevant reference files are:

1. `reference/EEE532-Project/main/display/tft/tft_panel.c`
2. `reference/EEE532-Project/main/display/tft/tft_panel_st7789t.c`
3. `reference/EEE532-Project/main/display/lvgl/lvgl_app.c`
4. `reference/EEE532-Project/main/display/lvgl/lvgl_app_internal.c`

The old singleton APIs and old AI/risk business flow are not copied. Only hardware-proven display constants, ST7789T initialization sequence, LVGL 9 patterns, and small pure formatting ideas are reused where they fit the new handle-based architecture.

## Approach

Use the current opaque-handle module boundaries from `classes.md`, while making the minimum necessary contract updates for real hardware operation.

Recommended approach:

1. Implement `network_manager` as a handle object over existing `network_link_t *` objects, with a monitor task that polls link states for failover and failback.
2. Implement `safety_guard` as a pure rule module that consumes `METERING_EVENT_SNAPSHOT` and publishes safety snapshots without directly operating relay hardware.
3. Implement `tft_panel` as a handle-based ESP-IDF `esp_lcd` adapter for the board-proven ST7789T panel.
4. Implement `lvgl_dashboard` as a LVGL 9 dashboard task that owns LVGL widgets, subscribes to business events, and borrows a `network_manager_t *` only for status polling.

Rejected alternatives:

1. Strictly keep `classes.md` unchanged: avoids documentation updates but leaves `lvgl_dashboard` unable to poll `network_manager_get_status()` and makes asynchronous TFT flush less reliable.
2. Stub the display path: compiles faster but does not satisfy the requested real LVGL/TFT implementation.
3. Directly migrate old singleton modules: faster for display, but conflicts with the new opaque-handle API, ownership model, and event/data-flow rules.

## Contract Updates

Update `docs/agents/classes.md` in two places:

1. Add a TFT flush completion callback type and registration API:

```c
typedef void (*tft_panel_flush_done_cb_t)(void *user_ctx);

esp_err_t tft_panel_register_flush_done_cb(tft_panel_t *me,
                                           tft_panel_flush_done_cb_t cb,
                                           void *user_ctx);
```

Keep `tft_panel_config_t` as the hardware configuration object:

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

The callback is registered after `tft_panel_create()` because `lvgl_dashboard` borrows an already-created panel handle. This avoids a circular construction dependency where a TFT config would need a LVGL display pointer before `lvgl_dashboard_create()` exists.

2. Add a borrowed network manager pointer to `lvgl_dashboard_config_t`:

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

`lvgl_dashboard` does not own `network_manager_t`. It only calls `network_manager_get_status()` in its own task. `app_controller` remains responsible for creating, starting, stopping, and destroying the network manager.

3. Clarify callback clearing for `network_link_register_rx_cb()`:

```c
esp_err_t network_link_register_rx_cb(network_link_t *me,
                                      network_rx_cb_t cb, void *ctx);
```

Passing `cb == NULL` clears the previously registered callback. This is required because `network_manager` borrows link objects and must remove its bridge callback during destroy to avoid dangling callbacks if the borrowed links are reused later.

## File Layout

Add these files:

```text
main/network/network_manager.h
main/network/network_manager.c
main/safety/safety_guard.h
main/safety/safety_guard.c
main/display/tft/tft_panel.h
main/display/tft/tft_panel.c
main/display/tft/tft_panel_st7789t.h
main/display/tft/tft_panel_st7789t.c
main/display/lvgl/lvgl_dashboard.h
main/display/lvgl/lvgl_dashboard.c
main/display/lvgl/lvgl_dashboard_internal.h
main/display/lvgl/lvgl_dashboard_internal.c
```

Update these files:

```text
docs/agents/classes.md
main/network/network_link.c
main/CMakeLists.txt
main/idf_component.yml
```

`main/CMakeLists.txt` will compile all new `.c` files and expose `main/safety`, `main/display/tft`, and `main/display/lvgl` as include directories. `main/idf_component.yml` will add `lvgl/lvgl` version `^9.0.0`, matching the old working project.

## Network Manager Public API

`network_manager.h` exposes:

1. `typedef struct network_manager network_manager_t;`
2. `network_manager_config_t`
3. `network_manager_status_t`
4. `network_manager_create()` / `destroy()`
5. `network_manager_start()` / `stop()`
6. `network_manager_get_status()`
7. `network_manager_is_ready()`
8. `network_manager_publish()`
9. `network_manager_subscribe()` / `unsubscribe()`
10. `network_manager_register_rx_cb()`

The manager receives already-created `network_link_t *` handles. It does not include `wifi_link.h` or `lte_link.h`, and it does not create concrete link subclasses.

## Network Manager Object Model

`struct network_manager` contains the fields listed in `classes.md` plus monitor-task shutdown support:

1. `TaskHandle_t monitor_task`
2. `SemaphoreHandle_t monitor_task_done_sema`
3. `bool monitor_task_running`

The manager owns its subscription intent table and mutex. It does not own `primary` or `backup` link objects. `network_manager_destroy()` stops the manager and frees manager-owned resources, but it does not call `network_link_destroy()` on injected links.

## Network Manager Link Selection

`start()` starts the primary link first. If primary start fails and backup exists, it starts backup and sets `active` to backup if backup can be used. If both start attempts fail, `start()` returns the first meaningful error.

The monitor task runs every `failover_recheck_ms` and reads both link statuses through `network_link_get_status()`.

Failover behavior:

1. If `active` is primary and primary is not usable, check backup.
2. If backup is `READY` or `DEGRADED`, switch `active` to backup.
3. After switching, replay all active subscription intents on the new active link.
4. If backup is not started and backup exists, best-effort start it before later checks.

Failback behavior:

1. If `active` is backup and primary is `READY`, record `failback_since_us` on the first ready observation.
2. Only switch back after primary remains `READY` for `failback_delay_ms`.
3. Reset `failback_since_us` if primary leaves `READY` before the delay expires.
4. Replay subscriptions after switching back to primary.

Usability rules:

1. `READY` means the link can publish immediately.
2. `DEGRADED` means the physical link may be up but MQTT is not ready, so `publish()` still returns `ESP_ERR_INVALID_STATE` if active is not ready.
3. `DEGRADED` is acceptable as a failover candidate only to avoid staying on a hard `ERROR` link; manager readiness remains false until the selected active link becomes `READY`.

## Network Manager Subscriptions and RX

`subscribe()` validates topic and QoS, stores or updates the subscription intent in the manager table, then calls `network_link_subscribe()` on the current active link when present. If no active link exists, storing the intent can still return `ESP_OK`; the monitor task will replay it when a link becomes active.

`unsubscribe()` removes the subscription intent and best-effort calls `network_link_unsubscribe()` on both primary and backup when they exist. This prevents stale subscriptions from surviving a later active-link switch.

`register_rx_cb()` stores one callback and user context. The manager registers an internal bridge callback on both links. The bridge forwards messages to the user callback only when the message comes from the currently active link. The callback is invoked without holding the manager mutex.

`network_manager_destroy()` clears the bridge callback on any borrowed link by calling `network_link_register_rx_cb(link, NULL, NULL)` before freeing the manager. This requires `network_link_register_rx_cb()` to accept `NULL` as a callback-clear operation.

## Safety Guard Public API

`safety_guard.h` exposes:

1. `typedef struct safety_guard safety_guard_t;`
2. `safety_guard_config_t`
3. `safety_guard_level_t`
4. `safety_guard_event_t`
5. `safety_guard_action_t`
6. `safety_guard_snapshot_t`
7. `SAFETY_GUARD_EVENT_BASE` and `safety_guard_event_id_t`
8. `safety_guard_create()` / `destroy()`
9. `safety_guard_start()` / `stop()`
10. `safety_guard_get_latest()`
11. `safety_guard_set_thresholds()`

Defaults applied during create:

1. `overcurrent_threshold_a <= 0` becomes `10.0f`.
2. `overpower_threshold_w <= 0` becomes `2200.0f`.
3. `persistence_samples <= 0` becomes `3`.

## Safety Guard Rules

`start()` registers a handler for `METERING_EVENT_SNAPSHOT`. `stop()` unregisters it.

For each valid metering snapshot:

1. `current > overcurrent_threshold_a` increments `overcurrent_persistence`; otherwise it resets to zero.
2. `power > overpower_threshold_w` increments `overpower_persistence`; otherwise it resets to zero.
3. If either counter is non-zero but below `persistence_samples`, publish `WARNING` with the matching event and `ACTION_NONE`.
4. If either counter reaches `persistence_samples`, publish `DANGER` with the matching event and `ACTION_RELAY_OFF`.
5. If both rules are active, overcurrent wins when its persistence count is greater than or equal to overpower; otherwise overpower wins.
6. A normal snapshot resets both counters and publishes `NORMAL`, `EVENT_NONE`, `ACTION_NONE`.

For invalid metering snapshots, publish a safety snapshot with `valid=false`, `level=WARNING`, `event=NONE`, and `suggested_action=NONE`. The guard remains started.

`safety_guard_set_thresholds()` updates thresholds under mutex and resets persistence counters so old rule streaks do not trip under the new thresholds.

`safety_guard` does not include `relay.h` and does not operate relay hardware. The suggested action is consumed later by `app_controller`.

## TFT Panel Public API

`tft_panel.h` exposes:

1. `typedef struct tft_panel tft_panel_t;`
2. `tft_panel_flush_done_cb_t`
3. `tft_panel_config_t`
4. `tft_panel_create()` / `destroy()`
5. `tft_panel_register_flush_done_cb()`
6. `tft_panel_draw_bitmap()`
7. `tft_panel_set_backlight()`
8. `tft_panel_get_width()` / `get_height()`

The config is injected by `app_controller` from `board_pinmap_t`; `tft_panel` does not include `board_pinmap.h`.

## TFT Panel Hardware Implementation

The implementation uses ESP-IDF `esp_lcd` and a custom ST7789T panel driver adapted from the old working project.

Board-proven defaults:

1. Panel size: `172x320`
2. SPI host: `SPI3_HOST`
3. Pixel clock: `12 MHz`
4. Buffer-line sizing basis: `20` lines
5. Backlight active level: high
6. RGB order: BGR
7. Bits per pixel: `16`
8. Mirror: X true, Y false
9. Color inversion: true
10. Gap: `x_gap=34`, `y_gap=0`

`tft_panel_create()` validates GPIO and size, allocates the handle, configures backlight off, initializes SPI bus, creates panel IO, creates the ST7789T panel, resets and initializes it, applies mirror/invert/gap/display-on settings, enables backlight, then returns a fully initialized handle.

`tft_panel_register_flush_done_cb()` stores the callback and context under the panel mutex. The SPI color-transfer completion callback invokes this registered callback after a bitmap transfer completes. Passing `NULL` clears the callback.

`draw_bitmap()` validates bounds against `panel_width` and `panel_height`, then forwards to `esp_lcd_panel_draw_bitmap()`.

`destroy()` disables backlight, turns panel output off, deletes panel, deletes panel IO, frees the SPI bus, resets the backlight GPIO, and frees the handle. Cleanup is best-effort and preserves the first cleanup error as the return value.

## ST7789T Driver

`tft_panel_st7789t.c` implements a private `esp_lcd_panel_t` compatible driver with the old project initialization sequence.

It supports:

1. `del`
2. `reset`
3. `init`
4. `draw_bitmap`
5. `invert_color`
6. `mirror`
7. `swap_xy`
8. `set_gap`
9. `disp_on_off`

The init sequence includes sleep-out, MADCTL, COLMOD, RAMCTRL, porch/power/gamma commands, inversion on, display on, and RAM write. This preserves the board-proven behavior from the old project.

## LVGL Dashboard Public API

`lvgl_dashboard.h` exposes:

1. `typedef struct lvgl_dashboard lvgl_dashboard_t;`
2. `dashboard_network_t`
3. `dashboard_state_t`
4. `lvgl_dashboard_config_t`
5. `lvgl_dashboard_create()` / `destroy()`
6. `lvgl_dashboard_start()` / `stop()`
7. `lvgl_dashboard_set_screen_enabled()`

The dashboard borrows `tft_panel_t *panel` and `network_manager_t *network_manager`. It does not own either object.

## LVGL Dashboard Internals

`lvgl_dashboard_create()` initializes LVGL if not already initialized, creates a LVGL display sized from `tft_panel_get_width()` and `tft_panel_get_height()`, registers a TFT flush-done callback with `tft_panel_register_flush_done_cb()`, allocates two DMA-capable RGB565 draw buffers, registers the LVGL display flush callback, builds the widget tree, and creates the LVGL tick timer.

Default config values:

1. `lvgl_task_stack <= 0` becomes `6144`.
2. `lvgl_task_priority <= 0` becomes `4`.
3. `lvgl_tick_period_ms == 0` becomes `10`.
4. `update_period_ms == 0` becomes `20`.

`start()` registers event handlers for:

1. `METERING_EVENT_SNAPSHOT`
2. `RELAY_EVENT_STATE_CHANGED`
3. `SAFETY_GUARD_EVENT_SNAPSHOT`

It then starts the tick timer and creates the LVGL task. Repeated `start()` returns `ESP_OK`.

`stop()` requests task exit, waits on a done semaphore, stops the tick timer, and unregisters event handlers. Repeated `stop()` returns `ESP_OK`.

`destroy()` calls `stop()`, deletes the tick timer, deletes the LVGL display, frees draw buffers, deletes semaphores, and frees the handle. It does not call `tft_panel_destroy()` and does not call `network_manager_destroy()`.

## LVGL Dashboard Data Flow

Business event handlers only update `state_cache` under the dashboard mutex. They never call LVGL widget APIs.

Event mapping:

1. `METERING_EVENT_SNAPSHOT` updates voltage, current, power, total energy, metering validity, and `last_update_us`.
2. `RELAY_EVENT_STATE_CHANGED` updates `relay_on` and `relay_known`.
3. `SAFETY_GUARD_EVENT_SNAPSHOT` updates `safety_level` and `safety_valid`.

In the LVGL task loop:

1. Copy `state_cache` under mutex.
2. If `network_manager` is not `NULL`, call `network_manager_get_status()`.
3. Map manager status to `dashboard_network_t` and `network_ready`.
4. Apply the copied state to widgets if it changed or stale status changed.
5. Call `lv_timer_handler()`.
6. Delay until the next `update_period_ms` tick.

Network mapping:

1. `ready=false` and any link `STARTING` or `CONNECTING`: `DASHBOARD_NET_CONNECTING`.
2. `ready=true` and active link `NETWORK_LINK_TYPE_WIFI`: `DASHBOARD_NET_WIFI`.
3. `ready=true` and active link `NETWORK_LINK_TYPE_LTE`: `DASHBOARD_NET_LTE`.
4. Otherwise: `DASHBOARD_NET_OFFLINE`.

## LVGL Widget Layout

Use the old project's proven lightweight single-screen layout, adapted to the new smart-socket state:

1. Status pill: network state
2. Status pill: relay state
3. Large card: active power
4. Two small cards: voltage and current
5. Small text line: energy and safety state

The visual style remains the board-verified light theme from the old project: white background, high-contrast black text, yellow main power card, cyan metric cards, green/red relay status, and purple/teal network status.

Formatting helpers live in `lvgl_dashboard_internal.c` so pure string/state logic can be tested separately later without LVGL hardware.

## Error Handling

All ordinary public APIs return `esp_err_t`, except handle-returning `create()` functions and simple `tft_panel_get_width()` / `get_height()` value getters.

Rules:

1. `create()` returns `NULL` on validation or allocation failure and rolls back partial resources.
2. `destroy(NULL)` returns `ESP_OK`.
3. Repeated `start()` and `stop()` return `ESP_OK`.
4. Invalid arguments return `ESP_ERR_INVALID_ARG`.
5. Operations requiring started or initialized state return `ESP_ERR_INVALID_STATE` when that state is absent.
6. Event post and best-effort cleanup failures are logged with `ESP_LOGW` and do not prevent remaining cleanup.

No ordinary module code uses `ESP_ERROR_CHECK`; errors are returned to the caller.

## Concurrency and Ownership

Each handle object owns its own mutex or synchronization primitives.

Ownership summary:

1. `network_manager` owns manager state, monitor task, and subscription table. It borrows `primary` and `backup` links.
2. `safety_guard` owns its thresholds, persistence counters, latest snapshot, event handler registration, and mutex.
3. `tft_panel` owns SPI/LCD/backlight resources created from its config.
4. `lvgl_dashboard` owns LVGL task, tick timer, draw buffers, LVGL display/widgets, and event handler registrations. It borrows `tft_panel_t *` and `network_manager_t *`.

Task shutdown uses flags plus done semaphores. Tasks are not force-deleted while running.

Callbacks and event handlers do not invoke user callbacks or LVGL APIs while holding module mutexes.

## Dependencies

`network_manager` depends on:

1. `network_link.h`
2. `network_types.h`
3. FreeRTOS task, semaphore, and mutex APIs
4. `esp_timer` for failback timing

`safety_guard` depends on:

1. `metering_service.h`
2. `esp_event`
3. FreeRTOS mutex APIs
4. `esp_timer` for timestamps when needed

`tft_panel` depends on:

1. `driver/gpio.h`
2. `driver/spi_master.h`
3. `esp_lcd` headers
4. FreeRTOS delay APIs for panel reset/init timing

`lvgl_dashboard` depends on:

1. `tft_panel.h`
2. `network_manager.h`
3. `metering_service.h`
4. `relay.h`
5. `safety_guard.h`
6. `lvgl/lvgl`
7. `esp_timer`
8. `esp_heap_caps`
9. FreeRTOS task and semaphore APIs

## Verification Plan

Verification will proceed in this order:

1. Update `classes.md` and inspect the API contract against this spec.
2. Implement pure formatting/state helpers first where useful.
3. Implement each module and update build files.
4. Run ESP-IDF build through the available ESP-IDF MCP build tool.
5. If MCP build cannot resolve dependencies or compile, use the shell workflow in `docs/agents/build-and-debug.md` as fallback.
6. Fix compile or dependency errors until build succeeds.

Build verification is required before claiming implementation completion. Flashing and real-screen validation are follow-up actions unless explicitly requested.
