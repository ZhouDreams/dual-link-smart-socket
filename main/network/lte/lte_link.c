/**
 * @file lte_link.c
 * @brief LTE 链路子类实现
 * @details LTE link subclass implementation (esp-lwlte current API)
 * @author OpenCode
 * @date 2026-06-24
 */

/*********************
 *      INCLUDES
 *********************/

#include "lte_link.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lte_link_internal.h"
#include "lwlte.h"
#include "network_link_priv.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "lte_link"

#define LTE_LINK_DEFAULT_MAX_SUBSCRIPTIONS (8)
#define LTE_LINK_DEFAULT_MAX_TOPIC_LEN     (128)
#define LTE_LINK_DEFAULT_BAUD_RATE         (115200)
#define LTE_LINK_PRIMARY_CID               (1)
#define LTE_LINK_RX_CB_POLL_MS             (10U)

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
    lwlte_handle_t *lwlte;
    lte_link_sub_entry_t *sub_table;
    int sub_table_size;
    int max_topic_len;
    SemaphoreHandle_t mutex;
    network_rx_cb_t rx_cb;
    void *rx_ctx;
    int active_rx_callbacks;
    esp_event_handler_instance_t net_handler;
    esp_event_handler_instance_t mqtt_handler;
    network_link_status_t cached_status;
    bool mqtt_active;
    bool started;
    bool destroying;
} lte_link_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static esp_err_t lte_link_destroy_impl(network_link_t *base);
static esp_err_t lte_link_start_impl(network_link_t *base);
static esp_err_t lte_link_stop_impl(network_link_t *base);
static esp_err_t lte_link_get_status_impl(network_link_t *base,
                                          network_link_status_t *out);
static esp_err_t lte_link_publish_impl(network_link_t *base,
                                       const network_publish_request_t *req);
static esp_err_t lte_link_subscribe_impl(network_link_t *base,
                                         const char *topic,
                                         network_mqtt_qos_t qos);
static esp_err_t lte_link_unsubscribe_impl(network_link_t *base,
                                           const char *topic);
static esp_err_t lte_link_register_rx_cb_impl(network_link_t *base,
                                              network_rx_cb_t cb, void *ctx);
static esp_err_t lte_link_set_active_impl(network_link_t *base, bool active);

static esp_err_t lte_link_wait_rx_callbacks_drained(lte_link_t *me);
static esp_err_t lte_link_validate_config(const lte_link_config_t *config);
static esp_err_t lte_link_copy_config(lte_link_t *me,
                                      const lte_link_config_t *config);
static void lte_link_free_config(lte_link_t *me);
static lte_link_t *lte_link_from_base(network_link_t *base);
static char *lte_link_strdup_or_null(const char *value);
static esp_err_t lte_link_init_lwlte(lte_link_t *me);
static void lte_link_build_mqtt_config(const lte_link_t *me,
                                       lwlte_mqtt_config_t *out);
static esp_err_t lte_link_register_events(lte_link_t *me);
static void lte_link_unregister_events(lte_link_t *me);
static esp_err_t lte_link_query_status(lte_link_t *me,
                                       network_link_status_t *out);
static bool lte_link_is_mqtt_connected(lte_link_t *me);
static esp_err_t lte_link_replay_subscriptions(lte_link_t *me);
static void lte_link_on_net_event(void *arg, esp_event_base_t base,
                                  int32_t event_id, void *event_data);
static void lte_link_on_mqtt_event(void *arg, esp_event_base_t base,
                                   int32_t event_id, void *event_data);
static void lte_link_handle_mqtt_data(lte_link_t *me,
                                      lwlte_mqtt_event_data_t *data);

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
    .set_active = lte_link_set_active_impl,
};

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
    lwlte_handle_t *lwlte = NULL;

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
        return ret;
    }

    /* Unregister events before powering off so no callback fires into a
     * half-torn-down object. */
    lte_link_unregister_events(me);

    if (me->started && lwlte != NULL) {
        (void)lwlte_stop(lwlte);  /* async power-off request; best-effort */
    }

    me->started = false;
    me->mqtt_active = false;
    me->cached_status = NETWORK_LINK_STATUS_IDLE;

    if (lwlte != NULL) {
        ret = lwlte_destroy(lwlte);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "lwlte destroy failed: %s", esp_err_to_name(ret));
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
    esp_err_t ret = ESP_OK;
    bool already_started = false;

    ESP_RETURN_ON_FALSE(base != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");

    lte_link_t *me = lte_link_from_base(base);
    ESP_RETURN_ON_FALSE(me->lwlte != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "link is not initialized");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (me->destroying) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    already_started = me->started;
    if (already_started) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_OK;
    }
    me->started = true;  /* claim started so events/callbacks see it */
    (void)xSemaphoreGive(me->mutex);

    ret = lte_link_register_events(me);
    if (ret != ESP_OK) {
        goto revert_started;
    }

    /* Async network bring-up (registration + PDP). Returns OK = submitted.
     * Status progresses to DEGRADED once online (observed via get_status). */
    ret = lwlte_start(me->lwlte);
    if (ret != ESP_OK) {
        lte_link_unregister_events(me);
        goto revert_started;
    }

    ESP_LOGI(TAG, "lwlte start submitted");
    return ESP_OK;

revert_started:
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
        me->started = false;
        (void)xSemaphoreGive(me->mutex);
    }
    return ret;
}

static esp_err_t lte_link_stop_impl(network_link_t *base)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(base != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");

    lte_link_t *me = lte_link_from_base(base);
    ESP_RETURN_ON_FALSE(me->lwlte != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "link is not initialized");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (!me->started) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_OK;
    }
    (void)xSemaphoreGive(me->mutex);

    lte_link_unregister_events(me);

    ret = lwlte_stop(me->lwlte);  /* async: stop MQTT + deactivate PDP + EN off */
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "lwlte stop failed: %s", esp_err_to_name(ret));
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
        me->started = false;
        me->mqtt_active = false;
        me->cached_status = NETWORK_LINK_STATUS_IDLE;
        (void)xSemaphoreGive(me->mutex);
    }
    return ESP_OK;
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

    ESP_RETURN_ON_FALSE(base != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");
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
    broker_io = (ret == ESP_OK) && !me->destroying;
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

    ESP_RETURN_ON_FALSE(base != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");
    ESP_RETURN_ON_FALSE(topic != NULL && topic[0] != '\0', ESP_ERR_INVALID_ARG,
                        TAG, "topic is empty");

    lte_link_t *me = lte_link_from_base(base);
    ESP_RETURN_ON_FALSE(me->config.mqtt_enabled, ESP_ERR_NOT_SUPPORTED, TAG,
                        "mqtt is disabled");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    ret = lte_link_internal_remove_subscription(me->sub_table, me->sub_table_size,
                                                topic);
    broker_io = (ret == ESP_OK) && !me->destroying;
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
    ESP_RETURN_ON_FALSE(base != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");

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

static esp_err_t lte_link_set_active_impl(network_link_t *base, bool active)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(base != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");

    lte_link_t *me = lte_link_from_base(base);
    if (!me->config.mqtt_enabled) {
        return ESP_OK;  /* MQTT disabled: role toggle is a no-op */
    }

    /* Call lwlte WITHOUT holding our mutex: lwlte_mqtt_start/stop are async
     * submits and we must not nest the lte_link mutex here (FSM events dispatch
     * from the lwlte task and re-enter via the esp_event handlers). */
    if (me->destroying) {
        return ESP_ERR_INVALID_STATE;
    }

    if (active) {
        ret = lwlte_mqtt_start(me->lwlte);
        if (ret == ESP_ERR_INVALID_STATE) {
            ret = ESP_OK;  /* already started/connecting */
        }
    } else {
        ret = lwlte_mqtt_stop(me->lwlte);
        if (ret == ESP_ERR_INVALID_STATE) {
            ret = ESP_OK;  /* already stopped */
        }
    }

    if (ret == ESP_OK) {
        if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
            me->mqtt_active = active;
            (void)xSemaphoreGive(me->mutex);
        }
    }
    return ret;
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
        vTaskDelay(pdMS_TO_TICKS(LTE_LINK_RX_CB_POLL_MS));
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

static void lte_link_build_mqtt_config(const lte_link_t *me,
                                       lwlte_mqtt_config_t *out)
{
    out->host = me->mqtt_broker_host;
    out->port = me->config.mqtt_broker_port;
    out->client_id = me->mqtt_client_id;
    out->username = me->mqtt_username;
    out->password = me->mqtt_password;
    out->keepalive_s = me->config.mqtt_keepalive_s;
    out->clean_session = me->config.mqtt_clean_session;
    out->fsm_queue_size = 0;       /* 0 -> esp-lwlte defaults */
    out->fsm_task_stack = 0;
    out->fsm_task_priority = 0;
}

static esp_err_t lte_link_init_lwlte(lte_link_t *me)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");

    /* Flat lte_link_config_t -> nested esp-lwlte base config. */
    const lwlte_air780ep_config_t lwlte_config = {
        .base = {
            .uart = {
                .num = me->config.uart_num,
                .tx_pin = me->config.tx_gpio,
                .rx_pin = me->config.rx_gpio,
                .baud_rate = me->config.baud_rate,
            },
            .at_engine = { 0 },                 /* defaults */
            .modem = {
                .en_pin = me->config.en_gpio,
                .reset_pulse_ms = 0,            /* default */
                .ready_timeout_ms = me->config.init_ready_timeout_ms,
                .default_cmd_timeout_ms = 0,
                .event_queue_size = 0,
                .event_task_stack = 0,
                .event_task_priority = 0,
            },
            .core = {
                .apn = me->apn,
                .primary_cid = LTE_LINK_PRIMARY_CID,
                .net_activate_timeout_ms = me->config.net_activate_timeout_ms,
                .reconnect_delay_ms = 0,        /* default */
                .fsm_queue_size = 0,
                .fsm_task_stack = 0,
                .fsm_task_priority = 0,
            },
            .event = {
                .loop = NULL,                   /* default loop */
            },
        },
    };

    /* Creates facade only — does not start module, wait for AT ready, or
     * activate PDP. Returns non-NULL handle even if hardware is absent. */
    esp_err_t ret = lwlte_air780ep_init(&lwlte_config, &me->lwlte);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "lwlte_air780ep_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (me->config.mqtt_enabled) {
        lwlte_mqtt_config_t mqtt_config;
        lte_link_build_mqtt_config(me, &mqtt_config);
        ret = lwlte_mqtt_init(me->lwlte, &mqtt_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "lwlte_mqtt_init failed: %s", esp_err_to_name(ret));
            (void)lwlte_destroy(me->lwlte);
            me->lwlte = NULL;
            return ret;
        }
    }

    return ESP_OK;
}

static esp_err_t lte_link_register_events(lte_link_t *me)
{
    esp_err_t ret = esp_event_handler_instance_register(
        LWLTE_EVENT, ESP_EVENT_ANY_ID, lte_link_on_net_event, me,
        &me->net_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register LWLTE_EVENT failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_event_handler_instance_register(
        LWLTE_MQTT_EVENT, ESP_EVENT_ANY_ID, lte_link_on_mqtt_event, me,
        &me->mqtt_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register LWLTE_MQTT_EVENT failed: %s",
                 esp_err_to_name(ret));
        (void)esp_event_handler_instance_unregister(
            LWLTE_EVENT, ESP_EVENT_ANY_ID, me->net_handler);
        me->net_handler = NULL;
        return ret;
    }
    return ESP_OK;
}

static void lte_link_unregister_events(lte_link_t *me)
{
    if (me->net_handler != NULL) {
        (void)esp_event_handler_instance_unregister(
            LWLTE_EVENT, ESP_EVENT_ANY_ID, me->net_handler);
        me->net_handler = NULL;
    }
    if (me->mqtt_handler != NULL) {
        (void)esp_event_handler_instance_unregister(
            LWLTE_MQTT_EVENT, ESP_EVENT_ANY_ID, me->mqtt_handler);
        me->mqtt_handler = NULL;
    }
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

    const bool query_ok = (lte_ret == ESP_OK) && (mqtt_ret == ESP_OK);
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
    return (lwlte_mqtt_get_state(me->lwlte, &mqtt_state) == ESP_OK) &&
           (mqtt_state == LWLTE_MQTT_STATE_CONNECTED);
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
            ESP_LOGW(TAG, "replay subscription failed: %s",
                     esp_err_to_name(ret));
        }
        free(topic);
    }

    return first_error;
}

static void lte_link_on_net_event(void *arg, esp_event_base_t base,
                                  int32_t event_id, void *event_data)
{
    lte_link_t *me = (lte_link_t *)arg;
    (void)base;
    (void)event_data;
    if (me == NULL || me->destroying) {
        return;
    }
    /* Diagnostic only: status is read live via get_status(). */
    switch ((lwlte_event_id_t)event_id) {
    case LWLTE_EVENT_NET_ONLINE:
        ESP_LOGI(TAG, "LTE network online");
        break;
    case LWLTE_EVENT_NET_OFFLINE:
        ESP_LOGW(TAG, "LTE network offline");
        break;
    case LWLTE_EVENT_NET_ERROR:
    case LWLTE_EVENT_ERROR:
        ESP_LOGW(TAG, "LTE network error (event=%d)", (int)event_id);
        break;
    default:
        break;
    }
}

static void lte_link_on_mqtt_event(void *arg, esp_event_base_t base,
                                   int32_t event_id, void *event_data)
{
    lte_link_t *me = (lte_link_t *)arg;
    (void)base;
    if (me == NULL || me->destroying) {
        /* If destroying, still release DATA buffers to avoid leaks. */
        if (event_id == LWLTE_MQTT_EVENT_DATA && event_data != NULL) {
            lwlte_mqtt_event_data_release((lwlte_mqtt_event_data_t *)event_data);
        }
        return;
    }

    switch ((lwlte_mqtt_event_id_t)event_id) {
    case LWLTE_MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "LTE MQTT connected");
        (void)lte_link_replay_subscriptions(me);
        break;
    case LWLTE_MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "LTE MQTT disconnected");
        break;
    case LWLTE_MQTT_EVENT_DATA:
        lte_link_handle_mqtt_data(me, (lwlte_mqtt_event_data_t *)event_data);
        break;
    case LWLTE_MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "LTE MQTT error");
        break;
    default:
        break;
    }
}

static void lte_link_handle_mqtt_data(lte_link_t *me,
                                      lwlte_mqtt_event_data_t *data)
{
    network_rx_cb_t rx_cb = NULL;
    void *rx_ctx = NULL;

    if (data == NULL || data->msg.topic == NULL || data->msg.topic_len == 0U ||
        data->msg.payload_len > (size_t)INT_MAX) {
        if (data != NULL) {
            lwlte_mqtt_event_data_release(data);
        }
        return;
    }

    /* Read rx_cb under mutex, then RELEASE before invoking it (lock invariant:
     * never hold lte_link mutex when entering the network_manager bridge, which
     * takes the manager mutex — avoids lock-order inversion with replay). */
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        lwlte_mqtt_event_data_release(data);
        return;
    }
    if (!me->destroying && me->rx_cb != NULL) {
        rx_cb = me->rx_cb;
        rx_ctx = me->rx_ctx;
        me->active_rx_callbacks++;
    }
    (void)xSemaphoreGive(me->mutex);

    if (rx_cb == NULL) {
        lwlte_mqtt_event_data_release(data);  /* single consumer: still release */
        return;
    }

    /* topic is not 0-terminated; copy to a 0-terminated buffer for consumers. */
    char *topic = malloc(data->msg.topic_len + 1U);
    if (topic == NULL) {
        ESP_LOGW(TAG, "allocate mqtt topic copy failed");
        if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
            if (me->active_rx_callbacks > 0) {
                me->active_rx_callbacks--;
            }
            (void)xSemaphoreGive(me->mutex);
        }
        lwlte_mqtt_event_data_release(data);
        return;
    }
    memcpy(topic, data->msg.topic, data->msg.topic_len);
    topic[data->msg.topic_len] = '\0';

    const network_rx_data_t rx_data = {
        .topic = topic,
        .data = (const char *)data->msg.payload,
        .data_len = (int)data->msg.payload_len,
    };
    rx_cb(&rx_data, rx_ctx);  /* INVARIANT: lte_link mutex not held here */
    free(topic);

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
        if (me->active_rx_callbacks > 0) {
            me->active_rx_callbacks--;
        }
        (void)xSemaphoreGive(me->mutex);
    }
    lwlte_mqtt_event_data_release(data);  /* exactly once, after rx_cb returns */
}
