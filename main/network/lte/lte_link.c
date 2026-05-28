/**
 * @file lte_link.c
 * @brief LTE 链路子类实现
 * @details LTE link subclass implementation
 * @author OpenCode
 * @date 2026-05-28
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
#include "freertos/task.h"
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

/**
 * @brief LTE 链路对象
 * @details LTE link object
 */
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
    int active_rx_callbacks;
    network_link_status_t cached_status;
    bool started;
    bool destroying;
    bool mqtt_started;
} lte_link_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**
 * @brief 销毁 LTE 链路实现
 * @details Destroy LTE link implementation
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lte_link_destroy_impl(network_link_t *base);

/**
 * @brief 启动 LTE 链路实现
 * @details Start LTE link implementation
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lte_link_start_impl(network_link_t *base);

/**
 * @brief 停止 LTE 链路实现
 * @details Stop LTE link implementation
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lte_link_stop_impl(network_link_t *base);

/**
 * @brief 获取 LTE 链路状态实现
 * @details Get LTE link status implementation
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @param[out] out 状态输出； Status output
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lte_link_get_status_impl(network_link_t *base,
                                          network_link_status_t *out);

/**
 * @brief 发布 LTE 链路消息实现
 * @details Publish LTE link message implementation
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @param[in] req 发布请求； Publish request
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lte_link_publish_impl(network_link_t *base,
                                       const network_publish_request_t *req);

/**
 * @brief 订阅 LTE 链路主题实现
 * @details Subscribe LTE link topic implementation
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @param[in] topic MQTT 主题； MQTT topic
 * @param[in] qos MQTT 服务质量等级； MQTT QoS level
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lte_link_subscribe_impl(network_link_t *base,
                                         const char *topic,
                                         network_mqtt_qos_t qos);

/**
 * @brief 取消订阅 LTE 链路主题实现
 * @details Unsubscribe LTE link topic implementation
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @param[in] topic MQTT 主题； MQTT topic
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lte_link_unsubscribe_impl(network_link_t *base,
                                           const char *topic);

/**
 * @brief 注册 LTE 链路接收回调实现
 * @details Register LTE link RX callback implementation
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @param[in] cb 接收回调； RX callback
 * @param[in] ctx 用户上下文； User context
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lte_link_register_rx_cb_impl(network_link_t *base,
                                              network_rx_cb_t cb, void *ctx);

/**
 * @brief 等待接收回调退出
 * @details Wait until active RX callbacks drain
 * @param[in,out] me LTE 链路对象； LTE link object
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lte_link_wait_rx_callbacks_drained(lte_link_t *me);

/**
 * @brief 校验 LTE 链路配置
 * @details Validate LTE link configuration
 * @param[in] config LTE 链路配置； LTE link configuration
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lte_link_validate_config(const lte_link_config_t *config);

/**
 * @brief 复制 LTE 链路配置
 * @details Copy LTE link configuration
 * @param[in,out] me LTE 链路对象； LTE link object
 * @param[in] config LTE 链路配置； LTE link configuration
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lte_link_copy_config(lte_link_t *me,
                                      const lte_link_config_t *config);

/**
 * @brief 释放 LTE 链路配置
 * @details Free LTE link configuration
 * @param[in,out] me LTE 链路对象； LTE link object
 */
static void lte_link_free_config(lte_link_t *me);

/**
 * @brief 从基类句柄获取 LTE 链路对象
 * @details Get LTE link object from base handle
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @return LTE 链路对象； LTE link object
 */
static lte_link_t *lte_link_from_base(network_link_t *base);

/**
 * @brief 复制字符串或返回 NULL
 * @details Duplicate string or return NULL
 * @param[in] value 字符串； String value
 * @return 已复制字符串，失败或输入为空返回 NULL； Duplicated string, or NULL
 */
static char *lte_link_strdup_or_null(const char *value);

/**
 * @brief 初始化 lwlte 门面
 * @details Initialize lwlte facade
 * @param[in,out] me LTE 链路对象； LTE link object
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lte_link_init_lwlte(lte_link_t *me);

/**
 * @brief 获取并缓存当前状态
 * @details Get and cache current status
 * @param[in,out] me LTE 链路对象； LTE link object
 * @param[out] out 状态输出； Status output
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lte_link_query_status(lte_link_t *me,
                                       network_link_status_t *out);

/**
 * @brief 当前 MQTT 是否已连接
 * @details Check whether MQTT is currently connected
 * @param[in] me LTE 链路对象； LTE link object
 * @return true 已连接，false 未连接； true if connected
 */
static bool lte_link_is_mqtt_connected(lte_link_t *me);

/**
 * @brief 重放 MQTT 订阅
 * @details Replay MQTT subscriptions
 * @param[in,out] me LTE 链路对象； LTE link object
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lte_link_replay_subscriptions(lte_link_t *me);

/**
 * @brief 处理 lwlte 事件
 * @details Handle lwlte event
 * @param[in] lte LTE 用户门面句柄； LTE user facade handle
 * @param[in] event_id 事件 ID； Event ID
 * @param[in] data 事件数据； Event data
 * @param[in] user_ctx 用户上下文； User context
 */
static void lte_link_on_lwlte_event(lwlte_t *lte, lwlte_event_id_t event_id,
                                    const lwlte_event_data_t *data,
                                    void *user_ctx);

/**
 * @brief 处理 MQTT 数据事件
 * @details Handle MQTT data event
 * @param[in,out] me LTE 链路对象； LTE link object
 * @param[in] data 事件数据； Event data
 */
static void lte_link_handle_mqtt_data(lte_link_t *me,
                                      const lwlte_event_data_t *data);

/**********************
 *  STATIC VARIABLES
 **********************/

static const network_link_ops_t lte_link_ops = {
    .destroy = lte_link_destroy_impl,
    .start = lte_link_start_impl,
    .stop = lte_link_stop_impl,
    .get_status = lte_link_get_status_impl,
    .publish = lte_link_publish_impl,
    .subscribe = lte_link_subscribe_impl,
    .unsubscribe = lte_link_unsubscribe_impl,
    .register_rx_cb = lte_link_register_rx_cb_impl,
};

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

network_link_t *lte_link_create(const lte_link_config_t *config)
{
    if (lte_link_validate_config(config) != ESP_OK) {
        return NULL;
    }

    lte_link_t *me = calloc(1, sizeof(*me));
    if (me == NULL) {
        ESP_LOGE(TAG, "calloc lte link failed");
        return NULL;
    }

    me->base.ops = &lte_link_ops;
    me->base.type = NETWORK_LINK_TYPE_LTE;
    me->cached_status = NETWORK_LINK_STATUS_IDLE;
    me->mutex = xSemaphoreCreateMutex();
    if (me->mutex == NULL) {
        ESP_LOGE(TAG, "create mutex failed");
        free(me);
        return NULL;
    }

    if (lte_link_copy_config(me, config) != ESP_OK) {
        vSemaphoreDelete(me->mutex);
        lte_link_free_config(me);
        free(me);
        return NULL;
    }

    me->sub_table = calloc((size_t)me->sub_table_size, sizeof(me->sub_table[0]));
    if (me->sub_table == NULL) {
        ESP_LOGE(TAG, "calloc subscription table failed");
        vSemaphoreDelete(me->mutex);
        lte_link_free_config(me);
        free(me);
        return NULL;
    }

    if (lte_link_init_lwlte(me) != ESP_OK) {
        lte_link_internal_clear_subscriptions(me->sub_table, me->sub_table_size);
        free(me->sub_table);
        vSemaphoreDelete(me->mutex);
        lte_link_free_config(me);
        free(me);
        return NULL;
    }

    return &me->base;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lte_link_t *lte_link_from_base(network_link_t *base)
{
    return (lte_link_t *)base;
}

static esp_err_t lte_link_destroy_impl(network_link_t *base)
{
    esp_err_t ret = ESP_OK;
    lwlte_t *lwlte = NULL;

    if (base == NULL) {
        return ESP_OK;
    }

    lte_link_t *me = lte_link_from_base(base);
    if (me->mutex != NULL && xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
        me->destroying = true;
        me->rx_cb = NULL;
        me->rx_ctx = NULL;
        lwlte = me->lwlte;
        (void)xSemaphoreGive(me->mutex);
    }

    ret = lte_link_wait_rx_callbacks_drained(me);
    if (ret != ESP_OK) {
        if (me->mutex != NULL && xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
            me->destroying = false;
            (void)xSemaphoreGive(me->mutex);
        }
        return ret;
    }

    ret = lte_link_stop_impl(base);
    if (ret != ESP_OK) {
        if (me->mutex != NULL && xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
            me->destroying = false;
            (void)xSemaphoreGive(me->mutex);
        }
        return ret;
    }

    if (lwlte != NULL) {
        ret = lwlte_register_event_callback(lwlte, NULL, NULL);
        if (ret != ESP_OK) {
            if (me->mutex != NULL && xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
                me->destroying = false;
                (void)xSemaphoreGive(me->mutex);
            }
            return ret;
        }
        ret = lwlte_destroy(lwlte);
        if (ret != ESP_OK) {
            if (me->mutex != NULL && xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
                me->destroying = false;
                (void)xSemaphoreGive(me->mutex);
            }
            return ret;
        }
        me->lwlte = NULL;
    }

    if (me->sub_table != NULL) {
        lte_link_internal_clear_subscriptions(me->sub_table, me->sub_table_size);
        free(me->sub_table);
        me->sub_table = NULL;
    }
    if (me->mutex != NULL) {
        vSemaphoreDelete(me->mutex);
        me->mutex = NULL;
    }
    lte_link_free_config(me);
    free(me);

    return ESP_OK;
}

static esp_err_t lte_link_start_impl(network_link_t *base)
{
    bool connect_needed = true;

    ESP_RETURN_ON_FALSE(base != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");

    lte_link_t *me = lte_link_from_base(base);
    ESP_RETURN_ON_FALSE(me->mutex != NULL && me->lwlte != NULL,
                        ESP_ERR_INVALID_STATE, TAG, "link is not initialized");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (me->destroying) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (me->started) {
        connect_needed = false;
        if (!me->config.mqtt_enabled || me->mqtt_started) {
            (void)xSemaphoreGive(me->mutex);
            return ESP_OK;
        }
    }
    (void)xSemaphoreGive(me->mutex);

    if (connect_needed) {
        esp_err_t ret = lwlte_connect(me->lwlte);
        if (ret != ESP_OK) {
            return ret;
        }

        ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                            ESP_ERR_TIMEOUT, TAG, "take mutex failed");
        if (me->destroying) {
            (void)xSemaphoreGive(me->mutex);
            return ESP_ERR_INVALID_STATE;
        }
        me->started = true;
        (void)xSemaphoreGive(me->mutex);
    }

    if (me->config.mqtt_enabled) {
        esp_err_t ret = lwlte_mqtt_start(me->lwlte);
        if (ret == ESP_ERR_INVALID_STATE) {
            lwlte_mqtt_state_t mqtt_state = LWLTE_MQTT_STATE_STOPPED;
            if (lwlte_mqtt_get_state(me->lwlte, &mqtt_state) == ESP_OK &&
                (mqtt_state == LWLTE_MQTT_STATE_WAITING_NET ||
                 mqtt_state == LWLTE_MQTT_STATE_CONNECTING ||
                 mqtt_state == LWLTE_MQTT_STATE_CONNECTED)) {
                ret = ESP_OK;
            }
        }
        if (ret != ESP_OK) {
            return ret;
        }
    }

    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    esp_err_t ret = ESP_OK;
    if (me->destroying) {
        ret = ESP_ERR_INVALID_STATE;
    } else {
        me->mqtt_started = me->config.mqtt_enabled;
    }
    (void)xSemaphoreGive(me->mutex);
    return ret;
}

static esp_err_t lte_link_stop_impl(network_link_t *base)
{
    esp_err_t first_error = ESP_OK;
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(base != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");

    lte_link_t *me = lte_link_from_base(base);
    ESP_RETURN_ON_FALSE(me->mutex != NULL && me->lwlte != NULL,
                        ESP_ERR_INVALID_STATE, TAG, "link is not initialized");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (!me->started) {
        me->mqtt_started = false;
        me->cached_status = NETWORK_LINK_STATUS_IDLE;
        (void)xSemaphoreGive(me->mutex);
        return ESP_OK;
    }
    (void)xSemaphoreGive(me->mutex);

    if (me->config.mqtt_enabled) {
        ret = lwlte_mqtt_stop(me->lwlte);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE &&
            first_error == ESP_OK) {
            first_error = ret;
        }
    }

    ret = lwlte_disconnect(me->lwlte);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE && first_error == ESP_OK) {
        first_error = ret;
    }

    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    me->started = false;
    me->mqtt_started = false;
    me->cached_status = NETWORK_LINK_STATUS_IDLE;
    (void)xSemaphoreGive(me->mutex);

    return first_error;
}

static esp_err_t lte_link_get_status_impl(network_link_t *base,
                                          network_link_status_t *out)
{
    ESP_RETURN_ON_FALSE(base != NULL && out != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "invalid argument");

    lte_link_t *me = lte_link_from_base(base);
    return lte_link_query_status(me, out);
}

static esp_err_t lte_link_publish_impl(network_link_t *base,
                                       const network_publish_request_t *req)
{
    network_link_status_t status = NETWORK_LINK_STATUS_IDLE;

    ESP_RETURN_ON_FALSE(base != NULL && req != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "invalid argument");
    ESP_RETURN_ON_FALSE(req->topic != NULL && req->topic[0] != '\0',
                        ESP_ERR_INVALID_ARG, TAG, "topic is empty");
    ESP_RETURN_ON_FALSE(req->payload != NULL && req->payload_len > 0U,
                        ESP_ERR_INVALID_ARG, TAG, "payload is empty");
    ESP_RETURN_ON_FALSE(lte_link_internal_is_valid_qos(req->qos),
                        ESP_ERR_INVALID_ARG, TAG, "invalid qos");

    lte_link_t *me = lte_link_from_base(base);
    ESP_RETURN_ON_FALSE(me->config.mqtt_enabled, ESP_ERR_NOT_SUPPORTED, TAG,
                        "mqtt is disabled");
    ESP_RETURN_ON_ERROR(lte_link_query_status(me, &status), TAG,
                        "query status failed");
    ESP_RETURN_ON_FALSE(status == NETWORK_LINK_STATUS_READY,
                        ESP_ERR_INVALID_STATE, TAG, "mqtt is not ready");

    return lwlte_mqtt_publish(me->lwlte, req->topic,
                              (const uint8_t *)req->payload, req->payload_len,
                              (uint8_t)req->qos, req->retain);
}

static esp_err_t lte_link_subscribe_impl(network_link_t *base,
                                         const char *topic,
                                         network_mqtt_qos_t qos)
{
    esp_err_t ret = ESP_OK;
    bool broker_io = false;

    ESP_RETURN_ON_FALSE(base != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "link is null");
    ESP_RETURN_ON_FALSE(topic != NULL && topic[0] != '\0', ESP_ERR_INVALID_ARG,
                        TAG, "topic is empty");
    ESP_RETURN_ON_FALSE(lte_link_internal_is_valid_qos(qos), ESP_ERR_INVALID_ARG,
                        TAG, "invalid qos");

    lte_link_t *me = lte_link_from_base(base);
    ESP_RETURN_ON_FALSE(me->config.mqtt_enabled, ESP_ERR_NOT_SUPPORTED, TAG,
                        "mqtt is disabled");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    ret = lte_link_internal_store_subscription(me->sub_table, me->sub_table_size,
                                               topic, qos, me->max_topic_len);
    broker_io = ret == ESP_OK && !me->destroying;
    (void)xSemaphoreGive(me->mutex);
    if (ret != ESP_OK) {
        return ret;
    }

    if (broker_io && lte_link_is_mqtt_connected(me)) {
        ret = lwlte_mqtt_subscribe(me->lwlte, topic, (uint8_t)qos);
    }
    return ret;
}

static esp_err_t lte_link_unsubscribe_impl(network_link_t *base,
                                           const char *topic)
{
    esp_err_t ret = ESP_OK;
    bool broker_io = false;

    ESP_RETURN_ON_FALSE(base != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "link is null");
    ESP_RETURN_ON_FALSE(topic != NULL && topic[0] != '\0', ESP_ERR_INVALID_ARG,
                        TAG, "topic is empty");

    lte_link_t *me = lte_link_from_base(base);
    ESP_RETURN_ON_FALSE(me->config.mqtt_enabled, ESP_ERR_NOT_SUPPORTED, TAG,
                        "mqtt is disabled");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    ret = lte_link_internal_remove_subscription(me->sub_table, me->sub_table_size,
                                                topic);
    broker_io = ret == ESP_OK && !me->destroying;
    (void)xSemaphoreGive(me->mutex);
    if (ret != ESP_OK) {
        return ret;
    }

    if (broker_io && lte_link_is_mqtt_connected(me)) {
        ret = lwlte_mqtt_unsubscribe(me->lwlte, topic);
    }
    return ret;
}

static esp_err_t lte_link_register_rx_cb_impl(network_link_t *base,
                                              network_rx_cb_t cb, void *ctx)
{
    ESP_RETURN_ON_FALSE(base != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "link is null");

    lte_link_t *me = lte_link_from_base(base);
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    me->rx_cb = cb;
    me->rx_ctx = cb ? ctx : NULL;
    (void)xSemaphoreGive(me->mutex);

    if (cb == NULL) {
        return lte_link_wait_rx_callbacks_drained(me);
    }
    return ESP_OK;
}

static esp_err_t lte_link_wait_rx_callbacks_drained(lte_link_t *me)
{
    ESP_RETURN_ON_FALSE(me != NULL && me->mutex != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "invalid argument");

    while (true) {
        ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                            ESP_ERR_TIMEOUT, TAG, "take mutex failed");
        const int active_rx_callbacks = me->active_rx_callbacks;
        (void)xSemaphoreGive(me->mutex);
        if (active_rx_callbacks == 0) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static esp_err_t lte_link_validate_config(const lte_link_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "config is null");
    ESP_RETURN_ON_FALSE(config->uart_num >= UART_NUM_0 &&
                            config->uart_num < UART_NUM_MAX,
                        ESP_ERR_INVALID_ARG, TAG, "invalid uart number");
    ESP_RETURN_ON_FALSE(config->tx_gpio != GPIO_NUM_NC &&
                            config->rx_gpio != GPIO_NUM_NC,
                        ESP_ERR_INVALID_ARG, TAG, "invalid uart gpio");
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(config->tx_gpio),
                        ESP_ERR_INVALID_ARG, TAG, "invalid uart tx gpio");
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_GPIO(config->rx_gpio),
                        ESP_ERR_INVALID_ARG, TAG, "invalid uart rx gpio");
    if (config->mqtt_enabled) {
        ESP_RETURN_ON_FALSE(config->mqtt_broker_host != NULL &&
                                config->mqtt_broker_host[0] != '\0',
                            ESP_ERR_INVALID_ARG, TAG, "broker host is empty");
        ESP_RETURN_ON_FALSE(config->mqtt_broker_port != 0U,
                            ESP_ERR_INVALID_ARG, TAG, "broker port is zero");
        ESP_RETURN_ON_FALSE(config->mqtt_client_id != NULL &&
                                config->mqtt_client_id[0] != '\0',
                            ESP_ERR_INVALID_ARG, TAG, "client id is empty");
    }
    return ESP_OK;
}

static esp_err_t lte_link_copy_config(lte_link_t *me,
                                      const lte_link_config_t *config)
{
    ESP_RETURN_ON_FALSE(me != NULL && config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "invalid argument");

    me->config = *config;
    me->config.baud_rate = (config->baud_rate > 0) ?
                           config->baud_rate : LTE_LINK_DEFAULT_BAUD_RATE;
    me->sub_table_size = (config->max_subscriptions > 0) ?
                         config->max_subscriptions :
                         LTE_LINK_DEFAULT_MAX_SUBSCRIPTIONS;
    me->max_topic_len = (config->max_topic_len > 0) ?
                        config->max_topic_len : LTE_LINK_DEFAULT_MAX_TOPIC_LEN;
    me->config.max_subscriptions = me->sub_table_size;
    me->config.max_topic_len = me->max_topic_len;

    me->apn = lte_link_strdup_or_null(config->apn);
    me->mqtt_broker_host = lte_link_strdup_or_null(config->mqtt_broker_host);
    me->mqtt_client_id = lte_link_strdup_or_null(config->mqtt_client_id);
    me->mqtt_username = lte_link_strdup_or_null(config->mqtt_username);
    me->mqtt_password = lte_link_strdup_or_null(config->mqtt_password);
    if ((config->apn != NULL && me->apn == NULL) ||
        (config->mqtt_broker_host != NULL && me->mqtt_broker_host == NULL) ||
        (config->mqtt_client_id != NULL && me->mqtt_client_id == NULL) ||
        (config->mqtt_username != NULL && me->mqtt_username == NULL) ||
        (config->mqtt_password != NULL && me->mqtt_password == NULL)) {
        lte_link_free_config(me);
        return ESP_ERR_NO_MEM;
    }

    me->config.apn = me->apn;
    me->config.mqtt_broker_host = me->mqtt_broker_host;
    me->config.mqtt_client_id = me->mqtt_client_id;
    me->config.mqtt_username = me->mqtt_username;
    me->config.mqtt_password = me->mqtt_password;
    return ESP_OK;
}

static void lte_link_free_config(lte_link_t *me)
{
    if (me == NULL) {
        return;
    }

    free(me->apn);
    free(me->mqtt_broker_host);
    free(me->mqtt_client_id);
    free(me->mqtt_username);
    free(me->mqtt_password);
    me->apn = NULL;
    me->mqtt_broker_host = NULL;
    me->mqtt_client_id = NULL;
    me->mqtt_username = NULL;
    me->mqtt_password = NULL;
    memset(&me->config, 0, sizeof(me->config));
}

static char *lte_link_strdup_or_null(const char *value)
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

static esp_err_t lte_link_init_lwlte(lte_link_t *me)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");

    const lwlte_air780ep_config_t lwlte_config = {
        .uart_num = me->config.uart_num,
        .uart_tx_pin = me->config.tx_gpio,
        .uart_rx_pin = me->config.rx_gpio,
        .uart_baud_rate = me->config.baud_rate,
        .en_pin = me->config.en_gpio,
        .apn = me->apn,
        .primary_cid = LTE_LINK_PRIMARY_CID,
        .auto_connect = me->config.auto_connect,
        .init_ready_timeout_ms = me->config.init_ready_timeout_ms,
        .net_activate_timeout_ms = me->config.net_activate_timeout_ms,
        .mqtt_client = {
            .enabled = me->config.mqtt_enabled,
            .host = me->mqtt_broker_host,
            .port = me->config.mqtt_broker_port,
            .client_id = me->mqtt_client_id,
            .username = me->mqtt_username,
            .password = me->mqtt_password,
            .keepalive_s = me->config.mqtt_keepalive_s,
            .clean_session = me->config.mqtt_clean_session,
        },
    };

    esp_err_t ret = lwlte_air780ep_init(&lwlte_config, &me->lwlte);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = lwlte_register_event_callback(me->lwlte, lte_link_on_lwlte_event, me);
    if (ret != ESP_OK) {
        (void)lwlte_register_event_callback(me->lwlte, NULL, NULL);
        (void)lwlte_destroy(me->lwlte);
        me->lwlte = NULL;
        return ret;
    }

    if (me->config.auto_connect) {
        ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                            ESP_ERR_TIMEOUT, TAG, "take mutex failed");
        me->started = true;
        (void)xSemaphoreGive(me->mutex);
    }

    return ESP_OK;
}

static esp_err_t lte_link_query_status(lte_link_t *me,
                                       network_link_status_t *out)
{
    ESP_RETURN_ON_FALSE(me != NULL && out != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "invalid argument");
    ESP_RETURN_ON_FALSE(me->lwlte != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "lwlte is null");

    lwlte_state_t lte_state = LWLTE_STATE_STOPPED;
    lwlte_mqtt_state_t mqtt_state = LWLTE_MQTT_STATE_STOPPED;
    esp_err_t lte_ret = lwlte_get_state(me->lwlte, &lte_state);
    esp_err_t mqtt_ret = ESP_OK;
    if (me->config.mqtt_enabled) {
        mqtt_ret = lwlte_mqtt_get_state(me->lwlte, &mqtt_state);
    }

    const bool query_ok = lte_ret == ESP_OK && mqtt_ret == ESP_OK;
    const network_link_status_t status = lte_link_internal_map_status(
        lte_state, mqtt_state, me->config.mqtt_enabled, query_ok);

    if (me->mutex != NULL && xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
        me->cached_status = status;
        (void)xSemaphoreGive(me->mutex);
    }
    *out = status;

    return query_ok ? ESP_OK : ESP_FAIL;
}

static bool lte_link_is_mqtt_connected(lte_link_t *me)
{
    lwlte_mqtt_state_t mqtt_state = LWLTE_MQTT_STATE_STOPPED;

    if (me == NULL || me->lwlte == NULL || !me->config.mqtt_enabled) {
        return false;
    }
    return lwlte_mqtt_get_state(me->lwlte, &mqtt_state) == ESP_OK &&
           mqtt_state == LWLTE_MQTT_STATE_CONNECTED;
}

static esp_err_t lte_link_replay_subscriptions(lte_link_t *me)
{
    esp_err_t first_error = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");

    for (int i = 0;; i++) {
        char *topic = NULL;
        network_mqtt_qos_t qos = NETWORK_MQTT_QOS0;
        bool entry_in_use = false;

        ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                            ESP_ERR_TIMEOUT, TAG, "take mutex failed");
        if (i >= me->sub_table_size || me->destroying) {
            (void)xSemaphoreGive(me->mutex);
            break;
        }
        entry_in_use = me->sub_table[i].in_use;
        if (entry_in_use && me->sub_table[i].topic != NULL) {
            topic = lte_link_strdup_or_null(me->sub_table[i].topic);
            qos = me->sub_table[i].qos;
        }
        (void)xSemaphoreGive(me->mutex);

        if (topic == NULL) {
            if (entry_in_use && first_error == ESP_OK) {
                first_error = ESP_ERR_NO_MEM;
            }
            continue;
        }

        esp_err_t ret = lwlte_mqtt_subscribe(me->lwlte, topic, (uint8_t)qos);
        if (ret != ESP_OK && first_error == ESP_OK) {
            first_error = ret;
            ESP_LOGW(TAG, "replay subscription failed: %s", esp_err_to_name(ret));
        }
        free(topic);
    }

    return first_error;
}

static void lte_link_on_lwlte_event(lwlte_t *lte, lwlte_event_id_t event_id,
                                    const lwlte_event_data_t *data,
                                    void *user_ctx)
{
    lte_link_t *me = (lte_link_t *)user_ctx;
    (void)lte;

    if (me == NULL || me->mutex == NULL) {
        return;
    }

    switch (event_id) {
    case LWLTE_EVENT_MQTT_CONNECTED:
        if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
            if (!me->destroying) {
                me->mqtt_started = true;
            }
            (void)xSemaphoreGive(me->mutex);
        }
        (void)lte_link_replay_subscriptions(me);
        break;
    case LWLTE_EVENT_MQTT_DISCONNECTED:
    case LWLTE_EVENT_MQTT_ERROR:
        if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
            me->mqtt_started = false;
            (void)xSemaphoreGive(me->mutex);
        }
        break;
    case LWLTE_EVENT_MQTT_DATA:
        lte_link_handle_mqtt_data(me, data);
        break;
    case LWLTE_EVENT_NET_ONLINE:
    case LWLTE_EVENT_NET_OFFLINE:
    case LWLTE_EVENT_NET_ERROR:
        if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
            bool destroying = me->destroying;
            (void)xSemaphoreGive(me->mutex);
            if (!destroying) {
                network_link_status_t unused = NETWORK_LINK_STATUS_IDLE;
                (void)lte_link_query_status(me, &unused);
            }
        }
        break;
    default:
        break;
    }
}

static void lte_link_handle_mqtt_data(lte_link_t *me,
                                      const lwlte_event_data_t *data)
{
    network_rx_cb_t rx_cb = NULL;
    void *rx_ctx = NULL;

    if (data == NULL || data->data.mqtt_msg.topic == NULL ||
        data->data.mqtt_msg.topic_len == 0U ||
        data->data.mqtt_msg.payload_len > (size_t)INT_MAX) {
        ESP_LOGW(TAG, "invalid mqtt data event");
        return;
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (!me->destroying && me->rx_cb != NULL) {
        rx_cb = me->rx_cb;
        rx_ctx = me->rx_ctx;
        me->active_rx_callbacks++;
    }
    (void)xSemaphoreGive(me->mutex);

    if (rx_cb == NULL) {
        return;
    }

    char *topic = malloc(data->data.mqtt_msg.topic_len + 1U);
    if (topic == NULL) {
        ESP_LOGW(TAG, "allocate mqtt topic copy failed");
        if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
            if (me->active_rx_callbacks > 0) {
                me->active_rx_callbacks--;
            }
            (void)xSemaphoreGive(me->mutex);
        }
        return;
    }
    memcpy(topic, data->data.mqtt_msg.topic, data->data.mqtt_msg.topic_len);
    topic[data->data.mqtt_msg.topic_len] = '\0';

    const network_rx_data_t rx_data = {
        .topic = topic,
        .data = (const char *)data->data.mqtt_msg.payload,
        .data_len = (int)data->data.mqtt_msg.payload_len,
    };
    rx_cb(&rx_data, rx_ctx);
    free(topic);
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
        if (me->active_rx_callbacks > 0) {
            me->active_rx_callbacks--;
        }
        (void)xSemaphoreGive(me->mutex);
    }
}
