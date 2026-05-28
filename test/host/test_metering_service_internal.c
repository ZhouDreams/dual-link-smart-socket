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
    metering_energy_delta_state_t state;
    metering_energy_delta_result_t result;

    metering_energy_delta_state_init(&state);

    assert(metering_energy_delta_prepare(&state, 100, &result) == ESP_OK);
    assert_float_near(result.energy_delta_mwh, 0.0f);
    assert(result.token != 0);
    assert(result.baseline_established);

    assert(metering_energy_delta_confirm(&state, result.token) == ESP_OK);
}

static void test_confirmed_delta_preserves_residual(void)
{
    metering_energy_delta_state_t state;
    metering_energy_delta_result_t result;

    metering_energy_delta_state_init(&state);

    assert(metering_energy_delta_prepare(&state, 100, &result) == ESP_OK);
    assert(metering_energy_delta_confirm(&state, result.token) == ESP_OK);

    assert(metering_energy_delta_prepare(&state, 102, &result) == ESP_OK);
    assert_float_near(result.energy_delta_mwh, 124.595f);
    assert(!result.baseline_established);
    assert(metering_energy_delta_confirm(&state, result.token) == ESP_OK);

    assert(metering_energy_delta_prepare(&state, 103, &result) == ESP_OK);
    assert_float_near(result.energy_delta_mwh, 62.298f);
    assert(!result.baseline_established);
}

static void test_failed_publish_keeps_old_confirmed_baseline(void)
{
    metering_energy_delta_state_t state;
    metering_energy_delta_result_t baseline;
    metering_energy_delta_result_t failed;
    metering_energy_delta_result_t retry;

    metering_energy_delta_state_init(&state);

    assert(metering_energy_delta_prepare(&state, 100, &baseline) == ESP_OK);
    assert(metering_energy_delta_confirm(&state, baseline.token) == ESP_OK);

    assert(metering_energy_delta_prepare(&state, 101, &failed) == ESP_OK);
    assert_float_near(failed.energy_delta_mwh, 62.297f);
    assert(metering_energy_delta_discard(&state, failed.token) == ESP_OK);

    assert(metering_energy_delta_prepare(&state, 102, &retry) == ESP_OK);
    assert_float_near(retry.energy_delta_mwh, 124.595f);
}

static void test_wrap_safe_delta(void)
{
    metering_energy_delta_state_t state;
    metering_energy_delta_result_t result;

    metering_energy_delta_state_init(&state);
    assert(metering_energy_delta_prepare(&state, 0x00FFFFFEUL, &result) == ESP_OK);
    assert(metering_energy_delta_confirm(&state, result.token) == ESP_OK);

    assert(metering_energy_delta_prepare(&state, 1, &result) == ESP_OK);
    assert_float_near(result.energy_delta_mwh, 186.893f);
}

static void test_pending_prepare_does_not_overwrite_original_token(void)
{
    metering_energy_delta_state_t state;
    metering_energy_delta_result_t result;
    uint32_t pending_token;

    metering_energy_delta_state_init(&state);
    assert(metering_energy_delta_prepare(&state, 100, &result) == ESP_OK);
    assert(metering_energy_delta_confirm(&state, result.token) == ESP_OK);

    assert(metering_energy_delta_prepare(&state, 101, &result) == ESP_OK);
    pending_token = result.token;

    assert(metering_energy_delta_prepare(&state, 102, &result) == ESP_ERR_INVALID_STATE);
    assert(metering_energy_delta_confirm(&state, pending_token) == ESP_OK);
    assert(metering_energy_delta_confirm(&state, pending_token) == ESP_ERR_INVALID_STATE);
}

static void test_discard_rejects_stale_or_repeated_token(void)
{
    metering_energy_delta_state_t state;
    metering_energy_delta_result_t result;
    uint32_t stale_token;

    metering_energy_delta_state_init(&state);
    assert(metering_energy_delta_prepare(&state, 100, &result) == ESP_OK);
    assert(metering_energy_delta_confirm(&state, result.token) == ESP_OK);
    stale_token = result.token;

    assert(metering_energy_delta_prepare(&state, 101, &result) == ESP_OK);
    assert(metering_energy_delta_discard(&state, stale_token) == ESP_ERR_INVALID_STATE);
    assert(metering_energy_delta_discard(&state, result.token) == ESP_OK);
    assert(metering_energy_delta_discard(&state, result.token) == ESP_ERR_INVALID_STATE);
}

static void test_reset_baseline_preserves_token_monotonicity(void)
{
    metering_energy_delta_state_t state;
    metering_energy_delta_result_t result;
    uint32_t old_token;

    metering_energy_delta_state_init(&state);
    assert(metering_energy_delta_prepare(&state, 100, &result) == ESP_OK);
    old_token = result.token;

    metering_energy_delta_reset_baseline(&state);
    assert(metering_energy_delta_confirm(&state, old_token) == ESP_ERR_INVALID_STATE);

    assert(metering_energy_delta_prepare(&state, 200, &result) == ESP_OK);
    assert(result.token > old_token);
    assert(metering_energy_delta_confirm(&state, old_token) == ESP_ERR_INVALID_STATE);
    assert(metering_energy_delta_confirm(&state, result.token) == ESP_OK);
}

int main(void)
{
    test_first_sample_establishes_baseline();
    test_confirmed_delta_preserves_residual();
    test_failed_publish_keeps_old_confirmed_baseline();
    test_wrap_safe_delta();
    test_pending_prepare_does_not_overwrite_original_token();
    test_discard_rejects_stale_or_repeated_token();
    test_reset_baseline_preserves_token_monotonicity();

    printf("metering internal tests passed\n");

    return 0;
}
