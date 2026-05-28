/**
 * @file app_controller_internal.h
 * @brief 应用控制器内部辅助接口
 * @details App controller internal helper interface
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

#include "esp_err.h"
#include "network_types.h"
#include "safety_guard.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 应用遥测源数据
 * @details App telemetry source data
 */
typedef struct {
    float voltage;                         /**< 电压； Voltage */
    float current;                         /**< 电流； Current */
    float power;                           /**< 功率； Power */
    float total_energy;                    /**< 总电量； Total energy */
    bool metering_valid;                   /**< 计量是否有效； Whether metering is valid */
    bool relay_on;                         /**< 继电器状态； Relay state */
    bool relay_known;                      /**< 继电器状态是否已知； Whether relay state is known */
    network_link_type_t active_link;       /**< 当前链路； Active link */
    safety_guard_level_t safety_level;     /**< 安全等级； Safety level */
    bool safety_valid;                     /**< 安全等级是否有效； Whether safety level is valid */
} app_controller_telemetry_source_t;

/**
 * @brief 应用遥测输出数据
 * @details App telemetry output data
 */
typedef struct {
    float voltage;                         /**< 电压； Voltage */
    float current;                         /**< 电流； Current */
    float power;                           /**< 功率； Power */
    float total_energy;                    /**< 总电量； Total energy */
    bool relay_on;                         /**< 继电器状态； Relay state */
    const char *active_link;               /**< 当前链路名称； Active link name */
    safety_guard_level_t safety_level;     /**< 安全等级； Safety level */
    bool valid;                            /**< 数据是否有效； Whether data is valid */
} app_controller_telemetry_output_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 获取链路名称
 * @details Get link name
 */
const char *app_controller_internal_link_name(network_link_type_t link_type);

/**
 * @brief 切换屏幕使能状态
 * @details Toggle screen enabled state
 */
bool app_controller_internal_toggle_screen(bool current_enabled);

/**
 * @brief 构建应用遥测输出
 * @details Build app telemetry output
 */
void app_controller_internal_build_telemetry(
    const app_controller_telemetry_source_t *source,
    app_controller_telemetry_output_t *out);

/**
 * @brief 格式化功率限制响应
 * @details Format power limit response
 */
esp_err_t app_controller_internal_format_power_limit_response(
    char *buf, size_t buf_size, float power_limit_w, size_t *out_len);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
