#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "thingsboard_client_internal.h"

static void test_extract_request_id(void)
{
    int32_t request_id = 0;

    assert(tb_internal_extract_rpc_request_id("v1/devices/me/rpc/request/42",
                                             &request_id) == ESP_OK);
    assert(request_id == 42);
    assert(tb_internal_extract_rpc_request_id("v1/devices/me/rpc/request/",
                                             &request_id) == ESP_ERR_INVALID_RESPONSE);
    assert(tb_internal_extract_rpc_request_id("v1/devices/me/rpc/request/42x",
                                             &request_id) == ESP_ERR_INVALID_RESPONSE);
    assert(tb_internal_extract_rpc_request_id("v1/devices/me/rpc/request/-1",
                                             &request_id) == ESP_ERR_INVALID_RESPONSE);
    assert(tb_internal_extract_rpc_request_id("v1/devices/me/rpc/request/2147483648",
                                             &request_id) == ESP_ERR_INVALID_RESPONSE);
    assert(tb_internal_extract_rpc_request_id(TB_TOPIC_TELEMETRY,
                                             &request_id) == ESP_ERR_NOT_FOUND);
}

static void test_parse_rpc(void)
{
    tb_internal_command_t command;
    const char *payload = "{\"method\":\"setRelay\",\"params\":true}";

    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/1", NULL, 1,
                                 &command) == ESP_ERR_INVALID_ARG);
    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/1", payload,
                                 strlen(payload), NULL) == ESP_ERR_INVALID_ARG);

    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/7",
                                 "{\"method\":\"setRelay\",\"params\":true}",
                                 strlen("{\"method\":\"setRelay\",\"params\":true}"),
                                 &command) == ESP_OK);
    assert(command.request_id == 7);
    assert(command.type == TB_INTERNAL_COMMAND_SET_RELAY);
    assert(command.relay_on == true);

    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/8",
                                 "{\"method\":\"getPowerLimit\"}",
                                 strlen("{\"method\":\"getPowerLimit\"}"),
                                 &command) == ESP_OK);
    assert(command.request_id == 8);
    assert(command.type == TB_INTERNAL_COMMAND_GET_POWER_LIMIT);

    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/9",
                                 "{\"method\":\"setPowerLimit\",\"params\":1500.0}",
                                 strlen("{\"method\":\"setPowerLimit\",\"params\":1500.0}"),
                                 &command) == ESP_OK);
    assert(command.request_id == 9);
    assert(command.type == TB_INTERNAL_COMMAND_SET_POWER_LIMIT);
    assert(command.power_limit_w > 1499.9f);
    assert(command.power_limit_w < 1500.1f);

    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/10",
                                 "{\"method\":\"setPowerLimit\",\"params\":-1.0}",
                                 strlen("{\"method\":\"setPowerLimit\",\"params\":-1.0}"),
                                 &command) == ESP_ERR_INVALID_RESPONSE);
    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/11",
                                 "{\"method\":\"setRelay\",\"params\":truex}",
                                 strlen("{\"method\":\"setRelay\",\"params\":truex}"),
                                 &command) == ESP_ERR_INVALID_RESPONSE);
    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/12",
                                 "{\"method\":\"setRelay\",\"params\":\"on\"}",
                                 strlen("{\"method\":\"setRelay\",\"params\":\"on\"}"),
                                 &command) == ESP_ERR_INVALID_RESPONSE);
    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/13",
                                 "{\"method\":\"unknown\",\"params\":true}",
                                 strlen("{\"method\":\"unknown\",\"params\":true}"),
                                 &command) == ESP_ERR_INVALID_RESPONSE);
    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/14",
                                 "{\"method\":\"setRelay\",\"params\":true",
                                 strlen("{\"method\":\"setRelay\",\"params\":true"),
                                 &command) == ESP_ERR_INVALID_RESPONSE);
    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/15",
                                 "{\"method\":\"setPowerLimit\",\"params\":nan}",
                                 strlen("{\"method\":\"setPowerLimit\",\"params\":nan}"),
                                 &command) == ESP_ERR_INVALID_RESPONSE);
    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/16",
                                 "{\"method\":\"setPowerLimit\",\"params\":inf}",
                                 strlen("{\"method\":\"setPowerLimit\",\"params\":inf}"),
                                 &command) == ESP_ERR_INVALID_RESPONSE);
    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/17",
                                 "{\"method\" : \"setRelay\", \"params\" : false}",
                                 strlen("{\"method\" : \"setRelay\", \"params\" : false}"),
                                 &command) == ESP_OK);
    assert(command.request_id == 17);
    assert(command.type == TB_INTERNAL_COMMAND_SET_RELAY);
    assert(command.relay_on == false);
    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/18",
                                 "{\"method\" : \"setPowerLimit\", \"params\" : 55.5}",
                                 strlen("{\"method\" : \"setPowerLimit\", \"params\" : 55.5}"),
                                 &command) == ESP_OK);
    assert(command.request_id == 18);
    assert(command.type == TB_INTERNAL_COMMAND_SET_POWER_LIMIT);
    assert(command.power_limit_w > 55.4f);
    assert(command.power_limit_w < 55.6f);
}

static void test_formatting(void)
{
    char buf[256];
    size_t out_len = 0;
    tb_internal_telemetry_t telemetry = {
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

    assert(tb_internal_format_telemetry(buf, sizeof(buf), &telemetry,
                                        &out_len) == ESP_OK);
    assert(out_len == strlen(buf));
    assert(strcmp(buf,
                  "{\"voltage\":230.12,\"current\":1.234,"
                  "\"power\":284.05,\"energyDelta\":12.345,"
                  "\"frequency\":50.02,\"relayOn\":true,"
                  "\"activeLink\":\"wifi\",\"safetyLevel\":1,"
                  "\"valid\":true}") == 0);
    assert(strstr(buf, "\"totalEnergy\":") == NULL);

    telemetry.active_link = "bad\"link";
    assert(tb_internal_format_telemetry(buf, sizeof(buf), &telemetry,
                                        &out_len) == ESP_OK);
    assert(strstr(buf, "\"activeLink\":\"none\"") != NULL);

    telemetry.active_link = NULL;
    assert(tb_internal_format_telemetry(buf, sizeof(buf), &telemetry,
                                        &out_len) == ESP_OK);
    assert(strstr(buf, "\"activeLink\":\"none\"") != NULL);

    telemetry.active_link = "wifi";
    telemetry.voltage = NAN;
    assert(tb_internal_format_telemetry(buf, sizeof(buf), &telemetry,
                                        &out_len) == ESP_ERR_INVALID_ARG);
    telemetry.voltage = 230.12f;
    telemetry.current = INFINITY;
    assert(tb_internal_format_telemetry(buf, sizeof(buf), &telemetry,
                                        &out_len) == ESP_ERR_INVALID_ARG);
    telemetry.current = 1.234f;
    telemetry.power = -INFINITY;
    assert(tb_internal_format_telemetry(buf, sizeof(buf), &telemetry,
                                        &out_len) == ESP_ERR_INVALID_ARG);
    telemetry.power = 284.05f;
    telemetry.energy_delta = NAN;
    assert(tb_internal_format_telemetry(buf, sizeof(buf), &telemetry,
                                        &out_len) == ESP_ERR_INVALID_ARG);
    telemetry.energy_delta = 12.345f;
    telemetry.frequency = INFINITY;
    assert(tb_internal_format_telemetry(buf, sizeof(buf), &telemetry,
                                        &out_len) == ESP_ERR_INVALID_ARG);
    telemetry.frequency = 50.02f;

    assert(tb_internal_format_relay_attribute(buf, sizeof(buf), false,
                                              &out_len) == ESP_OK);
    assert(strcmp(buf, "{\"relayOn\":false}") == 0);

    assert(tb_internal_format_power_limit_attribute(buf, sizeof(buf), 1500.0f,
                                                   &out_len) == ESP_OK);
    assert(strcmp(buf, "{\"powerLimit\":1500.00}") == 0);
    assert(tb_internal_format_power_limit_attribute(buf, sizeof(buf), NAN,
                                                   &out_len) == ESP_ERR_INVALID_ARG);
    assert(tb_internal_format_power_limit_attribute(buf, sizeof(buf), INFINITY,
                                                   &out_len) == ESP_ERR_INVALID_ARG);

    assert(tb_internal_format_rpc_response_topic(buf, sizeof(buf), 22,
                                                 &out_len) == ESP_OK);
    assert(strcmp(buf, "v1/devices/me/rpc/response/22") == 0);
}

int main(void)
{
    test_extract_request_id();
    test_parse_rpc();
    test_formatting();

    printf("thingsboard internal tests passed\n");

    return 0;
}
