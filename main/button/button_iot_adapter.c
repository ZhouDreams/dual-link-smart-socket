/**
 * @file button_iot_adapter.c
 * @brief 按键 iot_button 内部适配实现
 * @details Internal iot_button adapter implementation for button module
 * @author OpenCode
 * @date 2026-05-24
 */

/*********************
 *      INCLUDES
 *********************/

#include "button_iot_adapter.h"

#include <stdbool.h>

#include "button_gpio.h"
#include "iot_button.h"

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
 * @brief 转换适配事件为原生事件
 * @details Convert adapter event to native event
 * @param[in] event 适配事件； Adapter event
 * @return 原生事件； Native event
 */
static button_event_t button_iot_to_native_event(button_iot_event_t event);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

esp_err_t button_iot_create_gpio(gpio_num_t input_gpio, uint8_t active_level,
                                 button_iot_handle_t *out_handle)
{
    if (out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_handle = NULL;

    const button_config_t button_config = {
        .long_press_time = 0,
        .short_press_time = 0,
    };
    const button_gpio_config_t gpio_config = {
        .gpio_num = input_gpio,
        .active_level = active_level,
        .enable_power_save = false,
        .disable_pull = false,
    };
    button_handle_t native_handle = NULL;

    const esp_err_t ret = iot_button_new_gpio_device(&button_config,
                                                     &gpio_config,
                                                     &native_handle);
    if (ret == ESP_OK) {
        *out_handle = (button_iot_handle_t)native_handle;
    }

    return ret;
}

esp_err_t button_iot_register_cb(button_iot_handle_t handle,
                                 button_iot_event_t event,
                                 button_iot_event_cb_t cb,
                                 void *user_ctx)
{
    if (handle == NULL || cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const button_event_t native_event = button_iot_to_native_event(event);
    if (native_event == BUTTON_EVENT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    return iot_button_register_cb((button_handle_t)handle, native_event, NULL,
                                   cb, user_ctx);
}

esp_err_t button_iot_unregister_cb(button_iot_handle_t handle,
                                   button_iot_event_t event)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const button_event_t native_event = button_iot_to_native_event(event);
    if (native_event == BUTTON_EVENT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    return iot_button_unregister_cb((button_handle_t)handle, native_event,
                                    NULL);
}

esp_err_t button_iot_delete(button_iot_handle_t handle)
{
    if (handle == NULL) {
        return ESP_OK;
    }

    return iot_button_delete((button_handle_t)handle);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static button_event_t button_iot_to_native_event(button_iot_event_t event)
{
    switch (event) {
    case BUTTON_IOT_EVENT_SINGLE_CLICK:
        return BUTTON_SINGLE_CLICK;
    case BUTTON_IOT_EVENT_DOUBLE_CLICK:
        return BUTTON_DOUBLE_CLICK;
    case BUTTON_IOT_EVENT_LONG_PRESS_START:
        return BUTTON_LONG_PRESS_START;
    case BUTTON_IOT_EVENT_LONG_PRESS_HOLD:
        return BUTTON_LONG_PRESS_HOLD;
    default:
        return BUTTON_EVENT_MAX;
    }
}
