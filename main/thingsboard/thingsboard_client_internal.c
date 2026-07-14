/**
 * @file thingsboard_client_internal.c
 * @brief ThingsBoard 客户端内部辅助实现
 * @details ThingsBoard client internal helper implementation
 * @author OpenCode
 * @date 2026-05-28
 */

/*********************
 *      INCLUDES
 *********************/

#include "thingsboard_client_internal.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
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

/**
 * @brief 在限定长度载荷中查找文本
 * @details Find text in a length-limited payload
 * @param[in] payload 载荷； Payload
 * @param[in] payload_len 载荷长度； Payload length
 * @param[in] needle 查找文本； Text to find
 * @return 找到的位置，未找到返回 NULL； Match pointer, or NULL when not found
 */
static const char *find_in_payload(const char *payload, size_t payload_len,
                                   const char *needle);

/**
 * @brief 查找 JSON 字段值开始位置
 * @details Find the start of a JSON field value
 * @param[in] payload 载荷； Payload
 * @param[in] payload_len 载荷长度； Payload length
 * @param[in] key 字段名； Field key
 * @return 字段值位置，未找到返回 NULL； Field value pointer, or NULL
 */
static const char *find_json_field_value(const char *payload,
                                         size_t payload_len,
                                         const char *key);

/**
 * @brief 判断 JSON 字符串值
 * @details Check a JSON string value
 * @param[in] value 字段值位置； Field value pointer
 * @param[in] payload_end 载荷结束位置； Payload end pointer
 * @param[in] expected 期望字符串； Expected string
 * @return 是否匹配； Whether it matches
 */
static bool json_string_value_equals(const char *value,
                                     const char *payload_end,
                                     const char *expected);

/**
 * @brief 查找 params 字段值开始位置
 * @details Find the start of the params field value
 * @param[in] payload 载荷； Payload
 * @param[in] payload_len 载荷长度； Payload length
 * @return params 值位置，未找到返回 NULL； Params value pointer, or NULL
 */
static const char *find_params_value(const char *payload, size_t payload_len);

/**
 * @brief 解析布尔 params
 * @details Parse boolean params
 * @param[in] payload 载荷； Payload
 * @param[in] payload_len 载荷长度； Payload length
 * @param[in] params params 值位置； Params value pointer
 * @param[out] out_value 布尔输出； Boolean output
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_RESPONSE: 格式错误； Malformed value
 */
static esp_err_t parse_bool_param(const char *payload, size_t payload_len,
                                  const char *params, bool *out_value);

/**
 * @brief 判断 JSON 参数终止符
 * @details Check JSON parameter terminator
 * @param[in] ch 字符； Character
 * @return 是否为允许的终止符； Whether the terminator is allowed
 */
static bool is_json_param_terminator(char ch);

/**
 * @brief 判断 JSON 值后是否有有效终止符
 * @details Check whether a JSON value is followed by a valid terminator
 * @param[in] cursor 值后位置； Position after value
 * @param[in] payload_end 载荷结束位置； Payload end pointer
 * @return 是否有有效终止符； Whether a valid terminator is present
 */
static bool has_json_value_terminator(const char *cursor,
                                      const char *payload_end);

/**
 * @brief 判断 JSON 空白字符
 * @details Check JSON whitespace
 * @param[in] ch 字符； Character
 * @return 是否为空白字符； Whether it is whitespace
 */
static bool is_json_whitespace(char ch);

/**
 * @brief 解析正数浮点 params
 * @details Parse positive floating-point params
 * @param[in] payload 载荷； Payload
 * @param[in] payload_len 载荷长度； Payload length
 * @param[out] out_value 浮点输出； Float output
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_RESPONSE: 格式错误； Malformed value
 */
static esp_err_t parse_positive_float_param(const char *payload,
                                            size_t payload_len,
                                            float *out_value);

/**
 * @brief 获取安全的链路名称
 * @details Get a safe active link name for JSON serialization
 */
static const char *safe_active_link_name(const char *active_link);

/**
 * @brief 校验遥测数值是否有限
 * @details Validate telemetry numeric fields are finite
 */
static bool telemetry_values_are_finite(const tb_internal_telemetry_t *input);

/**
 * @brief 保存 snprintf 结果
 * @details Store snprintf result
 * @param[in] written snprintf 返回值； snprintf result
 * @param[in] buf_size 缓冲区大小； Buffer size
 * @param[out] out_len 输出长度，可为 NULL； Output length, may be NULL
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_SIZE: 缓冲区不足； Buffer too small
 *         - ESP_FAIL: 格式化失败； Format failed
 */
static esp_err_t finish_format(int written, size_t buf_size, size_t *out_len);

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
    long value;
    const char *id_start;
    const size_t prefix_len = strlen(TB_TOPIC_RPC_REQUEST_PREFIX);

    if ((topic == NULL) || (out_request_id == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strncmp(topic, TB_TOPIC_RPC_REQUEST_PREFIX, prefix_len) != 0) {
        return ESP_ERR_NOT_FOUND;
    }

    id_start = topic + prefix_len;
    if (*id_start == '\0') {
        return ESP_ERR_INVALID_RESPONSE;
    }

    errno = 0;
    value = strtol(id_start, &end, 10);
    if ((errno != 0) || (end == id_start) || (*end != '\0') ||
        (value < 0) || (value > INT32_MAX)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *out_request_id = (int32_t)value;

    return ESP_OK;
}

esp_err_t tb_internal_parse_rpc(const char *topic, const char *payload,
                                size_t payload_len,
                                tb_internal_command_t *out_command)
{
    esp_err_t err;
    int32_t request_id = 0;
    tb_internal_command_t command = { 0 };
    const char *method;
    const char *params;
    const char *payload_end;

    if ((payload == NULL) || (payload_len == 0U) || (out_command == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    payload_end = payload + payload_len;

    err = tb_internal_extract_rpc_request_id(topic, &request_id);
    if (err != ESP_OK) {
        return err;
    }

    command.request_id = request_id;

    method = find_json_field_value(payload, payload_len, "method");
    if (json_string_value_equals(method, payload_end, "setRelay")) {
        params = find_params_value(payload, payload_len);
        err = parse_bool_param(payload, payload_len, params, &command.relay_on);
        if (err != ESP_OK) {
            return err;
        }
        command.type = TB_INTERNAL_COMMAND_SET_RELAY;
    } else if (json_string_value_equals(method, payload_end, "getPowerLimit")) {
        command.type = TB_INTERNAL_COMMAND_GET_POWER_LIMIT;
    } else if (json_string_value_equals(method, payload_end, "setPowerLimit")) {
        err = parse_positive_float_param(payload, payload_len,
                                         &command.power_limit_w);
        if (err != ESP_OK) {
            return err;
        }
        command.type = TB_INTERNAL_COMMAND_SET_POWER_LIMIT;
    } else {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *out_command = command;

    return ESP_OK;
}

esp_err_t tb_internal_format_telemetry(char *buf, size_t buf_size,
                                       const tb_internal_telemetry_t *input,
                                       size_t *out_len)
{
    int written;
    const char *active_link;

    if ((buf == NULL) || (buf_size == 0U) || (input == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!telemetry_values_are_finite(input)) {
        return ESP_ERR_INVALID_ARG;
    }

    active_link = safe_active_link_name(input->active_link);
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

    return finish_format(written, buf_size, out_len);
}

esp_err_t tb_internal_format_relay_attribute(char *buf, size_t buf_size,
                                             bool relay_on, size_t *out_len)
{
    int written;

    if ((buf == NULL) || (buf_size == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(buf, buf_size, "{\"relayOn\":%s}",
                       relay_on ? "true" : "false");

    return finish_format(written, buf_size, out_len);
}

esp_err_t tb_internal_format_power_limit_attribute(char *buf, size_t buf_size,
                                                   float power_limit_w,
                                                   size_t *out_len)
{
    int written;

    if ((buf == NULL) || (buf_size == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!isfinite(power_limit_w)) {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(buf, buf_size, "{\"powerLimit\":%.2f}",
                       power_limit_w);

    return finish_format(written, buf_size, out_len);
}

esp_err_t tb_internal_format_power_limit_response(char *buf, size_t buf_size,
                                                   float power_limit_w,
                                                   size_t *out_len)
{
    return tb_internal_format_power_limit_attribute(buf, buf_size,
                                                    power_limit_w, out_len);
}

esp_err_t tb_internal_format_rpc_error_response(char *buf, size_t buf_size,
                                                size_t *out_len)
{
    int written;

    if ((buf == NULL) || (buf_size == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(buf, buf_size, "{\"error\":\"internal_error\"}");
    return finish_format(written, buf_size, out_len);
}

esp_err_t tb_internal_format_rpc_response_topic(char *buf, size_t buf_size,
                                                int32_t request_id,
                                                size_t *out_len)
{
    int written;

    if ((buf == NULL) || (buf_size == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (request_id < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(buf, buf_size, TB_TOPIC_RPC_RESPONSE_FMT,
                       (long)request_id);

    return finish_format(written, buf_size, out_len);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static const char *find_in_payload(const char *payload, size_t payload_len,
                                   const char *needle)
{
    size_t needle_len;
    size_t i;

    if ((payload == NULL) || (needle == NULL)) {
        return NULL;
    }

    needle_len = strlen(needle);
    if ((needle_len == 0U) || (needle_len > payload_len)) {
        return NULL;
    }

    for (i = 0; i <= (payload_len - needle_len); i++) {
        if (memcmp(payload + i, needle, needle_len) == 0) {
            return payload + i;
        }
    }

    return NULL;
}

static const char *safe_active_link_name(const char *active_link)
{
    if (active_link == NULL) {
        return "none";
    }
    if ((strcmp(active_link, "wifi") == 0) ||
        (strcmp(active_link, "lte") == 0) ||
        (strcmp(active_link, "none") == 0)) {
        return active_link;
    }

    return "none";
}

static bool telemetry_values_are_finite(const tb_internal_telemetry_t *input)
{
    return isfinite(input->voltage) && isfinite(input->current) &&
           isfinite(input->power) && isfinite(input->energy_delta) &&
           isfinite(input->frequency);
}

static const char *find_json_field_value(const char *payload,
                                         size_t payload_len,
                                         const char *key)
{
    char quoted_key[32];
    int key_len;
    const char *match;
    const char *cursor;
    const char *payload_end;

    if ((payload == NULL) || (key == NULL)) {
        return NULL;
    }
    payload_end = payload + payload_len;

    key_len = snprintf(quoted_key, sizeof(quoted_key), "\"%s\"", key);
    if ((key_len < 0) || ((size_t)key_len >= sizeof(quoted_key))) {
        return NULL;
    }

    match = find_in_payload(payload, payload_len, quoted_key);
    if (match == NULL) {
        return NULL;
    }

    cursor = match + (size_t)key_len;
    while ((cursor < payload_end) && is_json_whitespace(*cursor)) {
        cursor++;
    }
    if ((cursor >= payload_end) || (*cursor != ':')) {
        return NULL;
    }
    cursor++;
    while ((cursor < payload_end) && is_json_whitespace(*cursor)) {
        cursor++;
    }

    return (cursor < payload_end) ? cursor : NULL;
}

static bool json_string_value_equals(const char *value,
                                     const char *payload_end,
                                     const char *expected)
{
    size_t expected_len;
    const char *cursor;

    if ((value == NULL) || (payload_end == NULL) || (expected == NULL) ||
        (value >= payload_end) || (*value != '"')) {
        return false;
    }

    expected_len = strlen(expected);
    cursor = value + 1;
    if (((size_t)(payload_end - cursor) < (expected_len + 1U)) ||
        (memcmp(cursor, expected, expected_len) != 0) ||
        (cursor[expected_len] != '"')) {
        return false;
    }

    return true;
}

static const char *find_params_value(const char *payload, size_t payload_len)
{
    return find_json_field_value(payload, payload_len, "params");
}

static esp_err_t parse_bool_param(const char *payload, size_t payload_len,
                                  const char *params, bool *out_value)
{
    const char *payload_end;
    size_t remaining;

    if ((payload == NULL) || (params == NULL) || (out_value == NULL)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    payload_end = payload + payload_len;
    if ((params < payload) || (params > payload_end)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    remaining = (size_t)(payload_end - params);
    if ((remaining >= strlen("true")) &&
        (memcmp(params, "true", strlen("true")) == 0) &&
        has_json_value_terminator(params + strlen("true"), payload_end)) {
        *out_value = true;
        return ESP_OK;
    }

    if ((remaining >= strlen("false")) &&
        (memcmp(params, "false", strlen("false")) == 0) &&
        has_json_value_terminator(params + strlen("false"), payload_end)) {
        *out_value = false;
        return ESP_OK;
    }

    return ESP_ERR_INVALID_RESPONSE;
}

static bool is_json_param_terminator(char ch)
{
    return (ch == '}') || (ch == ',');
}

static bool has_json_value_terminator(const char *cursor,
                                      const char *payload_end)
{
    while ((cursor < payload_end) && is_json_whitespace(*cursor)) {
        cursor++;
    }

    return (cursor < payload_end) && is_json_param_terminator(*cursor);
}

static bool is_json_whitespace(char ch)
{
    return (ch == ' ') || (ch == '\t') || (ch == '\r') || (ch == '\n');
}

static esp_err_t parse_positive_float_param(const char *payload,
                                            size_t payload_len,
                                            float *out_value)
{
    char temp[32];
    char *end = NULL;
    float value;
    size_t value_len = 0;
    const char *params = find_params_value(payload, payload_len);
    const char *payload_end;

    if ((params == NULL) || (out_value == NULL)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    payload_end = payload + payload_len;

    while (((params + value_len) < payload_end) &&
           (value_len < (sizeof(temp) - 1U))) {
        char ch = params[value_len];

        if (is_json_param_terminator(ch) || is_json_whitespace(ch)) {
            break;
        }
        temp[value_len] = ch;
        value_len++;
    }

    if ((value_len == 0U) ||
        !has_json_value_terminator(params + value_len, payload_end)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    temp[value_len] = '\0';

    errno = 0;
    value = strtof(temp, &end);
    if ((errno != 0) || (end == temp) || (*end != '\0') ||
        !isfinite(value) || (value <= 0.0f)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *out_value = value;

    return ESP_OK;
}

static esp_err_t finish_format(int written, size_t buf_size, size_t *out_len)
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
