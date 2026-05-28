# Final Three Modules Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `thingsboard_client`, `lte_link`, and `app_controller`, then wire them into a complete Smart_Socket assembly with host tests and ESP-IDF build verification.

**Architecture:** `thingsboard_client` owns ThingsBoard topic/JSON/RPC semantics over the existing `network_manager`. `lte_link` is a `network_link_t` subclass backed by the local `esp-lwlte` facade. `app_controller` borrows all module handles from `main.c` and orchestrates callbacks, events, startup, and shutdown without creating lower-layer objects.

**Tech Stack:** C, ESP-IDF v6.0, FreeRTOS mutexes, ESP-IDF event loop, `esp-lwlte`, existing `network_link` ops table, host-side `cc` tests for pure C helpers.

---

## File Structure

| Path | Responsibility |
|---|---|
| `docs/superpowers/specs/2026-05-27-final-three-modules-design.md` | Approved design source for this plan. |
| `CMakeLists.txt` | Adds the local `../esp-lwlte/src` component before `project()`. |
| `main/idf_component.yml` | Aligns the main component IDF dependency with ESP-IDF 6.0. |
| `main/CMakeLists.txt` | Registers new app, LTE, ThingsBoard, and internal helper sources and include dirs. |
| `main/safety/safety_guard.h` | Adds read-only threshold getter needed by `GET_POWER_LIMIT`. |
| `main/safety/safety_guard.c` | Implements read-only threshold getter under the existing mutex. |
| `main/thingsboard/thingsboard_client.h` | Public ThingsBoard client types and APIs. |
| `main/thingsboard/thingsboard_client_internal.h` | Pure ThingsBoard helper declarations for host tests and production. |
| `main/thingsboard/thingsboard_client_internal.c` | Topic parsing, schema-specific RPC parsing, and JSON formatting helpers. |
| `main/thingsboard/thingsboard_client.c` | Opaque ThingsBoard client lifecycle, publish APIs, network RX bridge, and command callback dispatch. |
| `main/network/lte/lte_link.h` | Public LTE link config and factory returning `network_link_t *`. |
| `main/network/lte/lte_link_internal.h` | Pure LTE helper declarations for status mapping, QoS, and subscription table behavior. |
| `main/network/lte/lte_link_internal.c` | LTE status mapping and subscription-table helpers. |
| `main/network/lte/lte_link.c` | `network_link_t` subclass over `esp-lwlte`, including events, lifecycle, MQTT, and replay. |
| `main/app/app_controller.h` | Public app controller config and lifecycle API. |
| `main/app/app_controller_internal.h` | Pure app helper declarations for host tests. |
| `main/app/app_controller_internal.c` | Active-link string mapping, screen toggle, telemetry defaults, and RPC response helper. |
| `main/app/app_controller.c` | Callback/event orchestration and module start/stop ordering. |
| `main/main.c` | Complete object graph assembly using macro configuration. |
| `test/support/esp_err.h` | Minimal host-test ESP-IDF error-code stub. |
| `test/support/network_types.h` | Minimal host-test network type stub when compiling internal helpers outside ESP-IDF. |
| `test/support/lwlte.h` | Minimal host-test esp-lwlte type stub for internal LTE helper tests. |
| `test/support/safety_guard.h` | Minimal host-test safety enum stub. |
| `test/host/test_thingsboard_client_internal.c` | Host tests for ThingsBoard helpers. |
| `test/host/test_lte_link_internal.c` | Host tests for LTE helper logic. |
| `test/host/test_app_controller_internal.c` | Host tests for app controller helper logic. |
| `test/host/run_host_tests.sh` | Builds and runs all host tests with system `cc`. |

Scope check: the spec covers three tightly coupled final modules plus integration. Splitting into separate feature plans would leave no testable end-to-end app assembly, so this plan keeps one feature plan but decomposes tasks by module and helper layer.

## Task 1: Build Contracts And Safety Getter

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `main/idf_component.yml`
- Modify: `main/CMakeLists.txt`
- Modify: `main/safety/safety_guard.h`
- Modify: `main/safety/safety_guard.c`

- [ ] **Step 1: Update root CMake for esp-lwlte**

Replace `CMakeLists.txt` with:

```cmake
cmake_minimum_required(VERSION 3.22)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../esp-lwlte/src")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Smart_Socket)
```

- [ ] **Step 2: Align IDF dependency to v6.0**

Replace `main/idf_component.yml` with:

```yaml
## IDF Component Manager Manifest File
## Managed dependencies for the Smart_Socket main component.

dependencies:
  idf:
    version: "6.0"
  espressif/button: "^4.1"
  espressif/mqtt: "^1.0.0"
  lvgl/lvgl: "^9.0.0"
```

- [ ] **Step 3: Register new sources and include dirs**

Replace `main/CMakeLists.txt` with:

```cmake
idf_component_register(
    SRCS
        "main.c"
        "app/app_controller_internal.c"
        "app/app_controller.c"
        "platform/board_pinmap.c"
        "network/network_link.c"
        "network/network_manager.c"
        "network/wifi/wifi_link.c"
        "network/lte/lte_link_internal.c"
        "network/lte/lte_link.c"
        "thingsboard/thingsboard_client_internal.c"
        "thingsboard/thingsboard_client.c"
        "relay/relay.c"
        "button/button_iot_adapter.c"
        "button/button.c"
        "bl0942/bl0942.c"
        "metering/metering_service.c"
        "safety/safety_guard.c"
        "display/tft/tft_panel.c"
        "display/tft/tft_panel_st7789t.c"
        "display/lvgl/lvgl_dashboard_internal.c"
        "display/lvgl/lvgl_dashboard.c"
    INCLUDE_DIRS
        "."
        "app"
        "platform"
        "network"
        "network/wifi"
        "network/lte"
        "thingsboard"
        "relay"
        "button"
        "bl0942"
        "metering"
        "safety"
        "display/tft"
        "display/lvgl"
)
```

- [ ] **Step 4: Add safety threshold getter declaration**

In `main/safety/safety_guard.h`, add this declaration after `safety_guard_set_thresholds()`:

```c
/**
 * @brief 获取安全阈值
 * @details Get safety thresholds
 * @param[in] me 安全规则句柄； Safety guard handle
 * @param[out] out_overcurrent_a 过流阈值 A，可为 NULL； Overcurrent threshold, may be NULL
 * @param[out] out_overpower_w 过功率阈值 W，可为 NULL； Overpower threshold, may be NULL
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 获取互斥锁超时； Mutex timeout
 */
esp_err_t safety_guard_get_thresholds(safety_guard_t *me,
                                      float *out_overcurrent_a,
                                      float *out_overpower_w);
```

- [ ] **Step 5: Implement safety threshold getter**

In `main/safety/safety_guard.c`, add this public function after `safety_guard_set_thresholds()`:

```c
esp_err_t safety_guard_get_thresholds(safety_guard_t *me,
                                      float *out_overcurrent_a,
                                      float *out_overpower_w)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "guard is null");
    ESP_RETURN_ON_FALSE(out_overcurrent_a != NULL || out_overpower_w != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "no output requested");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "guard is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    if (out_overcurrent_a != NULL) {
        *out_overcurrent_a = me->config.overcurrent_threshold_a;
    }
    if (out_overpower_w != NULL) {
        *out_overpower_w = me->config.overpower_threshold_w;
    }

    (void)xSemaphoreGive(me->mutex);
    return ESP_OK;
}
```

- [ ] **Step 6: Build checkpoint for expected missing files**

Run: `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build`

Expected: FAIL naming one of the newly listed missing files such as `app/app_controller_internal.c`, `network/lte/lte_link_internal.c`, or `thingsboard/thingsboard_client_internal.c`. If the failure is in `safety_guard_get_thresholds()`, fix that exact compile error first.

- [ ] **Step 7: Checkpoint without committing**

Run: `git status --short`

Expected: root CMake, main manifest, main CMake, and safety guard files are modified. Do not commit unless explicitly authorized.

## Task 2: ThingsBoard Internal Helpers And Host Tests

**Files:**
- Create: `main/thingsboard/thingsboard_client_internal.h`
- Create: `main/thingsboard/thingsboard_client_internal.c`
- Create: `test/support/esp_err.h`
- Create: `test/support/network_types.h`
- Create: `test/support/safety_guard.h`
- Create: `test/host/test_thingsboard_client_internal.c`
- Create: `test/host/run_host_tests.sh`

- [ ] **Step 1: Create minimal host support headers**

Create `test/support/esp_err.h`:

```c
#pragma once

typedef int esp_err_t;

#define ESP_OK                   0
#define ESP_FAIL                 0x101
#define ESP_ERR_NO_MEM           0x101
#define ESP_ERR_INVALID_ARG      0x102
#define ESP_ERR_INVALID_STATE    0x103
#define ESP_ERR_INVALID_SIZE     0x104
#define ESP_ERR_NOT_FOUND        0x105
#define ESP_ERR_NOT_SUPPORTED    0x106
#define ESP_ERR_TIMEOUT          0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
```

Create `test/support/network_types.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    NETWORK_LINK_TYPE_NONE = 0,
    NETWORK_LINK_TYPE_WIFI,
    NETWORK_LINK_TYPE_LTE,
} network_link_type_t;

typedef enum {
    NETWORK_LINK_STATUS_IDLE = 0,
    NETWORK_LINK_STATUS_STARTING,
    NETWORK_LINK_STATUS_CONNECTING,
    NETWORK_LINK_STATUS_DEGRADED,
    NETWORK_LINK_STATUS_READY,
    NETWORK_LINK_STATUS_ERROR,
} network_link_status_t;

typedef enum {
    NETWORK_MQTT_QOS0 = 0,
    NETWORK_MQTT_QOS1 = 1,
    NETWORK_MQTT_QOS2 = 2,
} network_mqtt_qos_t;
```

Create `test/support/safety_guard.h`:

```c
#pragma once

typedef enum {
    SAFETY_GUARD_LEVEL_NORMAL = 0,
    SAFETY_GUARD_LEVEL_WARNING,
    SAFETY_GUARD_LEVEL_DANGER,
} safety_guard_level_t;
```

- [ ] **Step 2: Create ThingsBoard internal helper header**

Create `main/thingsboard/thingsboard_client_internal.h`:

```c
/**
 * @file thingsboard_client_internal.h
 * @brief ThingsBoard 纯逻辑辅助函数
 * @details ThingsBoard pure helper functions
 * @author OpenCode
 * @date 2026-05-27
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "safety_guard.h"

/*********************
 *      DEFINES
 *********************/

#define TB_TOPIC_TELEMETRY          "v1/devices/me/telemetry"
#define TB_TOPIC_ATTRIBUTES         "v1/devices/me/attributes"
#define TB_TOPIC_RPC_REQUEST_PREFIX "v1/devices/me/rpc/request/"
#define TB_TOPIC_RPC_REQUEST_SUB    "v1/devices/me/rpc/request/+"
#define TB_TOPIC_RPC_RESPONSE_FMT   "v1/devices/me/rpc/response/%ld"

/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
    TB_INTERNAL_COMMAND_SET_RELAY = 0,
    TB_INTERNAL_COMMAND_GET_POWER_LIMIT,
    TB_INTERNAL_COMMAND_SET_POWER_LIMIT,
} tb_internal_command_type_t;

typedef struct {
    tb_internal_command_type_t type;
    int32_t request_id;
    bool relay_on;
    float power_limit_w;
} tb_internal_command_t;

typedef struct {
    float voltage;
    float current;
    float power;
    float total_energy;
    bool relay_on;
    const char *active_link;
    safety_guard_level_t safety_level;
    bool valid;
} tb_internal_telemetry_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

esp_err_t tb_internal_extract_rpc_request_id(const char *topic,
                                             int32_t *out_request_id);
esp_err_t tb_internal_parse_rpc(const char *topic, const char *payload,
                                size_t payload_len,
                                tb_internal_command_t *out_command);
esp_err_t tb_internal_format_telemetry(char *buf, size_t buf_size,
                                       const tb_internal_telemetry_t *input,
                                       size_t *out_len);
esp_err_t tb_internal_format_relay_attribute(char *buf, size_t buf_size,
                                             bool relay_on, size_t *out_len);
esp_err_t tb_internal_format_power_limit_attribute(char *buf, size_t buf_size,
                                                   float power_limit_w,
                                                   size_t *out_len);
esp_err_t tb_internal_format_power_limit_response(char *buf, size_t buf_size,
                                                  float power_limit_w,
                                                  size_t *out_len);
esp_err_t tb_internal_format_rpc_response_topic(char *buf, size_t buf_size,
                                                int32_t request_id,
                                                size_t *out_len);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
```

- [ ] **Step 3: Implement ThingsBoard internal helpers**

Create `main/thingsboard/thingsboard_client_internal.c` with these helper semantics:

```c
/**
 * @file thingsboard_client_internal.c
 * @brief ThingsBoard 纯逻辑辅助函数实现
 * @details ThingsBoard pure helper function implementation
 * @author OpenCode
 * @date 2026-05-27
 */

/*********************
 *      INCLUDES
 *********************/

#include "thingsboard_client_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
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

static esp_err_t tb_internal_finish_format(int written, size_t buf_size,
                                           size_t *out_len);
static bool tb_internal_contains_method(const char *payload, size_t len,
                                        const char *method);
static esp_err_t tb_internal_parse_bool_param(const char *payload, size_t len,
                                              bool *out_value);
static esp_err_t tb_internal_parse_float_param(const char *payload, size_t len,
                                               float *out_value);
static const char *tb_internal_find_params(const char *payload, size_t len);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

esp_err_t tb_internal_extract_rpc_request_id(const char *topic,
                                             int32_t *out_request_id)
{
    char *end = NULL;
    long value = 0;
    const size_t prefix_len = strlen(TB_TOPIC_RPC_REQUEST_PREFIX);

    if (topic == NULL || out_request_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strncmp(topic, TB_TOPIC_RPC_REQUEST_PREFIX, prefix_len) != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (topic[prefix_len] == '\0') {
        return ESP_ERR_INVALID_RESPONSE;
    }

    value = strtol(topic + prefix_len, &end, 10);
    if (end == topic + prefix_len || end == NULL || *end != '\0' || value < 0 ||
        value > INT32_MAX) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *out_request_id = (int32_t)value;
    return ESP_OK;
}

esp_err_t tb_internal_parse_rpc(const char *topic, const char *payload,
                                size_t payload_len,
                                tb_internal_command_t *out_command)
{
    int32_t request_id = 0;

    if (payload == NULL || out_command == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = tb_internal_extract_rpc_request_id(topic, &request_id);
    if (ret != ESP_OK) {
        return ret;
    }

    memset(out_command, 0, sizeof(*out_command));
    out_command->request_id = request_id;

    if (tb_internal_contains_method(payload, payload_len, "setRelay")) {
        out_command->type = TB_INTERNAL_COMMAND_SET_RELAY;
        return tb_internal_parse_bool_param(payload, payload_len,
                                            &out_command->relay_on);
    }
    if (tb_internal_contains_method(payload, payload_len, "getPowerLimit")) {
        out_command->type = TB_INTERNAL_COMMAND_GET_POWER_LIMIT;
        return ESP_OK;
    }
    if (tb_internal_contains_method(payload, payload_len, "setPowerLimit")) {
        out_command->type = TB_INTERNAL_COMMAND_SET_POWER_LIMIT;
        return tb_internal_parse_float_param(payload, payload_len,
                                             &out_command->power_limit_w);
    }

    return ESP_ERR_INVALID_RESPONSE;
}

esp_err_t tb_internal_format_telemetry(char *buf, size_t buf_size,
                                       const tb_internal_telemetry_t *input,
                                       size_t *out_len)
{
    if (buf == NULL || buf_size == 0U || input == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *link = (input->active_link != NULL) ? input->active_link : "none";
    const int written = snprintf(
        buf, buf_size,
        "{\"voltage\":%.2f,\"current\":%.3f,\"power\":%.2f,"
        "\"totalEnergy\":%.2f,\"relayOn\":%s,\"activeLink\":\"%s\","
        "\"safetyLevel\":%d,\"valid\":%s}",
        input->voltage, input->current, input->power, input->total_energy,
        input->relay_on ? "true" : "false", link, (int)input->safety_level,
        input->valid ? "true" : "false");
    return tb_internal_finish_format(written, buf_size, out_len);
}

esp_err_t tb_internal_format_relay_attribute(char *buf, size_t buf_size,
                                             bool relay_on, size_t *out_len)
{
    if (buf == NULL || buf_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    const int written = snprintf(buf, buf_size, "{\"relayOn\":%s}",
                                 relay_on ? "true" : "false");
    return tb_internal_finish_format(written, buf_size, out_len);
}

esp_err_t tb_internal_format_power_limit_attribute(char *buf, size_t buf_size,
                                                   float power_limit_w,
                                                   size_t *out_len)
{
    if (buf == NULL || buf_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    const int written = snprintf(buf, buf_size, "{\"powerLimit\":%.2f}",
                                 power_limit_w);
    return tb_internal_finish_format(written, buf_size, out_len);
}

esp_err_t tb_internal_format_power_limit_response(char *buf, size_t buf_size,
                                                  float power_limit_w,
                                                  size_t *out_len)
{
    if (buf == NULL || buf_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    const int written = snprintf(buf, buf_size, "{\"powerLimit\":%.2f}",
                                 power_limit_w);
    return tb_internal_finish_format(written, buf_size, out_len);
}

esp_err_t tb_internal_format_rpc_response_topic(char *buf, size_t buf_size,
                                                int32_t request_id,
                                                size_t *out_len)
{
    if (buf == NULL || buf_size == 0U || request_id < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int written = snprintf(buf, buf_size, TB_TOPIC_RPC_RESPONSE_FMT,
                                 (long)request_id);
    return tb_internal_finish_format(written, buf_size, out_len);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static esp_err_t tb_internal_finish_format(int written, size_t buf_size,
                                           size_t *out_len)
{
    if (written < 0) {
        return ESP_FAIL;
    }
    if ((size_t)written >= buf_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (out_len != NULL) {
        *out_len = (size_t)written;
    }
    return ESP_OK;
}

static bool tb_internal_contains_method(const char *payload, size_t len,
                                        const char *method)
{
    if (payload == NULL || method == NULL) {
        return false;
    }

    const size_t method_len = strlen(method);
    if (method_len == 0U || len < method_len) {
        return false;
    }

    for (size_t i = 0; i + method_len <= len; i++) {
        if (memcmp(payload + i, method, method_len) == 0) {
            return true;
        }
    }
    return false;
}

static const char *tb_internal_find_params(const char *payload, size_t len)
{
    static const char key[] = "\"params\"";
    const size_t key_len = sizeof(key) - 1U;

    if (payload == NULL || len < key_len) {
        return NULL;
    }
    for (size_t i = 0; i + key_len <= len; i++) {
        if (memcmp(payload + i, key, key_len) == 0) {
            const char *cursor = payload + i + key_len;
            const char *end = payload + len;
            while (cursor < end && isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (cursor >= end || *cursor != ':') {
                return NULL;
            }
            cursor++;
            while (cursor < end && isspace((unsigned char)*cursor)) {
                cursor++;
            }
            return (cursor < end) ? cursor : NULL;
        }
    }
    return NULL;
}

static esp_err_t tb_internal_parse_bool_param(const char *payload, size_t len,
                                              bool *out_value)
{
    const char *params = tb_internal_find_params(payload, len);
    if (params == NULL || out_value == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (strncmp(params, "true", 4) == 0) {
        *out_value = true;
        return ESP_OK;
    }
    if (strncmp(params, "false", 5) == 0) {
        *out_value = false;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t tb_internal_parse_float_param(const char *payload, size_t len,
                                               float *out_value)
{
    char *end = NULL;
    const char *params = tb_internal_find_params(payload, len);

    if (params == NULL || out_value == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const float value = strtof(params, &end);
    if (end == params || end == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (!(value > 0.0f)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *out_value = value;
    return ESP_OK;
}
```

- [ ] **Step 4: Create ThingsBoard host tests**

Create `test/host/test_thingsboard_client_internal.c`:

```c
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "thingsboard_client_internal.h"

static void test_rpc_request_id(void)
{
    int32_t id = -1;
    assert(tb_internal_extract_rpc_request_id("v1/devices/me/rpc/request/42", &id) == ESP_OK);
    assert(id == 42);
    assert(tb_internal_extract_rpc_request_id("v1/devices/me/rpc/request/", &id) == ESP_ERR_INVALID_RESPONSE);
    assert(tb_internal_extract_rpc_request_id("v1/devices/me/telemetry", &id) == ESP_ERR_NOT_FOUND);
}

static void test_rpc_parse_set_relay(void)
{
    tb_internal_command_t cmd = {0};
    const char payload[] = "{\"method\":\"setRelay\",\"params\":true}";
    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/7", payload, strlen(payload), &cmd) == ESP_OK);
    assert(cmd.type == TB_INTERNAL_COMMAND_SET_RELAY);
    assert(cmd.request_id == 7);
    assert(cmd.relay_on == true);
}

static void test_rpc_parse_power_limit(void)
{
    tb_internal_command_t cmd = {0};
    const char get_payload[] = "{\"method\":\"getPowerLimit\",\"params\":{}}";
    const char set_payload[] = "{\"method\":\"setPowerLimit\",\"params\":1234.5}";
    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/8", get_payload, strlen(get_payload), &cmd) == ESP_OK);
    assert(cmd.type == TB_INTERNAL_COMMAND_GET_POWER_LIMIT);
    assert(cmd.request_id == 8);
    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/9", set_payload, strlen(set_payload), &cmd) == ESP_OK);
    assert(cmd.type == TB_INTERNAL_COMMAND_SET_POWER_LIMIT);
    assert(cmd.power_limit_w > 1234.0f && cmd.power_limit_w < 1235.0f);
}

static void test_rpc_rejects_bad_payload(void)
{
    tb_internal_command_t cmd = {0};
    const char payload[] = "{\"method\":\"setPowerLimit\",\"params\":-1}";
    assert(tb_internal_parse_rpc("v1/devices/me/rpc/request/1", payload, strlen(payload), &cmd) == ESP_ERR_INVALID_RESPONSE);
}

static void test_formatting(void)
{
    char buf[256];
    size_t len = 0;
    const tb_internal_telemetry_t telemetry = {
        .voltage = 220.12f,
        .current = 1.234f,
        .power = 271.80f,
        .total_energy = 12.50f,
        .relay_on = true,
        .active_link = "wifi",
        .safety_level = SAFETY_GUARD_LEVEL_NORMAL,
        .valid = true,
    };

    assert(tb_internal_format_telemetry(buf, sizeof(buf), &telemetry, &len) == ESP_OK);
    assert(strstr(buf, "\"voltage\":220.12") != NULL);
    assert(strstr(buf, "\"relayOn\":true") != NULL);
    assert(strstr(buf, "\"activeLink\":\"wifi\"") != NULL);
    assert(tb_internal_format_relay_attribute(buf, sizeof(buf), false, &len) == ESP_OK);
    assert(strcmp(buf, "{\"relayOn\":false}") == 0);
    assert(tb_internal_format_power_limit_attribute(buf, sizeof(buf), 1500.0f, &len) == ESP_OK);
    assert(strcmp(buf, "{\"powerLimit\":1500.00}") == 0);
    assert(tb_internal_format_rpc_response_topic(buf, sizeof(buf), 22, &len) == ESP_OK);
    assert(strcmp(buf, "v1/devices/me/rpc/response/22") == 0);
}

int main(void)
{
    test_rpc_request_id();
    test_rpc_parse_set_relay();
    test_rpc_parse_power_limit();
    test_rpc_rejects_bad_payload();
    test_formatting();
    puts("thingsboard internal tests passed");
    return 0;
}
```

- [ ] **Step 5: Create host test runner**

Create `test/host/run_host_tests.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/test/host/build"
CC_BIN="${CC:-cc}"

mkdir -p "${BUILD_DIR}"

"${CC_BIN}" -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/test/support" \
    -I"${ROOT_DIR}/main/thingsboard" \
    "${ROOT_DIR}/main/thingsboard/thingsboard_client_internal.c" \
    "${ROOT_DIR}/test/host/test_thingsboard_client_internal.c" \
    -o "${BUILD_DIR}/test_thingsboard_client_internal"

"${BUILD_DIR}/test_thingsboard_client_internal"

echo "host tests passed"
```

- [ ] **Step 6: Run host tests**

Run: `bash test/host/run_host_tests.sh`

Expected: PASS with `thingsboard internal tests passed` and `host tests passed`.

- [ ] **Step 7: Build checkpoint for remaining missing files**

Run: `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build`

Expected: FAIL because production `thingsboard_client.c`, LTE, and app files are still missing. Fix any helper compile error before continuing.

## Task 3: ThingsBoard Public Module

**Files:**
- Create: `main/thingsboard/thingsboard_client.h`
- Create: `main/thingsboard/thingsboard_client.c`
- Modify: `test/host/run_host_tests.sh`

- [ ] **Step 1: Create ThingsBoard public header**

Create `main/thingsboard/thingsboard_client.h`:

```c
/**
 * @file thingsboard_client.h
 * @brief ThingsBoard 云平台语义接口
 * @details ThingsBoard cloud platform semantic interface
 * @author OpenCode
 * @date 2026-05-27
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "network_manager.h"
#include "safety_guard.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

typedef struct thingsboard_client thingsboard_client_t;

typedef struct {
    network_manager_t *net_mgr;
    const char *device_token;
    bool enable_rpc;
    bool enable_attributes;
    int json_buf_size;
} tb_client_config_t;

typedef struct {
    float voltage;
    float current;
    float power;
    float total_energy;
    bool relay_on;
    const char *active_link;
    safety_guard_level_t safety_level;
    bool valid;
} tb_telemetry_input_t;

typedef enum {
    TB_COMMAND_SET_RELAY = 0,
    TB_COMMAND_GET_POWER_LIMIT,
    TB_COMMAND_SET_POWER_LIMIT,
} tb_command_type_t;

typedef struct {
    tb_command_type_t type;
    int32_t request_id;
    bool relay_on;
    float power_limit_w;
} tb_command_t;

typedef void (*tb_command_cb_t)(const tb_command_t *cmd, void *user_ctx);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

thingsboard_client_t *thingsboard_client_create(const tb_client_config_t *config);
esp_err_t thingsboard_client_destroy(thingsboard_client_t *me);
esp_err_t thingsboard_client_start(thingsboard_client_t *me);
esp_err_t thingsboard_client_stop(thingsboard_client_t *me);
esp_err_t thingsboard_client_publish_telemetry(thingsboard_client_t *me,
                                               const tb_telemetry_input_t *input);
esp_err_t thingsboard_client_report_relay_state(thingsboard_client_t *me,
                                                bool on);
esp_err_t thingsboard_client_report_power_limit(thingsboard_client_t *me,
                                                float power_limit_w);
esp_err_t thingsboard_client_send_rpc_response(thingsboard_client_t *me,
                                               int32_t request_id,
                                               const char *json,
                                               size_t json_len);
esp_err_t thingsboard_client_register_command_cb(thingsboard_client_t *me,
                                                 tb_command_cb_t cb,
                                                 void *ctx);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
```

After creating the file, add Doxygen comments for every public typedef and function following `docs/agents/coding-style.md`.

- [ ] **Step 2: Create ThingsBoard source skeleton**

Create `main/thingsboard/thingsboard_client.c` with object state and prototypes:

```c
/**
 * @file thingsboard_client.c
 * @brief ThingsBoard 云平台语义实现
 * @details ThingsBoard cloud platform semantic implementation
 * @author OpenCode
 * @date 2026-05-27
 */

/*********************
 *      INCLUDES
 *********************/

#include "thingsboard_client.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "network_types.h"
#include "thingsboard_client_internal.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "thingsboard_client"
#define TB_DEFAULT_JSON_BUF_SIZE (512)

/**********************
 *      TYPEDEFS
 **********************/

struct thingsboard_client {
    tb_client_config_t config;
    SemaphoreHandle_t mutex;
    char *json_buf;
    int json_buf_size;
    tb_command_cb_t cmd_cb;
    void *cmd_ctx;
    bool started;
    bool destroying;
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

static esp_err_t thingsboard_client_validate_config(const tb_client_config_t *config);
static int thingsboard_client_resolve_json_buf_size(const tb_client_config_t *config);
static void thingsboard_client_on_rx(const network_rx_data_t *rx_data, void *user_ctx);
static esp_err_t thingsboard_client_publish_json(thingsboard_client_t *me,
                                                 const char *topic,
                                                 const char *json,
                                                 size_t json_len);
static void thingsboard_client_copy_command(const tb_internal_command_t *src,
                                            tb_command_t *dst);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
```

- [ ] **Step 3: Implement create, destroy, start, and stop**

Implement lifecycle functions with these exact behaviors:

```text
create:
  validate config != NULL and config->net_mgr != NULL.
  allocate object with calloc.
  copy config by value; string pointers remain borrowed.
  json_buf_size = config->json_buf_size > 0 ? config->json_buf_size : 512.
  allocate json_buf with calloc(json_buf_size, 1).
  create mutex.
  on failure, delete/free in reverse order and return NULL.

destroy:
  NULL returns ESP_OK.
  set destroying=true under mutex.
  call stop.
  delete mutex, free json_buf, free object.

start:
  validate object, mutex, and not destroying.
  repeated start returns ESP_OK.
  if enable_rpc, call network_manager_subscribe(net_mgr, TB_TOPIC_RPC_REQUEST_SUB, NETWORK_MQTT_QOS0).
  if enable_attributes, call network_manager_subscribe(net_mgr, TB_TOPIC_ATTRIBUTES, NETWORK_MQTT_QOS0).
  call network_manager_register_rx_cb(net_mgr, thingsboard_client_on_rx, me).
  set started=true only after successful callback registration.
  on a failure after any subscription, best-effort unsubscribe from already subscribed topics.

stop:
  NULL returns ESP_ERR_INVALID_ARG because this is a normal public API, not destroy.
  repeated stop returns ESP_OK.
  call network_manager_register_rx_cb(net_mgr, NULL, NULL).
  best-effort unsubscribe RPC and attributes when enabled; return the first non-OK error except ESP_ERR_NOT_FOUND.
  set started=false.
```

Use `ESP_RETURN_ON_FALSE()` for public argument and state checks.

- [ ] **Step 4: Implement publish APIs**

Implement publish functions so they lock only while formatting into the shared `json_buf`, then unlock before calling `network_manager_publish()`:

```text
thingsboard_client_publish_telemetry:
  validate me and input.
  map tb_telemetry_input_t to tb_internal_telemetry_t.
  lock mutex, format with tb_internal_format_telemetry(), copy json_len, unlock.
  publish to TB_TOPIC_TELEMETRY.

thingsboard_client_report_relay_state:
  lock, format tb_internal_format_relay_attribute(), unlock, publish to TB_TOPIC_ATTRIBUTES.

thingsboard_client_report_power_limit:
  reject power_limit_w <= 0.
  lock, format tb_internal_format_power_limit_attribute(), unlock, publish to TB_TOPIC_ATTRIBUTES.

thingsboard_client_send_rpc_response:
  validate json pointer when json_len > 0.
  format topic into a local char topic[96] using tb_internal_format_rpc_response_topic().
  publish caller-provided json to that topic.
```

`thingsboard_client_publish_json()` constructs this request and delegates to network manager:

```c
network_publish_request_t req = {
    .topic = topic,
    .payload = json,
    .payload_len = json_len,
    .qos = NETWORK_MQTT_QOS0,
    .retain = false,
};
return network_manager_publish(me->config.net_mgr, &req);
```

- [ ] **Step 5: Implement command callback and RX handling**

Implement `thingsboard_client_register_command_cb()` to store or clear the callback under mutex. If `cb == NULL`, store `ctx = NULL`.

Implement `thingsboard_client_on_rx()` with this flow:

```text
1. Validate rx_data, topic, data, and user_ctx.
2. Parse with tb_internal_parse_rpc(topic, data, data_len, &internal_cmd).
3. Ignore ESP_ERR_NOT_FOUND so non-RPC messages do not log as errors.
4. On malformed RPC, log warning and return.
5. Lock object, copy cmd_cb and cmd_ctx unless destroying, unlock.
6. Convert tb_internal_command_t to tb_command_t.
7. If cmd_cb is non-NULL, call cmd_cb(&cmd, cmd_ctx) outside mutex.
```

- [ ] **Step 6: Add production module to host runner compile check**

Keep production `thingsboard_client.c` out of host tests because it depends on FreeRTOS and `network_manager`. Do not add it to `test/host/run_host_tests.sh`.

Run: `bash test/host/run_host_tests.sh`

Expected: PASS. This confirms internal helpers still compile after public-module work.

- [ ] **Step 7: ESP-IDF build checkpoint**

Run: `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build`

Expected: FAIL because LTE and app files are still missing. If the error is inside `thingsboard_client`, fix the exact type, include, or function mismatch before continuing.

## Task 4: LTE Internal Helpers And Host Tests

**Files:**
- Create: `main/network/lte/lte_link_internal.h`
- Create: `main/network/lte/lte_link_internal.c`
- Create: `test/support/lwlte.h`
- Create: `test/host/test_lte_link_internal.c`
- Modify: `test/host/run_host_tests.sh`

- [ ] **Step 1: Create host lwlte stub**

Create `test/support/lwlte.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct lwlte lwlte_t;

typedef enum {
    LWLTE_STATE_STOPPED = 0,
    LWLTE_STATE_STARTING,
    LWLTE_STATE_READY,
    LWLTE_STATE_NET_ACTIVATING,
    LWLTE_STATE_ONLINE,
    LWLTE_STATE_ERROR,
    LWLTE_STATE_DESTROYING,
} lwlte_state_t;

typedef enum {
    LWLTE_MQTT_STATE_STOPPED = 0,
    LWLTE_MQTT_STATE_WAITING_NET,
    LWLTE_MQTT_STATE_CONNECTING,
    LWLTE_MQTT_STATE_CONNECTED,
    LWLTE_MQTT_STATE_DISCONNECTING,
    LWLTE_MQTT_STATE_ERROR,
} lwlte_mqtt_state_t;
```

- [ ] **Step 2: Create LTE internal helper header**

Create `main/network/lte/lte_link_internal.h`:

```c
/**
 * @file lte_link_internal.h
 * @brief LTE 链路纯逻辑辅助函数
 * @details LTE link pure helper functions
 * @author OpenCode
 * @date 2026-05-27
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>

#include "esp_err.h"
#include "lwlte.h"
#include "network_types.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    char *topic;
    network_mqtt_qos_t qos;
    bool in_use;
} lte_link_sub_entry_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

bool lte_link_internal_is_valid_qos(network_mqtt_qos_t qos);
network_link_status_t lte_link_internal_map_status(lwlte_state_t lte_state,
                                                   lwlte_mqtt_state_t mqtt_state,
                                                   bool mqtt_enabled,
                                                   bool query_ok);
esp_err_t lte_link_internal_find_subscription(lte_link_sub_entry_t *table,
                                              int table_size,
                                              const char *topic,
                                              int *out_index,
                                              int *out_free_index);
esp_err_t lte_link_internal_store_subscription(lte_link_sub_entry_t *table,
                                               int table_size,
                                               const char *topic,
                                               network_mqtt_qos_t qos,
                                               int max_topic_len);
esp_err_t lte_link_internal_remove_subscription(lte_link_sub_entry_t *table,
                                                int table_size,
                                                const char *topic);
void lte_link_internal_clear_subscriptions(lte_link_sub_entry_t *table,
                                           int table_size);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
```

- [ ] **Step 3: Implement LTE internal helpers**

Create `main/network/lte/lte_link_internal.c`:

```c
/**
 * @file lte_link_internal.c
 * @brief LTE 链路纯逻辑辅助函数实现
 * @details LTE link pure helper function implementation
 * @author OpenCode
 * @date 2026-05-27
 */

/*********************
 *      INCLUDES
 *********************/

#include "lte_link_internal.h"

#include <stdlib.h>
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

static char *lte_link_internal_strdup(const char *value);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool lte_link_internal_is_valid_qos(network_mqtt_qos_t qos)
{
    return qos == NETWORK_MQTT_QOS0 || qos == NETWORK_MQTT_QOS1 ||
           qos == NETWORK_MQTT_QOS2;
}

network_link_status_t lte_link_internal_map_status(lwlte_state_t lte_state,
                                                   lwlte_mqtt_state_t mqtt_state,
                                                   bool mqtt_enabled,
                                                   bool query_ok)
{
    if (!query_ok) {
        return NETWORK_LINK_STATUS_ERROR;
    }
    if (lte_state == LWLTE_STATE_STOPPED) {
        return NETWORK_LINK_STATUS_IDLE;
    }
    if (lte_state == LWLTE_STATE_STARTING) {
        return NETWORK_LINK_STATUS_STARTING;
    }
    if (lte_state == LWLTE_STATE_READY ||
        lte_state == LWLTE_STATE_NET_ACTIVATING) {
        return NETWORK_LINK_STATUS_CONNECTING;
    }
    if (lte_state == LWLTE_STATE_ONLINE) {
        if (!mqtt_enabled) {
            return NETWORK_LINK_STATUS_DEGRADED;
        }
        if (mqtt_state == LWLTE_MQTT_STATE_CONNECTED) {
            return NETWORK_LINK_STATUS_READY;
        }
        return NETWORK_LINK_STATUS_DEGRADED;
    }
    return NETWORK_LINK_STATUS_ERROR;
}

esp_err_t lte_link_internal_find_subscription(lte_link_sub_entry_t *table,
                                              int table_size,
                                              const char *topic,
                                              int *out_index,
                                              int *out_free_index)
{
    if (table == NULL || table_size <= 0 || topic == NULL ||
        out_index == NULL || out_free_index == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_index = -1;
    *out_free_index = -1;
    for (int i = 0; i < table_size; i++) {
        if (table[i].in_use) {
            if (table[i].topic != NULL && strcmp(table[i].topic, topic) == 0) {
                *out_index = i;
            }
            continue;
        }
        if (*out_free_index < 0) {
            *out_free_index = i;
        }
    }
    return ESP_OK;
}

esp_err_t lte_link_internal_store_subscription(lte_link_sub_entry_t *table,
                                               int table_size,
                                               const char *topic,
                                               network_mqtt_qos_t qos,
                                               int max_topic_len)
{
    int index = -1;
    int free_index = -1;

    if (topic == NULL || topic[0] == '\0' || table == NULL || table_size <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!lte_link_internal_is_valid_qos(qos)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (max_topic_len <= 0 || strlen(topic) >= (size_t)max_topic_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = lte_link_internal_find_subscription(table, table_size, topic,
                                                        &index, &free_index);
    if (ret != ESP_OK) {
        return ret;
    }
    if (index >= 0) {
        table[index].qos = qos;
        return ESP_OK;
    }
    if (free_index < 0) {
        return ESP_ERR_NO_MEM;
    }

    table[free_index].topic = lte_link_internal_strdup(topic);
    if (table[free_index].topic == NULL) {
        return ESP_ERR_NO_MEM;
    }
    table[free_index].qos = qos;
    table[free_index].in_use = true;
    return ESP_OK;
}

esp_err_t lte_link_internal_remove_subscription(lte_link_sub_entry_t *table,
                                                int table_size,
                                                const char *topic)
{
    int index = -1;
    int free_index = -1;

    if (topic == NULL || topic[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = lte_link_internal_find_subscription(table, table_size, topic,
                                                        &index, &free_index);
    if (ret != ESP_OK) {
        return ret;
    }
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    free(table[index].topic);
    table[index].topic = NULL;
    table[index].qos = NETWORK_MQTT_QOS0;
    table[index].in_use = false;
    return ESP_OK;
}

void lte_link_internal_clear_subscriptions(lte_link_sub_entry_t *table,
                                           int table_size)
{
    if (table == NULL || table_size <= 0) {
        return;
    }
    for (int i = 0; i < table_size; i++) {
        free(table[i].topic);
        table[i].topic = NULL;
        table[i].qos = NETWORK_MQTT_QOS0;
        table[i].in_use = false;
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static char *lte_link_internal_strdup(const char *value)
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
```

- [ ] **Step 4: Add LTE helper host tests**

Create `test/host/test_lte_link_internal.c`:

```c
#include <assert.h>
#include <stdio.h>

#include "lte_link_internal.h"

static void test_status_mapping(void)
{
    assert(lte_link_internal_map_status(LWLTE_STATE_STOPPED,
                                        LWLTE_MQTT_STATE_STOPPED,
                                        true, true) == NETWORK_LINK_STATUS_IDLE);
    assert(lte_link_internal_map_status(LWLTE_STATE_STARTING,
                                        LWLTE_MQTT_STATE_STOPPED,
                                        true, true) == NETWORK_LINK_STATUS_STARTING);
    assert(lte_link_internal_map_status(LWLTE_STATE_READY,
                                        LWLTE_MQTT_STATE_STOPPED,
                                        true, true) == NETWORK_LINK_STATUS_CONNECTING);
    assert(lte_link_internal_map_status(LWLTE_STATE_ONLINE,
                                        LWLTE_MQTT_STATE_CONNECTED,
                                        true, true) == NETWORK_LINK_STATUS_READY);
    assert(lte_link_internal_map_status(LWLTE_STATE_ONLINE,
                                        LWLTE_MQTT_STATE_WAITING_NET,
                                        true, true) == NETWORK_LINK_STATUS_DEGRADED);
    assert(lte_link_internal_map_status(LWLTE_STATE_ERROR,
                                        LWLTE_MQTT_STATE_ERROR,
                                        true, true) == NETWORK_LINK_STATUS_ERROR);
    assert(lte_link_internal_map_status(LWLTE_STATE_ONLINE,
                                        LWLTE_MQTT_STATE_CONNECTED,
                                        true, false) == NETWORK_LINK_STATUS_ERROR);
}

static void test_qos(void)
{
    assert(lte_link_internal_is_valid_qos(NETWORK_MQTT_QOS0));
    assert(lte_link_internal_is_valid_qos(NETWORK_MQTT_QOS1));
    assert(lte_link_internal_is_valid_qos(NETWORK_MQTT_QOS2));
    assert(!lte_link_internal_is_valid_qos((network_mqtt_qos_t)3));
}

static void test_subscription_table(void)
{
    lte_link_sub_entry_t table[2] = {0};

    assert(lte_link_internal_store_subscription(table, 2, "a", NETWORK_MQTT_QOS0, 8) == ESP_OK);
    assert(table[0].in_use);
    assert(lte_link_internal_store_subscription(table, 2, "a", NETWORK_MQTT_QOS1, 8) == ESP_OK);
    assert(table[0].qos == NETWORK_MQTT_QOS1);
    assert(lte_link_internal_store_subscription(table, 2, "b", NETWORK_MQTT_QOS0, 8) == ESP_OK);
    assert(lte_link_internal_store_subscription(table, 2, "c", NETWORK_MQTT_QOS0, 8) == ESP_ERR_NO_MEM);
    assert(lte_link_internal_remove_subscription(table, 2, "a") == ESP_OK);
    assert(!table[0].in_use);
    assert(lte_link_internal_remove_subscription(table, 2, "a") == ESP_ERR_NOT_FOUND);
    lte_link_internal_clear_subscriptions(table, 2);
}

int main(void)
{
    test_status_mapping();
    test_qos();
    test_subscription_table();
    puts("lte internal tests passed");
    return 0;
}
```

- [ ] **Step 5: Extend host test runner**

Append this compile/run block to `test/host/run_host_tests.sh` before the final `echo`:

```bash
"${CC_BIN}" -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/test/support" \
    -I"${ROOT_DIR}/main/network/lte" \
    "${ROOT_DIR}/main/network/lte/lte_link_internal.c" \
    "${ROOT_DIR}/test/host/test_lte_link_internal.c" \
    -o "${BUILD_DIR}/test_lte_link_internal"

"${BUILD_DIR}/test_lte_link_internal"
```

- [ ] **Step 6: Run host tests**

Run: `bash test/host/run_host_tests.sh`

Expected: PASS with `thingsboard internal tests passed`, `lte internal tests passed`, and `host tests passed`.

- [ ] **Step 7: ESP-IDF build checkpoint**

Run: `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build`

Expected: FAIL because `main/network/lte/lte_link.c` or app files are still missing. Fix any internal helper compile error before continuing.

## Task 5: LTE Link Public Module

**Files:**
- Create: `main/network/lte/lte_link.h`
- Create: `main/network/lte/lte_link.c`

- [ ] **Step 1: Create LTE public header**

Create `main/network/lte/lte_link.h`:

```c
/**
 * @file lte_link.h
 * @brief LTE 链路子类接口
 * @details LTE link subclass interface
 * @author OpenCode
 * @date 2026-05-27
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

#include "driver/gpio.h"
#include "driver/uart.h"
#include "network_link.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    uart_port_t uart_num;
    gpio_num_t tx_gpio;
    gpio_num_t rx_gpio;
    int baud_rate;
    gpio_num_t en_gpio;
    const char *apn;
    bool auto_connect;
    bool mqtt_enabled;
    const char *mqtt_broker_host;
    uint16_t mqtt_broker_port;
    const char *mqtt_client_id;
    const char *mqtt_username;
    const char *mqtt_password;
    uint16_t mqtt_keepalive_s;
    bool mqtt_clean_session;
    uint32_t init_ready_timeout_ms;
    uint32_t net_activate_timeout_ms;
    int max_subscriptions;
    int max_topic_len;
} lte_link_config_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

network_link_t *lte_link_create(const lte_link_config_t *config);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
```

After creating the file, add Doxygen comments for `lte_link_config_t`, every field, and `lte_link_create()`.

- [ ] **Step 2: Create LTE source skeleton**

Create `main/network/lte/lte_link.c` with includes and object state:

```c
/**
 * @file lte_link.c
 * @brief LTE 链路子类实现
 * @details LTE link subclass implementation
 * @author OpenCode
 * @date 2026-05-27
 */

/*********************
 *      INCLUDES
 *********************/

#include "lte_link.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lte_link_internal.h"
#include "lwlte.h"
#include "lwlte_air780ep.h"
#include "network_link_priv.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "lte_link"
#define LTE_LINK_DEFAULT_MAX_SUBSCRIPTIONS (8)
#define LTE_LINK_DEFAULT_MAX_TOPIC_LEN     (128)
#define LTE_LINK_DEFAULT_BAUD_RATE         (115200)
#define LTE_LINK_PRIMARY_CID               (1)

/**********************
 *      TYPEDEFS
 **********************/

typedef struct lte_link {
    network_link_t base;
    lte_link_config_t config;
    char *apn;
    char *mqtt_broker_host;
    char *mqtt_client_id;
    char *mqtt_username;
    char *mqtt_password;
    lwlte_t *lwlte;
    lte_link_sub_entry_t *sub_table;
    int sub_table_size;
    int max_topic_len;
    SemaphoreHandle_t mutex;
    network_rx_cb_t rx_cb;
    void *rx_ctx;
    network_link_status_t cached_status;
    bool started;
    bool destroying;
    bool mqtt_started;
} lte_link_t;
```

Add static prototypes for all ops methods, config copy/free helpers, `lte_link_from_base()`, `lte_link_on_lwlte_event()`, and `lte_link_replay_subscriptions()`.

- [ ] **Step 3: Implement create and config helpers**

Implement create with these exact requirements:

```text
validate:
  config != NULL.
  uart_num is a valid UART enum.
  tx_gpio and rx_gpio are not GPIO_NUM_NC.
  mqtt_enabled requires mqtt_broker_host, mqtt_broker_port != 0, and mqtt_client_id.

defaults:
  baud_rate uses 115200 when <= 0.
  max_subscriptions uses 8 when <= 0.
  max_topic_len uses 128 when <= 0.

allocation:
  allocate object, mutex, subscription table, and duplicate nullable strings.
  set base.ops to static const ops and base.type to NETWORK_LINK_TYPE_LTE.
  call lwlte_air780ep_init() with lwlte_air780ep_config_t using board UART/GPIO/APN/MQTT fields.
  pass primary_cid=1, auto_connect=config->auto_connect, and all unspecified tuning fields as zero.
  register lwlte_register_event_callback(lwlte, lte_link_on_lwlte_event, me).
```

On any failure after `lwlte_air780ep_init()`, unregister the event callback with `lwlte_register_event_callback(lwlte, NULL, NULL)`, destroy `lwlte`, clear subscriptions, delete mutex, free copied strings, and free the object.

- [ ] **Step 4: Implement network_link ops**

Implement static `network_link_ops_t lte_link_ops` with all methods.

Ops behavior:

```text
destroy:
  NULL returns ESP_OK.
  set destroying=true.
  call stop impl.
  unregister lwlte callback.
  destroy lwlte.
  clear/free subscription table and strings.
  delete mutex and free object.

start:
  repeated start returns ESP_OK.
  reject destroying.
  call lwlte_connect().
  if MQTT enabled, call lwlte_mqtt_start(); ESP_ERR_INVALID_STATE is tolerated only when MQTT is already started/waiting.
  set started=true when connect submission succeeds.

stop:
  repeated stop returns ESP_OK.
  if MQTT enabled, best-effort lwlte_mqtt_stop().
  call lwlte_disconnect().
  clear started and mqtt_started on success; return first non-OK error.

get_status:
  call lwlte_get_state(); if MQTT enabled call lwlte_mqtt_get_state().
  use lte_link_internal_map_status().
  cache and return mapped status.

publish:
  validate req, topic, payload_len, payload, QoS, and payload_len <= UINT16_MAX or INT_MAX as required by lwlte API.
  require MQTT enabled and mapped status READY.
  call lwlte_mqtt_publish(lwlte, topic, payload, payload_len, qos, retain).

subscribe:
  store subscription under mutex.
  if MQTT connected, call lwlte_mqtt_subscribe().

unsubscribe:
  remove subscription under mutex.
  if MQTT connected, call lwlte_mqtt_unsubscribe().
  return ESP_ERR_NOT_FOUND when absent.

register_rx_cb:
  store or clear rx callback under mutex.
```

- [ ] **Step 5: Implement lwlte event bridge and replay**

`lte_link_on_lwlte_event()` behavior:

```text
LWLTE_EVENT_MQTT_CONNECTED:
  mark mqtt_started=true and replay subscriptions.

LWLTE_EVENT_MQTT_DISCONNECTED / LWLTE_EVENT_MQTT_ERROR:
  mark mqtt_started=false.

LWLTE_EVENT_MQTT_DATA:
  copy rx_cb and rx_ctx under mutex unless destroying.
  construct network_rx_data_t with topic pointer, payload pointer cast to const char *, and payload_len cast to int when <= INT_MAX.
  call rx_cb outside mutex.

LWLTE_EVENT_NET_ONLINE / NET_OFFLINE / NET_ERROR:
  update cached status using get_status helper when safe.
```

`lte_link_replay_subscriptions()` iterates the subscription table under mutex, copies one topic at a time to avoid holding the mutex across `lwlte_mqtt_subscribe()`, and returns/logs the first replay error.

- [ ] **Step 6: Run host tests**

Run: `bash test/host/run_host_tests.sh`

Expected: PASS.

- [ ] **Step 7: ESP-IDF build checkpoint**

Run: `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build`

Expected: FAIL because app files are still missing. If the error is inside LTE code, fix the exact `esp-lwlte` API, include, or type mismatch before continuing.

## Task 6: App Controller Internal Helpers And Host Tests

**Files:**
- Create: `main/app/app_controller_internal.h`
- Create: `main/app/app_controller_internal.c`
- Create: `test/host/test_app_controller_internal.c`
- Modify: `test/host/run_host_tests.sh`

- [ ] **Step 1: Create app internal helper header**

Create `main/app/app_controller_internal.h`:

```c
/**
 * @file app_controller_internal.h
 * @brief 应用编排纯逻辑辅助函数
 * @details Application orchestration pure helper functions
 * @author OpenCode
 * @date 2026-05-27
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "network_types.h"
#include "safety_guard.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    float voltage;
    float current;
    float power;
    float total_energy;
    bool metering_valid;
    bool relay_on;
    bool relay_known;
    network_link_type_t active_link;
    safety_guard_level_t safety_level;
    bool safety_valid;
} app_controller_telemetry_source_t;

typedef struct {
    float voltage;
    float current;
    float power;
    float total_energy;
    bool relay_on;
    const char *active_link;
    safety_guard_level_t safety_level;
    bool valid;
} app_controller_telemetry_output_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

const char *app_controller_internal_link_name(network_link_type_t link_type);
bool app_controller_internal_toggle_screen(bool current_enabled);
void app_controller_internal_build_telemetry(
    const app_controller_telemetry_source_t *source,
    app_controller_telemetry_output_t *out);
esp_err_t app_controller_internal_format_power_limit_response(
    char *buf, size_t buf_size, float power_limit_w, size_t *out_len);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
```

- [ ] **Step 2: Implement app internal helpers**

Create `main/app/app_controller_internal.c`:

```c
/**
 * @file app_controller_internal.c
 * @brief 应用编排纯逻辑辅助函数实现
 * @details Application orchestration pure helper function implementation
 * @author OpenCode
 * @date 2026-05-27
 */

/*********************
 *      INCLUDES
 *********************/

#include "app_controller_internal.h"

#include "thingsboard_client_internal.h"

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

void app_controller_internal_build_telemetry(
    const app_controller_telemetry_source_t *source,
    app_controller_telemetry_output_t *out)
{
    if (out == NULL) {
        return;
    }
    *out = (app_controller_telemetry_output_t){0};
    out->active_link = "none";

    if (source == NULL) {
        return;
    }

    out->voltage = source->voltage;
    out->current = source->current;
    out->power = source->power;
    out->total_energy = source->total_energy;
    out->relay_on = source->relay_known ? source->relay_on : false;
    out->active_link = app_controller_internal_link_name(source->active_link);
    out->safety_level = source->safety_valid ? source->safety_level :
                        SAFETY_GUARD_LEVEL_WARNING;
    out->valid = source->metering_valid;
}

esp_err_t app_controller_internal_format_power_limit_response(
    char *buf, size_t buf_size, float power_limit_w, size_t *out_len)
{
    if (!(power_limit_w > 0.0f)) {
        return ESP_ERR_INVALID_ARG;
    }
    return tb_internal_format_power_limit_response(buf, buf_size,
                                                   power_limit_w, out_len);
}
```

- [ ] **Step 3: Create app internal host tests**

Create `test/host/test_app_controller_internal.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "app_controller_internal.h"

static void test_link_name(void)
{
    assert(strcmp(app_controller_internal_link_name(NETWORK_LINK_TYPE_WIFI), "wifi") == 0);
    assert(strcmp(app_controller_internal_link_name(NETWORK_LINK_TYPE_LTE), "lte") == 0);
    assert(strcmp(app_controller_internal_link_name(NETWORK_LINK_TYPE_NONE), "none") == 0);
}

static void test_toggle_screen(void)
{
    assert(app_controller_internal_toggle_screen(true) == false);
    assert(app_controller_internal_toggle_screen(false) == true);
}

static void test_build_telemetry(void)
{
    const app_controller_telemetry_source_t source = {
        .voltage = 220.0f,
        .current = 1.0f,
        .power = 220.0f,
        .total_energy = 2.5f,
        .metering_valid = true,
        .relay_on = true,
        .relay_known = true,
        .active_link = NETWORK_LINK_TYPE_LTE,
        .safety_level = SAFETY_GUARD_LEVEL_NORMAL,
        .safety_valid = true,
    };
    app_controller_telemetry_output_t out = {0};

    app_controller_internal_build_telemetry(&source, &out);
    assert(out.valid == true);
    assert(out.relay_on == true);
    assert(strcmp(out.active_link, "lte") == 0);
    assert(out.safety_level == SAFETY_GUARD_LEVEL_NORMAL);
}

static void test_power_limit_response(void)
{
    char buf[64];
    size_t len = 0;
    assert(app_controller_internal_format_power_limit_response(buf, sizeof(buf), 1800.0f, &len) == ESP_OK);
    assert(strcmp(buf, "{\"powerLimit\":1800.00}") == 0);
    assert(len == strlen(buf));
    assert(app_controller_internal_format_power_limit_response(buf, sizeof(buf), 0.0f, &len) == ESP_ERR_INVALID_ARG);
}

int main(void)
{
    test_link_name();
    test_toggle_screen();
    test_build_telemetry();
    test_power_limit_response();
    puts("app controller internal tests passed");
    return 0;
}
```

- [ ] **Step 4: Extend host test runner**

Append this compile/run block to `test/host/run_host_tests.sh` before the final `echo`:

```bash
"${CC_BIN}" -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/test/support" \
    -I"${ROOT_DIR}/main/app" \
    -I"${ROOT_DIR}/main/thingsboard" \
    "${ROOT_DIR}/main/thingsboard/thingsboard_client_internal.c" \
    "${ROOT_DIR}/main/app/app_controller_internal.c" \
    "${ROOT_DIR}/test/host/test_app_controller_internal.c" \
    -o "${BUILD_DIR}/test_app_controller_internal"

"${BUILD_DIR}/test_app_controller_internal"
```

- [ ] **Step 5: Run host tests**

Run: `bash test/host/run_host_tests.sh`

Expected: PASS with all three internal test binaries passing.

- [ ] **Step 6: ESP-IDF build checkpoint**

Run: `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build`

Expected: FAIL because `main/app/app_controller.c` or `.h` is still missing. Fix any app helper compile error before continuing.

## Task 7: App Controller Public Module

**Files:**
- Create: `main/app/app_controller.h`
- Create: `main/app/app_controller.c`

- [ ] **Step 1: Create app controller public header**

Create `main/app/app_controller.h`:

```c
/**
 * @file app_controller.h
 * @brief 应用编排控制器接口
 * @details Application orchestration controller interface
 * @author OpenCode
 * @date 2026-05-27
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "bl0942.h"
#include "board_pinmap.h"
#include "button.h"
#include "esp_err.h"
#include "esp_event.h"
#include "lvgl_dashboard.h"
#include "metering_service.h"
#include "network_manager.h"
#include "relay.h"
#include "safety_guard.h"
#include "tft_panel.h"
#include "thingsboard_client.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

typedef struct app_controller app_controller_t;

typedef struct {
    esp_event_loop_handle_t event_loop;
    const board_pinmap_t *pinmap;
    relay_t *relay;
    button_t *button;
    bl0942_t *bl0942;
    tft_panel_t *tft_panel;
    metering_service_t *metering;
    safety_guard_t *safety;
    thingsboard_client_t *tb;
    network_manager_t *net_mgr;
    lvgl_dashboard_t *dashboard;
} app_controller_config_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

app_controller_t *app_controller_create(const app_controller_config_t *config);
esp_err_t app_controller_destroy(app_controller_t *me);
esp_err_t app_controller_start(app_controller_t *me);
esp_err_t app_controller_stop(app_controller_t *me);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
```

After creating the file, add Doxygen comments for the handle, config, each borrowed field, and lifecycle functions.

- [ ] **Step 2: Create app controller source skeleton**

Create `main/app/app_controller.c` with object state:

```c
/**
 * @file app_controller.c
 * @brief 应用编排控制器实现
 * @details Application orchestration controller implementation
 * @author OpenCode
 * @date 2026-05-27
 */

/*********************
 *      INCLUDES
 *********************/

#include "app_controller.h"

#include <stdlib.h>
#include <string.h>

#include "app_controller_internal.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "app_controller"
#define APP_CONTROLLER_RPC_BUF_SIZE (96)

/**********************
 *      TYPEDEFS
 **********************/

struct app_controller {
    app_controller_config_t cfg;
    SemaphoreHandle_t mutex;
    esp_event_handler_instance_t safety_handler;
    esp_event_handler_instance_t metering_handler;
    esp_event_handler_instance_t relay_handler;
    bool relay_on;
    bool relay_known;
    bool screen_enabled;
    bool started;
    bool stopping;
};
```

Add static prototypes for button callbacks, safety/metering/relay event handlers, TB command callback, registration helpers, stop helper, started-state getter, and telemetry publisher.

- [ ] **Step 3: Implement create and destroy**

`app_controller_create()` validates required borrowed handles:

```text
required: relay, button, bl0942, metering, safety, tb, net_mgr, dashboard.
optional but stored: event_loop, pinmap, tft_panel.
```

It allocates the object, copies config, creates mutex, initializes `screen_enabled=true`, reads initial relay state with `relay_get()` best-effort, and returns the handle.

`app_controller_destroy(NULL)` returns `ESP_OK`. Non-null destroy calls stop, deletes mutex, and frees the object. It never destroys borrowed module handles.

- [ ] **Step 4: Implement start**

`app_controller_start()` behavior:

```text
1. Validate object and mutex.
2. Lock, reject stopping, return ESP_OK if already started, unlock.
3. Register button callbacks:
   button_register_cb(button, BUTTON_EVENT_SINGLE_CLICK, app_controller_on_button_single_click, me)
   button_register_cb(button, BUTTON_EVENT_LONG_PRESS_START, app_controller_on_button_long_press, me)
4. Register SAFETY_GUARD_EVENT_BASE / SAFETY_GUARD_EVENT_SNAPSHOT with app_controller_on_safety_snapshot.
5. Register METERING_EVENT_BASE / METERING_EVENT_SNAPSHOT with app_controller_on_metering_snapshot.
6. Register RELAY_EVENT_BASE / RELAY_EVENT_STATE_CHANGED with app_controller_on_relay_changed.
7. Register ThingsBoard command callback with thingsboard_client_register_command_cb(tb, app_controller_on_tb_command, me).
8. Start modules in this order: bl0942, metering, safety, network_manager, thingsboard_client, lvgl_dashboard.
9. Mark started=true.
```

On any failure after registrations or partial starts, call `app_controller_stop(me)` before returning the original error.

- [ ] **Step 5: Implement stop**

`app_controller_stop()` behavior:

```text
1. Validate object and mutex.
2. Return ESP_OK when not started and no handler instances are registered.
3. Set stopping=true under mutex.
4. Stop modules in reverse order: dashboard, thingsboard_client, network_manager, safety, metering, bl0942.
5. Clear TB command callback with thingsboard_client_register_command_cb(tb, NULL, NULL).
6. Unregister relay, metering, and safety event handler instances when non-NULL.
7. Set started=false and stopping=false under mutex.
8. Return first non-OK error from stop/unregister operations.
```

The button module has no unregister API. Button callbacks must call a helper that checks `started && !stopping` under mutex and return without side effects otherwise.

- [ ] **Step 6: Implement callbacks**

Callbacks must not hold the app mutex while calling lower modules except for short state checks.

Required behavior:

```text
single click:
  if app is started, call relay_toggle(relay, RELAY_SOURCE_LOCAL_BUTTON).

long press:
  if app is started, compute next screen state using app_controller_internal_toggle_screen(), store it, then call lvgl_dashboard_set_screen_enabled(dashboard, next).

safety snapshot:
  if action == SAFETY_GUARD_ACTION_RELAY_OFF and app is started, call relay_set(relay, RELAY_SOURCE_SAFETY, false).

metering snapshot:
  if app is started and snapshot pointer is valid, call app_controller_publish_telemetry(me, snapshot).

relay changed:
  update relay_on and relay_known under mutex.
  if source != RELAY_SOURCE_CLOUD and app is started, call thingsboard_client_report_relay_state(tb, on).

TB command:
  SET_RELAY -> relay_set(relay, RELAY_SOURCE_CLOUD, relay_on), then thingsboard_client_report_relay_state(tb, relay_on).
  GET_POWER_LIMIT -> safety_guard_get_thresholds(safety, NULL, &power), format with app_controller_internal_format_power_limit_response(), send with thingsboard_client_send_rpc_response().
  SET_POWER_LIMIT -> safety_guard_get_thresholds(safety, &current_a, NULL), safety_guard_set_thresholds(safety, current_a, cmd->power_limit_w), report power limit.
```

- [ ] **Step 7: Implement telemetry publisher**

`app_controller_publish_telemetry()` reads current state:

```text
relay state: use cached relay state; if unknown, call relay_get() best-effort.
network state: network_manager_get_status() best-effort; active link defaults to NONE on failure.
safety state: safety_guard_get_latest() best-effort; invalid safety defaults to WARNING.
metering: from event snapshot.
```

Build `app_controller_telemetry_source_t`, convert with `app_controller_internal_build_telemetry()`, then map to `tb_telemetry_input_t` and call `thingsboard_client_publish_telemetry()`.

- [ ] **Step 8: Run host tests**

Run: `bash test/host/run_host_tests.sh`

Expected: PASS.

- [ ] **Step 9: ESP-IDF build checkpoint**

Run: `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build`

Expected: FAIL only if `main.c` still references old minimal logic or if integration config is incomplete. Fix app controller compile errors before moving to `main.c` assembly.

## Task 8: Complete Main Assembly

**Files:**
- Modify: `main/main.c`

- [ ] **Step 1: Replace main includes and configuration macros**

Replace `main/main.c` with a full assembly source. Start with these includes and macros:

```c
#include "app_controller.h"
#include "bl0942.h"
#include "board_pinmap.h"
#include "button.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lte_link.h"
#include "lvgl_dashboard.h"
#include "metering_service.h"
#include "network_manager.h"
#include "relay.h"
#include "safety_guard.h"
#include "tft_panel.h"
#include "thingsboard_client.h"
#include "wifi_link.h"

#define SMART_SOCKET_WIFI_SSID        "SmartSocketWiFi"
#define SMART_SOCKET_WIFI_PASSWORD    "password"
#define SMART_SOCKET_TB_HOST          "thingsboard.cloud"
#define SMART_SOCKET_TB_PORT          (1883)
#define SMART_SOCKET_TB_CLIENT_ID     "smart_socket_001"
#define SMART_SOCKET_TB_TOKEN         "replace_with_access_token"
#define SMART_SOCKET_LTE_APN          "cmnet"

#define SMART_SOCKET_BL0942_UART      UART_NUM_1
#define SMART_SOCKET_LTE_UART         UART_NUM_2
#define SMART_SOCKET_PANEL_WIDTH      (172)
#define SMART_SOCKET_PANEL_HEIGHT     (320)

static const char *TAG = "main";
```

If `UART_NUM_2` is not available for the target during build, change `SMART_SOCKET_LTE_UART` to the board-validated unused UART port and document the reason in a short code comment.

- [ ] **Step 2: Add fatal idle helper**

Add this helper in `main.c`:

```c
static void smart_socket_idle_forever(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

- [ ] **Step 3: Implement app_main initialization**

In `app_main()`, initialize base services:

```c
ESP_LOGI(TAG, "Smart_Socket starting...");

esp_err_t ret = nvs_flash_init();
if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
}
ESP_ERROR_CHECK(ret);
ESP_ERROR_CHECK(esp_netif_init());
ESP_ERROR_CHECK(esp_event_loop_create_default());

const board_pinmap_t *pm = board_pinmap_get();
if (pm == NULL) {
    ESP_LOGE(TAG, "board pinmap is null");
    smart_socket_idle_forever();
}
```

- [ ] **Step 4: Create driver and service modules**

Create the objects using stack compound literals:

```c
relay_t *relay = relay_create(&(relay_config_t){
    .ctrl_gpio = pm->relay_ctrl_gpio,
    .active_level = (relay_active_level_t)pm->relay_active_level,
});
button_t *button = button_create(&(button_config_t){
    .input_gpio = pm->button_gpio,
    .active_level = (button_active_level_t)pm->button_active_level,
});
bl0942_t *bl0942 = bl0942_create(&(bl0942_config_t){
    .uart_num = SMART_SOCKET_BL0942_UART,
    .en_gpio = pm->bl0942_en_gpio,
    .tx_gpio = pm->bl0942_tx_gpio,
    .rx_gpio = pm->bl0942_rx_gpio,
    .baud_rate = 9600,
    .device_address = 0,
    .rx_buf_size = 256,
    .read_timeout_ms = 500,
    .sample_period_ms = 100,
    .fault_threshold = 10,
    .hard_reset_max_attempts = 3,
});
metering_service_t *metering = metering_service_create(&(metering_config_t){
    .window_samples = 10,
    .publish_period_ms = 1000,
});
safety_guard_t *safety = safety_guard_create(&(safety_guard_config_t){
    .overcurrent_threshold_a = 10.0f,
    .overpower_threshold_w = 2200.0f,
    .persistence_samples = 3,
});
tft_panel_t *tft = tft_panel_create(&(tft_panel_config_t){
    .sclk_gpio = pm->tft_sclk_gpio,
    .mosi_gpio = pm->tft_mosi_gpio,
    .dc_gpio = pm->tft_dc_gpio,
    .cs_gpio = pm->tft_cs_gpio,
    .rst_gpio = pm->tft_rst_gpio,
    .bl_gpio = pm->tft_bl_gpio,
    .panel_width = SMART_SOCKET_PANEL_WIDTH,
    .panel_height = SMART_SOCKET_PANEL_HEIGHT,
});
```

After creation, check every pointer. On any `NULL`, log `ESP_LOGE(TAG, "create base module failed")` and call `smart_socket_idle_forever()`.

- [ ] **Step 5: Create network and cloud modules**

Create network links and manager:

```c
network_link_t *wifi = wifi_link_create(&(wifi_link_config_t){
    .ssid = SMART_SOCKET_WIFI_SSID,
    .password = SMART_SOCKET_WIFI_PASSWORD,
    .mqtt_broker_host = SMART_SOCKET_TB_HOST,
    .mqtt_broker_port = SMART_SOCKET_TB_PORT,
    .mqtt_client_id = SMART_SOCKET_TB_CLIENT_ID,
    .mqtt_username = SMART_SOCKET_TB_TOKEN,
    .mqtt_password = NULL,
    .mqtt_keepalive_s = 60,
    .mqtt_clean_session = true,
    .mqtt_use_tls = false,
    .max_subscriptions = 8,
    .max_topic_len = 128,
});
network_link_t *lte = lte_link_create(&(lte_link_config_t){
    .uart_num = SMART_SOCKET_LTE_UART,
    .tx_gpio = pm->lte_tx_gpio,
    .rx_gpio = pm->lte_rx_gpio,
    .baud_rate = 115200,
    .en_gpio = pm->lte_en_gpio,
    .apn = SMART_SOCKET_LTE_APN,
    .auto_connect = false,
    .mqtt_enabled = true,
    .mqtt_broker_host = SMART_SOCKET_TB_HOST,
    .mqtt_broker_port = SMART_SOCKET_TB_PORT,
    .mqtt_client_id = SMART_SOCKET_TB_CLIENT_ID,
    .mqtt_username = SMART_SOCKET_TB_TOKEN,
    .mqtt_password = NULL,
    .mqtt_keepalive_s = 60,
    .mqtt_clean_session = true,
    .init_ready_timeout_ms = 60000,
    .net_activate_timeout_ms = 120000,
    .max_subscriptions = 8,
    .max_topic_len = 128,
});
network_manager_t *net_mgr = network_manager_create(&(network_manager_config_t){
    .primary = wifi,
    .backup = lte,
    .preferred_primary = NETWORK_LINK_TYPE_WIFI,
    .failover_recheck_ms = 5000,
    .failback_delay_ms = 30000,
    .max_subscriptions = 8,
});
thingsboard_client_t *tb = thingsboard_client_create(&(tb_client_config_t){
    .net_mgr = net_mgr,
    .device_token = SMART_SOCKET_TB_TOKEN,
    .enable_rpc = true,
    .enable_attributes = true,
    .json_buf_size = 512,
});
```

Check all four pointers. On failure, log `ESP_LOGE(TAG, "create network/cloud module failed")` and idle forever.

- [ ] **Step 6: Create dashboard and app controller**

Create dashboard and app controller:

```c
lvgl_dashboard_t *dashboard = lvgl_dashboard_create(&(lvgl_dashboard_config_t){
    .panel = tft,
    .network_manager = net_mgr,
    .lvgl_task_stack = 6144,
    .lvgl_task_priority = 4,
    .lvgl_tick_period_ms = 10,
    .update_period_ms = 50,
});
app_controller_t *app = app_controller_create(&(app_controller_config_t){
    .event_loop = NULL,
    .pinmap = pm,
    .relay = relay,
    .button = button,
    .bl0942 = bl0942,
    .tft_panel = tft,
    .metering = metering,
    .safety = safety,
    .tb = tb,
    .net_mgr = net_mgr,
    .dashboard = dashboard,
});
```

Check both pointers. On failure, log `ESP_LOGE(TAG, "create app/display module failed")` and idle forever.

- [ ] **Step 7: Start app controller**

Start and idle:

```c
ret = app_controller_start(app);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "start app controller failed: %s", esp_err_to_name(ret));
    smart_socket_idle_forever();
}

ESP_LOGI(TAG, "Smart_Socket started");
smart_socket_idle_forever();
```

- [ ] **Step 8: Run host tests**

Run: `bash test/host/run_host_tests.sh`

Expected: PASS.

- [ ] **Step 9: ESP-IDF build checkpoint**

Run: `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build`

Expected: PASS, or a concrete compiler/linker error in the newly integrated code. Fix the first concrete error and rerun until the build passes or only an external dependency/environment issue remains.

## Task 9: Full Verification And Cleanup

**Files:**
- Inspect all files created or modified by Tasks 1-8.

- [ ] **Step 1: Run all host tests**

Run: `bash test/host/run_host_tests.sh`

Expected: PASS with:

```text
thingsboard internal tests passed
lte internal tests passed
app controller internal tests passed
host tests passed
```

- [ ] **Step 2: Run ESP-IDF MCP build**

Use the ESP-IDF MCP build tool for the project.

Expected: PASS. If MCP returns an environment/dependency error unrelated to C code, run Step 3.

- [ ] **Step 3: Fallback shell build**

Run: `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build`

Expected: PASS. If it fails, capture the first compiler or linker error, fix that concrete code error, and rerun.

- [ ] **Step 4: Static dependency checks**

Run:

```bash
rg "#include \"wifi_link.h\"|#include \"lte_link.h\"" main/thingsboard
rg "#include \"board_pinmap.h\"" main/thingsboard main/network/lte
rg "TODO|TBD|PLACEHOLDER" main/thingsboard main/network/lte main/app test/host test/support
```

Expected:

```text
No direct Wi-Fi/LTE include from ThingsBoard client.
No board_pinmap dependency in ThingsBoard or LTE link.
No unfinished TODO/TBD/PLACEHOLDER markers in new code.
```

- [ ] **Step 5: Inspect worktree**

Run: `git status --short`

Expected: intended files from this plan plus pre-existing user changes. Do not revert `.gitignore` or `docs/agents/classes.md` user changes. Do not commit unless explicitly authorized.

- [ ] **Step 6: Final report**

Report:

```text
Implemented final three modules: thingsboard_client, lte_link, app_controller.
Host verification: test/host/run_host_tests.sh passed.
Build verification: ESP-IDF build passed, or failed due to [exact external/code reason].
Hardware verification: not flashed and not serial-validated in this pass.
```

## Self-Review Notes

Spec coverage:

1. `thingsboard_client` public API, JSON formatting, RPC topic parsing, command callback, telemetry/attribute/RPC publish, and network-manager-only dependency are covered by Tasks 2 and 3.
2. `lte_link` public API, esp-lwlte facade creation, network_link ops, status mapping, MQTT publish/subscribe/unsubscribe, RX bridge, and subscription replay are covered by Tasks 4 and 5.
3. `app_controller` public API, borrowed-handle orchestration, callbacks, event handlers, start/stop order, telemetry assembly, safety action, cloud command handling, and screen toggle are covered by Tasks 6 and 7.
4. `main.c` complete object assembly, IDF 6.0 alignment, and local esp-lwlte component integration are covered by Tasks 1 and 8.
5. Host tests for ThingsBoard, LTE, and app pure logic are covered by Tasks 2, 4, 6, and 9.
6. ESP-IDF build and static dependency verification are covered by Task 9.

Placeholder scan: no intentional TBD, TODO, or placeholder implementation steps remain in this plan.

Type consistency: public names match the approved spec and `docs/agents/classes.md`; internal helper names use module prefixes and are included by both production code and host tests.
