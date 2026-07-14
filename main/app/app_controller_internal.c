/**
 * @file app_controller_internal.c
 * @brief 应用控制器内部辅助实现
 * @details App controller internal helper implementation
 * @author OpenCode
 * @date 2026-05-28
 */

/*********************
 *      INCLUDES
 *********************/

#include "app_controller_internal.h"

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

const char *app_controller_internal_link_name(network_link_type_t link_type)
{
    switch (link_type) {
    case NETWORK_LINK_TYPE_WIFI:
        return "wifi";
    case NETWORK_LINK_TYPE_LTE:
        return "lte";
    case NETWORK_LINK_TYPE_NONE:
    default:
        return "none";
    }
}

bool app_controller_internal_toggle_screen(bool current_enabled)
{
    return !current_enabled;
}

bool app_controller_internal_has_energy_delta_token(bool metering_valid,
                                                    uint32_t token)
{
    return metering_valid && token != 0U;
}

void app_controller_internal_build_telemetry(
    const app_controller_telemetry_source_t *source,
    app_controller_telemetry_output_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    out->active_link = "none";

    if (source == NULL) {
        return;
    }

    out->voltage = source->voltage;
    out->current = source->current;
    out->power = source->power;
    out->energy_delta = source->energy_delta;
    out->frequency = source->frequency;
    out->relay_on = source->relay_known ? source->relay_on : false;
    out->active_link = app_controller_internal_link_name(source->active_link);
    out->safety_level = source->safety_valid ? source->safety_level :
                        SAFETY_GUARD_LEVEL_WARNING;
    out->valid = source->metering_valid;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
