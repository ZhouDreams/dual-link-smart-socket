#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_controller_internal.h"

static void test_link_names(void)
{
    assert(strcmp(app_controller_internal_link_name(NETWORK_LINK_TYPE_WIFI),
                  "wifi") == 0);
    assert(strcmp(app_controller_internal_link_name(NETWORK_LINK_TYPE_LTE),
                  "lte") == 0);
    assert(strcmp(app_controller_internal_link_name(NETWORK_LINK_TYPE_NONE),
                  "none") == 0);
}

static void test_toggle_screen(void)
{
    assert(app_controller_internal_toggle_screen(true) == false);
    assert(app_controller_internal_toggle_screen(false) == true);
}

static void test_build_telemetry(void)
{
    app_controller_telemetry_output_t out;
    const app_controller_telemetry_source_t source = {
        .voltage = 229.5f,
        .current = 1.25f,
        .power = 286.8f,
        .total_energy = 42.75f,
        .metering_valid = true,
        .relay_on = true,
        .relay_known = true,
        .active_link = NETWORK_LINK_TYPE_LTE,
        .safety_level = SAFETY_GUARD_LEVEL_NORMAL,
        .safety_valid = true,
    };

    app_controller_internal_build_telemetry(&source, &out);

    assert(out.voltage > 229.4f);
    assert(out.voltage < 229.6f);
    assert(out.current > 1.24f);
    assert(out.current < 1.26f);
    assert(out.power > 286.7f);
    assert(out.power < 286.9f);
    assert(out.total_energy > 42.7f);
    assert(out.total_energy < 42.8f);
    assert(out.relay_on == true);
    assert(strcmp(out.active_link, "lte") == 0);
    assert(out.safety_level == SAFETY_GUARD_LEVEL_NORMAL);
    assert(out.valid == true);
}

static void test_power_limit_response(void)
{
    char buf[64];
    size_t out_len = 0;

    assert(app_controller_internal_format_power_limit_response(buf,
                                                               sizeof(buf),
                                                               1800.0f,
                                                               &out_len) == ESP_OK);
    assert(strcmp(buf, "{\"powerLimit\":1800.00}") == 0);
    assert(out_len == strlen(buf));
    assert(app_controller_internal_format_power_limit_response(buf,
                                                               sizeof(buf),
                                                               0.0f,
                                                               &out_len) == ESP_ERR_INVALID_ARG);
}

int main(void)
{
    test_link_names();
    test_toggle_screen();
    test_build_telemetry();
    test_power_limit_response();

    printf("app controller internal tests passed\n");

    return 0;
}
