# Modules 7-8 Implementation Design

## Scope

Implement modules 7 and 8 from `docs/agents/classes.md`:

1. `metering_service`
2. `wifi_link`

The implementation will add module files under `main/metering/` and `main/network/wifi/`, update `main/CMakeLists.txt`, and add the `espressif/mqtt` dependency to `main/idf_component.yml`.

This work does not implement `network_manager`, `lte_link`, `thingsboard_client`, `display_service`, `safety_guard`, or `app_controller`. The current `main/main.c` remains a minimal entry point.

## Reference Inputs

The authoritative public API contract is `docs/agents/classes.md`, sections 7 and 8. Architecture and dependency constraints come from `docs/agents/architecture.md`, `docs/agents/directory-structure.md`, `docs/agents/coding-style.md`, `docs/agents/oop-design.md`, and `docs/agents/err.md`.

The old project under `reference/EEE532-Project` is used as a validated implementation reference. The metering conversion formulas come from `reference/EEE532-Project/main/features/feature_pipeline_internal.c`. CF energy accounting comes from `reference/EEE532-Project/main/app/app_controller.c`. Wi-Fi/MQTT lifecycle patterns come from `reference/EEE532-Project/main/network/wifi/`, but the old singleton API will not be copied into this project.

## Approach

Use the current opaque-handle and network-subclass APIs from `classes.md`, while reusing proven formulas and protocol behavior from the old project where they fit the new boundaries.

Recommended approach:

1. Implement `metering_service` as an object that subscribes to BL0942 events and emits engineering-unit snapshots.
2. Implement `wifi_link` as a concrete `network_link_t` subclass with ESP-IDF Wi-Fi STA and ESP-MQTT.
3. Keep both modules independent of future `app_controller` wiring.

Rejected alternatives:

1. Stub APIs for compile-only success: does not satisfy the requested full implementation.
2. Copy the old singleton modules directly: conflicts with the new opaque-handle and polymorphic network design.
3. Add `network_manager` or app wiring now: expands scope beyond modules 7 and 8 and couples configuration decisions too early.

## File Layout

Add these files:

```text
main/metering/metering_service.h
main/metering/metering_service.c
main/network/wifi/wifi_link.h
main/network/wifi/wifi_link.c
```

Update these files:

```text
main/CMakeLists.txt
main/idf_component.yml
```

`main/CMakeLists.txt` will compile the new `.c` files and expose `main/metering` and `main/network/wifi` as include directories. `main/idf_component.yml` will add `espressif/mqtt` for ESP-MQTT.

## Metering Service Public API

`metering_service.h` exposes:

1. `typedef struct metering_service metering_service_t;`
2. `metering_config_t`
3. `metering_snapshot_t`
4. `METERING_EVENT_BASE` and `metering_event_id_t`
5. `metering_service_create()` / `destroy()`
6. `metering_service_start()` / `stop()`
7. `metering_service_get_latest()`
8. `metering_service_reset_energy()`

The public structures follow `classes.md` exactly. The coefficient fields in `metering_config_t` remain available as optional overrides. When a coefficient is greater than zero, that coefficient is used. When it is zero or negative, the module uses the old project's calibrated BL0942 formulas.

## Metering Service Internals

`struct metering_service` contains the fields listed in `classes.md` plus the minimal state needed for CF-based energy accumulation:

1. `uint32_t last_cf_cnt_raw`
2. `uint64_t total_energy_nwh`
3. `bool have_last_cf_cnt_raw`
4. `bool started`

It owns one mutex protecting configuration, latest snapshot, window sums, energy state, and lifecycle flags.

`start()` registers handlers for:

1. `BL0942_EVENT_MEASUREMENT`
2. `BL0942_EVENT_FAULT`

`stop()` unregisters those handlers. Repeated `start()` and `stop()` return `ESP_OK`.

The service does not hold a `bl0942_t *`, does not read UART, and does not control relay state. It only consumes BL0942 event payloads and publishes metering snapshots.

## Metering Conversion Formulas

The default conversion path uses the old project constants:

```text
VREF_MV = 1218
RL_MILLIOHM = 3
R1_OHM = 510
R2_OHM = 1950000
RSUM_OHM = R1_OHM + R2_OHM
KI_SCALE = 305978
KV_SCALE = 73989
KP_SCALE = 3537
FREQ_CLOCK_HZ = 1000000
```

Raw conversion first produces fixed-point engineering values:

```text
current_ma = i_rms_raw * VREF_MV / (KI_SCALE * RL_MILLIOHM)
voltage_cv = v_rms_raw * VREF_MV * RSUM_OHM / (KV_SCALE * R1_OHM * 10000)
power_cw = watt_raw * VREF_MV * VREF_MV * RSUM_OHM / (KP_SCALE * RL_MILLIOHM * R1_OHM * 10000000)
frequency_chz = freq_raw == 0 ? 0 : FREQ_CLOCK_HZ * 100 / freq_raw
```

Voltage, current, and power are clamped to zero if the converted value is negative. `frequency_chz` is capped at `UINT16_MAX` before conversion to float.

The public snapshot uses float units:

```text
voltage = voltage_cv / 100.0f
current = current_ma / 1000.0f
power = power_cw / 100.0f
frequency = frequency_chz / 100.0f
```

If caller-provided coefficients are positive, the override path computes:

```text
voltage = v_rms_raw * v_rms_coeff
current = i_rms_raw * i_rms_coeff
power = watt_raw * watt_coeff
```

Frequency always uses the old project's `FREQ_CLOCK_HZ / freq_raw` formula because `metering_config_t` has no frequency coefficient field.

## Metering Windowing

Each valid BL0942 measurement is converted and added to the current window. The window is emitted when either condition is true:

1. `window_count >= window_samples`
2. elapsed time from `window_start_us` is at least `publish_period_ms`

The emitted snapshot uses simple arithmetic means for voltage, current, power, and frequency. The timestamp is the latest sample timestamp in the emitted window. `valid` is true when at least one valid measurement contributed to the window.

Defaults applied during `create()`:

1. `window_samples <= 0` becomes `10`.
2. `publish_period_ms <= 0` becomes `1000`.

Windowing remains bounded and uses only accumulators, not sample arrays.

## Metering Energy Accounting

`total_energy` is accumulated from the BL0942 24-bit CF counter using old project constants and wrap-safe delta logic:

```text
CF_CNT_U24_MASK = 0x00FFFFFF
PULSE_ENERGY_NWH = 62297938
delta = (current - previous) & CF_CNT_U24_MASK
```

The first valid CF value establishes a baseline and does not add energy. Each later valid sample adds `delta * PULSE_ENERGY_NWH` to `total_energy_nwh`, preserving sub-Wh precision across snapshots. The public snapshot reports `total_energy = total_energy_nwh / 1000000000.0f`.

`reset_energy()` clears `total_energy`, `total_energy_nwh`, and the CF baseline flag. The next valid CF sample after reset becomes the new baseline.

If `cf_coeff > 0`, the service uses `delta * cf_coeff` as the Wh increment and updates `total_energy` directly. If `cf_coeff <= 0`, the service uses the old project's calibrated nWh-per-pulse value and derives public Wh from `total_energy_nwh`.

## Metering Fault Handling

On `BL0942_EVENT_FAULT`, the service publishes one snapshot with `valid=false`. It preserves `total_energy`, uses the current time if no latest timestamp exists, and does not stop the service.

Fault snapshots allow downstream consumers to detect stale or invalid measurements without losing the last accumulated energy value.

## Metering Self-Test

The module includes a private conversion self-test using the old project's golden sample:

```text
i_rms_raw = 753639
v_rms_raw = 3494335
watt_raw = 411438
freq_raw = 20000
```

Expected ranges:

```text
current: 994-1004 mA
voltage: 21989-22009 centivolts
power: 21989-22009 centiwatts
frequency: 4995-5005 centihertz
```

`metering_service_create()` runs this self-test before returning a handle. If it fails, create returns `NULL` and logs an error.

## Wi-Fi Link Public API

`wifi_link.h` exposes only:

1. `wifi_link_config_t`
2. `network_link_t *wifi_link_create(const wifi_link_config_t *config)`

It includes `network_link.h` and returns the base handle directly. Callers operate through `network_link_*()` wrappers. The concrete `struct wifi_link` remains private in `wifi_link.c`.

## Wi-Fi Link Object Model

`struct wifi_link` has `network_link_t base` as its first field. `wifi_link_create()` sets:

```text
base.ops = &wifi_link_ops
base.type = NETWORK_LINK_TYPE_WIFI
```

The ops table is a `static const network_link_ops_t` and implements:

1. `destroy`
2. `start`
3. `stop`
4. `get_status`
5. `publish`
6. `subscribe`
7. `unsubscribe`
8. `register_rx_cb`

The implementation uses a small private `wifi_link_sub_entry_t` table with configurable capacity and topic length.

## Wi-Fi Link Configuration

`wifi_link_create()` validates and deep-copies all string fields from `wifi_link_config_t`. It does not keep caller-owned string pointers.

Validation rules:

1. `ssid` must be non-empty.
2. `mqtt_broker_host` must be non-empty.
3. `mqtt_broker_port` must be non-zero.
4. QoS validation happens on subscribe and publish calls.

Defaults:

1. `mqtt_keepalive_s == 0` becomes `60`.
2. `max_subscriptions <= 0` becomes `8`.
3. `max_topic_len <= 0` becomes `128`.

MQTT URI construction:

1. `mqtt_use_tls == false`: `mqtt://host:port`
2. `mqtt_use_tls == true`: `mqtts://host:port`
3. Raw IPv6 literals are wrapped as `[host]` in the URI.

For TLS, the client uses ESP-IDF certificate bundle verification through `esp_crt_bundle_attach`.

## Wi-Fi Link Lifecycle

`start()` is idempotent. If already started, it returns `ESP_OK`. Otherwise it creates the default Wi-Fi STA netif, registers Wi-Fi and IP event handlers, initializes ESP Wi-Fi, configures STA mode, and starts the driver.

State transitions:

```text
IDLE -> STARTING -> CONNECTING -> DEGRADED -> READY
```

Event behavior:

1. `WIFI_EVENT_STA_START`: call `esp_wifi_connect()` and set status to `CONNECTING`.
2. `IP_EVENT_STA_GOT_IP`: mark Wi-Fi connected, start MQTT, and set status to `DEGRADED` until MQTT is ready.
3. `MQTT_EVENT_CONNECTED`: mark MQTT connected, replay subscriptions, then set status to `READY` on success.
4. `MQTT_EVENT_DISCONNECTED`: mark MQTT not ready; status is `DEGRADED` if Wi-Fi is still connected.
5. `WIFI_EVENT_STA_DISCONNECTED`: mark Wi-Fi and MQTT not ready, keep the MQTT client handle for ESP-MQTT reconnect handling, call `esp_wifi_connect()` when still started, and set status to `CONNECTING`.

On a later `IP_EVENT_STA_GOT_IP`, the module creates and starts an MQTT client only if none exists. If a client already exists, ESP-MQTT's reconnect flow is allowed to restore the session and emit `MQTT_EVENT_CONNECTED`.

`stop()` is idempotent. It stops and destroys MQTT, disconnects and stops Wi-Fi, unregisters handlers, destroys the netif, resets runtime status to `IDLE`, but keeps the object, copied config, and subscription table for later restart.

`destroy()` calls `stop()`, releases the subscription table, copied strings, mutex, and object memory. `destroy(NULL)` returns `ESP_OK`.

## Wi-Fi Link MQTT Operations

`publish()` validates the request and requires MQTT to be ready. It uses `esp_mqtt_client_enqueue()` so the call does not block on broker I/O. If MQTT is not ready, it returns `ESP_ERR_INVALID_STATE`; it does not buffer messages.

`subscribe()` first records the topic in the local subscription table. If MQTT is ready, it calls `esp_mqtt_client_subscribe()` immediately. If MQTT is not ready, it returns `ESP_OK` after recording the intent; `MQTT_EVENT_CONNECTED` will replay it later.

`unsubscribe()` removes the topic from the local subscription table. If MQTT is ready, it calls `esp_mqtt_client_unsubscribe()` immediately. If MQTT is not ready, it returns `ESP_OK` after removing the local intent.

On `MQTT_EVENT_CONNECTED`, the link iterates the local subscription table and replays all active subscriptions. A replay failure leaves the link in `DEGRADED`.

## Wi-Fi Link RX Flow

`register_rx_cb()` saves one callback and context. Re-registering replaces the previous callback.

On `MQTT_EVENT_DATA`, the module creates a `network_rx_data_t` containing:

1. topic pointer
2. data pointer
3. data length

The callback is invoked synchronously in the MQTT event context after releasing the object mutex. The callback must finish quickly and must not retain the topic or data pointers after it returns. The first implementation handles the current event payload only; large-message fragment reassembly is deliberately deferred until a consumer requires it.

## Wi-Fi Link Concurrency

The object mutex protects:

1. Wi-Fi and MQTT connection flags
2. status
3. subscription table
4. MQTT client pointer
5. RX callback and context
6. lifecycle flags

The implementation avoids calling user callbacks while holding the mutex. It also avoids holding the mutex across long ESP-MQTT operations where possible by copying the client handle and checking state before the call.

`get_status()` returns the current `network_link_status_t` enum:

1. `IDLE` when stopped
2. `STARTING` while start is being issued
3. `CONNECTING` while Wi-Fi is connecting or reconnecting
4. `DEGRADED` when Wi-Fi is up but MQTT is not ready
5. `READY` when Wi-Fi and MQTT are both ready
6. `ERROR` when start or MQTT setup fails and no retry is currently pending

## Error Handling

All ordinary public APIs return `esp_err_t` through the `network_link_*()` wrappers or the module's own create function.

Rules:

1. `create()` returns `NULL` on validation or allocation failure and rolls back partial resources.
2. `destroy(NULL)` returns `ESP_OK`.
3. Repeated `start()` and `stop()` return `ESP_OK`.
4. Invalid arguments return `ESP_ERR_INVALID_ARG`.
5. Operations requiring an initialized or ready runtime state return `ESP_ERR_INVALID_STATE` when that state is absent.
6. Event post and best-effort cleanup failures are logged with `ESP_LOGW` and do not mask the primary result when cleanup can safely continue.

No ordinary module code uses `ESP_ERROR_CHECK`; errors are returned to the caller.

## Dependencies

`metering_service` depends on:

1. `bl0942.h`
2. `esp_event`
3. FreeRTOS mutex APIs
4. `esp_timer` for fault timestamps when needed

`wifi_link` depends on:

1. `network_link.h`
2. `network_link_priv.h`
3. `network_types.h`
4. `esp_wifi`
5. `esp_netif`
6. `esp_event`
7. `esp-mqtt`
8. FreeRTOS mutex APIs

`metering_service` does not depend on `relay`, `thingsboard`, `display`, or `safety`. `wifi_link` does not understand ThingsBoard topics or business telemetry fields.

## Verification Plan

Verification will proceed in this order:

1. Static review against `docs/agents/classes.md` and this spec.
2. ESP-IDF build through the available ESP-IDF MCP build tool.
3. If MCP build cannot resolve dependencies or compile, use the shell workflow in `docs/agents/build-and-debug.md` as fallback.

Build verification is required before claiming the implementation is complete. Flashing and real Wi-Fi/MQTT connectivity verification are follow-up actions because they require device credentials and a broker target.
