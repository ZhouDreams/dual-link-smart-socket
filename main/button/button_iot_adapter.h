/**
 * @file button_iot_adapter.h
 * @brief 按键 iot_button 内部适配接口
 * @details Internal iot_button adapter interface for button module
 * @author OpenCode
 * @date 2026-05-24
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief iot_button 句柄
 * @details iot_button handle
 */
typedef void *button_iot_handle_t;

/**
 * @brief iot_button 事件回调
 * @details iot_button event callback
 * @param[in] button_handle 底层按键句柄； Underlying button handle
 * @param[in] user_ctx 用户上下文； User context
 */
typedef void (*button_iot_event_cb_t)(void *button_handle, void *user_ctx);

/**
 * @brief iot_button 适配事件
 * @details iot_button adapter event
 */
typedef enum {
    BUTTON_IOT_EVENT_SINGLE_CLICK = 0,  /**< 单击； Single click */
    BUTTON_IOT_EVENT_DOUBLE_CLICK,      /**< 双击； Double click */
    BUTTON_IOT_EVENT_LONG_PRESS_START,  /**< 长按开始； Long press start */
    BUTTON_IOT_EVENT_LONG_PRESS_HOLD,   /**< 长按保持； Long press hold */
    BUTTON_IOT_EVENT_MAX,               /**< 事件数量； Event count */
} button_iot_event_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 创建 GPIO iot_button 实例
 * @details Create GPIO iot_button instance
 * @param[in] input_gpio 输入 GPIO； Input GPIO
 * @param[in] active_level 有效电平； Active level
 * @param[out] out_handle 输出句柄； Output handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_NO_MEM: 内存不足； No memory
 */
esp_err_t button_iot_create_gpio(gpio_num_t input_gpio, uint8_t active_level,
                                 button_iot_handle_t *out_handle);

/**
 * @brief 注册 iot_button 事件回调
 * @details Register iot_button event callback
 * @param[in] handle iot_button 句柄； iot_button handle
 * @param[in] event 事件； Event
 * @param[in] cb 回调； Callback
 * @param[in] user_ctx 用户上下文； User context
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 */
esp_err_t button_iot_register_cb(button_iot_handle_t handle,
                                 button_iot_event_t event,
                                 button_iot_event_cb_t cb,
                                 void *user_ctx);

/**
 * @brief 注销 iot_button 事件回调
 * @details Unregister iot_button event callbacks
 * @param[in] handle iot_button 句柄； iot_button handle
 * @param[in] event 事件； Event
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 该事件未注册回调； No callback registered for event
 */
esp_err_t button_iot_unregister_cb(button_iot_handle_t handle,
                                   button_iot_event_t event);

/**
 * @brief 删除 iot_button 实例
 * @details Delete iot_button instance
 * @param[in] handle iot_button 句柄； iot_button handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_FAIL: 删除失败； Delete failed
 */
esp_err_t button_iot_delete(button_iot_handle_t handle);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
