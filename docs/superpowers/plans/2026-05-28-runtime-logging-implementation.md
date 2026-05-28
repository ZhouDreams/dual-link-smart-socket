# Runtime Logging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add useful `ESP_LOGI` runtime logs for BL0942 lifecycle, metering snapshots, MQTT readiness, and telemetry publish success.

**Architecture:** Keep logs at existing module boundaries instead of adding new state flows. BL0942 logs lifecycle events in `bl0942.c`, metering logs converted snapshots in `metering_service.c`, Wi-Fi logs MQTT readiness in `wifi_link.c`, and app controller logs successful telemetry publishes after the ThingsBoard client returns `ESP_OK`.

**Tech Stack:** ESP-IDF logging (`ESP_LOGI`), existing C modules, host shell tests, ESP-IDF build/flash/serial monitor.

---

### Task 1: Add Metering Snapshot Logs

**Files:**
- Modify: `main/metering/metering_service.c`

- [ ] **Step 1: Capture current missing-log baseline**

Run: `python3 docs/agents/serial_monitor.py --timeout 15 --port /dev/cu.usbmodem1211101 --no-reset`

Expected: no `metering snapshot #` log appears before implementation.

- [ ] **Step 2: Add low-frequency converted snapshot log**

Add a static counter near `metering_post_snapshot(&snapshot)` and log every 10 emitted snapshots:

```c
static uint32_t s_snapshot_log_count;

s_snapshot_log_count++;
if ((s_snapshot_log_count % 10U) == 0U) {
    ESP_LOGI(TAG,
             "metering snapshot #%lu: V=%.2fV I=%.3fA P=%.2fW E=%.3fWh F=%.2fHz valid=%d",
             (unsigned long)s_snapshot_log_count,
             (double)snapshot.voltage,
             (double)snapshot.current,
             (double)snapshot.power,
             (double)snapshot.total_energy,
             (double)snapshot.frequency,
             snapshot.valid ? 1 : 0);
}
```

- [ ] **Step 3: Build**

Run: `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build`

Expected: `Project build complete.`

### Task 2: Add Telemetry Publish Success Logs

**Files:**
- Modify: `main/app/app_controller.c`

- [ ] **Step 1: Add publish success counter and log**

In `app_controller_publish_telemetry()`, store the return value from `thingsboard_client_publish_telemetry()`. On `ESP_OK`, increment a static counter and log:

```c
ESP_LOGI(TAG, "telemetry publish #%lu ok: energy=%.3f Wh link=%s",
         (unsigned long)s_publish_count,
         (double)input.total_energy,
         input.active_link);
```

- [ ] **Step 2: Preserve existing warning behavior**

Return the original `esp_err_t` so `app_controller_on_metering_snapshot()` still logs failures exactly as before.

### Task 3: Add MQTT Readiness Logs

**Files:**
- Modify: `main/network/wifi/wifi_link.c`

- [ ] **Step 1: Log MQTT session connected**

In `MQTT_EVENT_CONNECTED`, after the client is accepted as current, log:

```c
ESP_LOGI(TAG, "mqtt session connected; replaying subscriptions");
```

- [ ] **Step 2: Log MQTT ready**

After `wifi_link_replay_subscriptions()` returns `ESP_OK`, log:

```c
ESP_LOGI(TAG, "mqtt ready");
```

### Task 4: Add BL0942 Lifecycle Logs

**Files:**
- Modify: `main/bl0942/bl0942.c`

- [ ] **Step 1: Log initialization success**

After `me->initialized = true`, log UART, TX/RX, baud, address, and sample period.

- [ ] **Step 2: Log sampling task start/stop**

After successful task creation in `bl0942_start()`, log effective sample Hz. After `bl0942_stop_impl()` has stopped the task, log `sample task stopped`.

- [ ] **Step 3: Log power-cycle and hard reset success**

In `bl0942_power_cycle()`, log the EN GPIO after the high settle delay. In `bl0942_hard_reset()`, log the resumed baud after successful reset.

### Task 5: Verify On Hardware

**Files:**
- No source changes.

- [ ] **Step 1: Run host tests**

Run: `./test/host/run_host_tests.sh`

Expected: `host tests passed`.

- [ ] **Step 2: Build firmware**

Run: `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build`

Expected: `Project build complete.`

- [ ] **Step 3: Flash firmware**

Run: `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py -p /dev/cu.usbmodem1211101 flash`

Expected: `Hash of data verified.` and `Hard resetting via RTS pin... Done`.

- [ ] **Step 4: Monitor logs**

Run: `source ~/.espressif/v6.0/esp-idf/export.sh && python3 docs/agents/serial_monitor.py --timeout 45 --port /dev/cu.usbmodem1211101`

Expected: logs include `initialized BL0942`, `sample task started`, `mqtt ready`, `telemetry publish #`, and at least one `metering snapshot #` if enough snapshots are emitted during the capture.
