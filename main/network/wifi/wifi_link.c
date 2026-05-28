/**
 * @file wifi_link.c
 * @brief Wi-Fi 链路子类实现
 * @details Wi-Fi link subclass implementation
 * @author OpenCode
 * @date 2026-05-24
 */

/*********************
 *      INCLUDES
 *********************/

#include "wifi_link.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "network_link_priv.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "wifi_link"

#define WIFI_LINK_DEFAULT_MAX_SUBSCRIPTIONS (8)
#define WIFI_LINK_DEFAULT_MAX_TOPIC_LEN     (128)
#define WIFI_LINK_DEFAULT_KEEPALIVE_S       (60)
#define WIFI_LINK_MAX_SSID_LEN              (32)
#define WIFI_LINK_MAX_PASSWORD_LEN          (64)
#define WIFI_LINK_RUNTIME_DRAIN_POLL_MS     (10)

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief Wi-Fi 链路订阅项
 * @details Wi-Fi link subscription entry
 */
typedef struct {
    char *topic;
    network_mqtt_qos_t qos;
    bool in_use;
} wifi_link_sub_entry_t;

/**
 * @brief Wi-Fi 链路对象
 * @details Wi-Fi link object
 */
typedef struct wifi_link {
    network_link_t base;
    wifi_link_config_t config;
    char *ssid;
    char *password;
    char *mqtt_broker_host;
    char *mqtt_client_id;
    char *mqtt_username;
    char *mqtt_password;
    char *mqtt_uri;
    esp_netif_t *netif;
    esp_event_handler_instance_t wifi_event_instance;
    esp_event_handler_instance_t ip_event_instance;
    esp_mqtt_client_handle_t mqtt_client;
    bool wifi_connected;
    bool mqtt_connected;
    wifi_link_sub_entry_t *sub_table;
    int sub_table_size;
    int max_topic_len;
    SemaphoreHandle_t mutex;
    network_rx_cb_t rx_cb;
    void *rx_ctx;
    bool started;
    bool starting;
    bool stopping;
    bool start_failed;
    bool destroying;
    int runtime_action_count;
    bool mqtt_op_active;
} wifi_link_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**
 * @brief 销毁 Wi-Fi 链路实现
 * @details Destroy Wi-Fi link implementation
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t wifi_link_destroy_impl(network_link_t *base);

/**
 * @brief 启动 Wi-Fi 链路实现
 * @details Start Wi-Fi link implementation
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t wifi_link_start_impl(network_link_t *base);

/**
 * @brief 停止 Wi-Fi 链路实现
 * @details Stop Wi-Fi link implementation
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t wifi_link_stop_impl(network_link_t *base);

/**
 * @brief 获取 Wi-Fi 链路状态实现
 * @details Get Wi-Fi link status implementation
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @param[out] out 状态输出； Status output
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t wifi_link_get_status_impl(network_link_t *base,
                                           network_link_status_t *out);

/**
 * @brief 发布 Wi-Fi 链路消息实现
 * @details Publish Wi-Fi link message implementation
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @param[in] req 发布请求； Publish request
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t wifi_link_publish_impl(network_link_t *base,
                                        const network_publish_request_t *req);

/**
 * @brief 订阅 Wi-Fi 链路主题实现
 * @details Subscribe Wi-Fi link topic implementation
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @param[in] topic MQTT 主题； MQTT topic
 * @param[in] qos MQTT 服务质量等级； MQTT QoS level
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t wifi_link_subscribe_impl(network_link_t *base,
                                          const char *topic,
                                          network_mqtt_qos_t qos);

/**
 * @brief 取消订阅 Wi-Fi 链路主题实现
 * @details Unsubscribe Wi-Fi link topic implementation
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @param[in] topic MQTT 主题； MQTT topic
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t wifi_link_unsubscribe_impl(network_link_t *base,
                                            const char *topic);

/**
 * @brief 注册 Wi-Fi 链路接收回调实现
 * @details Register Wi-Fi link RX callback implementation
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @param[in] cb 接收回调； RX callback
 * @param[in] ctx 用户上下文； User context
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t wifi_link_register_rx_cb_impl(network_link_t *base,
                                               network_rx_cb_t cb,
                                               void *ctx);

/**
 * @brief 校验 Wi-Fi 链路配置
 * @details Validate Wi-Fi link configuration
 * @param[in] config Wi-Fi 链路配置； Wi-Fi link configuration
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t wifi_link_validate_config(const wifi_link_config_t *config);

/**
 * @brief 复制 Wi-Fi 链路配置
 * @details Copy Wi-Fi link configuration
 * @param[in,out] me Wi-Fi 链路对象； Wi-Fi link object
 * @param[in] config Wi-Fi 链路配置； Wi-Fi link configuration
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t wifi_link_copy_config(wifi_link_t *me,
                                       const wifi_link_config_t *config);

/**
 * @brief 释放 Wi-Fi 链路配置
 * @details Free Wi-Fi link configuration
 * @param[in,out] me Wi-Fi 链路对象； Wi-Fi link object
 */
static void wifi_link_free_config(wifi_link_t *me);

/**
 * @brief 从基类句柄获取 Wi-Fi 链路对象
 * @details Get Wi-Fi link object from base handle
 * @param[in] base 网络链路基类句柄； Network link base handle
 * @return Wi-Fi 链路对象； Wi-Fi link object
 */
static wifi_link_t *wifi_link_from_base(network_link_t *base);

/**
 * @brief 校验 MQTT 服务质量等级
 * @details Validate MQTT quality of service level
 * @param[in] qos MQTT 服务质量等级； MQTT QoS level
 * @return true 有效，false 无效； true if valid, false otherwise
 */
static bool wifi_link_is_valid_qos(network_mqtt_qos_t qos);

/**
 * @brief 复制字符串或返回 NULL
 * @details Duplicate string or return NULL
 * @param[in] value 字符串； String value
 * @return 已复制字符串，失败或输入为空返回 NULL； Duplicated string, or NULL
 */
static char *wifi_link_strdup_or_null(const char *value);

/**
 * @brief 判断原始 IPv6 字面量
 * @details Check raw IPv6 literal
 * @param[in] host 主机名； Host name
 * @return true 是原始 IPv6，false 不是； true if raw IPv6 literal
 */
static bool wifi_link_is_raw_ipv6_literal(const char *host);

/**
 * @brief 构建 MQTT URI
 * @details Build MQTT URI
 * @param[in] host MQTT 主机； MQTT host
 * @param[in] port MQTT 端口； MQTT port
 * @param[in] use_tls 是否使用 TLS； Whether TLS is used
 * @return URI 字符串，失败返回 NULL； URI string, or NULL on failure
 */
static char *wifi_link_build_uri(const char *host, uint16_t port, bool use_tls);

/**
 * @brief 获取当前状态
 * @details Get current status
 * @param[in] me Wi-Fi 链路对象； Wi-Fi link object
 * @return 网络链路状态； Network link status
 */
static network_link_status_t wifi_link_current_status_locked(
    const wifi_link_t *me);

/**
 * @brief Begin runtime action
 * @details Begin runtime action
 * @param[in,out] me Wi-Fi link object
 * @return true if begun, false otherwise
 */
static bool wifi_link_begin_runtime_action_locked(wifi_link_t *me);

/**
 * @brief End runtime action
 * @details End runtime action
 * @param[in,out] me Wi-Fi link object
 */
static void wifi_link_end_runtime_action(wifi_link_t *me);

/**
 * @brief Wait for runtime actions to drain
 * @details Wait for runtime actions to drain
 * @param[in,out] me Wi-Fi link object
 * @return ESP-IDF error code
 */
static esp_err_t wifi_link_wait_runtime_actions_drained(wifi_link_t *me);

/**
 * @brief Begin serialized MQTT broker operation
 * @details Begin serialized MQTT broker operation
 * @param[in,out] me Wi-Fi link object
 * @return true if begun, false otherwise
 */
static bool wifi_link_begin_mqtt_op_locked(wifi_link_t *me);

/**
 * @brief End serialized MQTT broker operation
 * @details End serialized MQTT broker operation
 * @param[in,out] me Wi-Fi link object
 */
static void wifi_link_end_mqtt_op(wifi_link_t *me);

/**
 * @brief 清理 Wi-Fi 链路运行时资源
 * @details Cleanup Wi-Fi link runtime resources
 * @param[in,out] me Wi-Fi 链路对象； Wi-Fi link object
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t wifi_link_cleanup_resources(wifi_link_t *me);

/**
 * @brief 判断 Wi-Fi 清理错误是否可忽略
 * @details Check if Wi-Fi cleanup error can be ignored
 * @param[in] err ESP-IDF 错误码； ESP-IDF error code
 * @return true 可忽略，false 不可忽略； true if ignored
 */
static bool wifi_link_is_expected_wifi_cleanup_error(esp_err_t err);

/**
 * @brief Wi-Fi 事件处理函数
 * @details Wi-Fi event handler
 * @param[in] arg 用户参数； User argument
 * @param[in] base 事件基； Event base
 * @param[in] event_id 事件 ID； Event ID
 * @param[in] event_data 事件数据； Event data
 */
static void wifi_link_wifi_event_handler(void *arg, esp_event_base_t base,
                                         int32_t event_id, void *event_data);

/**
 * @brief IP 事件处理函数
 * @details IP event handler
 * @param[in] arg 用户参数； User argument
 * @param[in] base 事件基； Event base
 * @param[in] event_id 事件 ID； Event ID
 * @param[in] event_data 事件数据； Event data
 */
static void wifi_link_ip_event_handler(void *arg, esp_event_base_t base,
                                       int32_t event_id, void *event_data);

/**
 * @brief 启动 MQTT 客户端
 * @details Start MQTT client
 * @param[in,out] me Wi-Fi 链路对象； Wi-Fi link object
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t wifi_link_start_mqtt(wifi_link_t *me);

/**
 * @brief MQTT 事件处理函数
 * @details MQTT event handler
 * @param[in] handler_args 处理函数参数； Handler argument
 * @param[in] base 事件基； Event base
 * @param[in] event_id 事件 ID； Event ID
 * @param[in] event_data 事件数据； Event data
 */
static void wifi_link_mqtt_event_handler(void *handler_args,
                                         esp_event_base_t base,
                                         int32_t event_id, void *event_data);

/**
 * @brief 重放 MQTT 订阅
 * @details Replay MQTT subscriptions
 * @param[in,out] me Wi-Fi 链路对象； Wi-Fi link object
 * @param[in] client MQTT 客户端； MQTT client
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t wifi_link_replay_subscriptions(wifi_link_t *me,
                                                esp_mqtt_client_handle_t client);

/**
 * @brief 查找订阅项
 * @details Find subscription entry
 * @param[in,out] me Wi-Fi 链路对象； Wi-Fi link object
 * @param[in] topic MQTT 主题； MQTT topic
 * @param[out] out_index 已存在订阅索引； Existing subscription index
 * @param[out] out_free_index 首个空闲索引； First free index
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t wifi_link_find_subscription_locked(wifi_link_t *me,
                                                    const char *topic,
                                                    int *out_index,
                                                    int *out_free_index);

/**
 * @brief 存储订阅意图
 * @details Store subscription intent
 * @param[in,out] me Wi-Fi 链路对象； Wi-Fi link object
 * @param[in] topic MQTT 主题； MQTT topic
 * @param[in] qos MQTT 服务质量等级； MQTT QoS level
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t wifi_link_store_subscription_locked(wifi_link_t *me,
                                                     const char *topic,
                                                     network_mqtt_qos_t qos);

/**
 * @brief 移除订阅意图
 * @details Remove subscription intent
 * @param[in,out] me Wi-Fi 链路对象； Wi-Fi link object
 * @param[in] topic MQTT 主题； MQTT topic
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t wifi_link_remove_subscription_locked(wifi_link_t *me,
                                                      const char *topic);

/**********************
 *  STATIC VARIABLES
 **********************/

static const network_link_ops_t wifi_link_ops = {
    .destroy = wifi_link_destroy_impl,
    .start = wifi_link_start_impl,
    .stop = wifi_link_stop_impl,
    .get_status = wifi_link_get_status_impl,
    .publish = wifi_link_publish_impl,
    .subscribe = wifi_link_subscribe_impl,
    .unsubscribe = wifi_link_unsubscribe_impl,
    .register_rx_cb = wifi_link_register_rx_cb_impl,
};

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

network_link_t *wifi_link_create(const wifi_link_config_t *config)
{
    if (wifi_link_validate_config(config) != ESP_OK) {
        return NULL;
    }

    wifi_link_t *me = calloc(1, sizeof(*me));
    if (me == NULL) {
        ESP_LOGE(TAG, "calloc wifi link failed");
        return NULL;
    }

    me->base.ops = &wifi_link_ops;
    me->base.type = NETWORK_LINK_TYPE_WIFI;
    me->mutex = xSemaphoreCreateMutex();
    if (me->mutex == NULL) {
        ESP_LOGE(TAG, "create mutex failed");
        free(me);
        return NULL;
    }

    if (wifi_link_copy_config(me, config) != ESP_OK) {
        vSemaphoreDelete(me->mutex);
        wifi_link_free_config(me);
        free(me);
        return NULL;
    }

    me->sub_table = calloc((size_t)me->sub_table_size, sizeof(me->sub_table[0]));
    if (me->sub_table == NULL) {
        ESP_LOGE(TAG, "calloc subscription table failed");
        vSemaphoreDelete(me->mutex);
        wifi_link_free_config(me);
        free(me);
        return NULL;
    }

    return &me->base;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static wifi_link_t *wifi_link_from_base(network_link_t *base)
{
    return (wifi_link_t *)base;
}

static esp_err_t wifi_link_destroy_impl(network_link_t *base)
{
    esp_err_t ret = ESP_OK;

    if (base == NULL) {
        return ESP_OK;
    }

    wifi_link_t *me = wifi_link_from_base(base);
    if (me->mutex != NULL && xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
        if (me->starting || me->stopping || me->runtime_action_count > 0) {
            (void)xSemaphoreGive(me->mutex);
            return ESP_ERR_INVALID_STATE;
        }
        me->destroying = true;
        (void)xSemaphoreGive(me->mutex);
    }

    ret = wifi_link_stop_impl(base);
    if (ret != ESP_OK) {
        return ret;
    }

    for (int i = 0; i < me->sub_table_size; i++) {
        free(me->sub_table[i].topic);
        me->sub_table[i].topic = NULL;
        me->sub_table[i].in_use = false;
    }
    free(me->sub_table);
    me->sub_table = NULL;

    if (me->mutex != NULL) {
        vSemaphoreDelete(me->mutex);
        me->mutex = NULL;
    }
    wifi_link_free_config(me);
    free(me);
    return ESP_OK;
}

static esp_err_t wifi_link_start_impl(network_link_t *base)
{
    esp_err_t ret = ESP_OK;
    esp_err_t cleanup_ret = ESP_OK;
    wifi_config_t wifi_config = {0};
    esp_netif_t *netif = NULL;
    esp_event_handler_instance_t wifi_event_instance = NULL;
    esp_event_handler_instance_t ip_event_instance = NULL;

    ESP_RETURN_ON_FALSE(base != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");

    wifi_link_t *me = wifi_link_from_base(base);
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (me->starting || me->stopping) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (me->started) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_OK;
    }
    if (me->mqtt_client != NULL || me->netif != NULL ||
        me->wifi_event_instance != NULL || me->ip_event_instance != NULL) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    ESP_GOTO_ON_FALSE(!me->destroying, ESP_ERR_INVALID_STATE, release_mutex,
                      TAG, "link is destroying");
    me->starting = true;
    me->start_failed = false;
    me->wifi_connected = false;
    me->mqtt_connected = false;
    (void)xSemaphoreGive(me->mutex);

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "init esp-netif failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    ret = ESP_OK;
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "create default event loop failed: %s",
                 esp_err_to_name(ret));
        goto cleanup;
    }
    ret = ESP_OK;

    netif = esp_netif_create_default_wifi_sta();
    if (netif == NULL) {
        ret = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "create default wifi sta netif failed");
        goto cleanup;
    }
    ESP_GOTO_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                      ESP_ERR_TIMEOUT, cleanup, TAG, "take mutex failed");
    me->netif = netif;
    if (me->destroying) {
        ret = ESP_ERR_INVALID_STATE;
    }
    (void)xSemaphoreGive(me->mutex);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_link_wifi_event_handler, me,
                                               &wifi_event_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register wifi event handler failed: %s",
                 esp_err_to_name(ret));
        goto cleanup;
    }
    ESP_GOTO_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                      ESP_ERR_TIMEOUT, cleanup, TAG, "take mutex failed");
    me->wifi_event_instance = wifi_event_instance;
    if (me->destroying) {
        ret = ESP_ERR_INVALID_STATE;
    }
    (void)xSemaphoreGive(me->mutex);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_link_ip_event_handler, me,
                                               &ip_event_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register ip event handler failed: %s",
                 esp_err_to_name(ret));
        goto cleanup;
    }
    ESP_GOTO_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                      ESP_ERR_TIMEOUT, cleanup, TAG, "take mutex failed");
    me->ip_event_instance = ip_event_instance;
    if (me->destroying) {
        ret = ESP_ERR_INVALID_STATE;
    }
    (void)xSemaphoreGive(me->mutex);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&init_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "init wifi failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    ESP_GOTO_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                      ESP_ERR_TIMEOUT, cleanup, TAG, "take mutex failed");
    if (me->destroying) {
        ret = ESP_ERR_INVALID_STATE;
    }
    (void)xSemaphoreGive(me->mutex);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    const size_t ssid_len = strlen(me->ssid);
    const size_t password_len = strlen(me->password);
    memcpy(wifi_config.sta.ssid, me->ssid, ssid_len);
    memcpy(wifi_config.sta.password, me->password, password_len);
    wifi_config.sta.threshold.authmode = (password_len == 0U) ?
                                         WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set wifi mode failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set wifi config failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    ESP_GOTO_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                      ESP_ERR_TIMEOUT, cleanup, TAG, "take mutex failed");
    if (me->destroying) {
        ret = ESP_ERR_INVALID_STATE;
    } else {
        me->started = true;
    }
    (void)xSemaphoreGive(me->mutex);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "start wifi failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    ESP_GOTO_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                      ESP_ERR_TIMEOUT, cleanup, TAG, "take mutex failed");
    if (me->destroying) {
        ret = ESP_ERR_INVALID_STATE;
    } else {
        me->starting = false;
        me->start_failed = false;
        me->started = true;
    }
    (void)xSemaphoreGive(me->mutex);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    return ESP_OK;

release_mutex:
    (void)xSemaphoreGive(me->mutex);
    return ret;

cleanup:
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
        me->started = false;
        me->wifi_connected = false;
        me->mqtt_connected = false;
        (void)xSemaphoreGive(me->mutex);
    }
    cleanup_ret = wifi_link_cleanup_resources(me);
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
        me->starting = false;
        me->start_failed = true;
        me->started = false;
        me->wifi_connected = false;
        me->mqtt_connected = false;
        (void)xSemaphoreGive(me->mutex);
    }
    return (cleanup_ret != ESP_OK) ? cleanup_ret : ret;
}

static esp_err_t wifi_link_stop_impl(network_link_t *base)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(base != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");

    wifi_link_t *me = wifi_link_from_base(base);
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (me->starting || me->stopping || me->runtime_action_count > 0) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (!me->started && me->mqtt_client == NULL && me->netif == NULL &&
        me->wifi_event_instance == NULL && me->ip_event_instance == NULL) {
        me->wifi_connected = false;
        me->mqtt_connected = false;
        me->start_failed = false;
        (void)xSemaphoreGive(me->mutex);
        return ESP_OK;
    }

    me->stopping = true;
    me->started = false;
    me->wifi_connected = false;
    me->mqtt_connected = false;
    (void)xSemaphoreGive(me->mutex);

    ret = wifi_link_cleanup_resources(me);

    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    me->stopping = false;
    if (ret == ESP_OK) {
        me->started = false;
        me->wifi_connected = false;
        me->mqtt_connected = false;
        me->start_failed = false;
    } else {
        me->start_failed = true;
    }
    (void)xSemaphoreGive(me->mutex);
    return ret;
}

static esp_err_t wifi_link_get_status_impl(network_link_t *base,
                                           network_link_status_t *out)
{
    ESP_RETURN_ON_FALSE(base != NULL && out != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "invalid argument");

    wifi_link_t *me = wifi_link_from_base(base);
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    *out = wifi_link_current_status_locked(me);
    (void)xSemaphoreGive(me->mutex);
    return ESP_OK;
}

static esp_err_t wifi_link_publish_impl(network_link_t *base,
                                        const network_publish_request_t *req)
{
    esp_mqtt_client_handle_t client = NULL;
    int msg_id = -1;

    ESP_RETURN_ON_FALSE(base != NULL && req != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "invalid argument");
    ESP_RETURN_ON_FALSE(req->topic != NULL && req->topic[0] != '\0',
                        ESP_ERR_INVALID_ARG, TAG, "topic is empty");
    ESP_RETURN_ON_FALSE(wifi_link_is_valid_qos(req->qos), ESP_ERR_INVALID_ARG,
                        TAG, "invalid qos");
    ESP_RETURN_ON_FALSE(req->payload_len <= (size_t)INT_MAX,
                        ESP_ERR_INVALID_SIZE, TAG, "payload too large");
    ESP_RETURN_ON_FALSE(req->payload_len == 0U || req->payload != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "payload is null");

    wifi_link_t *me = wifi_link_from_base(base);
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (!me->started || me->stopping || me->destroying ||
        !me->mqtt_connected || me->mqtt_client == NULL) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (!wifi_link_begin_mqtt_op_locked(me)) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    client = me->mqtt_client;
    (void)xSemaphoreGive(me->mutex);

    msg_id = esp_mqtt_client_enqueue(client, req->topic,
                                     (const char *)req->payload,
                                     (int)req->payload_len, (int)req->qos,
                                     req->retain ? 1 : 0, false);
    wifi_link_end_mqtt_op(me);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t wifi_link_subscribe_impl(network_link_t *base,
                                          const char *topic,
                                          network_mqtt_qos_t qos)
{
    esp_err_t ret = ESP_OK;
    esp_mqtt_client_handle_t client = NULL;
    bool mqtt_op_started = false;
    bool needs_broker_io = false;
    int msg_id = -1;

    ESP_RETURN_ON_FALSE(base != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "link is null");
    ESP_RETURN_ON_FALSE(topic != NULL && topic[0] != '\0', ESP_ERR_INVALID_ARG,
                        TAG, "topic is empty");
    ESP_RETURN_ON_FALSE(wifi_link_is_valid_qos(qos), ESP_ERR_INVALID_ARG, TAG,
                        "invalid qos");

    wifi_link_t *me = wifi_link_from_base(base);
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    needs_broker_io = me->mqtt_connected && me->mqtt_client != NULL &&
                      !me->stopping && !me->destroying;
    if (needs_broker_io) {
        mqtt_op_started = wifi_link_begin_mqtt_op_locked(me);
        if (!mqtt_op_started) {
            (void)xSemaphoreGive(me->mutex);
            return ESP_ERR_INVALID_STATE;
        }
        client = me->mqtt_client;
    }
    ret = wifi_link_store_subscription_locked(me, topic, qos);
    if (ret != ESP_OK) {
        if (mqtt_op_started) {
            me->mqtt_op_active = false;
            if (me->runtime_action_count > 0) {
                me->runtime_action_count--;
            }
        }
        (void)xSemaphoreGive(me->mutex);
        return ret;
    }
    (void)xSemaphoreGive(me->mutex);

    if (!mqtt_op_started) {
        return ESP_OK;
    }

    msg_id = esp_mqtt_client_subscribe(client, topic, (int)qos);
    wifi_link_end_mqtt_op(me);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t wifi_link_unsubscribe_impl(network_link_t *base,
                                            const char *topic)
{
    esp_err_t ret = ESP_OK;
    esp_mqtt_client_handle_t client = NULL;
    bool mqtt_op_started = false;
    bool needs_broker_io = false;
    int msg_id = -1;

    ESP_RETURN_ON_FALSE(base != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "link is null");
    ESP_RETURN_ON_FALSE(topic != NULL && topic[0] != '\0', ESP_ERR_INVALID_ARG,
                        TAG, "topic is empty");

    wifi_link_t *me = wifi_link_from_base(base);
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    needs_broker_io = me->mqtt_connected && me->mqtt_client != NULL &&
                      !me->stopping && !me->destroying;
    if (needs_broker_io) {
        mqtt_op_started = wifi_link_begin_mqtt_op_locked(me);
        if (!mqtt_op_started) {
            (void)xSemaphoreGive(me->mutex);
            return ESP_ERR_INVALID_STATE;
        }
        client = me->mqtt_client;
    }
    ret = wifi_link_remove_subscription_locked(me, topic);
    if (ret != ESP_OK) {
        if (mqtt_op_started) {
            me->mqtt_op_active = false;
            if (me->runtime_action_count > 0) {
                me->runtime_action_count--;
            }
        }
        (void)xSemaphoreGive(me->mutex);
        return ret;
    }
    (void)xSemaphoreGive(me->mutex);

    if (!mqtt_op_started) {
        return ret;
    }

    msg_id = esp_mqtt_client_unsubscribe(client, topic);
    wifi_link_end_mqtt_op(me);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t wifi_link_register_rx_cb_impl(network_link_t *base,
                                               network_rx_cb_t cb,
                                               void *ctx)
{
    ESP_RETURN_ON_FALSE(base != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "link is null");

    wifi_link_t *me = wifi_link_from_base(base);
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    me->rx_cb = cb;
    me->rx_ctx = (cb != NULL) ? ctx : NULL;
    (void)xSemaphoreGive(me->mutex);

    if (cb != NULL) {
        return ESP_OK;
    }

    return wifi_link_wait_runtime_actions_drained(me);
}

static esp_err_t wifi_link_cleanup_resources(wifi_link_t *me)
{
    esp_err_t first_error = ESP_OK;
    esp_err_t ret = ESP_OK;
    esp_mqtt_client_handle_t mqtt_client = NULL;
    esp_netif_t *netif = NULL;
    esp_event_handler_instance_t wifi_event_instance = NULL;
    esp_event_handler_instance_t ip_event_instance = NULL;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (me->runtime_action_count > 0) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    mqtt_client = me->mqtt_client;
    wifi_event_instance = me->wifi_event_instance;
    ip_event_instance = me->ip_event_instance;
    netif = me->netif;
    (void)xSemaphoreGive(me->mutex);

    if (mqtt_client != NULL) {
        ret = esp_mqtt_client_stop(mqtt_client);
        if (ret != ESP_OK) {
            if (ret != ESP_FAIL && first_error == ESP_OK) {
                first_error = ret;
            }
            ESP_LOGW(TAG, "stop mqtt client failed: %s", esp_err_to_name(ret));
        }
        if (ret == ESP_OK || ret == ESP_FAIL) {
            ret = esp_mqtt_client_destroy(mqtt_client);
            if (ret != ESP_OK) {
                if (first_error == ESP_OK) {
                    first_error = ret;
                }
                ESP_LOGW(TAG, "destroy mqtt client failed: %s",
                         esp_err_to_name(ret));
            } else {
                ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) ==
                                        pdTRUE,
                                    ESP_ERR_TIMEOUT, TAG, "take mutex failed");
                if (me->mqtt_client == mqtt_client) {
                    me->mqtt_client = NULL;
                }
                (void)xSemaphoreGive(me->mutex);
            }
        }
    }

    if (wifi_event_instance != NULL) {
        ret = esp_event_handler_instance_unregister(WIFI_EVENT,
                                                    ESP_EVENT_ANY_ID,
                                                    wifi_event_instance);
        if (ret != ESP_OK) {
            if (first_error == ESP_OK) {
                first_error = ret;
            }
            ESP_LOGW(TAG, "unregister wifi event handler failed: %s",
                     esp_err_to_name(ret));
        } else {
            ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) ==
                                    pdTRUE,
                                ESP_ERR_TIMEOUT, TAG, "take mutex failed");
            if (me->wifi_event_instance == wifi_event_instance) {
                me->wifi_event_instance = NULL;
            }
            (void)xSemaphoreGive(me->mutex);
        }
    }

    if (ip_event_instance != NULL) {
        ret = esp_event_handler_instance_unregister(IP_EVENT,
                                                    IP_EVENT_STA_GOT_IP,
                                                    ip_event_instance);
        if (ret != ESP_OK) {
            if (first_error == ESP_OK) {
                first_error = ret;
            }
            ESP_LOGW(TAG, "unregister ip event handler failed: %s",
                     esp_err_to_name(ret));
        } else {
            ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) ==
                                    pdTRUE,
                                ESP_ERR_TIMEOUT, TAG, "take mutex failed");
            if (me->ip_event_instance == ip_event_instance) {
                me->ip_event_instance = NULL;
            }
            (void)xSemaphoreGive(me->mutex);
        }
    }

    ret = esp_wifi_disconnect();
    if (!wifi_link_is_expected_wifi_cleanup_error(ret)) {
        if (first_error == ESP_OK) {
            first_error = ret;
        }
        ESP_LOGW(TAG, "disconnect wifi failed: %s", esp_err_to_name(ret));
    }

    ret = esp_wifi_stop();
    if (!wifi_link_is_expected_wifi_cleanup_error(ret)) {
        if (first_error == ESP_OK) {
            first_error = ret;
        }
        ESP_LOGW(TAG, "stop wifi failed: %s", esp_err_to_name(ret));
    }

    ret = esp_wifi_deinit();
    if (!wifi_link_is_expected_wifi_cleanup_error(ret)) {
        if (first_error == ESP_OK) {
            first_error = ret;
        }
        ESP_LOGW(TAG, "deinit wifi failed: %s", esp_err_to_name(ret));
    }

    if (netif != NULL && wifi_link_is_expected_wifi_cleanup_error(ret)) {
        esp_netif_destroy_default_wifi(netif);
        ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                            ESP_ERR_TIMEOUT, TAG, "take mutex failed");
        if (me->netif == netif) {
            me->netif = NULL;
        }
        (void)xSemaphoreGive(me->mutex);
    }

    return first_error;
}

static esp_err_t wifi_link_validate_config(const wifi_link_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "config is null");
    ESP_RETURN_ON_FALSE(config->ssid != NULL && config->ssid[0] != '\0',
                        ESP_ERR_INVALID_ARG, TAG, "ssid is empty");
    ESP_RETURN_ON_FALSE(strlen(config->ssid) <= WIFI_LINK_MAX_SSID_LEN,
                        ESP_ERR_INVALID_ARG, TAG, "ssid too long");
    ESP_RETURN_ON_FALSE(config->password == NULL ||
                            strlen(config->password) <=
                                WIFI_LINK_MAX_PASSWORD_LEN,
                        ESP_ERR_INVALID_ARG, TAG, "password too long");
    ESP_RETURN_ON_FALSE(config->mqtt_broker_host != NULL &&
                            config->mqtt_broker_host[0] != '\0',
                        ESP_ERR_INVALID_ARG, TAG, "broker host is empty");
    ESP_RETURN_ON_FALSE(config->mqtt_broker_port != 0U, ESP_ERR_INVALID_ARG,
                        TAG, "broker port is zero");
    return ESP_OK;
}

static esp_err_t wifi_link_copy_config(wifi_link_t *me,
                                       const wifi_link_config_t *config)
{
    ESP_RETURN_ON_FALSE(me != NULL && config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "invalid argument");

    me->sub_table_size = (config->max_subscriptions > 0) ?
                         config->max_subscriptions :
                         WIFI_LINK_DEFAULT_MAX_SUBSCRIPTIONS;
    me->max_topic_len = (config->max_topic_len > 0) ?
                        config->max_topic_len : WIFI_LINK_DEFAULT_MAX_TOPIC_LEN;

    me->ssid = wifi_link_strdup_or_null(config->ssid);
    me->password = wifi_link_strdup_or_null(
        (config->password != NULL) ? config->password : "");
    me->mqtt_broker_host = wifi_link_strdup_or_null(config->mqtt_broker_host);
    me->mqtt_client_id = wifi_link_strdup_or_null(config->mqtt_client_id);
    me->mqtt_username = wifi_link_strdup_or_null(config->mqtt_username);
    me->mqtt_password = wifi_link_strdup_or_null(config->mqtt_password);
    me->mqtt_uri = wifi_link_build_uri(config->mqtt_broker_host,
                                       config->mqtt_broker_port,
                                       config->mqtt_use_tls);

    if (me->ssid == NULL || me->password == NULL ||
        me->mqtt_broker_host == NULL || me->mqtt_uri == NULL ||
        (config->mqtt_client_id != NULL && me->mqtt_client_id == NULL) ||
        (config->mqtt_username != NULL && me->mqtt_username == NULL) ||
        (config->mqtt_password != NULL && me->mqtt_password == NULL)) {
        wifi_link_free_config(me);
        return ESP_ERR_NO_MEM;
    }

    me->config = *config;
    me->config.ssid = me->ssid;
    me->config.password = me->password;
    me->config.mqtt_broker_host = me->mqtt_broker_host;
    me->config.mqtt_client_id = me->mqtt_client_id;
    me->config.mqtt_username = me->mqtt_username;
    me->config.mqtt_password = me->mqtt_password;
    if (me->config.mqtt_keepalive_s == 0U) {
        me->config.mqtt_keepalive_s = WIFI_LINK_DEFAULT_KEEPALIVE_S;
    }
    me->config.max_subscriptions = me->sub_table_size;
    me->config.max_topic_len = me->max_topic_len;
    return ESP_OK;
}

static void wifi_link_free_config(wifi_link_t *me)
{
    if (me == NULL) {
        return;
    }

    free(me->ssid);
    free(me->password);
    free(me->mqtt_broker_host);
    free(me->mqtt_client_id);
    free(me->mqtt_username);
    free(me->mqtt_password);
    free(me->mqtt_uri);
    me->ssid = NULL;
    me->password = NULL;
    me->mqtt_broker_host = NULL;
    me->mqtt_client_id = NULL;
    me->mqtt_username = NULL;
    me->mqtt_password = NULL;
    me->mqtt_uri = NULL;
    memset(&me->config, 0, sizeof(me->config));
}

static bool wifi_link_is_valid_qos(network_mqtt_qos_t qos)
{
    return qos == NETWORK_MQTT_QOS0 || qos == NETWORK_MQTT_QOS1 ||
           qos == NETWORK_MQTT_QOS2;
}

static char *wifi_link_strdup_or_null(const char *value)
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

static bool wifi_link_is_raw_ipv6_literal(const char *host)
{
    return host != NULL && strchr(host, ':') != NULL &&
           strchr(host, '[') == NULL && strchr(host, ']') == NULL;
}

static char *wifi_link_build_uri(const char *host, uint16_t port, bool use_tls)
{
    const char *scheme = use_tls ? "mqtts" : "mqtt";
    const bool ipv6 = wifi_link_is_raw_ipv6_literal(host);
    int len = 0;

    if (ipv6) {
        len = snprintf(NULL, 0, "%s://[%s]:%u", scheme, host,
                       (unsigned int)port);
    } else {
        len = snprintf(NULL, 0, "%s://%s:%u", scheme, host,
                       (unsigned int)port);
    }
    if (len <= 0) {
        return NULL;
    }

    char *uri = malloc((size_t)len + 1U);
    if (uri == NULL) {
        return NULL;
    }
    if (ipv6) {
        (void)snprintf(uri, (size_t)len + 1U, "%s://[%s]:%u", scheme, host,
                       (unsigned int)port);
    } else {
        (void)snprintf(uri, (size_t)len + 1U, "%s://%s:%u", scheme, host,
                       (unsigned int)port);
    }
    return uri;
}

static bool wifi_link_begin_runtime_action_locked(wifi_link_t *me)
{
    if (me == NULL || !me->started || me->stopping || me->destroying) {
        return false;
    }

    me->runtime_action_count++;
    return true;
}

static void wifi_link_end_runtime_action(wifi_link_t *me)
{
    if (me == NULL || me->mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (me->runtime_action_count > 0) {
        me->runtime_action_count--;
    }
    (void)xSemaphoreGive(me->mutex);
}

static esp_err_t wifi_link_wait_runtime_actions_drained(wifi_link_t *me)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");

    while (true) {
        int runtime_action_count = 0;

        if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
        runtime_action_count = me->runtime_action_count;
        (void)xSemaphoreGive(me->mutex);

        if (runtime_action_count == 0) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(WIFI_LINK_RUNTIME_DRAIN_POLL_MS));
    }
}

static bool wifi_link_begin_mqtt_op_locked(wifi_link_t *me)
{
    if (me == NULL || me->mqtt_op_active) {
        return false;
    }
    if (!wifi_link_begin_runtime_action_locked(me)) {
        return false;
    }

    me->mqtt_op_active = true;
    return true;
}

static void wifi_link_end_mqtt_op(wifi_link_t *me)
{
    if (me == NULL || me->mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    me->mqtt_op_active = false;
    if (me->runtime_action_count > 0) {
        me->runtime_action_count--;
    }
    (void)xSemaphoreGive(me->mutex);
}

static network_link_status_t wifi_link_current_status_locked(
    const wifi_link_t *me)
{
    if (me->starting) {
        return NETWORK_LINK_STATUS_STARTING;
    }
    if (me->start_failed) {
        return NETWORK_LINK_STATUS_ERROR;
    }
    if (!me->started) {
        return NETWORK_LINK_STATUS_IDLE;
    }
    if (me->mqtt_connected) {
        return NETWORK_LINK_STATUS_READY;
    }
    if (me->wifi_connected) {
        return NETWORK_LINK_STATUS_DEGRADED;
    }
    return NETWORK_LINK_STATUS_CONNECTING;
}

static bool wifi_link_is_expected_wifi_cleanup_error(esp_err_t err)
{
    return err == ESP_OK || err == ESP_ERR_WIFI_NOT_INIT ||
           err == ESP_ERR_WIFI_NOT_STARTED || err == ESP_ERR_WIFI_NOT_CONNECT;
}

static esp_err_t wifi_link_find_subscription_locked(wifi_link_t *me,
                                                    const char *topic,
                                                    int *out_index,
                                                    int *out_free_index)
{
    ESP_RETURN_ON_FALSE(me != NULL && topic != NULL && out_index != NULL &&
                            out_free_index != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    *out_index = -1;
    *out_free_index = -1;
    for (int i = 0; i < me->sub_table_size; i++) {
        if (me->sub_table[i].in_use) {
            if (me->sub_table[i].topic != NULL &&
                strcmp(me->sub_table[i].topic, topic) == 0) {
                *out_index = i;
            }
            continue;
        }
        if (*out_free_index < 0) {
            *out_free_index = i;
        }
    }

    return ESP_OK;
}

static esp_err_t wifi_link_store_subscription_locked(wifi_link_t *me,
                                                     const char *topic,
                                                     network_mqtt_qos_t qos)
{
    int index = -1;
    int free_index = -1;
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "link is null");
    ESP_RETURN_ON_FALSE(topic != NULL && topic[0] != '\0', ESP_ERR_INVALID_ARG,
                        TAG, "topic is empty");
    ESP_RETURN_ON_FALSE(strlen(topic) < (size_t)me->max_topic_len,
                        ESP_ERR_INVALID_SIZE, TAG, "topic too long");
    ESP_RETURN_ON_FALSE(wifi_link_is_valid_qos(qos), ESP_ERR_INVALID_ARG, TAG,
                        "invalid qos");

    ret = wifi_link_find_subscription_locked(me, topic, &index, &free_index);
    if (ret != ESP_OK) {
        return ret;
    }
    if (index >= 0) {
        me->sub_table[index].qos = qos;
        return ESP_OK;
    }
    if (free_index < 0) {
        return ESP_ERR_NO_MEM;
    }

    me->sub_table[free_index].topic = wifi_link_strdup_or_null(topic);
    if (me->sub_table[free_index].topic == NULL) {
        return ESP_ERR_NO_MEM;
    }
    me->sub_table[free_index].qos = qos;
    me->sub_table[free_index].in_use = true;
    return ESP_OK;
}

static esp_err_t wifi_link_remove_subscription_locked(wifi_link_t *me,
                                                      const char *topic)
{
    int index = -1;
    int free_index = -1;
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "link is null");
    ESP_RETURN_ON_FALSE(topic != NULL && topic[0] != '\0', ESP_ERR_INVALID_ARG,
                        TAG, "topic is empty");

    ret = wifi_link_find_subscription_locked(me, topic, &index, &free_index);
    if (ret != ESP_OK) {
        return ret;
    }
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    free(me->sub_table[index].topic);
    me->sub_table[index].topic = NULL;
    me->sub_table[index].qos = NETWORK_MQTT_QOS0;
    me->sub_table[index].in_use = false;
    return ESP_OK;
}

static void wifi_link_wifi_event_handler(void *arg, esp_event_base_t base,
                                         int32_t event_id, void *event_data)
{
    wifi_link_t *me = (wifi_link_t *)arg;
    (void)base;
    (void)event_data;

    if (me == NULL) {
        return;
    }
    if (event_id == WIFI_EVENT_STA_START) {
        bool action_started = false;

        if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
            return;
        }
        action_started = wifi_link_begin_runtime_action_locked(me);
        (void)xSemaphoreGive(me->mutex);
        if (action_started) {
            (void)esp_wifi_connect();
            wifi_link_end_runtime_action(me);
        }
        return;
    }
    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        bool action_started = false;

        if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
            return;
        }
        me->wifi_connected = false;
        me->mqtt_connected = false;
        action_started = wifi_link_begin_runtime_action_locked(me);
        (void)xSemaphoreGive(me->mutex);
        if (action_started) {
            (void)esp_wifi_connect();
            wifi_link_end_runtime_action(me);
        }
    }
}

static void wifi_link_ip_event_handler(void *arg, esp_event_base_t base,
                                       int32_t event_id, void *event_data)
{
    wifi_link_t *me = (wifi_link_t *)arg;
    bool action_started = false;
    esp_err_t ret = ESP_OK;
    (void)base;
    (void)event_data;

    if (me == NULL || event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (me->started && !me->stopping && !me->destroying) {
        me->wifi_connected = true;
        me->mqtt_connected = false;
        if (me->mqtt_client == NULL) {
            me->start_failed = false;
            action_started = wifi_link_begin_runtime_action_locked(me);
        }
    }
    (void)xSemaphoreGive(me->mutex);

    if (action_started) {
        ret = wifi_link_start_mqtt(me);
        if (ret != ESP_OK && xSemaphoreTake(me->mutex, portMAX_DELAY) ==
                pdTRUE) {
            ESP_LOGW(TAG, "start mqtt failed: %s", esp_err_to_name(ret));
            if (me->started && !me->stopping && !me->destroying) {
                me->mqtt_connected = false;
                me->start_failed = true;
            }
            (void)xSemaphoreGive(me->mutex);
        }
        wifi_link_end_runtime_action(me);
    }
}

static esp_err_t wifi_link_start_mqtt(wifi_link_t *me)
{
    esp_err_t ret = ESP_OK;
    esp_err_t state_ret = ESP_OK;
    esp_mqtt_client_handle_t client = NULL;
    bool client_assigned = false;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (!me->started || me->stopping || me->destroying ||
        me->mqtt_client != NULL) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_OK;
    }
    (void)xSemaphoreGive(me->mutex);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = me->mqtt_uri,
        .credentials.client_id = me->mqtt_client_id,
        .credentials.username = me->mqtt_username,
        .credentials.authentication.password = me->mqtt_password,
        .session.keepalive = me->config.mqtt_keepalive_s,
        .session.disable_clean_session = !me->config.mqtt_clean_session,
    };

    if (me->config.mqtt_use_tls) {
        mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }

    client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == NULL) {
        return ESP_FAIL;
    }

    ret = esp_mqtt_client_register_event(client, MQTT_EVENT_ANY,
                                         wifi_link_mqtt_event_handler, me);
    if (ret != ESP_OK) {
        (void)esp_mqtt_client_destroy(client);
        return ret;
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        (void)esp_mqtt_client_destroy(client);
        return ESP_ERR_TIMEOUT;
    }
    if (me->started && !me->stopping && !me->destroying &&
        me->mqtt_client == NULL) {
        me->mqtt_client = client;
        client_assigned = true;
    } else if (!me->started || me->stopping || me->destroying) {
        state_ret = ESP_ERR_INVALID_STATE;
    }
    (void)xSemaphoreGive(me->mutex);

    if (!client_assigned) {
        (void)esp_mqtt_client_destroy(client);
        return state_ret;
    }

    ret = esp_mqtt_client_start(client);
    if (ret != ESP_OK) {
        if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
            if (me->mqtt_client == client) {
                me->mqtt_client = NULL;
                me->mqtt_connected = false;
            }
            (void)xSemaphoreGive(me->mutex);
        }
        (void)esp_mqtt_client_destroy(client);
        return ret;
    }

    return ESP_OK;
}

static void wifi_link_mqtt_event_handler(void *handler_args,
                                         esp_event_base_t base,
                                         int32_t event_id, void *event_data)
{
    wifi_link_t *me = (wifi_link_t *)handler_args;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = NULL;
    (void)base;

    if (me == NULL || event == NULL) {
        return;
    }
    client = event->client;
    if (client == NULL) {
        return;
    }

    if (event_id == MQTT_EVENT_CONNECTED) {
        bool action_started = false;
        esp_err_t replay_ret = ESP_OK;

        if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
            return;
        }
        if (me->started && !me->stopping && !me->destroying &&
            me->mqtt_client == client) {
            me->mqtt_connected = true;
            me->start_failed = false;
            action_started = wifi_link_begin_runtime_action_locked(me);
            if (!action_started) {
                me->mqtt_connected = false;
            }
        }
        (void)xSemaphoreGive(me->mutex);

        if (!action_started) {
            return;
        }

        replay_ret = wifi_link_replay_subscriptions(me, client);
        if (replay_ret != ESP_OK) {
            if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
                if (me->mqtt_client == client) {
                    me->mqtt_connected = false;
                }
                (void)xSemaphoreGive(me->mutex);
            }
        }
        wifi_link_end_runtime_action(me);
        return;
    }

    if (event_id == MQTT_EVENT_DISCONNECTED) {
        if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
            return;
        }
        if (me->mqtt_client == client) {
            me->mqtt_connected = false;
        }
        (void)xSemaphoreGive(me->mutex);
        return;
    }

    if (event_id == MQTT_EVENT_DATA) {
        network_rx_cb_t rx_cb = NULL;
        void *rx_ctx = NULL;
        bool action_started = false;

        if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
            return;
        }
        if (me->started && !me->stopping && !me->destroying &&
            me->mqtt_client == client && me->rx_cb != NULL) {
            action_started = wifi_link_begin_runtime_action_locked(me);
            if (action_started) {
                rx_cb = me->rx_cb;
                rx_ctx = me->rx_ctx;
            }
        }
        (void)xSemaphoreGive(me->mutex);

        if (action_started && rx_cb != NULL) {
            char *topic = NULL;
            const int topic_len = event->topic_len;

            if (event->topic == NULL || topic_len <= 0) {
                ESP_LOGW(TAG, "mqtt data event has invalid topic");
                wifi_link_end_runtime_action(me);
                return;
            }

            topic = malloc((size_t)topic_len + 1U);
            if (topic == NULL) {
                ESP_LOGW(TAG, "allocate mqtt topic copy failed");
                wifi_link_end_runtime_action(me);
                return;
            }
            memcpy(topic, event->topic, (size_t)topic_len);
            topic[topic_len] = '\0';

            const network_rx_data_t rx_data = {
                .topic = topic,
                .data = event->data,
                .data_len = event->data_len,
            };

            rx_cb(&rx_data, rx_ctx);
            free(topic);
            wifi_link_end_runtime_action(me);
        }
    }
}

static esp_err_t wifi_link_replay_subscriptions(wifi_link_t *me,
                                                esp_mqtt_client_handle_t client)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL && client != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "invalid argument");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (!wifi_link_begin_mqtt_op_locked(me)) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    (void)xSemaphoreGive(me->mutex);

    for (int i = 0;; i++) {
        char *topic = NULL;
        network_mqtt_qos_t qos = NETWORK_MQTT_QOS0;

        if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
            ret = ESP_ERR_TIMEOUT;
            break;
        }
        if (i >= me->sub_table_size) {
            (void)xSemaphoreGive(me->mutex);
            break;
        }
        if (me->sub_table[i].in_use && me->sub_table[i].topic != NULL) {
            topic = wifi_link_strdup_or_null(me->sub_table[i].topic);
            qos = me->sub_table[i].qos;
            if (topic == NULL) {
                (void)xSemaphoreGive(me->mutex);
                ret = ESP_ERR_NO_MEM;
                break;
            }
        }
        (void)xSemaphoreGive(me->mutex);

        if (topic == NULL) {
            continue;
        }

        const int msg_id = esp_mqtt_client_subscribe(client, topic, (int)qos);
        free(topic);
        if (msg_id < 0) {
            ret = ESP_FAIL;
            break;
        }
    }

    wifi_link_end_mqtt_op(me);
    return ret;
}
