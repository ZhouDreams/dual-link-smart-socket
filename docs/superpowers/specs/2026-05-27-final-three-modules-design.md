# Final Three Modules Design

Date: 2026-05-27

## Scope

Implement the final three modules defined in `docs/agents/classes.md`:

1. `thingsboard_client`: ThingsBoard cloud semantics over `network_manager`.
2. `lte_link`: LTE `network_link_t` subclass backed by the local `esp-lwlte` component.
3. `app_controller`: application orchestration layer that wires local control, cloud control, safety actions, telemetry, and display behavior.

The implementation also includes complete `main.c` object assembly, build-system integration, ESP-IDF 6.0 alignment, and host-side tests for pure logic helpers.

## Chosen Approach

Use a minimal complete closed loop:

1. Implement real production logic for all three modules.
2. Keep configuration in `main.c` macros for now, rather than adding Kconfig.
3. Add `../esp-lwlte/src` as an ESP-IDF extra component directory.
4. Keep module boundaries aligned with the existing architecture and C OOP rules.
5. Add host tests for deterministic pure logic without simulating FreeRTOS or ESP-IDF event loops.

This approach gives a compilable and explainable system slice while avoiding premature configuration and persistence work.

## Module Boundaries

`thingsboard_client` belongs to the business service layer. It depends on `network_manager`, `network_types`, and public value types such as `safety_guard_level_t`. It does not include or call `wifi_link` or `lte_link` directly.

`lte_link` belongs to the network abstraction layer. It implements the existing `network_link_ops_t` interface and returns `network_link_t *` from `lte_link_create()`. It owns its `lwlte_t *` facade handle, copied string configuration, subscription table, mutex, and RX callback slot.

`app_controller` belongs to the application orchestration layer. It borrows all module handles from `main.c`, registers callbacks and ESP-IDF event handlers, and controls start/stop order. It does not create bottom-layer objects.

`main.c` is the composition root. It initializes NVS, `esp_netif`, and the default event loop, creates module objects from `board_pinmap` and configuration macros, creates `app_controller`, and starts it.

## ThingsBoard Client Design

Public files:

1. `main/thingsboard/thingsboard_client.h`
2. `main/thingsboard/thingsboard_client.c`

Internal pure helper files for tests:

1. `main/thingsboard/thingsboard_client_internal.h`
2. `main/thingsboard/thingsboard_client_internal.c`

The public API follows `classes.md`:

1. `thingsboard_client_create()` copies or stores configuration, allocates a mutex, and preallocates `json_buf`.
2. `thingsboard_client_start()` subscribes to RPC and attribute topics as configured and registers a single `network_manager` RX callback.
3. `thingsboard_client_stop()` unsubscribes best-effort from configured topics and clears the `network_manager` RX callback.
4. Publish helpers build JSON into the preallocated buffer and call `network_manager_publish()`.
5. The RX callback parses only the project-supported ThingsBoard RPC schema and calls the registered semantic command callback outside the mutex.

Supported topics:

1. Telemetry publish: `v1/devices/me/telemetry`
2. Attribute publish: `v1/devices/me/attributes`
3. RPC subscribe: `v1/devices/me/rpc/request/+`
4. RPC response publish: `v1/devices/me/rpc/response/{request_id}`

Supported RPC methods:

1. `setRelay`: parses a boolean parameter into `TB_COMMAND_SET_RELAY`.
2. `getPowerLimit`: parses request id only into `TB_COMMAND_GET_POWER_LIMIT`.
3. `setPowerLimit`: parses a numeric parameter into `TB_COMMAND_SET_POWER_LIMIT`.

RPC parsing uses a small schema-specific parser in `thingsboard_client_internal` rather than `cJSON`. This keeps host tests dependency-light. The parser is not a general JSON parser; it accepts the fixed ThingsBoard RPC shapes used by this project and rejects malformed payloads with `ESP_ERR_INVALID_RESPONSE`.

## LTE Link Design

Public files:

1. `main/network/lte/lte_link.h`
2. `main/network/lte/lte_link.c`

Internal pure helper files for tests:

1. `main/network/lte/lte_link_internal.h`
2. `main/network/lte/lte_link_internal.c`

`lte_link_create()` validates configuration, allocates the object, initializes `base.ops` and `base.type`, copies string configuration, allocates a subscription table, initializes `esp-lwlte` through `lwlte_air780ep_init()`, and registers a `lwlte_event_callback_t` bridge.

`lte_link_start()` submits `lwlte_connect()`. If MQTT is enabled, it also submits `lwlte_mqtt_start()`. Both APIs are asynchronous request submissions; link readiness is observed via `get_status()` and lwlte events.

`lte_link_stop()` stops MQTT when enabled, disconnects LTE, and preserves allocated resources so the link can be started again.

`lte_link_publish()` requires MQTT enabled and connected, then delegates to `lwlte_mqtt_publish()`.

`lte_link_subscribe()` and `lte_link_unsubscribe()` maintain the local subscription table. If MQTT is connected, they also call `lwlte_mqtt_subscribe()` or `lwlte_mqtt_unsubscribe()`. When `LWLTE_EVENT_MQTT_CONNECTED` arrives, the link replays all stored subscriptions.

Status mapping is centralized in `lte_link_internal`:

1. `LWLTE_STATE_STOPPED` maps to `NETWORK_LINK_STATUS_IDLE`.
2. `LWLTE_STATE_STARTING` maps to `NETWORK_LINK_STATUS_STARTING`.
3. `LWLTE_STATE_READY` or `LWLTE_STATE_NET_ACTIVATING` maps to `NETWORK_LINK_STATUS_CONNECTING`.
4. `LWLTE_STATE_ONLINE` plus MQTT connected maps to `NETWORK_LINK_STATUS_READY`.
5. `LWLTE_STATE_ONLINE` plus MQTT not connected maps to `NETWORK_LINK_STATUS_DEGRADED`.
6. `LWLTE_STATE_ERROR` or query failure maps to `NETWORK_LINK_STATUS_ERROR`.

The LTE module does not understand ThingsBoard topics or business telemetry fields.

## App Controller Design

Public files:

1. `main/app/app_controller.h`
2. `main/app/app_controller.c`

Internal pure helper files for tests:

1. `main/app/app_controller_internal.h`
2. `main/app/app_controller_internal.c`

The controller stores borrowed handles and event handler instances. `app_controller_start()` registers callbacks before starting modules, then starts modules in dependency order:

1. `bl0942_start()`
2. `metering_service_start()`
3. `safety_guard_start()`
4. `network_manager_start()`
5. `thingsboard_client_start()`
6. `lvgl_dashboard_start()`

`app_controller_stop()` stops modules in reverse order and unregisters event handlers. The existing button API has no unregister function. To avoid expanding the button module, button callbacks check the app `started` state before acting.

Callbacks:

1. Button single click calls `relay_toggle(..., RELAY_SOURCE_LOCAL_BUTTON)`.
2. Button long press toggles a controller-owned `screen_enabled` flag and calls `lvgl_dashboard_set_screen_enabled()`.
3. Safety snapshot with `SAFETY_GUARD_ACTION_RELAY_OFF` calls `relay_set(..., RELAY_SOURCE_SAFETY, false)`.
4. Metering snapshot builds `tb_telemetry_input_t` from metering, relay, network, and safety state, then calls `thingsboard_client_publish_telemetry()`.
5. Relay event reports relay state through ThingsBoard unless the event came from cloud and would only echo a cloud command.
6. ThingsBoard command callback maps semantic commands to relay and safety APIs.

`GET_POWER_LIMIT` requires reading the current safety threshold. The existing `safety_guard` API only supports setting thresholds, so this design adds a small public getter:

```c
esp_err_t safety_guard_get_thresholds(safety_guard_t *me,
                                      float *out_overcurrent_a,
                                      float *out_overpower_w);
```

The getter is read-only, mutex-protected, and does not change safety behavior.

## Build Integration

Root `CMakeLists.txt` will add the local LTE component path before `project()`:

```cmake
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../esp-lwlte/src")
```

`main/CMakeLists.txt` will add these production sources:

1. `app/app_controller.c`
2. `app/app_controller_internal.c`
3. `thingsboard/thingsboard_client.c`
4. `thingsboard/thingsboard_client_internal.c`
5. `network/lte/lte_link.c`
6. `network/lte/lte_link_internal.c`

`main/CMakeLists.txt` will add include directories for `app`, `thingsboard`, and `network/lte`.

`main/idf_component.yml` will set the IDF dependency to version `6.0`, matching `esp-lwlte`.

## Main Assembly

`main.c` will become a complete assembly example using macros for credentials and runtime constants. The macros will include Wi-Fi SSID/password, ThingsBoard broker host/port/client id/token, LTE APN, and task/default timing values.

The example will create:

1. `relay_t`
2. `button_t`
3. `bl0942_t`
4. `metering_service_t`
5. `safety_guard_t`
6. `tft_panel_t`
7. `lvgl_dashboard_t`
8. `wifi_link`
9. `lte_link`
10. `network_manager_t`
11. `thingsboard_client_t`
12. `app_controller_t`

If any required object creation fails, `main.c` logs the failure and stays in a delay loop instead of dereferencing null handles.

## Host Tests

Host tests live under `test/host` and use `test/host/run_host_tests.sh` as the entry point.

Support headers under `test/support` provide minimal definitions for ESP-IDF error codes and any external enum types required by pure helpers. The host tests do not include or simulate FreeRTOS semaphores, ESP event loops, GPIO, UART, MQTT clients, or LVGL.

Test groups:

1. ThingsBoard helper tests cover RPC request id extraction, command parsing, malformed payloads, telemetry JSON building, attribute JSON building, and RPC response topic building.
2. LTE helper tests cover status mapping, QoS validation, subscription table insert/update/delete/full behavior, and subscription replay selection behavior at the helper level.
3. App controller helper tests cover active link string mapping, screen toggle behavior, telemetry input assembly defaults, and power-limit response JSON building.

Firmware integration remains verified by ESP-IDF build.

## Error Handling

All public APIs return `esp_err_t` except create functions, which return handles or `NULL` on failure, matching existing modules.

Resource creation follows reverse-order cleanup. Runtime publish/subscribe failures are returned to callers and logged where useful. Destructors accept `NULL` and return `ESP_OK`.

Callbacks avoid holding module mutexes while entering higher-level callbacks. Event handlers keep work short and defer blocking work to existing public APIs only where already established by the project design.

## Out Of Scope

This implementation does not add Kconfig, NVS persistence, OTA, TLS certificate provisioning, ThingsBoard shared-attribute state sync, or a generic JSON parser. Those can be added after the basic three-module loop builds and runs.

## Acceptance Criteria

1. The three modules compile as part of the ESP-IDF main component.
2. `main.c` creates and starts the complete object graph with macro-based configuration.
3. `thingsboard_client` can publish telemetry, relay state, power limit, and RPC responses through `network_manager`.
4. `thingsboard_client` parses supported ThingsBoard RPC commands and calls the registered command callback.
5. `lte_link` behaves as a `network_link_t` subclass and can be passed to `network_manager` as backup link.
6. `app_controller` wires button, metering, safety, relay, cloud command, and display flows.
7. `safety_guard_get_thresholds()` enables `GET_POWER_LIMIT` responses.
8. Host tests pass through `test/host/run_host_tests.sh`.
9. ESP-IDF build is run and any code-level failures are addressed.
