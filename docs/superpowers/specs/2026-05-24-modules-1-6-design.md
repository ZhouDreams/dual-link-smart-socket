# Modules 1-6 Implementation Design

## Scope

Implement the first six modules defined in `docs/agents/classes.md` for the current Smart_Socket application:

1. `board_pinmap`
2. `network_types`
3. `relay`
4. `button`
5. `bl0942`
6. `network_link`

The implementation will create the module directories and source files under `main/`, update `main/CMakeLists.txt`, and add `main/idf_component.yml` for the `espressif/button` dependency. The existing `main/main.c` remains a minimal entry point; application wiring belongs to the later `app_controller` module.

This work does not implement `wifi_link`, `lte_link`, `network_manager`, `metering_service`, `thingsboard_client`, display modules, or `app_controller`.

## Reference Inputs

The authoritative API contract is `docs/agents/classes.md`. Architecture and dependency constraints come from `docs/agents/architecture.md`, `docs/agents/directory-structure.md`, `docs/agents/coding-style.md`, `docs/agents/oop-design.md`, and `docs/agents/err.md`.

The old working project at `reference/EEE532-Project` is used only as a validated hardware and implementation reference. Its pinmap and proven BL0942/button implementation details are reused where they do not conflict with the new module API.

## Approach

Use the current `classes.md` API exactly, while borrowing validated hardware constants and protocol details from the old project. This keeps the new architecture clean and avoids carrying over old singleton-style APIs such as `relay_control_*`, `local_button_*`, and `bl0942_service_*`.

Rejected alternatives:

1. Directly rename and migrate old modules: faster, but conflicts with the new opaque-handle design and module names.
2. Stub out APIs for compile-only success: fast, but does not satisfy the requested implementation and wastes already validated reference code.

## File Layout

Add these files:

```text
main/platform/board_pinmap.h
main/platform/board_pinmap.c
main/network/network_types.h
main/network/network_link.h
main/network/network_link.c
main/network/network_link_priv.h
main/relay/relay.h
main/relay/relay.c
main/button/button.h
main/button/button.c
main/bl0942/bl0942.h
main/bl0942/bl0942.c
main/idf_component.yml
```

Update `main/CMakeLists.txt` so the new `.c` files are compiled and their include directories are visible to the application component.

## Board Pinmap

`board_pinmap` is a read-only singleton implemented with `static const board_pinmap_t s_pinmap` in `board_pinmap.c`. The only public function is `board_pinmap_get()`.

Pin values use the old project's known-good hardware allocation, plus the ESP32-S3-LCD-1.47B board-fixed TFT pins:

| Signal | GPIO | Notes |
|---|---:|---|
| Button input | GPIO2 | Active low, internal pull-up expected by button module |
| Relay control | GPIO4 | Active high |
| LTE EN | GPIO5 | Reserved for later LTE link |
| LTE ESP TX | GPIO6 | ESP TX to LTE RX |
| LTE ESP RX | GPIO7 | ESP RX from LTE TX |
| BL0942 EN | GPIO8 | Power enable and hard reset |
| BL0942 ESP TX | GPIO10 | ESP TX to BL0942 RX/SDI |
| BL0942 ESP RX | GPIO11 | ESP RX from BL0942 TX/SDO |
| TFT SCLK | GPIO40 | Board-fixed |
| TFT MOSI | GPIO45 | Board-fixed |
| TFT DC | GPIO41 | Board-fixed |
| TFT CS | GPIO42 | Board-fixed |
| TFT RST | GPIO39 | Board-fixed |
| TFT BL | GPIO46 | Board-fixed |

The module depends only on `driver/gpio.h` for `gpio_num_t` and exposes no mutable state.

## Network Types

`network_types.h` is a pure header with no `.c` file and no ESP-IDF dependency. It includes only standard C headers needed by the public types: `<stdbool.h>`, `<stddef.h>`, and `<stdint.h>`.

It defines:

1. `network_link_type_t`
2. `network_link_status_t`
3. `network_mqtt_qos_t`
4. `network_publish_request_t`
5. `network_rx_data_t`
6. `network_rx_cb_t`

The payload type remains `const void *` so upper layers can publish JSON or binary payloads without the network layer knowing their semantics.

## Relay

`relay` is an opaque-handle driver adapter. `relay_create()` allocates `relay_t`, validates config, creates a mutex, configures the output GPIO, drives the relay to logical off, and returns a fully initialized handle. On any failure, it releases resources and returns `NULL`.

`relay_set()`, `relay_toggle()`, and `relay_get()` validate arguments and state. `relay_set()` and `relay_toggle()` hold the instance mutex across the read-modify-write sequence and GPIO write, then post state-change events after releasing the mutex. This avoids deadlock and avoids calling `esp_event_post()` while holding the lock.

State-change event behavior:

1. Event base: `RELAY_EVENT_BASE`.
2. Event id: `RELAY_EVENT_STATE_CHANGED`.
3. Payload: `relay_state_changed_event_t` containing the new state and operation source.
4. Events are posted only when the logical state changes.
5. Event post failure is logged as a warning and does not change the return value of the successful GPIO operation.

The relay module does not include or call `board_pinmap`; the future `app_controller` injects `gpio_num_t` and active level through `relay_config_t`.

## Button

`button` is an opaque-handle adapter over the `espressif/button` component. The project will declare `espressif/button` version `^4.1` in `main/idf_component.yml`, matching the old project's ESP-IDF v6-compatible implementation.

`button_create()` allocates `button_t`, validates config, creates an internal mutex, creates an `iot_button` GPIO device with `iot_button_new_gpio_device()`, and registers internal callbacks for:

1. `BUTTON_SINGLE_CLICK`
2. `BUTTON_DOUBLE_CLICK`
3. `BUTTON_LONG_PRESS_START`
4. `BUTTON_LONG_PRESS_HOLD`

`button_register_cb()` updates the callback slot for a single project-level event. Re-registering the same event replaces the previous callback. Passing `NULL` clears the callback.

The iot_button callback reads the current callback slot under the mutex, releases the mutex, then calls the user callback. This avoids executing user code while holding module state locks. Callbacks run in the iot_button task context and must remain short and non-blocking.

`button_destroy()` deletes the underlying iot_button handle with `iot_button_delete()`, deletes the mutex, and frees the `button_t` handle. It is safe when passed `NULL`.

The button module does not directly control relay or display state.

## BL0942

`bl0942` is an opaque-handle UART driver adapter. It uses the current `classes.md` API and structure rather than the old singleton API.

`bl0942_create()` validates config, allocates `bl0942_t`, creates mutex and task-done semaphore, configures EN GPIO when present, power-cycles the module through EN, installs the UART driver, configures UART parameters, sets UART pins, initializes the latest-measurement cache, and returns a fully initialized handle. It uses `config->baud_rate` directly and does not perform the old project's boot-baud to operating-baud switch.

Frame handling uses the old project's proven protocol details:

1. Full parameter frame size: 23 bytes.
2. Expected frame header: `0x55`.
3. Packet read address: `0xAA`.
4. Read command: `0x58 | (device_address & 0x03)`.
5. Checksum: bitwise-not of the low byte of the command plus frame bytes excluding the checksum byte.
6. Raw fields are decoded as little-endian 24-bit or signed 24-bit values as appropriate.

`bl0942_read()` is synchronous and serialized with the internal sampling task by the instance mutex. It flushes UART input, sends the two-byte packet-read command, waits for TX completion, reads one full frame, validates header and checksum, parses the measurement, updates `latest`, marks it valid, and returns the parsed snapshot.

`bl0942_start()` creates a periodic sampling task if not already running. The task loops every `sample_period_ms`, calls `bl0942_read()`, posts `BL0942_EVENT_MEASUREMENT` on success, and resets failure counters.

Fault behavior:

1. Each failed read increments `consecutive_failures`.
2. When the count reaches `fault_threshold`, the task posts `BL0942_EVENT_FAULT` with `bl0942_fault_info_t`.
3. If EN GPIO is valid and `hard_reset_count` is below `hard_reset_max_attempts`, the task power-cycles the module and rebuilds the UART driver.
4. If hard reset attempts are exhausted, the task stops itself and signals `sample_task_done_sema`.

`bl0942_stop()` requests task exit and waits on `sample_task_done_sema`. `bl0942_destroy()` stops the task, deletes the UART driver, deletes synchronization primitives, and frees the handle.

The module emits raw register values only. Engineering-unit conversion belongs to later `metering_service`.

## Network Link

`network_link` implements the base class for future Wi-Fi and LTE link subclasses.

Public header `network_link.h` exposes only:

1. `typedef struct network_link network_link_t;`
2. wrapper functions such as `network_link_start()` and `network_link_publish()`
3. `network_link_get_type()`

Private header `network_link_priv.h` defines:

1. `network_link_ops_t`
2. `struct network_link { const network_link_ops_t *ops; network_link_type_t type; }`

Only future subclasses such as `wifi_link` and `lte_link` include `network_link_priv.h`. `network_manager` and business modules include only `network_link.h`.

Wrapper behavior:

1. Invalid arguments return `ESP_ERR_INVALID_ARG`.
2. Missing ops entries return `ESP_ERR_NOT_SUPPORTED`, matching `docs/agents/err.md`.
3. Valid calls delegate to the ops table.
4. `network_link_get_type(NULL)` returns `NETWORK_LINK_TYPE_NONE`; valid handles return `me->type` directly.

`network_link_get_status()` has a `const network_link_t *` public signature. Internally it validates the handle and delegates to the non-const ops signature used by `classes.md`; the implementation treats this as a status read and does not mutate base-class fields.

## Error Handling

All public APIs return `esp_err_t` except direct handle-returning `create` functions and value getter `network_link_get_type()`. Resource-owning create paths use cleanup-on-error and return no partially initialized handles.

`destroy()` functions accept `NULL` and return `ESP_OK`, consistent with the current `classes.md` signatures. If a lower-level cleanup operation fails during destroy, the module logs a warning and continues releasing remaining resources when it is safe to do so.

Ordinary module code does not call `ESP_ERROR_CHECK`; errors are returned to the caller. Frequent sampling paths avoid `ESP_LOGI` to prevent log flooding.

## Concurrency

`relay` and `button` use instance-level mutexes for handle state. `bl0942` uses an instance mutex to serialize UART access and latest-measurement updates, plus a done semaphore for orderly task shutdown.

No public API relies on global mutable singleton state, except for ESP-IDF event bases defined once per module as required by `esp_event`.

## Dependency Rules

`relay`, `button`, and `bl0942` do not include `board_pinmap.h`. They accept concrete GPIO and UART settings through their config structures. This keeps hardware mapping in `board_pinmap` and future application assembly in `app_controller`.

`network_types.h` is dependency-free from ESP-IDF. `network_link` depends only on `network_types.h` and `esp_err.h` publicly, with private ops internals hidden in `network_link_priv.h`.

## Verification Plan

Verification will proceed in this order:

1. Static contract review against `docs/agents/classes.md`.
2. ESP-IDF build using the available ESP-IDF MCP build tool first.
3. If MCP build cannot complete dependency resolution or compilation, use the ESP-IDF shell workflow from `docs/agents/build-and-debug.md` as a fallback.

Default verification stops at build success. Flashing and serial-log hardware validation are separate follow-up actions.
