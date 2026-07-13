/**
 * @file relay.c
 * @brief 继电器控制实现
 * @details Relay control implementation
 * @author OpenCode
 * @date 2026-05-24
 */

/*********************
 *      INCLUDES
 *********************/

#include "relay.h"

#include <stdint.h>
#include <stdlib.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "relay"

/**********************
 *      TYPEDEFS
 **********************/

struct relay {
    gpio_num_t ctrl_gpio;
    relay_active_level_t active_level;
    bool on;
    SemaphoreHandle_t mutex;
    relay_source_t pending_source;
    bool event_pending;
    bool initialized;
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**
 * @brief 校验继电器配置
 * @details Validate relay configuration
 * @param[in] config 继电器配置； Relay configuration
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 */
static esp_err_t relay_validate_config(const relay_config_t *config);

/**
 * @brief 转换逻辑状态为 GPIO 电平
 * @details Convert logical state to GPIO level
 * @param[in] me 继电器句柄； Relay handle
 * @param[in] on 逻辑状态； Logical state
 * @return GPIO 电平； GPIO level
 */
static uint32_t relay_logical_to_level(const relay_t *me, bool on);

/**
 * @brief 发布状态变化事件
 * @details Post state changed event
 * @param[in] on 继电器状态； Relay state
 * @param[in] source 操作来源； Operation source
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t relay_post_state_changed(bool on, relay_source_t source);

/**********************
 *  STATIC VARIABLES
 **********************/

ESP_EVENT_DEFINE_BASE(RELAY_EVENT_BASE);

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

relay_t *relay_create(const relay_config_t *config)
{
    if (relay_validate_config(config) != ESP_OK) {
        return NULL;
    }

    relay_t *me = calloc(1, sizeof(*me));
    if (me == NULL) {
        ESP_LOGE(TAG, "calloc relay failed");
        return NULL;
    }

    me->ctrl_gpio = config->ctrl_gpio;
    me->active_level = config->active_level;

    me->mutex = xSemaphoreCreateMutex();
    if (me->mutex == NULL) {
        ESP_LOGE(TAG, "create mutex failed");
        free(me);
        return NULL;
    }

    const gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << (uint32_t)me->ctrl_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "configure GPIO failed: %s", esp_err_to_name(ret));
        vSemaphoreDelete(me->mutex);
        free(me);
        return NULL;
    }

    ret = gpio_set_level(me->ctrl_gpio, relay_logical_to_level(me, false));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "drive relay off failed: %s", esp_err_to_name(ret));
        vSemaphoreDelete(me->mutex);
        free(me);
        return NULL;
    }

    me->on = false;
    me->initialized = true;

    return me;
}

esp_err_t relay_destroy(relay_t *me)
{
    esp_err_t ret = ESP_OK;

    if (me == NULL) {
        return ESP_OK;
    }

    if (me->initialized) {
        ret = gpio_set_level(me->ctrl_gpio, relay_logical_to_level(me, false));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "drive relay off during destroy failed: %s",
                     esp_err_to_name(ret));
        }
        me->initialized = false;
        me->on = false;
    }

    if (me->mutex != NULL) {
        vSemaphoreDelete(me->mutex);
    }

    free(me);
    return ret;
}

esp_err_t relay_set(relay_t *me, relay_source_t source, bool on)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "relay is null");
    ESP_RETURN_ON_FALSE(source >= RELAY_SOURCE_INTERNAL &&
                            source < RELAY_SOURCE_MAX,
                        ESP_ERR_INVALID_ARG, TAG, "invalid source");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "relay is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    const bool previous_on = me->on;
    esp_err_t ret = gpio_set_level(me->ctrl_gpio,
                                   relay_logical_to_level(me, on));
    if (ret == ESP_OK) {
        me->on = on;
        if (previous_on != on) {
            me->pending_source = source;
            me->event_pending = true;
        }
        if (me->event_pending &&
            relay_post_state_changed(me->on, me->pending_source) == ESP_OK) {
            me->event_pending = false;
        }
    }

    (void)xSemaphoreGive(me->mutex);

    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t relay_toggle(relay_t *me, relay_source_t source)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "relay is null");
    ESP_RETURN_ON_FALSE(source >= RELAY_SOURCE_INTERNAL &&
                            source < RELAY_SOURCE_MAX,
                        ESP_ERR_INVALID_ARG, TAG, "invalid source");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "relay is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    const bool new_on = !me->on;
    esp_err_t ret = gpio_set_level(me->ctrl_gpio,
                                   relay_logical_to_level(me, new_on));
    if (ret == ESP_OK) {
        me->on = new_on;
        me->pending_source = source;
        me->event_pending = true;
        if (relay_post_state_changed(new_on, source) == ESP_OK) {
            me->event_pending = false;
        }
    }

    (void)xSemaphoreGive(me->mutex);

    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t relay_get(const relay_t *me, bool *out_on)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "relay is null");
    ESP_RETURN_ON_FALSE(out_on != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "state output is null");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "relay is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    *out_on = me->on;

    (void)xSemaphoreGive(me->mutex);
    return ESP_OK;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static esp_err_t relay_validate_config(const relay_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "config is null");
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(config->ctrl_gpio),
                        ESP_ERR_INVALID_ARG, TAG, "invalid control GPIO");
    ESP_RETURN_ON_FALSE(config->active_level == RELAY_ACTIVE_LOW ||
                            config->active_level == RELAY_ACTIVE_HIGH,
                        ESP_ERR_INVALID_ARG, TAG, "invalid active level");

    return ESP_OK;
}

static uint32_t relay_logical_to_level(const relay_t *me, bool on)
{
    const bool active_high = (me->active_level == RELAY_ACTIVE_HIGH);

    return (on == active_high) ? 1U : 0U;
}

static esp_err_t relay_post_state_changed(bool on, relay_source_t source)
{
    const relay_state_changed_event_t event = {
        .on = on,
        .source = source,
    };

    const esp_err_t ret = esp_event_post(RELAY_EVENT_BASE,
                                         RELAY_EVENT_STATE_CHANGED, &event,
                                         sizeof(event), 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "post state changed event failed: %s",
                 esp_err_to_name(ret));
    }
    return ret;
}
