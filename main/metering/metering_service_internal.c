/**
 * @file metering_service_internal.c
 * @brief 计量服务内部电量增量辅助实现
 * @details Metering service internal energy delta helper implementation
 * @author OpenCode
 * @date 2026-05-28
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

/**
 * @brief 获取下一个非零令牌
 * @details Get the next non-zero token
 * @param[in,out] state 电量增量状态； Energy delta state
 * @return 非零令牌； Non-zero token
 */
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
    state->next_token = 1;
}

void metering_energy_delta_reset_baseline(metering_energy_delta_state_t *state)
{
    uint32_t next_token;

    if (state == NULL) {
        return;
    }

    next_token = state->next_token;
    metering_energy_delta_state_init(state);
    if (next_token != 0U) {
        state->next_token = next_token;
    }
}

esp_err_t metering_energy_delta_prepare(metering_energy_delta_state_t *state,
                                        uint32_t cf_cnt_raw,
                                        metering_energy_delta_result_t *out)
{
    uint32_t token;
    uint32_t masked_cf_cnt;
    uint32_t delta;
    uint64_t total_nwh;
    uint64_t mwh_thousandths;
    uint32_t next_residual_nwh;

    if ((state == NULL) || (out == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    if (state->have_pending) {
        return ESP_ERR_INVALID_STATE;
    }

    token = metering_energy_next_token(state);
    masked_cf_cnt = cf_cnt_raw & METERING_ENERGY_CF_CNT_U24_MASK;

    if (!state->have_confirmed_cf_cnt_raw) {
        state->have_confirmed_cf_cnt_raw = true;
        state->confirmed_cf_cnt_raw = masked_cf_cnt;
        state->confirmed_residual_nwh = 0;
        state->have_pending = true;
        state->pending_cf_cnt_raw = masked_cf_cnt;
        state->pending_residual_nwh = 0;
        state->pending_token = token;

        out->energy_delta_mwh = 0.0f;
        out->token = token;
        out->baseline_established = true;
        return ESP_OK;
    }

    delta = metering_energy_u24_delta(masked_cf_cnt,
                                      state->confirmed_cf_cnt_raw);
    total_nwh = (uint64_t)state->confirmed_residual_nwh +
                ((uint64_t)delta * METERING_ENERGY_PULSE_NWH);
    mwh_thousandths = total_nwh / METERING_ENERGY_NWH_PER_MWH_Q3;
    next_residual_nwh = (uint32_t)(total_nwh % METERING_ENERGY_NWH_PER_MWH_Q3);

    state->have_pending = true;
    state->pending_cf_cnt_raw = masked_cf_cnt;
    state->pending_residual_nwh = next_residual_nwh;
    state->pending_token = token;

    out->energy_delta_mwh = (float)mwh_thousandths / 1000.0f;
    out->token = token;
    out->baseline_established = false;

    return ESP_OK;
}

esp_err_t metering_energy_delta_confirm(metering_energy_delta_state_t *state,
                                        uint32_t token)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((!state->have_pending) || (state->pending_token != token)) {
        return ESP_ERR_INVALID_STATE;
    }

    state->have_confirmed_cf_cnt_raw = true;
    state->confirmed_cf_cnt_raw = state->pending_cf_cnt_raw;
    state->confirmed_residual_nwh = state->pending_residual_nwh;
    state->have_pending = false;

    return ESP_OK;
}

esp_err_t metering_energy_delta_discard(metering_energy_delta_state_t *state,
                                        uint32_t token)
{
    if ((state == NULL) || (token == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((!state->have_pending) || (state->pending_token != token)) {
        return ESP_ERR_INVALID_STATE;
    }

    state->have_pending = false;
    state->pending_cf_cnt_raw = 0U;
    state->pending_residual_nwh = 0U;
    state->pending_token = 0U;
    return ESP_OK;
}

uint32_t metering_energy_u24_delta(uint32_t current, uint32_t previous)
{
    return (current - previous) & METERING_ENERGY_CF_CNT_U24_MASK;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static uint32_t metering_energy_next_token(metering_energy_delta_state_t *state)
{
    uint32_t token = state->next_token;

    state->next_token++;
    if (state->next_token == 0) {
        state->next_token = 1;
    }

    if (token == 0) {
        token = state->next_token;
        state->next_token++;
        if (state->next_token == 0) {
            state->next_token = 1;
        }
    }

    return token;
}
