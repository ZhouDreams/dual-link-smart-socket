/**
 * @file thingsboard_client_internal.h
 * @brief ThingsBoard 客户端内部辅助接口
 * @details ThingsBoard client internal helper interface
 * @author OpenCode
 * @date 2026-05-28
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

/**
 * @brief ThingsBoard RPC 命令类型
 * @details ThingsBoard RPC command type
 */
typedef enum {
    TB_INTERNAL_COMMAND_SET_RELAY = 0,       /**< 设置继电器； Set relay */
    TB_INTERNAL_COMMAND_GET_POWER_LIMIT,     /**< 获取功率限制； Get power limit */
    TB_INTERNAL_COMMAND_SET_POWER_LIMIT,     /**< 设置功率限制； Set power limit */
} tb_internal_command_type_t;

/**
 * @brief ThingsBoard RPC 命令
 * @details ThingsBoard RPC command
 */
typedef struct {
    tb_internal_command_type_t type; /**< 命令类型； Command type */
    int32_t request_id;              /**< 请求 ID； Request ID */
    bool relay_on;                   /**< 继电器状态； Relay state */
    float power_limit_w;             /**< 功率限制 W； Power limit in watts */
} tb_internal_command_t;

/**
 * @brief ThingsBoard 遥测输入
 * @details ThingsBoard telemetry input
 */
typedef struct {
    float voltage;                         /**< 电压 V； Voltage in volts */
    float current;                         /**< 电流 A； Current in amperes */
    float power;                           /**< 功率 W； Power in watts */
    float energy_delta;                    /**< 上报区间电能增量 mWh； Interval energy delta in milliwatt-hours */
    float frequency;                       /**< 电网频率 Hz； Grid frequency in hertz */
    bool relay_on;                         /**< 继电器状态； Relay state */
    const char *active_link;               /**< 当前链路； Active link */
    safety_guard_level_t safety_level;     /**< 安全等级； Safety level */
    bool valid;                            /**< 数据是否有效； Whether data is valid */
} tb_internal_telemetry_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 提取 RPC 请求 ID
 * @details Extract RPC request ID
 * @param[in] topic MQTT 主题； MQTT topic
 * @param[out] out_request_id 请求 ID 输出； Request ID output
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_NOT_FOUND: 非 RPC 请求主题； Not an RPC request topic
 *         - ESP_ERR_INVALID_RESPONSE: RPC 请求主题格式错误； Malformed RPC request topic
 */
esp_err_t tb_internal_extract_rpc_request_id(const char *topic,
                                             int32_t *out_request_id);

/**
 * @brief 解析 RPC 命令
 * @details Parse RPC command
 * @param[in] topic MQTT 主题； MQTT topic
 * @param[in] payload 载荷； Payload
 * @param[in] payload_len 载荷长度； Payload length
 * @param[out] out_command 命令输出； Command output
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_NOT_FOUND: 非 RPC 请求主题； Not an RPC request topic
 *         - ESP_ERR_INVALID_RESPONSE: RPC 内容格式错误； Malformed RPC content
 */
esp_err_t tb_internal_parse_rpc(const char *topic, const char *payload,
                                size_t payload_len,
                                tb_internal_command_t *out_command);

/**
 * @brief 格式化遥测 JSON
 * @details Format telemetry JSON
 * @param[out] buf 输出缓冲区； Output buffer
 * @param[in] buf_size 缓冲区大小； Buffer size
 * @param[in] input 遥测输入； Telemetry input
 * @param[out] out_len 输出长度，可为 NULL； Output length, may be NULL
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_SIZE: 缓冲区不足； Buffer too small
 *         - ESP_FAIL: 格式化失败； Format failed
 */
esp_err_t tb_internal_format_telemetry(char *buf, size_t buf_size,
                                       const tb_internal_telemetry_t *input,
                                       size_t *out_len);

/**
 * @brief 格式化继电器属性 JSON
 * @details Format relay attribute JSON
 */
esp_err_t tb_internal_format_relay_attribute(char *buf, size_t buf_size,
                                             bool relay_on, size_t *out_len);

/**
 * @brief 格式化功率限制属性 JSON
 * @details Format power limit attribute JSON
 */
esp_err_t tb_internal_format_power_limit_attribute(char *buf, size_t buf_size,
                                                   float power_limit_w,
                                                   size_t *out_len);

/**
 * @brief 格式化功率限制 RPC 响应 JSON
 * @details Format power limit RPC response JSON
 */
esp_err_t tb_internal_format_power_limit_response(char *buf, size_t buf_size,
                                                  float power_limit_w,
                                                  size_t *out_len);

/**
 * @brief 格式化 RPC 响应主题
 * @details Format RPC response topic
 */
esp_err_t tb_internal_format_rpc_response_topic(char *buf, size_t buf_size,
                                                int32_t request_id,
                                                size_t *out_len);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
