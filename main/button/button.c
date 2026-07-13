/**
 * @file button.c
 * @brief 本地按键实现
 * @details Local button implementation
 * @author OpenCode
 * @date 2026-05-24
 */

/*********************
 *      INCLUDES
 *********************/

#include "button.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "button_iot_adapter.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "button"
#define BUTTON_CALLBACK_DRAIN_POLL_MS (10U)
#define BUTTON_CALLBACK_DRAIN_TIMEOUT_MS (5000U)

/**********************
 *      TYPEDEFS
 **********************/

struct button {
    gpio_num_t input_gpio;
    button_active_level_t active_level;
    button_iot_handle_t iot_button_handle;
    button_event_cb_t callbacks[BUTTON_EVENT_MAX];
    void *user_ctxs[BUTTON_EVENT_MAX];
    uint32_t active_callbacks[BUTTON_EVENT_MAX];
    SemaphoreHandle_t mutex;
    bool initialized;
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**
 * @brief 校验按键配置
 * @details Validate button configuration
 * @param[in] config 按键配置； Button configuration
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 */
static esp_err_t button_validate_config(const button_config_t *config);

/**
 * @brief 分发按键事件
 * @details Dispatch button event
 * @param[in] me 按键句柄； Button handle
 * @param[in] event 按键事件； Button event
 */
static void button_dispatch(button_t *me, button_event_t event);

/**
 * @brief 注销底层按键回调
 * @details Unregister underlying button callbacks
 * @param[in] handle 底层按键句柄； Underlying button handle
 */
static void button_unregister_iot_callbacks(button_iot_handle_t handle);

/**
 * @brief 等待指定事件回调退出
 * @details Wait for one event callback to drain
 * @param[in] me 按键句柄； Button handle
 * @param[in] event 按键事件； Button event
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_TIMEOUT: 获取互斥锁超时； Mutex timeout
 */
static esp_err_t button_wait_event_callbacks_drained(button_t *me,
                                                     button_event_t event);

/**
 * @brief 等待所有事件回调退出
 * @details Wait for all event callbacks to drain
 * @param[in] me 按键句柄； Button handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_TIMEOUT: 获取互斥锁超时； Mutex timeout
 */
static esp_err_t button_wait_all_callbacks_drained(button_t *me);

/**
 * @brief 处理单击事件
 * @details Handle single click event
 * @param[in] button_handle 底层按键句柄； Underlying button handle
 * @param[in] usr_data 用户数据； User data
 */
static void button_on_single_click(void *button_handle, void *usr_data);

/**
 * @brief 处理双击事件
 * @details Handle double click event
 * @param[in] button_handle 底层按键句柄； Underlying button handle
 * @param[in] usr_data 用户数据； User data
 */
static void button_on_double_click(void *button_handle, void *usr_data);

/**
 * @brief 处理长按开始事件
 * @details Handle long press start event
 * @param[in] button_handle 底层按键句柄； Underlying button handle
 * @param[in] usr_data 用户数据； User data
 */
static void button_on_long_press_start(void *button_handle, void *usr_data);

/**
 * @brief 处理长按保持事件
 * @details Handle long press hold event
 * @param[in] button_handle 底层按键句柄； Underlying button handle
 * @param[in] usr_data 用户数据； User data
 */
static void button_on_long_press_hold(void *button_handle, void *usr_data);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

button_t *button_create(const button_config_t *config)
{
    if (button_validate_config(config) != ESP_OK) {
        return NULL;
    }

    button_t *me = calloc(1, sizeof(*me));
    if (me == NULL) {
        ESP_LOGE(TAG, "calloc button failed");
        return NULL;
    }

    me->input_gpio = config->input_gpio;
    me->active_level = config->active_level;

    me->mutex = xSemaphoreCreateMutex();
    if (me->mutex == NULL) {
        ESP_LOGE(TAG, "create mutex failed");
        free(me);
        return NULL;
    }

    esp_err_t ret = button_iot_create_gpio(me->input_gpio,
                                           (uint8_t)me->active_level,
                                           &me->iot_button_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "create iot button failed: %s", esp_err_to_name(ret));
        goto err;
    }

    ret = button_iot_register_cb(me->iot_button_handle,
                                 BUTTON_IOT_EVENT_SINGLE_CLICK,
                                 button_on_single_click, me);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register single click callback failed: %s",
                 esp_err_to_name(ret));
        goto err;
    }

    ret = button_iot_register_cb(me->iot_button_handle,
                                 BUTTON_IOT_EVENT_DOUBLE_CLICK,
                                 button_on_double_click, me);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register double click callback failed: %s",
                 esp_err_to_name(ret));
        goto err;
    }

    ret = button_iot_register_cb(me->iot_button_handle,
                                 BUTTON_IOT_EVENT_LONG_PRESS_START,
                                 button_on_long_press_start, me);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register long press start callback failed: %s",
                 esp_err_to_name(ret));
        goto err;
    }

    ret = button_iot_register_cb(me->iot_button_handle,
                                 BUTTON_IOT_EVENT_LONG_PRESS_HOLD,
                                 button_on_long_press_hold, me);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register long press hold callback failed: %s",
                 esp_err_to_name(ret));
        goto err;
    }

    me->initialized = true;
    return me;

err:
    if (me->iot_button_handle != NULL) {
        button_unregister_iot_callbacks(me->iot_button_handle);
        const esp_err_t delete_ret = button_iot_delete(me->iot_button_handle);
        if (delete_ret != ESP_OK) {
            ESP_LOGW(TAG, "delete iot button after create failure failed: %s",
                     esp_err_to_name(delete_ret));
        }
        me->iot_button_handle = NULL;
    }
    if (me->mutex != NULL) {
        vSemaphoreDelete(me->mutex);
    }
    free(me);
    return NULL;
}

esp_err_t button_destroy(button_t *me)
{
    esp_err_t ret = ESP_OK;

    if (me == NULL) {
        return ESP_OK;
    }

    if (me->mutex != NULL && xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    me->initialized = false;
    for (button_event_t event = BUTTON_EVENT_SINGLE_CLICK;
         event < BUTTON_EVENT_MAX; ++event) {
        me->callbacks[event] = NULL;
        me->user_ctxs[event] = NULL;
    }

    if (me->mutex != NULL) {
        (void)xSemaphoreGive(me->mutex);
    }

    button_unregister_iot_callbacks(me->iot_button_handle);
    ret = button_wait_all_callbacks_drained(me);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = button_iot_delete(me->iot_button_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "delete iot button failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (me->mutex != NULL) {
        vSemaphoreDelete(me->mutex);
    }

    free(me);
    return ESP_OK;
}

esp_err_t button_register_cb(button_t *me, button_event_t event,
                             button_event_cb_t cb, void *user_ctx)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "button is null");
    ESP_RETURN_ON_FALSE(event >= BUTTON_EVENT_SINGLE_CLICK &&
                            event < BUTTON_EVENT_MAX,
                        ESP_ERR_INVALID_ARG, TAG, "invalid event");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "button is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    me->callbacks[event] = cb;
    me->user_ctxs[event] = (cb != NULL) ? user_ctx : NULL;

    (void)xSemaphoreGive(me->mutex);

    if (cb == NULL) {
        return button_wait_event_callbacks_drained(me, event);
    }

    return ESP_OK;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static esp_err_t button_validate_config(const button_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "config is null");
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_GPIO(config->input_gpio),
                        ESP_ERR_INVALID_ARG, TAG, "invalid input GPIO");
    ESP_RETURN_ON_FALSE(config->active_level == BUTTON_ACTIVE_LOW ||
                            config->active_level == BUTTON_ACTIVE_HIGH,
                        ESP_ERR_INVALID_ARG, TAG, "invalid active level");

    return ESP_OK;
}

static void button_dispatch(button_t *me, button_event_t event)
{
    if (me == NULL || event < BUTTON_EVENT_SINGLE_CLICK ||
        event >= BUTTON_EVENT_MAX || me->mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    button_event_cb_t cb = NULL;
    void *user_ctx = NULL;
    if (me->initialized) {
        cb = me->callbacks[event];
        user_ctx = me->user_ctxs[event];
        if (cb != NULL) {
            me->active_callbacks[event]++;
        }
    }

    (void)xSemaphoreGive(me->mutex);

    if (cb != NULL) {
        cb(event, user_ctx);

        if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
            if (me->active_callbacks[event] > 0U) {
                me->active_callbacks[event]--;
            }
            (void)xSemaphoreGive(me->mutex);
        }
    }
}

static esp_err_t button_wait_event_callbacks_drained(button_t *me,
                                                     button_event_t event)
{
    bool drained = false;
    uint32_t waited_ms = 0U;

    while (!drained) {
        if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
        drained = me->active_callbacks[event] == 0U;
        (void)xSemaphoreGive(me->mutex);

        if (!drained) {
            if (waited_ms >= BUTTON_CALLBACK_DRAIN_TIMEOUT_MS) {
                ESP_LOGE(TAG, "wait for event callback %d timed out",
                         (int)event);
                return ESP_ERR_TIMEOUT;
            }
            vTaskDelay(pdMS_TO_TICKS(BUTTON_CALLBACK_DRAIN_POLL_MS));
            waited_ms += BUTTON_CALLBACK_DRAIN_POLL_MS;
        }
    }

    return ESP_OK;
}

static esp_err_t button_wait_all_callbacks_drained(button_t *me)
{
    bool drained = false;
    uint32_t waited_ms = 0U;

    while (!drained) {
        drained = true;
        if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
        for (button_event_t event = BUTTON_EVENT_SINGLE_CLICK;
             event < BUTTON_EVENT_MAX; ++event) {
            if (me->active_callbacks[event] != 0U) {
                drained = false;
                break;
            }
        }
        (void)xSemaphoreGive(me->mutex);

        if (!drained) {
            if (waited_ms >= BUTTON_CALLBACK_DRAIN_TIMEOUT_MS) {
                ESP_LOGE(TAG, "wait for callbacks timed out");
                return ESP_ERR_TIMEOUT;
            }
            vTaskDelay(pdMS_TO_TICKS(BUTTON_CALLBACK_DRAIN_POLL_MS));
            waited_ms += BUTTON_CALLBACK_DRAIN_POLL_MS;
        }
    }

    return ESP_OK;
}

static void button_unregister_iot_callbacks(button_iot_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

    for (button_iot_event_t event = BUTTON_IOT_EVENT_SINGLE_CLICK;
         event < BUTTON_IOT_EVENT_MAX; ++event) {
        const esp_err_t ret = button_iot_unregister_cb(handle, event);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "unregister iot button callback failed: %s",
                     esp_err_to_name(ret));
        }
    }
}

static void button_on_single_click(void *button_handle, void *usr_data)
{
    (void)button_handle;
    button_dispatch((button_t *)usr_data, BUTTON_EVENT_SINGLE_CLICK);
}

static void button_on_double_click(void *button_handle, void *usr_data)
{
    (void)button_handle;
    button_dispatch((button_t *)usr_data, BUTTON_EVENT_DOUBLE_CLICK);
}

static void button_on_long_press_start(void *button_handle, void *usr_data)
{
    (void)button_handle;
    button_dispatch((button_t *)usr_data, BUTTON_EVENT_LONG_PRESS_START);
}

static void button_on_long_press_hold(void *button_handle, void *usr_data)
{
    (void)button_handle;
    button_dispatch((button_t *)usr_data, BUTTON_EVENT_LONG_PRESS_HOLD);
}
