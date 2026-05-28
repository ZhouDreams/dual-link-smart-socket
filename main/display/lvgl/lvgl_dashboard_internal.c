/**
 * @file lvgl_dashboard_internal.c
 * @brief LVGL 本地看板纯逻辑 helper 实现
 * @details LVGL local dashboard pure-logic helper implementation
 * @author OpenCode
 * @date 2026-05-25
 */

/*********************
 *      INCLUDES
 *********************/

#include "lvgl_dashboard_internal.h"

#include <stdio.h>
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

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool lvgl_dashboard_internal_is_stale(bool data_valid,
                                      uint64_t last_update_us,
                                      uint64_t now_us)
{
    if (!data_valid || last_update_us == 0U || now_us <= last_update_us) {
        return false;
    }

    return (now_us - last_update_us) > LVGL_DASHBOARD_STALE_TIMEOUT_US;
}

const char *lvgl_dashboard_internal_network_text(dashboard_network_t network)
{
    switch (network) {
        case DASHBOARD_NET_CONNECTING:
            return "CONNECTING";
        case DASHBOARD_NET_WIFI:
            return "WIFI";
        case DASHBOARD_NET_LTE:
            return "LTE";
        case DASHBOARD_NET_OFFLINE:
        default:
            return "OFFLINE";
    }
}

const char *lvgl_dashboard_internal_safety_text(safety_guard_level_t level,
                                                bool valid)
{
    if (!valid) {
        return "SAFETY ?";
    }

    switch (level) {
        case SAFETY_GUARD_LEVEL_NORMAL:
            return "SAFE";
        case SAFETY_GUARD_LEVEL_WARNING:
            return "WARN";
        case SAFETY_GUARD_LEVEL_DANGER:
            return "DANGER";
        default:
            return "SAFETY ?";
    }
}

const char *lvgl_dashboard_internal_bottom_status_text(bool data_valid,
                                                       bool data_stale)
{
    if (!data_valid) {
        return "WAITING FOR DATA";
    }
    if (data_stale) {
        return "DATA STALE";
    }

    return "LOCAL MONITOR";
}

bool lvgl_dashboard_internal_should_apply_state(bool has_rendered_state,
                                                const dashboard_state_t *rendered_state,
                                                bool rendered_stale,
                                                const dashboard_state_t *next_state,
                                                bool next_stale)
{
    if (next_state == NULL) {
        return false;
    }

    if (!has_rendered_state || rendered_state == NULL) {
        return true;
    }

    if (rendered_stale != next_stale) {
        return true;
    }

    return memcmp(rendered_state, next_state, sizeof(*next_state)) != 0;
}

esp_err_t lvgl_dashboard_internal_format_power(char *out, size_t out_len,
                                               float power_w)
{
    int written = 0;

    if (out == NULL || out_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(out, out_len, "%.2f W", power_w);
    if (written < 0 || (size_t)written >= out_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t lvgl_dashboard_internal_format_voltage(char *out, size_t out_len,
                                                 float voltage_v)
{
    int written = 0;

    if (out == NULL || out_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(out, out_len, "%.1f V", voltage_v);
    if (written < 0 || (size_t)written >= out_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t lvgl_dashboard_internal_format_current(char *out, size_t out_len,
                                                 float current_a)
{
    int written = 0;

    if (out == NULL || out_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(out, out_len, "%.3f A", current_a);
    if (written < 0 || (size_t)written >= out_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

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

/**********************
 *   STATIC FUNCTIONS
 **********************/
