# Modules 7-8 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `metering_service` and `wifi_link` from `docs/agents/classes.md` using the approved design in `docs/superpowers/specs/2026-05-24-modules-7-8-design.md`.

**Architecture:** `metering_service` is an opaque-handle business service that consumes BL0942 events, converts raw measurements into engineering snapshots, and publishes metering events. `wifi_link` is a concrete `network_link_t` subclass that owns Wi-Fi STA, ESP-MQTT, subscriptions, and RX dispatch while exposing only the base `network_link_t *` API.

**Tech Stack:** ESP-IDF v6.0, C, FreeRTOS mutexes, ESP-IDF event loop, `esp_timer`, ESP Wi-Fi, ESP Netif, ESP-MQTT via `espressif/mqtt ^1.0.0`.

---

## File Structure

Create and modify these files:

| Path | Responsibility |
|---|---|
| `main/idf_component.yml` | Add `espressif/mqtt ^1.0.0` while preserving existing IDF and button dependencies. |
| `main/CMakeLists.txt` | Compile new metering and Wi-Fi link sources and expose their include directories. |
| `main/metering/metering_service.h` | Public metering opaque handle, config, snapshot, event base, event ID, and API. |
| `main/metering/metering_service.c` | BL0942 event consumer, conversion formulas, window aggregation, CF energy accounting, self-test, and snapshot events. |
| `main/network/wifi/wifi_link.h` | Public Wi-Fi link config and factory returning `network_link_t *`. |
| `main/network/wifi/wifi_link.c` | `network_link_t` subclass, Wi-Fi STA lifecycle, MQTT lifecycle, subscription table, publish/subscribe/unsubscribe, RX callback dispatch. |

Scope check: the spec covers two modules in one batch. They are independent enough to build separately, but both are part of the same `classes.md` batch and share the same build dependency update. This plan keeps them in separate tasks so either module can be reviewed independently.

## Task 1: Build Registration And MQTT Dependency

**Files:**
- Modify: `main/idf_component.yml`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Update `main/idf_component.yml`**

Replace the file content with:

```yaml
## IDF Component Manager Manifest File
## Managed dependencies for the Smart_Socket main component.

dependencies:
  idf:
    version: ">=5.0"
  espressif/button: "^4.1"
  espressif/mqtt: "^1.0.0"
```

- [ ] **Step 2: Update `main/CMakeLists.txt`**

Replace the file content with:

```cmake
idf_component_register(
    SRCS
        "main.c"
        "platform/board_pinmap.c"
        "network/network_link.c"
        "network/wifi/wifi_link.c"
        "relay/relay.c"
        "button/button_iot_adapter.c"
        "button/button.c"
        "bl0942/bl0942.c"
        "metering/metering_service.c"
    INCLUDE_DIRS
        "."
        "platform"
        "network"
        "network/wifi"
        "relay"
        "button"
        "bl0942"
        "metering"
)
```

- [ ] **Step 3: Build to verify expected missing-file failure**

Run: `idf.py build`

Expected: FAIL because `main/network/wifi/wifi_link.c` and `main/metering/metering_service.c` do not exist yet. A useful failure names one of those missing files.

- [ ] **Step 4: Checkpoint without committing**

Run: `git status --short`

Expected: `main/CMakeLists.txt` and `main/idf_component.yml` are modified. Do not run `git commit` unless the user explicitly requests commits.

## Task 2: Metering Public Header

**Files:**
- Create: `main/metering/metering_service.h`

- [ ] **Step 1: Create `metering_service.h`**

Create the file using the project header template from `docs/agents/coding-style.md`. The public API must contain these declarations and type names:

```c
/**
 * @file metering_service.h
 * @brief 电参量业务聚合接口
 * @details Electrical metering service interface
 * @author OpenCode
 * @date 2026-05-24
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_event.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 电参量服务句柄
 * @details Metering service handle
 */
typedef struct metering_service metering_service_t;

/**
 * @brief 电参量服务配置
 * @details Metering service configuration
 */
typedef struct {
    float v_rms_coeff;       /**< 电压转换系数； Voltage conversion coefficient */
    float i_rms_coeff;       /**< 电流转换系数； Current conversion coefficient */
    float watt_coeff;        /**< 功率转换系数； Power conversion coefficient */
    float cf_coeff;          /**< 电能转换系数 Wh/pulse； Energy coefficient in Wh per pulse */
    int window_samples;      /**< 聚合窗口样本数； Aggregation window sample count */
    int publish_period_ms;   /**< 快照发布周期； Snapshot publish period in milliseconds */
} metering_config_t;

/**
 * @brief 电参量快照
 * @details Metering snapshot
 */
typedef struct {
    float voltage;            /**< 电压 V； Voltage in volts */
    float current;            /**< 电流 A； Current in amperes */
    float power;              /**< 有功功率 W； Active power in watts */
    float total_energy;       /**< 累计电能 Wh； Total energy in watt-hours */
    float frequency;          /**< 电网频率 Hz； Grid frequency in hertz */
    uint64_t timestamp_us;    /**< 快照时间戳； Snapshot timestamp in microseconds */
    bool valid;               /**< 快照是否有效； Whether snapshot is valid */
} metering_snapshot_t;

ESP_EVENT_DECLARE_BASE(METERING_EVENT_BASE);

/**
 * @brief 电参量事件 ID
 * @details Metering event ID
 */
typedef enum {
    METERING_EVENT_SNAPSHOT = 0, /**< 聚合快照； Aggregated snapshot */
} metering_event_id_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

metering_service_t *metering_service_create(const metering_config_t *config);
esp_err_t metering_service_destroy(metering_service_t *me);
esp_err_t metering_service_start(metering_service_t *me);
esp_err_t metering_service_stop(metering_service_t *me);
esp_err_t metering_service_get_latest(metering_service_t *me,
                                      metering_snapshot_t *out);
esp_err_t metering_service_reset_energy(metering_service_t *me);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
```

- [ ] **Step 2: Build checkpoint**

Run: `idf.py build`

Expected: FAIL because `main/metering/metering_service.c` or `main/network/wifi/wifi_link.c` still does not exist. If the error is in `metering_service.h`, fix the include or declaration mismatch before continuing.

- [ ] **Step 3: Checkpoint without committing**

Run: `git status --short`

Expected: new `main/metering/metering_service.h` appears. Do not run `git commit` unless the user explicitly requests commits.

## Task 3: Metering Conversion And Self-Test

**Files:**
- Create: `main/metering/metering_service.c`

- [ ] **Step 1: Create source skeleton and constants**

Create `main/metering/metering_service.c` with the project source template. Include these headers:

```c
#include "metering_service.h"

#include <stdlib.h>
#include <string.h>

#include "bl0942.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
```

Add these defines:

```c
#define TAG "metering_service"

#define METERING_DEFAULT_WINDOW_SAMPLES      (10)
#define METERING_DEFAULT_PUBLISH_PERIOD_MS   (1000)
#define METERING_EVENT_POST_TIMEOUT_MS       (10)

#define METERING_VREF_MV         (1218)
#define METERING_RL_MILLIOHM     (3)
#define METERING_R1_OHM          (510)
#define METERING_R2_OHM          (1950000)
#define METERING_RSUM_OHM        (METERING_R1_OHM + METERING_R2_OHM)
#define METERING_KI_SCALE        (305978)
#define METERING_KV_SCALE        (73989)
#define METERING_KP_SCALE        (3537)
#define METERING_FREQ_CLOCK_HZ   (1000000UL)
#define METERING_CF_CNT_U24_MASK (0x00FFFFFFUL)
#define METERING_PULSE_ENERGY_NWH (62297938ULL)
#define METERING_NWH_PER_WH      (1000000000ULL)
```

Define the event base:

```c
ESP_EVENT_DEFINE_BASE(METERING_EVENT_BASE);
```

- [ ] **Step 2: Add internal types and object state**

Add these internal types:

```c
typedef struct {
    int32_t voltage_cv;
    int32_t current_ma;
    int32_t power_cw;
    uint16_t frequency_chz;
    uint32_t cf_cnt_raw;
    uint64_t timestamp_us;
    bool valid;
} metering_fixed_sample_t;

struct metering_service {
    metering_config_t config;
    SemaphoreHandle_t mutex;
    metering_snapshot_t latest;
    bool has_latest;
    float window_v_sum;
    float window_i_sum;
    float window_p_sum;
    float window_freq_sum;
    int window_count;
    uint64_t window_start_us;
    uint64_t window_last_us;
    float total_energy;
    uint64_t total_energy_nwh;
    uint32_t last_cf_cnt_raw;
    bool have_last_cf_cnt_raw;
    bool started;
    bool initialized;
};
```

Add static prototypes for all helpers used by the remaining metering steps:

```c
static void metering_apply_defaults(const metering_config_t *config,
                                    metering_config_t *out);
static esp_err_t metering_run_conversion_selftest(void);
static void metering_convert_default(const bl0942_measurement_t *in,
                                     metering_fixed_sample_t *out);
static void metering_convert_with_config(const metering_service_t *me,
                                         const bl0942_measurement_t *in,
                                         metering_snapshot_t *out);
static uint32_t metering_u24_delta(uint32_t current, uint32_t previous);
static void metering_update_energy_locked(metering_service_t *me,
                                          uint32_t cf_cnt_raw);
static void metering_on_bl0942_measurement(void *arg, esp_event_base_t base,
                                           int32_t id, void *event_data);
static void metering_on_bl0942_fault(void *arg, esp_event_base_t base,
                                     int32_t id, void *event_data);
static void metering_post_snapshot(const metering_snapshot_t *snapshot);
```

- [ ] **Step 3: Implement default config and raw conversion**

Add these helper implementations exactly, adjusting only line wrapping to satisfy formatting:

```c
static void metering_apply_defaults(const metering_config_t *config,
                                    metering_config_t *out)
{
    if (out == NULL) {
        return;
    }

    if (config != NULL) {
        *out = *config;
    } else {
        memset(out, 0, sizeof(*out));
    }

    if (out->window_samples <= 0) {
        out->window_samples = METERING_DEFAULT_WINDOW_SAMPLES;
    }
    if (out->publish_period_ms <= 0) {
        out->publish_period_ms = METERING_DEFAULT_PUBLISH_PERIOD_MS;
    }
}

static uint32_t metering_u24_delta(uint32_t current, uint32_t previous)
{
    current &= METERING_CF_CNT_U24_MASK;
    previous &= METERING_CF_CNT_U24_MASK;
    return (current - previous) & METERING_CF_CNT_U24_MASK;
}

static void metering_convert_default(const bl0942_measurement_t *in,
                                     metering_fixed_sample_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    if (in == NULL || !in->valid) {
        return;
    }

    out->current_ma = (int32_t)(((int64_t)in->i_rms_raw * METERING_VREF_MV) /
                                ((int64_t)METERING_KI_SCALE * METERING_RL_MILLIOHM));
    out->voltage_cv = (int32_t)(((int64_t)in->v_rms_raw * METERING_VREF_MV *
                                 METERING_RSUM_OHM) /
                                ((int64_t)METERING_KV_SCALE * METERING_R1_OHM * 10000LL));
    out->power_cw = (int32_t)(((int64_t)in->watt_raw * METERING_VREF_MV *
                               METERING_VREF_MV * METERING_RSUM_OHM) /
                              ((int64_t)METERING_KP_SCALE * METERING_RL_MILLIOHM *
                               METERING_R1_OHM * 10000000LL));
    if (in->freq_raw != 0U) {
        uint32_t frequency_chz = (uint32_t)(((uint64_t)METERING_FREQ_CLOCK_HZ * 100ULL) /
                                            in->freq_raw);
        if (frequency_chz > UINT16_MAX) {
            frequency_chz = UINT16_MAX;
        }
        out->frequency_chz = (uint16_t)frequency_chz;
    }

    if (out->voltage_cv < 0) {
        out->voltage_cv = 0;
    }
    if (out->current_ma < 0) {
        out->current_ma = 0;
    }
    if (out->power_cw < 0) {
        out->power_cw = 0;
    }

    out->cf_cnt_raw = in->cf_cnt_raw & METERING_CF_CNT_U24_MASK;
    out->timestamp_us = in->capture_time_us;
    out->valid = true;
}
```

- [ ] **Step 4: Implement configured conversion and self-test**

Add these helper implementations:

```c
static void metering_convert_with_config(const metering_service_t *me,
                                         const bl0942_measurement_t *in,
                                         metering_snapshot_t *out)
{
    metering_fixed_sample_t fixed = {0};

    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    if (me == NULL || in == NULL || !in->valid) {
        return;
    }

    metering_convert_default(in, &fixed);
    if (!fixed.valid) {
        return;
    }

    out->voltage = (me->config.v_rms_coeff > 0.0f) ?
                   ((float)in->v_rms_raw * me->config.v_rms_coeff) :
                   ((float)fixed.voltage_cv / 100.0f);
    out->current = (me->config.i_rms_coeff > 0.0f) ?
                   ((float)in->i_rms_raw * me->config.i_rms_coeff) :
                   ((float)fixed.current_ma / 1000.0f);
    out->power = (me->config.watt_coeff > 0.0f) ?
                 ((float)in->watt_raw * me->config.watt_coeff) :
                 ((float)fixed.power_cw / 100.0f);
    out->frequency = (float)fixed.frequency_chz / 100.0f;
    out->timestamp_us = fixed.timestamp_us;
    out->valid = true;
}

static esp_err_t metering_run_conversion_selftest(void)
{
    const bl0942_measurement_t golden = {
        .i_rms_raw = 753639,
        .v_rms_raw = 3494335,
        .i_fast_rms_raw = 0,
        .watt_raw = 411438,
        .cf_cnt_raw = 0,
        .freq_raw = 20000,
        .status_raw = 0,
        .capture_time_us = 0,
        .valid = true,
    };
    metering_fixed_sample_t out = {0};

    metering_convert_default(&golden, &out);
    ESP_RETURN_ON_FALSE(out.current_ma >= 994 && out.current_ma <= 1004,
                        ESP_FAIL, TAG, "current self-test failed");
    ESP_RETURN_ON_FALSE(out.voltage_cv >= 21989 && out.voltage_cv <= 22009,
                        ESP_FAIL, TAG, "voltage self-test failed");
    ESP_RETURN_ON_FALSE(out.power_cw >= 21989 && out.power_cw <= 22009,
                        ESP_FAIL, TAG, "power self-test failed");
    ESP_RETURN_ON_FALSE(out.frequency_chz >= 4995U && out.frequency_chz <= 5005U,
                        ESP_FAIL, TAG, "frequency self-test failed");

    return ESP_OK;
}
```

- [ ] **Step 5: Build to verify source still fails on missing public functions**

Run: `idf.py build`

Expected: FAIL because `metering_service_create()` and the other public functions are declared in the header but not implemented yet, or because `wifi_link.c` is still missing. Continue to Task 4 to complete metering.

## Task 4: Metering Lifecycle, Events, Windowing, And Energy

**Files:**
- Modify: `main/metering/metering_service.c`

- [ ] **Step 1: Implement energy update and event posting helpers**

Add these functions:

```c
static void metering_update_energy_locked(metering_service_t *me,
                                          uint32_t cf_cnt_raw)
{
    if (me == NULL) {
        return;
    }

    cf_cnt_raw &= METERING_CF_CNT_U24_MASK;
    if (!me->have_last_cf_cnt_raw) {
        me->last_cf_cnt_raw = cf_cnt_raw;
        me->have_last_cf_cnt_raw = true;
        return;
    }

    const uint32_t delta = metering_u24_delta(cf_cnt_raw, me->last_cf_cnt_raw);
    me->last_cf_cnt_raw = cf_cnt_raw;

    if (me->config.cf_coeff > 0.0f) {
        me->total_energy += (float)delta * me->config.cf_coeff;
        return;
    }

    me->total_energy_nwh += (uint64_t)delta * METERING_PULSE_ENERGY_NWH;
    me->total_energy = (float)((double)me->total_energy_nwh /
                               (double)METERING_NWH_PER_WH);
}

static void metering_post_snapshot(const metering_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    const esp_err_t ret = esp_event_post(METERING_EVENT_BASE,
                                         METERING_EVENT_SNAPSHOT,
                                         snapshot, sizeof(*snapshot),
                                         pdMS_TO_TICKS(METERING_EVENT_POST_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "post snapshot event failed: %s", esp_err_to_name(ret));
    }
}
```

- [ ] **Step 2: Implement lifecycle APIs**

Add the public functions with these semantics:

```c
metering_service_t *metering_service_create(const metering_config_t *config)
{
    metering_config_t resolved = {0};

    if (metering_run_conversion_selftest() != ESP_OK) {
        return NULL;
    }

    metering_service_t *me = calloc(1, sizeof(*me));
    if (me == NULL) {
        ESP_LOGE(TAG, "calloc metering service failed");
        return NULL;
    }

    metering_apply_defaults(config, &resolved);
    me->config = resolved;
    me->mutex = xSemaphoreCreateMutex();
    if (me->mutex == NULL) {
        ESP_LOGE(TAG, "create mutex failed");
        free(me);
        return NULL;
    }

    me->initialized = true;
    return me;
}

esp_err_t metering_service_destroy(metering_service_t *me)
{
    if (me == NULL) {
        return ESP_OK;
    }

    (void)metering_service_stop(me);
    if (me->mutex != NULL) {
        vSemaphoreDelete(me->mutex);
    }
    free(me);
    return ESP_OK;
}
```

Implement `start()` and `stop()` with `esp_event_handler_register()` and `esp_event_handler_unregister()`:

```c
esp_err_t metering_service_start(metering_service_t *me)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "service is null");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG, "service is not initialized");

    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (me->started) {
        xSemaphoreGive(me->mutex);
        return ESP_OK;
    }
    xSemaphoreGive(me->mutex);

    ESP_RETURN_ON_ERROR(esp_event_handler_register(BL0942_EVENT_BASE,
                                                   BL0942_EVENT_MEASUREMENT,
                                                   metering_on_bl0942_measurement,
                                                   me),
                        TAG, "register measurement handler failed");
    esp_err_t ret = esp_event_handler_register(BL0942_EVENT_BASE,
                                               BL0942_EVENT_FAULT,
                                               metering_on_bl0942_fault,
                                               me);
    if (ret != ESP_OK) {
        (void)esp_event_handler_unregister(BL0942_EVENT_BASE,
                                           BL0942_EVENT_MEASUREMENT,
                                           metering_on_bl0942_measurement);
        return ret;
    }

    xSemaphoreTake(me->mutex, portMAX_DELAY);
    me->started = true;
    xSemaphoreGive(me->mutex);
    return ESP_OK;
}
```

`metering_service_stop()` must check `me`, return `ESP_OK` if not started, unregister both handlers, then set `started = false` under the mutex. If unregister of the second handler fails, return that error after attempting both unregister operations.

- [ ] **Step 3: Implement query and reset APIs**

Add:

```c
esp_err_t metering_service_get_latest(metering_service_t *me,
                                      metering_snapshot_t *out)
{
    ESP_RETURN_ON_FALSE(me != NULL && out != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE,
                        TAG, "service is not initialized");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    if (!me->has_latest) {
        xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }

    *out = me->latest;
    xSemaphoreGive(me->mutex);
    return ESP_OK;
}

esp_err_t metering_service_reset_energy(metering_service_t *me)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "service is null");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE,
                        TAG, "service is not initialized");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    me->total_energy = 0.0f;
    me->total_energy_nwh = 0;
    me->last_cf_cnt_raw = 0;
    me->have_last_cf_cnt_raw = false;
    if (me->has_latest) {
        me->latest.total_energy = 0.0f;
    }

    xSemaphoreGive(me->mutex);
    return ESP_OK;
}
```

- [ ] **Step 4: Implement measurement event handler**

Add `metering_on_bl0942_measurement()` that follows this exact flow:

```c
static void metering_on_bl0942_measurement(void *arg, esp_event_base_t base,
                                           int32_t id, void *event_data)
{
    metering_service_t *me = (metering_service_t *)arg;
    const bl0942_measurement_t *measurement = (const bl0942_measurement_t *)event_data;
    metering_snapshot_t sample = {0};
    metering_snapshot_t snapshot = {0};
    bool emit = false;

    (void)base;
    (void)id;

    if (me == NULL || measurement == NULL || !measurement->valid) {
        return;
    }

    metering_convert_with_config(me, measurement, &sample);
    if (!sample.valid) {
        return;
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (!me->started) {
        xSemaphoreGive(me->mutex);
        return;
    }

    metering_update_energy_locked(me, measurement->cf_cnt_raw);
    sample.total_energy = me->total_energy;

    if (me->window_count == 0) {
        me->window_start_us = sample.timestamp_us;
    }
    me->window_last_us = sample.timestamp_us;
    me->window_v_sum += sample.voltage;
    me->window_i_sum += sample.current;
    me->window_p_sum += sample.power;
    me->window_freq_sum += sample.frequency;
    me->window_count++;

    const uint64_t elapsed_us = (me->window_last_us >= me->window_start_us) ?
                                (me->window_last_us - me->window_start_us) : 0ULL;
    if (me->window_count >= me->config.window_samples ||
        elapsed_us >= ((uint64_t)me->config.publish_period_ms * 1000ULL)) {
        snapshot.voltage = me->window_v_sum / (float)me->window_count;
        snapshot.current = me->window_i_sum / (float)me->window_count;
        snapshot.power = me->window_p_sum / (float)me->window_count;
        snapshot.frequency = me->window_freq_sum / (float)me->window_count;
        snapshot.total_energy = me->total_energy;
        snapshot.timestamp_us = me->window_last_us;
        snapshot.valid = true;

        me->latest = snapshot;
        me->has_latest = true;
        me->window_v_sum = 0.0f;
        me->window_i_sum = 0.0f;
        me->window_p_sum = 0.0f;
        me->window_freq_sum = 0.0f;
        me->window_count = 0;
        me->window_start_us = 0;
        me->window_last_us = 0;
        emit = true;
    }
    xSemaphoreGive(me->mutex);

    if (emit) {
        metering_post_snapshot(&snapshot);
    }
}
```

- [ ] **Step 5: Implement fault event handler**

Add:

```c
static void metering_on_bl0942_fault(void *arg, esp_event_base_t base,
                                     int32_t id, void *event_data)
{
    metering_service_t *me = (metering_service_t *)arg;
    metering_snapshot_t snapshot = {0};

    (void)base;
    (void)id;
    (void)event_data;

    if (me == NULL) {
        return;
    }
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (!me->started) {
        xSemaphoreGive(me->mutex);
        return;
    }

    snapshot = me->latest;
    snapshot.total_energy = me->total_energy;
    snapshot.timestamp_us = (snapshot.timestamp_us != 0ULL) ?
                            snapshot.timestamp_us :
                            (uint64_t)esp_timer_get_time();
    snapshot.valid = false;
    me->latest = snapshot;
    me->has_latest = true;
    xSemaphoreGive(me->mutex);

    metering_post_snapshot(&snapshot);
}
```

- [ ] **Step 6: Build checkpoint**

Run: `idf.py build`

Expected: FAIL only because `main/network/wifi/wifi_link.c` and/or `main/network/wifi/wifi_link.h` is missing. If the error is in `metering_service`, fix the exact missing include, signature, or helper implementation before continuing.

- [ ] **Step 7: Checkpoint without committing**

Run: `git status --short`

Expected: `main/metering/metering_service.h` and `.c` appear. Do not run `git commit` unless the user explicitly requests commits.

## Task 5: Wi-Fi Link Header And Object Skeleton

**Files:**
- Create: `main/network/wifi/wifi_link.h`
- Create: `main/network/wifi/wifi_link.c`

- [ ] **Step 1: Create `wifi_link.h` public API**

Create the file with this content shape and public declarations:

```c
/**
 * @file wifi_link.h
 * @brief Wi-Fi 链路子类接口
 * @details Wi-Fi link subclass interface
 * @author OpenCode
 * @date 2026-05-24
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stdint.h>

#include "network_link.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

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

/**********************
 * GLOBAL PROTOTYPES
 **********************/

network_link_t *wifi_link_create(const wifi_link_config_t *config);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
```

Add Doxygen comments for the config struct, fields, and factory function according to `docs/agents/coding-style.md`.

- [ ] **Step 2: Create Wi-Fi source includes, constants, and structs**

Create `main/network/wifi/wifi_link.c` with these includes:

```c
#include "wifi_link.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mqtt_client.h"
#include "network_link_priv.h"
```

Add constants:

```c
#define TAG "wifi_link"

#define WIFI_LINK_DEFAULT_MAX_SUBSCRIPTIONS (8)
#define WIFI_LINK_DEFAULT_MAX_TOPIC_LEN     (128)
#define WIFI_LINK_DEFAULT_KEEPALIVE_S       (60)
#define WIFI_LINK_MAX_SSID_LEN              (32)
#define WIFI_LINK_MAX_PASSWORD_LEN          (64)
```

Add internal structs:

```c
typedef struct {
    char *topic;
    network_mqtt_qos_t qos;
    bool in_use;
} wifi_link_sub_entry_t;

typedef struct wifi_link {
    network_link_t base;
    wifi_link_config_t config;
    char *ssid;
    char *password;
    char *mqtt_broker_host;
    char *mqtt_client_id;
    char *mqtt_username;
    char *mqtt_password;
    char *mqtt_uri;
    esp_netif_t *netif;
    esp_mqtt_client_handle_t mqtt_client;
    bool wifi_connected;
    bool mqtt_connected;
    wifi_link_sub_entry_t *sub_table;
    int sub_table_size;
    int max_topic_len;
    SemaphoreHandle_t mutex;
    network_rx_cb_t rx_cb;
    void *rx_ctx;
    bool started;
    bool destroying;
} wifi_link_t;
```

- [ ] **Step 3: Add ops prototypes and factory skeleton**

Add prototypes for all ops and helpers:

```c
static esp_err_t wifi_link_destroy_impl(network_link_t *base);
static esp_err_t wifi_link_start_impl(network_link_t *base);
static esp_err_t wifi_link_stop_impl(network_link_t *base);
static esp_err_t wifi_link_get_status_impl(network_link_t *base,
                                           network_link_status_t *out);
static esp_err_t wifi_link_publish_impl(network_link_t *base,
                                        const network_publish_request_t *req);
static esp_err_t wifi_link_subscribe_impl(network_link_t *base,
                                          const char *topic,
                                          network_mqtt_qos_t qos);
static esp_err_t wifi_link_unsubscribe_impl(network_link_t *base,
                                            const char *topic);
static esp_err_t wifi_link_register_rx_cb_impl(network_link_t *base,
                                               network_rx_cb_t cb,
                                               void *ctx);
static esp_err_t wifi_link_validate_config(const wifi_link_config_t *config);
static esp_err_t wifi_link_copy_config(wifi_link_t *me,
                                       const wifi_link_config_t *config);
static void wifi_link_free_config(wifi_link_t *me);
static wifi_link_t *wifi_link_from_base(network_link_t *base);
static bool wifi_link_is_valid_qos(network_mqtt_qos_t qos);
```

Add the ops table:

```c
static const network_link_ops_t wifi_link_ops = {
    .destroy = wifi_link_destroy_impl,
    .start = wifi_link_start_impl,
    .stop = wifi_link_stop_impl,
    .get_status = wifi_link_get_status_impl,
    .publish = wifi_link_publish_impl,
    .subscribe = wifi_link_subscribe_impl,
    .unsubscribe = wifi_link_unsubscribe_impl,
    .register_rx_cb = wifi_link_register_rx_cb_impl,
};
```

Implement the base conversion helper using the first-member invariant:

```c
static wifi_link_t *wifi_link_from_base(network_link_t *base)
{
    return (wifi_link_t *)base;
}
```

- [ ] **Step 4: Implement `wifi_link_create()` allocation path**

Implement the factory:

```c
network_link_t *wifi_link_create(const wifi_link_config_t *config)
{
    if (wifi_link_validate_config(config) != ESP_OK) {
        return NULL;
    }

    wifi_link_t *me = calloc(1, sizeof(*me));
    if (me == NULL) {
        ESP_LOGE(TAG, "calloc wifi link failed");
        return NULL;
    }

    me->base.ops = &wifi_link_ops;
    me->base.type = NETWORK_LINK_TYPE_WIFI;
    me->mutex = xSemaphoreCreateMutex();
    if (me->mutex == NULL) {
        ESP_LOGE(TAG, "create mutex failed");
        free(me);
        return NULL;
    }

    if (wifi_link_copy_config(me, config) != ESP_OK) {
        vSemaphoreDelete(me->mutex);
        wifi_link_free_config(me);
        free(me);
        return NULL;
    }

    me->sub_table = calloc((size_t)me->sub_table_size, sizeof(me->sub_table[0]));
    if (me->sub_table == NULL) {
        ESP_LOGE(TAG, "calloc subscription table failed");
        vSemaphoreDelete(me->mutex);
        wifi_link_free_config(me);
        free(me);
        return NULL;
    }

    return &me->base;
}
```

- [ ] **Step 5: Build checkpoint**

Run: `idf.py build`

Expected: FAIL because the static functions referenced by `wifi_link_ops` are declared but not implemented. Continue to Task 6.

## Task 6: Wi-Fi Config Copy, URI Builder, And Lifecycle

**Files:**
- Modify: `main/network/wifi/wifi_link.c`

- [ ] **Step 1: Implement validation and string helpers**

Add these helpers:

```c
static bool wifi_link_is_valid_qos(network_mqtt_qos_t qos)
{
    return qos == NETWORK_MQTT_QOS0 ||
           qos == NETWORK_MQTT_QOS1 ||
           qos == NETWORK_MQTT_QOS2;
}

static char *wifi_link_strdup_or_null(const char *value)
{
    if (value == NULL) {
        return NULL;
    }
    const size_t len = strlen(value);
    char *copy = malloc(len + 1U);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, value, len + 1U);
    return copy;
}

static esp_err_t wifi_link_validate_config(const wifi_link_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");
    ESP_RETURN_ON_FALSE(config->ssid != NULL && config->ssid[0] != '\0',
                        ESP_ERR_INVALID_ARG, TAG, "ssid is empty");
    ESP_RETURN_ON_FALSE(strlen(config->ssid) <= WIFI_LINK_MAX_SSID_LEN,
                        ESP_ERR_INVALID_ARG, TAG, "ssid too long");
    ESP_RETURN_ON_FALSE(config->password == NULL ||
                            strlen(config->password) <= WIFI_LINK_MAX_PASSWORD_LEN,
                        ESP_ERR_INVALID_ARG, TAG, "password too long");
    ESP_RETURN_ON_FALSE(config->mqtt_broker_host != NULL &&
                            config->mqtt_broker_host[0] != '\0',
                        ESP_ERR_INVALID_ARG, TAG, "broker host is empty");
    ESP_RETURN_ON_FALSE(config->mqtt_broker_port != 0U,
                        ESP_ERR_INVALID_ARG, TAG, "broker port is zero");
    return ESP_OK;
}
```

- [ ] **Step 2: Implement URI construction and config copy**

Add raw IPv6 detection and URI builder:

```c
static bool wifi_link_is_raw_ipv6_literal(const char *host)
{
    return host != NULL && strchr(host, ':') != NULL &&
           strchr(host, '[') == NULL && strchr(host, ']') == NULL;
}

static char *wifi_link_build_uri(const char *host, uint16_t port, bool use_tls)
{
    const char *scheme = use_tls ? "mqtts" : "mqtt";
    const bool ipv6 = wifi_link_is_raw_ipv6_literal(host);
    int len = 0;

    if (ipv6) {
        len = snprintf(NULL, 0, "%s://[%s]:%u", scheme, host, (unsigned int)port);
    } else {
        len = snprintf(NULL, 0, "%s://%s:%u", scheme, host, (unsigned int)port);
    }
    if (len <= 0) {
        return NULL;
    }

    char *uri = malloc((size_t)len + 1U);
    if (uri == NULL) {
        return NULL;
    }
    if (ipv6) {
        (void)snprintf(uri, (size_t)len + 1U, "%s://[%s]:%u", scheme, host,
                       (unsigned int)port);
    } else {
        (void)snprintf(uri, (size_t)len + 1U, "%s://%s:%u", scheme, host,
                       (unsigned int)port);
    }
    return uri;
}
```

Implement `wifi_link_copy_config()` so it deep-copies all strings and applies defaults:

```c
static esp_err_t wifi_link_copy_config(wifi_link_t *me,
                                       const wifi_link_config_t *config)
{
    me->sub_table_size = (config->max_subscriptions > 0) ?
                         config->max_subscriptions :
                         WIFI_LINK_DEFAULT_MAX_SUBSCRIPTIONS;
    me->max_topic_len = (config->max_topic_len > 0) ?
                        config->max_topic_len :
                        WIFI_LINK_DEFAULT_MAX_TOPIC_LEN;

    me->ssid = wifi_link_strdup_or_null(config->ssid);
    me->password = wifi_link_strdup_or_null(config->password != NULL ? config->password : "");
    me->mqtt_broker_host = wifi_link_strdup_or_null(config->mqtt_broker_host);
    me->mqtt_client_id = wifi_link_strdup_or_null(config->mqtt_client_id);
    me->mqtt_username = wifi_link_strdup_or_null(config->mqtt_username);
    me->mqtt_password = wifi_link_strdup_or_null(config->mqtt_password);
    me->mqtt_uri = wifi_link_build_uri(config->mqtt_broker_host,
                                       config->mqtt_broker_port,
                                       config->mqtt_use_tls);
    if (me->ssid == NULL || me->password == NULL || me->mqtt_broker_host == NULL ||
        me->mqtt_uri == NULL) {
        wifi_link_free_config(me);
        return ESP_ERR_NO_MEM;
    }

    me->config = *config;
    me->config.ssid = me->ssid;
    me->config.password = me->password;
    me->config.mqtt_broker_host = me->mqtt_broker_host;
    me->config.mqtt_client_id = me->mqtt_client_id;
    me->config.mqtt_username = me->mqtt_username;
    me->config.mqtt_password = me->mqtt_password;
    if (me->config.mqtt_keepalive_s == 0U) {
        me->config.mqtt_keepalive_s = WIFI_LINK_DEFAULT_KEEPALIVE_S;
    }
    me->config.max_subscriptions = me->sub_table_size;
    me->config.max_topic_len = me->max_topic_len;
    return ESP_OK;
}
```

Implement `wifi_link_free_config()` by freeing every owned string and setting each pointer to `NULL`.

- [ ] **Step 3: Implement status and stop/destroy ops**

Implement status mapping:

```c
static network_link_status_t wifi_link_current_status_locked(const wifi_link_t *me)
{
    if (!me->started) {
        return NETWORK_LINK_STATUS_IDLE;
    }
    if (me->mqtt_connected) {
        return NETWORK_LINK_STATUS_READY;
    }
    if (me->wifi_connected) {
        return NETWORK_LINK_STATUS_DEGRADED;
    }
    return NETWORK_LINK_STATUS_CONNECTING;
}
```

Implement `wifi_link_get_status_impl()` by validating `base` and `out`, taking the mutex, assigning `*out = wifi_link_current_status_locked(me)`, releasing the mutex, and returning `ESP_OK`.

Implement `wifi_link_stop_impl()` with this cleanup order:

```text
1. Return ESP_OK for NULL base through wrapper behavior is already handled; inside ops reject NULL with ESP_ERR_INVALID_ARG.
2. Take mutex.
3. If not started, release mutex and return ESP_OK.
4. Set started=false, wifi_connected=false, mqtt_connected=false.
5. Copy mqtt_client and netif to locals, set object pointers to NULL, release mutex.
6. Unregister Wi-Fi and IP handlers with esp_event_handler_unregister().
7. If mqtt_client != NULL, call esp_mqtt_client_stop() then esp_mqtt_client_destroy().
8. Call esp_wifi_disconnect(), esp_wifi_stop(), esp_wifi_deinit(). Ignore ESP_ERR_WIFI_NOT_INIT during stop cleanup.
9. If netif != NULL, call esp_netif_destroy_default_wifi(netif).
10. Return ESP_OK unless a non-ignored stop error occurred.
```

Implement `wifi_link_destroy_impl()` by calling `wifi_link_stop_impl(base)`, freeing every subscription topic, freeing `sub_table`, deleting the mutex, freeing copied config strings, and freeing `me`.

- [ ] **Step 4: Implement Wi-Fi start and event handlers**

Add prototypes and functions for handlers:

```c
static void wifi_link_wifi_event_handler(void *arg, esp_event_base_t base,
                                         int32_t event_id, void *event_data);
static void wifi_link_ip_event_handler(void *arg, esp_event_base_t base,
                                       int32_t event_id, void *event_data);
static esp_err_t wifi_link_start_mqtt(wifi_link_t *me);
```

Implement `wifi_link_start_impl()` flow:

```text
1. Validate base.
2. Take mutex and return ESP_OK if already started.
3. Set started=true, wifi_connected=false, mqtt_connected=false.
4. Release mutex.
5. Create default Wi-Fi STA netif with esp_netif_create_default_wifi_sta().
6. Register WIFI_EVENT/ESP_EVENT_ANY_ID with wifi_link_wifi_event_handler and arg=me.
7. Register IP_EVENT/IP_EVENT_STA_GOT_IP with wifi_link_ip_event_handler and arg=me.
8. Initialize Wi-Fi with WIFI_INIT_CONFIG_DEFAULT().
9. Fill wifi_config_t sta.ssid and sta.password from copied config.
10. Use WIFI_AUTH_OPEN when password is empty, otherwise WIFI_AUTH_WPA2_PSK.
11. Set mode WIFI_MODE_STA, set config on WIFI_IF_STA, start Wi-Fi.
12. On any failure call wifi_link_stop_impl(base) and return the failing esp_err_t.
```

Use this handler behavior:

```c
static void wifi_link_wifi_event_handler(void *arg, esp_event_base_t base,
                                         int32_t event_id, void *event_data)
{
    wifi_link_t *me = (wifi_link_t *)arg;
    (void)base;
    (void)event_data;

    if (me == NULL) {
        return;
    }
    if (event_id == WIFI_EVENT_STA_START) {
        (void)esp_wifi_connect();
        return;
    }
    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xSemaphoreTake(me->mutex, portMAX_DELAY);
        const bool should_reconnect = me->started;
        me->wifi_connected = false;
        me->mqtt_connected = false;
        xSemaphoreGive(me->mutex);
        if (should_reconnect) {
            (void)esp_wifi_connect();
        }
    }
}
```

`wifi_link_ip_event_handler()` must mark `wifi_connected=true`, `mqtt_connected=false`, then call `wifi_link_start_mqtt(me)` if `me->mqtt_client == NULL`.

- [ ] **Step 5: Build checkpoint**

Run: `idf.py build`

Expected: FAIL because MQTT operation helpers are not implemented yet. If the error is in Wi-Fi config, lifecycle, URI building, or missing includes, fix it before continuing.

## Task 7: Wi-Fi MQTT Operations, Subscription Replay, And RX

**Files:**
- Modify: `main/network/wifi/wifi_link.c`

- [ ] **Step 1: Implement MQTT start and event handler**

Add prototypes:

```c
static void wifi_link_mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                         int32_t event_id, void *event_data);
static esp_err_t wifi_link_replay_subscriptions(wifi_link_t *me,
                                                esp_mqtt_client_handle_t client);
```

Implement `wifi_link_start_mqtt()`:

```c
static esp_err_t wifi_link_start_mqtt(wifi_link_t *me)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = me->mqtt_uri,
        .credentials.client_id = me->mqtt_client_id,
        .credentials.username = me->mqtt_username,
        .credentials.authentication.password = me->mqtt_password,
        .session.keepalive = me->config.mqtt_keepalive_s,
        .session.disable_clean_session = !me->config.mqtt_clean_session,
    };

    if (me->config.mqtt_use_tls) {
        mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }

    xSemaphoreTake(me->mutex, portMAX_DELAY);
    if (!me->started || me->mqtt_client != NULL) {
        xSemaphoreGive(me->mutex);
        return ESP_OK;
    }
    xSemaphoreGive(me->mutex);

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t ret = esp_mqtt_client_register_event(client, MQTT_EVENT_ANY,
                                                   wifi_link_mqtt_event_handler,
                                                   me);
    if (ret != ESP_OK) {
        esp_mqtt_client_destroy(client);
        return ret;
    }
    ret = esp_mqtt_client_start(client);
    if (ret != ESP_OK) {
        esp_mqtt_client_destroy(client);
        return ret;
    }

    xSemaphoreTake(me->mutex, portMAX_DELAY);
    if (me->started && me->mqtt_client == NULL) {
        me->mqtt_client = client;
        client = NULL;
    }
    xSemaphoreGive(me->mutex);

    if (client != NULL) {
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
    }
    return ESP_OK;
}
```

Implement MQTT event behavior:

```text
MQTT_EVENT_CONNECTED:
  set mqtt_connected=true under mutex, copy client handle, release mutex, call wifi_link_replay_subscriptions().
  If replay fails, set mqtt_connected=false under mutex so status remains DEGRADED.

MQTT_EVENT_DISCONNECTED:
  set mqtt_connected=false under mutex.

MQTT_EVENT_DATA:
  copy rx_cb and rx_ctx under mutex, release mutex, build network_rx_data_t from event->topic, event->data, and event->data_len, then call rx_cb if non-null.

Other events:
  no state changes.
```

- [ ] **Step 2: Implement subscription table helpers**

Add these helpers:

```c
static esp_err_t wifi_link_find_subscription_locked(wifi_link_t *me,
                                                    const char *topic,
                                                    int *out_index,
                                                    int *out_free_index);
static esp_err_t wifi_link_store_subscription_locked(wifi_link_t *me,
                                                     const char *topic,
                                                     network_mqtt_qos_t qos);
static esp_err_t wifi_link_remove_subscription_locked(wifi_link_t *me,
                                                      const char *topic);
```

Required behavior:

```text
find:
  Iterate 0..sub_table_size-1.
  If in_use and topic matches, set out_index.
  Track first free slot in out_free_index.
  Return ESP_OK even when not found; output indexes carry the result.

store:
  Reject NULL or empty topic with ESP_ERR_INVALID_ARG.
  Reject strlen(topic) >= max_topic_len with ESP_ERR_INVALID_SIZE.
  Reject invalid QoS with ESP_ERR_INVALID_ARG.
  Update existing slot if found.
  Allocate copy with wifi_link_strdup_or_null() into first free slot.
  Return ESP_ERR_NO_MEM if no slot or allocation fails.

remove:
  Reject NULL or empty topic with ESP_ERR_INVALID_ARG.
  Return ESP_ERR_NOT_FOUND if not present.
  Free topic, clear slot, return ESP_OK.
```

Implement replay:

```c
static esp_err_t wifi_link_replay_subscriptions(wifi_link_t *me,
                                                esp_mqtt_client_handle_t client)
{
    esp_err_t ret = ESP_OK;

    xSemaphoreTake(me->mutex, portMAX_DELAY);
    for (int i = 0; i < me->sub_table_size; ++i) {
        if (!me->sub_table[i].in_use) {
            continue;
        }
        const char *topic = me->sub_table[i].topic;
        const network_mqtt_qos_t qos = me->sub_table[i].qos;
        xSemaphoreGive(me->mutex);
        const int msg_id = esp_mqtt_client_subscribe(client, topic, (int)qos);
        if (msg_id < 0) {
            ret = ESP_FAIL;
        }
        xSemaphoreTake(me->mutex, portMAX_DELAY);
        if (ret != ESP_OK) {
            break;
        }
    }
    xSemaphoreGive(me->mutex);
    return ret;
}
```

- [ ] **Step 3: Implement publish, subscribe, unsubscribe, and RX callback ops**

Implement `wifi_link_publish_impl()`:

```text
1. Validate base, req, req->topic, payload pointer/length consistency, and QoS.
2. Take mutex.
3. If mqtt_connected is false or mqtt_client is NULL, release mutex and return ESP_ERR_INVALID_STATE.
4. Copy client handle, release mutex.
5. Call esp_mqtt_client_enqueue(client, req->topic, (const char *)req->payload, (int)req->payload_len, (int)req->qos, req->retain ? 1 : 0, false).
6. Return ESP_OK when msg_id >= 0, otherwise ESP_FAIL.
```

Implement `wifi_link_subscribe_impl()`:

```text
1. Validate base, topic, and QoS.
2. Take mutex and store subscription intent with wifi_link_store_subscription_locked().
3. If mqtt_connected and mqtt_client are present, copy client handle and release mutex; otherwise release mutex and return ESP_OK.
4. Call esp_mqtt_client_subscribe(client, topic, (int)qos).
5. Return ESP_OK when msg_id >= 0, otherwise ESP_FAIL.
```

Implement `wifi_link_unsubscribe_impl()`:

```text
1. Validate base and topic.
2. Take mutex and remove subscription intent with wifi_link_remove_subscription_locked().
3. If mqtt_connected and mqtt_client are present, copy client handle and release mutex; otherwise release mutex and return the remove result.
4. Call esp_mqtt_client_unsubscribe(client, topic).
5. Return ESP_OK when msg_id >= 0, otherwise ESP_FAIL.
```

Implement `wifi_link_register_rx_cb_impl()`:

```c
static esp_err_t wifi_link_register_rx_cb_impl(network_link_t *base,
                                               network_rx_cb_t cb,
                                               void *ctx)
{
    wifi_link_t *me = wifi_link_from_base(base);
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    me->rx_cb = cb;
    me->rx_ctx = ctx;
    xSemaphoreGive(me->mutex);
    return ESP_OK;
}
```

This implementation intentionally allows `cb == NULL` to clear the callback, matching the implementation design even though `network_link_register_rx_cb()` currently rejects NULL at the wrapper layer.

- [ ] **Step 4: Build checkpoint**

Run: `idf.py build`

Expected: PASS or fail with concrete ESP-IDF API field-name errors from `esp_mqtt_client_config_t` or Wi-Fi APIs. If field names fail, inspect the local ESP-MQTT headers and correct the exact field names while preserving the same behavior.

- [ ] **Step 5: Checkpoint without committing**

Run: `git status --short`

Expected: new `main/network/wifi/*`, new `main/metering/*`, modified `main/CMakeLists.txt`, and modified `main/idf_component.yml`. Do not run `git commit` unless the user explicitly requests commits.

## Task 8: Final Verification And Contract Review

**Files:**
- Review: `docs/agents/classes.md:881-1201`
- Review: `docs/superpowers/specs/2026-05-24-modules-7-8-design.md`
- Review: `main/metering/metering_service.h`
- Review: `main/network/wifi/wifi_link.h`
- Review: `main/network/wifi/wifi_link.c`
- Review: `main/metering/metering_service.c`

- [ ] **Step 1: Run ESP-IDF MCP build**

Run the available ESP-IDF MCP build tool for the project.

Expected: PASS. If it fails, record the exact compiler or linker error and fix only the module 7/8 files or build manifest entries needed for that error.

- [ ] **Step 2: If MCP build is unavailable, run shell build fallback**

Run:

```bash
source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build
```

Expected: PASS. If dependency resolution downloads `espressif/mqtt`, allow it to complete. If the failure is a network download issue, report it separately from compile errors.

- [ ] **Step 3: Verify public API signatures**

Check these signatures exist exactly:

```c
metering_service_t *metering_service_create(const metering_config_t *config);
esp_err_t metering_service_destroy(metering_service_t *me);
esp_err_t metering_service_start(metering_service_t *me);
esp_err_t metering_service_stop(metering_service_t *me);
esp_err_t metering_service_get_latest(metering_service_t *me,
                                      metering_snapshot_t *out);
esp_err_t metering_service_reset_energy(metering_service_t *me);
network_link_t *wifi_link_create(const wifi_link_config_t *config);
```

Expected: all signatures match `docs/agents/classes.md`.

- [ ] **Step 4: Verify no forbidden dependency direction was introduced**

Run content searches or inspect includes to confirm:

```text
main/metering/metering_service.c does not include relay.h, thingsboard headers, display headers, safety headers, or board_pinmap.h.
main/network/wifi/wifi_link.c does not include thingsboard headers, metering headers, relay headers, safety headers, display headers, or board_pinmap.h.
```

Expected: dependency boundaries match the spec.

- [ ] **Step 5: Check final worktree state without committing**

Run: `git status --short`

Expected: only intended module 7/8 files and build/dependency files are changed, plus the already-created spec and this plan. Do not run `git commit` unless the user explicitly requests commits.
