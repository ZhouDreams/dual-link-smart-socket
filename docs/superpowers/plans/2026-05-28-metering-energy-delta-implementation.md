# Metering Energy Delta Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Change energy telemetry from local cumulative `totalEnergy` to metering-owned interval `energyDelta` in mWh, with 1 Hz direct sampling and frequency upload.

**Architecture:** Move CF-counter-to-energy-delta state into a pure `metering_service_internal` helper that `metering_service` owns. `app_controller` publishes `energyDelta` and `frequency`, then calls a metering confirmation API after publish success. ThingsBoard JSON no longer contains `totalEnergy`.

**Tech Stack:** ESP-IDF C, FreeRTOS mutexes/events, BL0942 CF counter, ThingsBoard MQTT telemetry, host C tests via `test/host/run_host_tests.sh`.

---

## File Structure

- Create `main/metering/metering_service_internal.h`: pure internal energy-delta state types and APIs used by firmware and host tests.
- Create `main/metering/metering_service_internal.c`: wrap-safe CF delta, mWh conversion, pending token, and confirmation logic.
- Create `test/host/test_metering_service_internal.c`: host tests for first baseline, wrap, residual, failed-publish retry, stale confirmation rejection.
- Modify `test/host/run_host_tests.sh`: compile and run the new metering internal host test.
- Modify `main/CMakeLists.txt`: include `metering/metering_service_internal.c` in firmware build.
- Modify `main/metering/metering_service.h`: replace cumulative `total_energy` snapshot field with `energy_delta` and `energy_delta_token`; add confirmation API; remove window-only config fields.
- Modify `main/metering/metering_service.c`: remove window aggregation and local total-energy accumulation; emit each valid BL0942 sample as one snapshot; compute `energy_delta`; confirm baseline only through the new API; update logs.
- Modify `main/app/app_controller_internal.h` and `main/app/app_controller_internal.c`: rename telemetry flow fields from `total_energy` to `energy_delta`, add `frequency`.
- Modify `main/app/app_controller.c`: pass `energy_delta` and `frequency`, call `metering_service_confirm_energy_delta()` after publish success, call `metering_service_discard_energy_delta()` after publish failure, unify log format.
- Modify `main/thingsboard/thingsboard_client.h`, `main/thingsboard/thingsboard_client_internal.h`, `main/thingsboard/thingsboard_client.c`, and `main/thingsboard/thingsboard_client_internal.c`: replace `totalEnergy` with `energyDelta`, add `frequency`.
- Modify `test/host/test_thingsboard_client_internal.c` and `test/host/test_app_controller_internal.c`: update host assertions for new telemetry contract.
- Modify `main/display/lvgl/lvgl_dashboard.h`, `main/display/lvgl/lvgl_dashboard.c`, and `main/display/lvgl/lvgl_dashboard_internal.c`: compile against `energy_delta` and show mWh instead of stale cumulative Wh.
- Modify `main/main.c`: configure BL0942 with `sample_period_ms = 1000` and stop passing removed metering window fields.
- Modify `docs/agents/classes.md`: update metering class contract before implementation details are considered complete.

### Commit Policy

This plan does not include git commit steps because the repository-level instruction says commits require explicit user request. After each task, inspect `git diff` and continue without committing unless the user asks for a commit.

---

### Task 1: Add Pure Metering Energy Delta Helper

**Files:**
- Create: `main/metering/metering_service_internal.h`
- Create: `main/metering/metering_service_internal.c`
- Create: `test/host/test_metering_service_internal.c`
- Modify: `test/host/run_host_tests.sh`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Write the failing host test**

Create `test/host/test_metering_service_internal.c` with these tests:

```c
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "metering_service_internal.h"

static void assert_float_near(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.001f);
}

static void test_first_sample_establishes_baseline(void)
{
    metering_energy_delta_state_t state = {0};
    metering_energy_delta_result_t result = {0};

    metering_energy_delta_state_init(&state);

    assert(metering_energy_delta_prepare(&state, 10U, &result) == ESP_OK);
    assert_float_near(result.energy_delta_mwh, 0.0f);
    assert(result.token != 0U);
    assert(result.baseline_established == true);
    assert(metering_energy_delta_confirm(&state, result.token) == ESP_OK);
}

static void test_confirmed_delta_preserves_residual(void)
{
    metering_energy_delta_state_t state = {0};
    metering_energy_delta_result_t baseline = {0};
    metering_energy_delta_result_t two_pulses = {0};
    metering_energy_delta_result_t one_pulse = {0};

    metering_energy_delta_state_init(&state);
    assert(metering_energy_delta_prepare(&state, 10U, &baseline) == ESP_OK);
    assert(metering_energy_delta_confirm(&state, baseline.token) == ESP_OK);

    assert(metering_energy_delta_prepare(&state, 12U, &two_pulses) == ESP_OK);
    assert_float_near(two_pulses.energy_delta_mwh, 124.595f);
    assert(metering_energy_delta_confirm(&state, two_pulses.token) == ESP_OK);

    assert(metering_energy_delta_prepare(&state, 13U, &one_pulse) == ESP_OK);
    assert_float_near(one_pulse.energy_delta_mwh, 62.298f);
    assert(metering_energy_delta_confirm(&state, one_pulse.token) == ESP_OK);
}

static void test_failed_publish_keeps_old_confirmed_baseline(void)
{
    metering_energy_delta_state_t state = {0};
    metering_energy_delta_result_t baseline = {0};
    metering_energy_delta_result_t failed = {0};
    metering_energy_delta_result_t retry = {0};

    metering_energy_delta_state_init(&state);
    assert(metering_energy_delta_prepare(&state, 100U, &baseline) == ESP_OK);
    assert(metering_energy_delta_confirm(&state, baseline.token) == ESP_OK);

    assert(metering_energy_delta_prepare(&state, 101U, &failed) == ESP_OK);
    assert_float_near(failed.energy_delta_mwh, 62.297f);

    assert(metering_energy_delta_prepare(&state, 102U, &retry) == ESP_OK);
    assert_float_near(retry.energy_delta_mwh, 124.595f);
    assert(metering_energy_delta_confirm(&state, retry.token) == ESP_OK);
}

static void test_wrap_safe_delta(void)
{
    metering_energy_delta_state_t state = {0};
    metering_energy_delta_result_t baseline = {0};
    metering_energy_delta_result_t wrapped = {0};

    metering_energy_delta_state_init(&state);
    assert(metering_energy_delta_prepare(&state, 0x00FFFFFEUL, &baseline) == ESP_OK);
    assert(metering_energy_delta_confirm(&state, baseline.token) == ESP_OK);

    assert(metering_energy_delta_prepare(&state, 1U, &wrapped) == ESP_OK);
    assert_float_near(wrapped.energy_delta_mwh, 186.893f);
}

static void test_stale_confirmation_is_rejected(void)
{
    metering_energy_delta_state_t state = {0};
    metering_energy_delta_result_t baseline = {0};
    metering_energy_delta_result_t old_pending = {0};
    metering_energy_delta_result_t new_pending = {0};

    metering_energy_delta_state_init(&state);
    assert(metering_energy_delta_prepare(&state, 1U, &baseline) == ESP_OK);
    assert(metering_energy_delta_confirm(&state, baseline.token) == ESP_OK);

    assert(metering_energy_delta_prepare(&state, 2U, &old_pending) == ESP_OK);
    assert(metering_energy_delta_prepare(&state, 3U, &new_pending) == ESP_OK);
    assert(metering_energy_delta_confirm(&state, old_pending.token) == ESP_ERR_INVALID_STATE);
    assert(metering_energy_delta_confirm(&state, new_pending.token) == ESP_OK);
}

int main(void)
{
    test_first_sample_establishes_baseline();
    test_confirmed_delta_preserves_residual();
    test_failed_publish_keeps_old_confirmed_baseline();
    test_wrap_safe_delta();
    test_stale_confirmation_is_rejected();

    printf("metering internal tests passed\n");

    return 0;
}
```

- [ ] **Step 2: Wire the failing test into the host runner**

Insert this compile/run block in `test/host/run_host_tests.sh` after the Wi-Fi internal test block:

```bash
"${CC_BIN}" -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/test/support" \
    -I"${ROOT_DIR}/main/metering" \
    "${ROOT_DIR}/main/metering/metering_service_internal.c" \
    "${ROOT_DIR}/test/host/test_metering_service_internal.c" \
    -o "${BUILD_DIR}/test_metering_service_internal"

"${BUILD_DIR}/test_metering_service_internal"
```

- [ ] **Step 3: Run the host tests and verify the new test fails to compile**

Run: `./test/host/run_host_tests.sh`

Expected: failure mentioning missing `metering_service_internal.c` or `metering_service_internal.h`.

- [ ] **Step 4: Create the internal helper header**

Create `main/metering/metering_service_internal.h`:

```c
/**
 * @file metering_service_internal.h
 * @brief Metering service internal pure helpers
 * @details Host-testable helpers for BL0942 CF counter energy deltas
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

/*********************
 *      DEFINES
 *********************/

#define METERING_ENERGY_CF_CNT_U24_MASK       (0x00FFFFFFUL)
#define METERING_ENERGY_PULSE_NWH             (62297938ULL)
#define METERING_ENERGY_NWH_PER_MWH_Q3        (1000ULL)

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    bool have_confirmed_cf_cnt_raw;
    uint32_t confirmed_cf_cnt_raw;
    uint32_t confirmed_residual_nwh;
    bool have_pending;
    uint32_t pending_cf_cnt_raw;
    uint32_t pending_residual_nwh;
    uint32_t pending_token;
    uint32_t next_token;
} metering_energy_delta_state_t;

typedef struct {
    float energy_delta_mwh;
    uint32_t token;
    bool baseline_established;
} metering_energy_delta_result_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

void metering_energy_delta_state_init(metering_energy_delta_state_t *state);

esp_err_t metering_energy_delta_prepare(metering_energy_delta_state_t *state,
                                        uint32_t cf_cnt_raw,
                                        metering_energy_delta_result_t *out);

esp_err_t metering_energy_delta_confirm(metering_energy_delta_state_t *state,
                                        uint32_t token);

uint32_t metering_energy_u24_delta(uint32_t current, uint32_t previous);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
```

- [ ] **Step 5: Create the internal helper implementation**

Create `main/metering/metering_service_internal.c`:

```c
/**
 * @file metering_service_internal.c
 * @brief Metering service internal pure helpers
 * @details Host-testable helpers for BL0942 CF counter energy deltas
 */

/*********************
 *      INCLUDES
 *********************/

#include "metering_service_internal.h"

#include <string.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static uint32_t metering_energy_next_token(metering_energy_delta_state_t *state);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void metering_energy_delta_state_init(metering_energy_delta_state_t *state)
{
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->next_token = 1U;
}

esp_err_t metering_energy_delta_prepare(metering_energy_delta_state_t *state,
                                        uint32_t cf_cnt_raw,
                                        metering_energy_delta_result_t *out)
{
    uint64_t total_nwh = 0ULL;
    uint64_t mwh_thousandths = 0ULL;
    uint32_t current_cf_cnt_raw = 0U;
    uint32_t delta = 0U;
    uint32_t next_residual_nwh = 0U;
    uint32_t token = 0U;

    if (state == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    current_cf_cnt_raw = cf_cnt_raw & METERING_ENERGY_CF_CNT_U24_MASK;
    token = metering_energy_next_token(state);

    if (!state->have_confirmed_cf_cnt_raw) {
        state->have_confirmed_cf_cnt_raw = true;
        state->confirmed_cf_cnt_raw = current_cf_cnt_raw;
        state->confirmed_residual_nwh = 0U;
        out->baseline_established = true;
    } else {
        delta = metering_energy_u24_delta(current_cf_cnt_raw,
                                          state->confirmed_cf_cnt_raw);
        total_nwh = ((uint64_t)state->confirmed_residual_nwh) +
                    ((uint64_t)delta * METERING_ENERGY_PULSE_NWH);
        mwh_thousandths = total_nwh / METERING_ENERGY_NWH_PER_MWH_Q3;
        next_residual_nwh = (uint32_t)(total_nwh %
                                       METERING_ENERGY_NWH_PER_MWH_Q3);
        out->energy_delta_mwh = (float)((double)mwh_thousandths / 1000.0);
    }

    state->pending_cf_cnt_raw = current_cf_cnt_raw;
    state->pending_residual_nwh = next_residual_nwh;
    state->pending_token = token;
    state->have_pending = true;
    out->token = token;

    return ESP_OK;
}

esp_err_t metering_energy_delta_confirm(metering_energy_delta_state_t *state,
                                        uint32_t token)
{
    if (state == NULL || token == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!state->have_pending || state->pending_token != token) {
        return ESP_ERR_INVALID_STATE;
    }

    state->confirmed_cf_cnt_raw = state->pending_cf_cnt_raw;
    state->confirmed_residual_nwh = state->pending_residual_nwh;
    state->have_confirmed_cf_cnt_raw = true;
    state->have_pending = false;
    return ESP_OK;
}

uint32_t metering_energy_u24_delta(uint32_t current, uint32_t previous)
{
    current &= METERING_ENERGY_CF_CNT_U24_MASK;
    previous &= METERING_ENERGY_CF_CNT_U24_MASK;
    return (current - previous) & METERING_ENERGY_CF_CNT_U24_MASK;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static uint32_t metering_energy_next_token(metering_energy_delta_state_t *state)
{
    uint32_t token = state->next_token;

    state->next_token++;
    if (state->next_token == 0U) {
        state->next_token = 1U;
    }
    if (token == 0U) {
        token = metering_energy_next_token(state);
    }

    return token;
}
```

- [ ] **Step 6: Include the helper in firmware build**

Add this source to `main/CMakeLists.txt` near `metering/metering_service.c`:

```cmake
        "metering/metering_service_internal.c"
```

- [ ] **Step 7: Run host tests**

Run: `./test/host/run_host_tests.sh`

Expected: output includes `metering internal tests passed` and `host tests passed`.

---

### Task 2: Update Metering Public Contract And Direct 1 Hz Snapshot Flow

**Files:**
- Modify: `main/metering/metering_service.h`
- Modify: `main/metering/metering_service.c`
- Modify: `main/main.c`
- Modify: `docs/agents/classes.md`

- [ ] **Step 1: Update the metering public header**

In `main/metering/metering_service.h`, change `metering_config_t` to only contain conversion coefficients:

```c
typedef struct {
    float v_rms_coeff;       /**< 电压转换系数； Voltage conversion coefficient */
    float i_rms_coeff;       /**< 电流转换系数； Current conversion coefficient */
    float watt_coeff;        /**< 功率转换系数； Power conversion coefficient */
} metering_config_t;
```

Change `metering_snapshot_t` to use interval energy:

```c
typedef struct {
    float voltage;              /**< 电压 V； Voltage in volts */
    float current;              /**< 电流 A； Current in amperes */
    float power;                /**< 有功功率 W； Active power in watts */
    float energy_delta;         /**< 上报区间电能增量 mWh； Interval energy delta in milliwatt-hours */
    float frequency;            /**< 电网频率 Hz； Grid frequency in hertz */
    uint64_t timestamp_us;      /**< 快照时间戳； Snapshot timestamp in microseconds */
    uint32_t energy_delta_token;/**< 电能增量确认令牌； Energy delta confirmation token */
    bool valid;                 /**< 快照是否有效； Whether snapshot is valid */
} metering_snapshot_t;
```

Change the event comment:

```c
typedef enum {
    METERING_EVENT_SNAPSHOT = 0, /**< 单次快照； Single sample snapshot */
} metering_event_id_t;
```

Replace the reset comment and add the confirmation/discard APIs:

```c
/**
 * @brief 重置电能增量基准
 * @details Reset the energy delta baseline
 */
esp_err_t metering_service_reset_energy(metering_service_t *me);

/**
 * @brief 确认电能增量已成功上报
 * @details Confirm that a snapshot energy delta was published successfully
 */
esp_err_t metering_service_confirm_energy_delta(
    metering_service_t *me, const metering_snapshot_t *snapshot);

/**
 * @brief 放弃电能增量上报
 * @details Release a failed-publish energy delta without advancing the baseline
 */
esp_err_t metering_service_discard_energy_delta(
    metering_service_t *me, const metering_snapshot_t *snapshot);
```

- [ ] **Step 2: Update metering implementation includes and state**

In `main/metering/metering_service.c`, include the new helper:

```c
#include "metering_service_internal.h"
```

Change `struct metering_service` by removing window and total-energy fields, and adding helper state:

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

Remove `METERING_DEFAULT_WINDOW_SAMPLES`, `METERING_DEFAULT_PUBLISH_PERIOD_MS`, `METERING_CF_CNT_U24_MASK`, `METERING_PULSE_ENERGY_NWH`, `METERING_NWH_PER_WH`, `metering_u24_delta()`, and `metering_update_energy_locked()` from `metering_service.c`.

- [ ] **Step 3: Initialize and reset energy delta state**

After allocating and zeroing `metering_service_t` in `metering_service_create()`, initialize the helper state:

```c
metering_energy_delta_state_init(&me->energy_delta_state);
```

Replace `metering_apply_defaults()` with coefficient-only default handling:

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
}
```

Update `metering_service_reset_energy()` to reset the helper state and latest snapshot delta:

```c
esp_err_t metering_service_reset_energy(metering_service_t *me)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "service is null");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "service is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    metering_energy_delta_state_init(&me->energy_delta_state);
    if (me->has_latest) {
        me->latest.energy_delta = 0.0f;
        me->latest.energy_delta_token = 0U;
    }

    (void)xSemaphoreGive(me->mutex);
    return ESP_OK;
}
```

- [ ] **Step 4: Add the confirmation API implementation**

Add this function after `metering_service_reset_energy()`:

```c
esp_err_t metering_service_confirm_energy_delta(
    metering_service_t *me, const metering_snapshot_t *snapshot)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL && snapshot != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "confirm args are null");
    ESP_RETURN_ON_FALSE(snapshot->valid, ESP_ERR_INVALID_ARG, TAG,
                        "snapshot is invalid");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "service is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    ret = metering_energy_delta_confirm(&me->energy_delta_state,
                                        snapshot->energy_delta_token);

    (void)xSemaphoreGive(me->mutex);
    return ret;
}
```

- [ ] **Step 5: Convert each BL0942 sample into one snapshot**

Replace the body of `metering_on_bl0942_measurement()` after lifecycle checks with direct snapshot emission:

```c
    metering_energy_delta_result_t energy_delta = {0};

    metering_convert_with_config(me, measurement, &sample);
    if (!sample.valid) {
        (void)xSemaphoreGive(me->mutex);
        return;
    }

    if (metering_energy_delta_prepare(&me->energy_delta_state,
                                      measurement->cf_cnt_raw,
                                      &energy_delta) != ESP_OK) {
        (void)xSemaphoreGive(me->mutex);
        return;
    }
    sample.energy_delta = energy_delta.energy_delta_mwh;
    sample.energy_delta_token = energy_delta.token;

    me->latest = sample;
    me->has_latest = true;
    snapshot = sample;
    emit = true;
```

Remove all window sum updates and the window-ready condition. Keep the existing unlock and post flow.

- [ ] **Step 6: Update metering logs and fault handling**

In the `if (emit)` block, log every 1 Hz snapshot with the unified format:

```c
        s_snapshot_log_count++;
        ESP_LOGI(TAG,
                 "metering snapshot #%lu: V=%.2fV I=%.3fA P=%.2fW E=%.3fmWh F=%.2fHz valid=%d",
                 (unsigned long)s_snapshot_log_count,
                 (double)snapshot.voltage,
                 (double)snapshot.current,
                 (double)snapshot.power,
                 (double)snapshot.energy_delta,
                 (double)snapshot.frequency,
                 snapshot.valid ? 1 : 0);
```

In `metering_on_bl0942_fault()`, remove references to total energy and reset the helper state on hard reset:

```c
    if (fault_info != NULL && fault_info->hard_reset_attempted) {
        metering_energy_delta_state_init(&me->energy_delta_state);
    }
    snapshot.energy_delta = 0.0f;
    snapshot.energy_delta_token = 0U;
    snapshot.valid = false;
```

- [ ] **Step 7: Update metering self-test energy checks**

In `metering_run_conversion_selftest()`, replace the old accumulated Wh checks with helper checks:

```c
    metering_energy_delta_state_t energy_state = {0};
    metering_energy_delta_result_t first_delta = {0};
    metering_energy_delta_result_t second_delta = {0};
```

At the end of the self-test, use:

```c
    metering_energy_delta_state_init(&energy_state);
    ESP_RETURN_ON_FALSE(metering_energy_delta_prepare(&energy_state, 10U,
                                                      &first_delta) == ESP_OK,
                        ESP_FAIL, TAG, "energy baseline prepare failed");
    ESP_RETURN_ON_FALSE(first_delta.energy_delta_mwh == 0.0f &&
                            first_delta.token != 0U,
                        ESP_FAIL, TAG, "energy baseline self-test failed");
    ESP_RETURN_ON_FALSE(metering_energy_delta_confirm(&energy_state,
                                                      first_delta.token) == ESP_OK,
                        ESP_FAIL, TAG, "energy baseline confirm failed");
    ESP_RETURN_ON_FALSE(metering_energy_delta_prepare(&energy_state, 12U,
                                                      &second_delta) == ESP_OK,
                        ESP_FAIL, TAG, "energy delta prepare failed");
    ESP_RETURN_ON_FALSE(second_delta.energy_delta_mwh > 124.594f &&
                            second_delta.energy_delta_mwh < 124.596f,
                        ESP_FAIL, TAG, "energy delta self-test failed");
```

- [ ] **Step 8: Configure BL0942 for 1 Hz sampling and simplify metering config**

In `main/main.c`, change BL0942 sampling:

```c
        .sample_period_ms = 1000,
```

Change metering service creation to:

```c
    metering_service_t *metering = metering_service_create(&(metering_config_t) {
    });
```

- [ ] **Step 9: Update class documentation before considering the contract complete**

In `docs/agents/classes.md`, update section 7 so it describes:

```c
typedef struct {
    float v_rms_coeff;
    float i_rms_coeff;
    float watt_coeff;
} metering_config_t;

typedef struct {
    float voltage;
    float current;
    float power;
    float energy_delta;
    float frequency;
    uint64_t timestamp_us;
    uint32_t energy_delta_token;
    bool valid;
} metering_snapshot_t;

esp_err_t metering_service_confirm_energy_delta(
    metering_service_t *me, const metering_snapshot_t *snapshot);
```

Also update the section text from “window aggregation / total energy Wh” to “single 1 Hz snapshot / interval energy delta mWh / confirmation after successful cloud publish / discard after failed cloud publish”.

- [ ] **Step 10: Run host tests**

Run: `./test/host/run_host_tests.sh`

Expected: host tests still pass through the new metering internal test. Tests that compile app or ThingsBoard may fail later until Task 3 and Task 4 update telemetry structs.

---

### Task 3: Update ThingsBoard Telemetry Contract

**Files:**
- Modify: `main/thingsboard/thingsboard_client.h`
- Modify: `main/thingsboard/thingsboard_client_internal.h`
- Modify: `main/thingsboard/thingsboard_client.c`
- Modify: `main/thingsboard/thingsboard_client_internal.c`
- Modify: `test/host/test_thingsboard_client_internal.c`

- [ ] **Step 1: Update the failing ThingsBoard host test first**

In `test/host/test_thingsboard_client_internal.c`, update the telemetry fixture:

```c
    const tb_internal_telemetry_t telemetry = {
        .voltage = 230.12f,
        .current = 1.234f,
        .power = 284.05f,
        .energy_delta = 12.345f,
        .frequency = 50.02f,
        .relay_on = true,
        .active_link = "wifi",
        .safety_level = SAFETY_GUARD_LEVEL_WARNING,
        .valid = true,
    };
```

Update assertions after formatting:

```c
    assert(strstr(buf, "\"voltage\":230.12") != NULL);
    assert(strstr(buf, "\"current\":1.234") != NULL);
    assert(strstr(buf, "\"power\":284.05") != NULL);
    assert(strstr(buf, "\"energyDelta\":12.345") != NULL);
    assert(strstr(buf, "\"frequency\":50.02") != NULL);
    assert(strstr(buf, "\"totalEnergy\":") == NULL);
    assert(strstr(buf, "\"relayOn\":true") != NULL);
    assert(strstr(buf, "\"activeLink\":\"wifi\"") != NULL);
```

- [ ] **Step 2: Run the host tests and verify ThingsBoard test fails**

Run: `./test/host/run_host_tests.sh`

Expected: compile failure for missing `energy_delta` or `frequency` in `tb_internal_telemetry_t`.

- [ ] **Step 3: Update public and internal telemetry types**

In `main/thingsboard/thingsboard_client.h`, replace `total_energy` with `energy_delta` and add `frequency`:

```c
typedef struct {
    float voltage;                         /**< 电压 V； Voltage in volts */
    float current;                         /**< 电流 A； Current in amperes */
    float power;                           /**< 功率 W； Power in watts */
    float energy_delta;                    /**< 上报区间电能增量 mWh； Interval energy delta in milliwatt-hours */
    float frequency;                       /**< 电网频率 Hz； Grid frequency in hertz */
    bool relay_on;                         /**< 继电器状态； Relay state */
    const char *active_link;               /**< 当前活动链路； Active link */
    safety_guard_level_t safety_level;     /**< 安全等级； Safety level */
    bool valid;                            /**< 数据是否有效； Whether data is valid */
} tb_telemetry_input_t;
```

In `main/thingsboard/thingsboard_client_internal.h`, make the same field change for `tb_internal_telemetry_t`.

- [ ] **Step 4: Update ThingsBoard client mapping**

In `main/thingsboard/thingsboard_client.c`, replace total-energy mapping with:

```c
    internal_input.energy_delta = input->energy_delta;
    internal_input.frequency = input->frequency;
```

- [ ] **Step 5: Update JSON formatting**

In `main/thingsboard/thingsboard_client_internal.c`, replace the telemetry JSON format with:

```c
    written = snprintf(buf, buf_size,
                       "{\"voltage\":%.2f,\"current\":%.3f,"
                       "\"power\":%.2f,\"energyDelta\":%.3f,"
                       "\"frequency\":%.2f,\"relayOn\":%s,"
                       "\"activeLink\":\"%s\",\"safetyLevel\":%d,"
                       "\"valid\":%s}",
                       input->voltage, input->current, input->power,
                       input->energy_delta, input->frequency,
                       input->relay_on ? "true" : "false",
                       active_link, (int)input->safety_level,
                       input->valid ? "true" : "false");
```

- [ ] **Step 6: Run host tests**

Run: `./test/host/run_host_tests.sh`

Expected: ThingsBoard internal tests pass. App controller internal test may fail until Task 4 updates app telemetry fields.

---

### Task 4: Update App Controller Telemetry Flow And Confirmation

**Files:**
- Modify: `main/app/app_controller_internal.h`
- Modify: `main/app/app_controller_internal.c`
- Modify: `main/app/app_controller.c`
- Modify: `test/host/test_app_controller_internal.c`

- [ ] **Step 1: Update the failing app-controller host test first**

In `test/host/test_app_controller_internal.c`, change the telemetry source fixture:

```c
    const app_controller_telemetry_source_t source = {
        .voltage = 229.5f,
        .current = 1.25f,
        .power = 286.8f,
        .energy_delta = 3.125f,
        .frequency = 50.01f,
        .metering_valid = true,
        .relay_on = true,
        .relay_known = true,
        .active_link = NETWORK_LINK_TYPE_LTE,
        .safety_level = SAFETY_GUARD_LEVEL_NORMAL,
        .safety_valid = true,
    };
```

Replace total-energy assertions with:

```c
    assert(out.energy_delta > 3.124f);
    assert(out.energy_delta < 3.126f);
    assert(out.frequency > 50.00f);
    assert(out.frequency < 50.02f);
```

- [ ] **Step 2: Run host tests and verify app test fails**

Run: `./test/host/run_host_tests.sh`

Expected: compile failure for missing `energy_delta` or `frequency` in app telemetry structs.

- [ ] **Step 3: Update app telemetry structs**

In `main/app/app_controller_internal.h`, replace the total-energy field in both source and output structs:

```c
    float energy_delta;                    /**< 电能增量 mWh； Energy delta in milliwatt-hours */
    float frequency;                       /**< 电网频率 Hz； Grid frequency in hertz */
```

- [ ] **Step 4: Update app telemetry mapping helper**

In `main/app/app_controller_internal.c`, replace the total-energy assignment:

```c
    out->energy_delta = source->energy_delta;
    out->frequency = source->frequency;
```

- [ ] **Step 5: Update publish telemetry source mapping**

In `main/app/app_controller.c`, change source mapping:

```c
    source.voltage = snapshot->voltage;
    source.current = snapshot->current;
    source.power = snapshot->power;
    source.energy_delta = snapshot->energy_delta;
    source.frequency = snapshot->frequency;
```

Change ThingsBoard input mapping:

```c
    input.voltage = output.voltage;
    input.current = output.current;
    input.power = output.power;
    input.energy_delta = output.energy_delta;
    input.frequency = output.frequency;
```

- [ ] **Step 6: Confirm energy delta only after publish success**

In `app_controller_publish_telemetry()`, inside `if (ret == ESP_OK)`, call metering confirmation before logging:

```c
        const esp_err_t confirm_ret =
            metering_service_confirm_energy_delta(me->cfg.metering, snapshot);
        if (confirm_ret != ESP_OK) {
            ESP_LOGW(TAG, "confirm energy delta failed: %s",
                     esp_err_to_name(confirm_ret));
        }
```

Keep publish return behavior unchanged: `app_controller_publish_telemetry()` still returns the ThingsBoard publish result.

- [ ] **Step 6A: Discard energy delta after publish failure**

After the success block, add failure handling that releases the pending token without advancing the metering baseline:

```c
    if (ret != ESP_OK) {
        const esp_err_t discard_ret =
            metering_service_discard_energy_delta(me->cfg.metering, snapshot);
        if (discard_ret != ESP_OK) {
            ESP_LOGW(TAG, "discard energy delta failed: %s",
                     esp_err_to_name(discard_ret));
        }
    }
```

This keeps energy conversion inside `metering_service` while allowing the next snapshot to include the failed interval.

- [ ] **Step 7: Unify app publish log with metering log**

Replace the success log with:

```c
        ESP_LOGI(TAG,
                 "telemetry publish #%lu ok: V=%.2fV I=%.3fA P=%.2fW E=%.3fmWh F=%.2fHz valid=%d link=%s",
                 (unsigned long)s_telemetry_publish_count,
                 (double)input.voltage,
                 (double)input.current,
                 (double)input.power,
                 (double)input.energy_delta,
                 (double)input.frequency,
                 input.valid ? 1 : 0,
                 input.active_link);
```

- [ ] **Step 8: Run host tests**

Run: `./test/host/run_host_tests.sh`

Expected: app controller internal tests pass with energy delta and frequency fields.

---

### Task 5: Update Display Compile Path For Energy Delta

**Files:**
- Modify: `main/display/lvgl/lvgl_dashboard.h`
- Modify: `main/display/lvgl/lvgl_dashboard.c`
- Modify: `main/display/lvgl/lvgl_dashboard_internal.c`
- Modify: `main/display/lvgl/lvgl_dashboard_internal.h`

- [ ] **Step 1: Rename dashboard state energy field**

In `main/display/lvgl/lvgl_dashboard.h`, replace `total_energy` with:

```c
    float energy_delta;                    /**< 电能增量 mWh； Energy delta in milliwatt-hours */
```

- [ ] **Step 2: Update metering snapshot ingestion**

In `main/display/lvgl/lvgl_dashboard.c`, replace:

```c
    me->state_cache.total_energy = snapshot->total_energy;
```

with:

```c
    me->state_cache.energy_delta = snapshot->energy_delta;
```

- [ ] **Step 3: Update energy text usage**

In `lvgl_dashboard_apply_state()`, replace:

```c
state->total_energy
```

with:

```c
state->energy_delta
```

Change the empty-state energy text definition near the top of `lvgl_dashboard.c`:

```c
#define LVGL_DASHBOARD_PLACEHOLDER_ENERGY     "--.--- mWh"
```

- [ ] **Step 4: Update display formatter units**

In `main/display/lvgl/lvgl_dashboard_internal.h`, rename the parameter comment for `lvgl_dashboard_internal_format_energy()` to mWh.

In `main/display/lvgl/lvgl_dashboard_internal.c`, update the function body:

```c
esp_err_t lvgl_dashboard_internal_format_energy(char *out, size_t out_len,
                                                float energy_mwh)
{
    int written = 0;

    if (out == NULL || out_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(out, out_len, "%.3f mWh", energy_mwh);
    if (written < 0 || (size_t)written >= out_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}
```

- [ ] **Step 5: Run host tests**

Run: `./test/host/run_host_tests.sh`

Expected: host tests pass. Display code is not covered by host tests, so firmware build in Task 6 verifies compile correctness.

---

### Task 6: Build Verification And Source Cleanup

**Files:**
- Verify all touched files.

- [ ] **Step 1: Search for removed cumulative energy names**

Run: use repository search for these patterns:

```text
total_energy
window_samples
publish_period_ms
cf_coeff
```

Expected remaining results:

- `total_energy` may remain only in unrelated historical docs or old committed plan text.
- `totalEnergy` may remain only in historical docs or old committed plan text.
- `window_samples`, `publish_period_ms`, and `cf_coeff` may remain only in historical docs or old committed plan text.

If any result appears under `main/` or `test/host/`, update it to the new `energy_delta` or remove it.

- [ ] **Step 2: Check git diff for accidental unrelated changes**

Run: `git diff --stat`

Expected: only files listed in this plan are modified or added.

- [ ] **Step 3: Run host tests**

Run: `./test/host/run_host_tests.sh`

Expected: every test binary prints its success line and final output includes `host tests passed`.

- [ ] **Step 4: Run ESP-IDF build**

Run: `source "$HOME/.espressif/v6.0/esp-idf/export.sh" && idf.py build`

Expected: build exits 0 and output includes `Project build complete.`

- [ ] **Step 5: Inspect telemetry JSON formatting with host test evidence**

Confirm `test_thingsboard_client_internal` asserted:

```c
assert(strstr(buf, "\"energyDelta\":12.345") != NULL);
assert(strstr(buf, "\"frequency\":50.02") != NULL);
assert(strstr(buf, "\"totalEnergy\":") == NULL);
```

- [ ] **Step 6: Report final status without committing**

Report:

- Files changed.
- Host test command and result.
- ESP-IDF build command and result.
- Any hardware verification not run.
- Reminder that changes are uncommitted unless the user asks for a commit.

---

## Self-Review Checklist

- Spec requirement “metering owns CF conversion”: covered by Task 1 and Task 2.
- Spec requirement “app only confirms after publish success”: covered by Task 4.
- Spec requirement “energyDelta in mWh, no totalEnergy”: covered by Task 3 and Task 6.
- Spec requirement “frequency upload”: covered by Task 3 and Task 4.
- Spec requirement “1 Hz direct sampling, no window”: covered by Task 2 and `main/main.c` update.
- Spec requirement “unified logs”: covered by Task 2 and Task 4.
- Compile consumers beyond telemetry: display path covered by Task 5.
- Verification commands: host tests and ESP-IDF build covered by Task 6.
