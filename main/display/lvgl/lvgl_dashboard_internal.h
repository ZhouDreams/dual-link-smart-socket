/**
 * @file lvgl_dashboard_internal.h
 * @brief LVGL 本地看板纯逻辑 helper 接口
 * @details LVGL local dashboard pure-logic helper interface
 * @author OpenCode
 * @date 2026-05-25
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
#include "lvgl_dashboard.h"

/*********************
 *      DEFINES
 *********************/

#define LVGL_DASHBOARD_STALE_TIMEOUT_US     (3000000ULL)
#define LVGL_DASHBOARD_DRAW_BUF_LINES       (20U)
#define LVGL_DASHBOARD_STATUS_PILL_WIDTH    (148)
#define LVGL_DASHBOARD_STATUS_PILL_HEIGHT   (30)
#define LVGL_DASHBOARD_NETWORK_BOX_Y        (8)
#define LVGL_DASHBOARD_RELAY_BOX_Y          (42)
#define LVGL_DASHBOARD_POWER_BOX_Y          (82)
#define LVGL_DASHBOARD_POWER_BOX_HEIGHT     (106)
#define LVGL_DASHBOARD_METRIC_CARD_Y        (216)
#define LVGL_DASHBOARD_SCREEN_BG_HEX        (0xFFFFFFU)
#define LVGL_DASHBOARD_SCREEN_TEXT_HEX      (0x000000U)
#define LVGL_DASHBOARD_POWER_BG_HEX         (0xFFD54FU)
#define LVGL_DASHBOARD_VOLTAGE_BG_HEX       (0x4DD0E1U)
#define LVGL_DASHBOARD_CURRENT_BG_HEX       (0x4DD0E1U)
#define LVGL_DASHBOARD_RELAY_ON_BG_HEX      (0xA5D6A7U)
#define LVGL_DASHBOARD_RELAY_OFF_BG_HEX     (0xEF5350U)
#define LVGL_DASHBOARD_NETWORK_WIFI_BG_HEX  (0xCE93D8U)
#define LVGL_DASHBOARD_NETWORK_LTE_BG_HEX   (0x00897BU)

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 判断电参量数据是否过期
 * @details Check whether metering data is stale
 * @param[in] data_valid 数据是否有效； Whether data is valid
 * @param[in] last_update_us 最近更新时间 us； Last update time in microseconds
 * @param[in] now_us 当前时间 us； Current time in microseconds
 * @return true 表示过期，false 表示未过期； true if stale, false otherwise
 */
bool lvgl_dashboard_internal_is_stale(bool data_valid,
                                      uint64_t last_update_us,
                                      uint64_t now_us);

/**
 * @brief 获取网络状态文本
 * @details Get network state text
 * @param[in] network 网络状态； Network state
 * @return 网络状态文本； Network state text
 */
const char *lvgl_dashboard_internal_network_text(dashboard_network_t network);

/**
 * @brief 获取安全状态文本
 * @details Get safety state text
 * @param[in] level 安全等级； Safety level
 * @param[in] valid 安全状态是否有效； Whether safety state is valid
 * @return 安全状态文本； Safety state text
 */
const char *lvgl_dashboard_internal_safety_text(safety_guard_level_t level,
                                                bool valid);

/**
 * @brief 获取底部状态文本
 * @details Get bottom status text
 * @param[in] data_valid 电参量数据是否有效； Whether metering data is valid
 * @param[in] data_stale 电参量数据是否过期； Whether metering data is stale
 * @return 底部状态文本； Bottom status text
 */
const char *lvgl_dashboard_internal_bottom_status_text(bool data_valid,
                                                       bool data_stale);

/**
 * @brief 判断是否需要刷新已渲染状态
 * @details Check whether rendered state should be updated
 * @param[in] has_rendered_state 是否已有渲染状态； Whether a rendered state exists
 * @param[in] rendered_state 已渲染状态； Rendered state
 * @param[in] rendered_stale 已渲染状态的过期标志； Rendered stale flag
 * @param[in] next_state 下一个状态； Next state
 * @param[in] next_stale 下一个状态的过期标志； Next stale flag
 * @return true 表示需要刷新，false 表示无需刷新； true if state should be applied
 */
bool lvgl_dashboard_internal_should_apply_state(bool has_rendered_state,
                                                const dashboard_state_t *rendered_state,
                                                bool rendered_stale,
                                                const dashboard_state_t *next_state,
                                                bool next_stale);

/**
 * @brief 格式化功率文本
 * @details Format power text
 * @param[out] out 输出缓冲区； Writable destination buffer
 * @param[in] out_len 输出缓冲区长度； Destination buffer length
 * @param[in] power_w 功率 W； Power in watts
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_SIZE: 缓冲区过小； Buffer too small
 */
esp_err_t lvgl_dashboard_internal_format_power(char *out, size_t out_len,
                                               float power_w);

/**
 * @brief 格式化电压文本
 * @details Format voltage text
 * @param[out] out 输出缓冲区； Writable destination buffer
 * @param[in] out_len 输出缓冲区长度； Destination buffer length
 * @param[in] voltage_v 电压 V； Voltage in volts
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_SIZE: 缓冲区过小； Buffer too small
 */
esp_err_t lvgl_dashboard_internal_format_voltage(char *out, size_t out_len,
                                                 float voltage_v);

/**
 * @brief 格式化电流文本
 * @details Format current text
 * @param[out] out 输出缓冲区； Writable destination buffer
 * @param[in] out_len 输出缓冲区长度； Destination buffer length
 * @param[in] current_a 电流 A； Current in amperes
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_SIZE: 缓冲区过小； Buffer too small
 */
esp_err_t lvgl_dashboard_internal_format_current(char *out, size_t out_len,
                                                 float current_a);

/**
 * @brief 格式化电能文本
 * @details Format energy text
 * @param[out] out 输出缓冲区； Writable destination buffer
 * @param[in] out_len 输出缓冲区长度； Destination buffer length
 * @param[in] energy_wh 电能 Wh； Energy in watt-hours
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_SIZE: 缓冲区过小； Buffer too small
 */
esp_err_t lvgl_dashboard_internal_format_energy(char *out, size_t out_len,
                                                float energy_wh);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
