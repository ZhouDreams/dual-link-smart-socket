/**
 * @file network_manager.c
 * @brief 双模网络管理器实现
 * @details Dual-mode network manager implementation
 * @author OpenCode
 * @date 2026-05-24
 */

/*********************
 *      INCLUDES
 *********************/

#include "network_manager.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "network_manager"

#define NETWORK_MANAGER_DEFAULT_RECHECK_MS        (5000U)
#define NETWORK_MANAGER_DEFAULT_FAILBACK_DELAY_MS (30000U)
#define NETWORK_MANAGER_DEFAULT_MAX_SUBS          (8)
#define NETWORK_MANAGER_MAX_TOPIC_LEN             (128)
#define NETWORK_MANAGER_TASK_NAME                 "net_mgr"
#define NETWORK_MANAGER_TASK_STACK                (4096)
#define NETWORK_MANAGER_TASK_PRIORITY             (4)
#define NETWORK_MANAGER_STOP_TIMEOUT_MS           (3000U)
#define NETWORK_MANAGER_STOP_POLL_MS              (100U)
#define NETWORK_MANAGER_RX_CB_POLL_MS             (10U)
#define NETWORK_MANAGER_RX_CB_TIMEOUT_MS          (5000U)

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 订阅意图表项
 * @details Subscription intent table entry
 */
typedef struct {
    char *topic;
    network_mqtt_qos_t qos;
    bool in_use;
} network_manager_sub_entry_t;

/**
 * @brief 链路接收桥接上下文
 * @details Link RX bridge context
 */
typedef struct {
    network_manager_t *manager;
    network_link_t *link;
} network_manager_rx_bridge_ctx_t;

/**
 * @brief 双模网络管理器对象
 * @details Dual-mode network manager object
 */
struct network_manager {
    network_link_t *primary;
    network_link_t *backup;
    network_link_t *active;
    network_link_type_t preferred_primary;
    uint32_t failover_recheck_ms;
    uint32_t failback_delay_ms;
    network_manager_sub_entry_t *sub_table;
    int sub_table_size;
    SemaphoreHandle_t mutex;
    network_rx_cb_t rx_cb;
    void *rx_ctx;
    int active_rx_callbacks;
    uint64_t failback_since_us;
    TaskHandle_t monitor_task;
    SemaphoreHandle_t monitor_task_done_sema;
    network_manager_rx_bridge_ctx_t primary_rx_ctx;
    network_manager_rx_bridge_ctx_t backup_rx_ctx;
    bool monitor_task_running;
    bool started;
    bool stop_pending;
    bool destroying;
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**
 * @brief 校验 MQTT QoS
 * @details Validate MQTT QoS
 * @param[in] qos MQTT 服务质量等级； MQTT quality of service level
 * @return true 有效，false 无效； true if valid, false otherwise
 */
static bool network_manager_is_valid_qos(network_mqtt_qos_t qos);

/**
 * @brief 判断链路状态是否可用于承载连接
 * @details Check whether link status is usable for connectivity
 * @param[in] status 链路状态； Link status
 * @return true 可用，false 不可用； true if usable, false otherwise
 */
static bool network_manager_status_is_usable(network_link_status_t status);

/**
 * @brief 校验管理器配置
 * @details Validate manager configuration
 * @param[in] config 管理器配置； Manager configuration
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t network_manager_validate_config(
    const network_manager_config_t *config);

/**
 * @brief 应用配置默认值
 * @details Apply configuration defaults
 * @param[in] config 输入配置； Input configuration
 * @param[out] me 管理器对象； Manager object
 */
static void network_manager_apply_config(const network_manager_config_t *config,
                                         network_manager_t *me);

/**
 * @brief 清理管理器创建过程中的资源
 * @details Cleanup resources allocated while creating manager
 * @param[in,out] me 管理器对象； Manager object
 * @param[in] clear_primary 是否清理主链路回调； Whether to clear primary callback
 * @param[in] clear_backup 是否清理备链路回调； Whether to clear backup callback
 * @return true 已释放资源，false 因回调清理失败而保留对象； true if resources were freed,
 *         false if retained after callback clear failure
 */
static bool network_manager_cleanup_create_failure(network_manager_t *me,
                                                   bool clear_primary,
                                                   bool clear_backup);

/**
 * @brief 清理链路接收回调
 * @details Clear link RX callback
 * @param[in] link 链路对象； Link object
 * @param[in] name 日志使用的链路名称； Link name for logging
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t network_manager_clear_link_rx_cb(network_link_t *link,
                                                  const char *name);

/**
 * @brief 释放管理器本地资源
 * @details Free manager-owned local resources
 * @param[in,out] me 管理器对象； Manager object
 */
static void network_manager_free_resources(network_manager_t *me);

/**
 * @brief 查找订阅意图
 * @details Find subscription intent
 * @param[in] me 管理器对象； Manager object
 * @param[in] topic MQTT 主题； MQTT topic
 * @param[out] out_index 已存在索引； Existing index
 * @param[out] out_free_index 首个空闲索引； First free index
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t network_manager_find_subscription_locked(
    network_manager_t *me, const char *topic, int *out_index,
    int *out_free_index);

/**
 * @brief 存储订阅意图
 * @details Store subscription intent
 * @param[in,out] me 管理器对象； Manager object
 * @param[in] topic MQTT 主题； MQTT topic
 * @param[in] qos MQTT 服务质量等级； MQTT QoS level
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t network_manager_store_subscription_locked(
    network_manager_t *me, const char *topic, network_mqtt_qos_t qos);

/**
 * @brief 移除订阅意图
 * @details Remove subscription intent
 * @param[in,out] me 管理器对象； Manager object
 * @param[in] topic MQTT 主题； MQTT topic
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t network_manager_remove_subscription_locked(
    network_manager_t *me, const char *topic);

/**
 * @brief 重放订阅意图
 * @details Replay subscription intents
 * @param[in,out] me 管理器对象； Manager object
 * @param[in] link 目标链路； Target link
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t network_manager_replay_subscriptions_locked(
    network_manager_t *me, network_link_t *link);

/**
 * @brief 启动监控任务
 * @details Start monitor task
 * @param[in,out] me 管理器对象； Manager object
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t network_manager_start_monitor_locked(network_manager_t *me);

/**
 * @brief 停止监控任务
 * @details Stop monitor task
 * @param[in,out] me 管理器对象； Manager object
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t network_manager_stop_monitor(network_manager_t *me);

/**
 * @brief 监控任务入口
 * @details Monitor task entry
 * @param[in] arg 管理器对象； Manager object
 */
static void network_manager_monitor_task(void *arg);

/**
 * @brief 执行一次链路监控
 * @details Execute one link monitor pass
 * @param[in,out] me 管理器对象； Manager object
 */
static void network_manager_monitor_once(network_manager_t *me);

/**
 * @brief 可中断延时
 * @details Delay while allowing monitor stop to complete promptly
 * @param[in,out] me 管理器对象； Manager object
 * @param[in] delay_ms 目标延时毫秒数； Target delay in milliseconds
 */
static void network_manager_monitor_delay(network_manager_t *me,
                                          uint32_t delay_ms);

/**
 * @brief 查询监控任务是否应继续运行
 * @details Query whether monitor task should continue running
 * @param[in,out] me 管理器对象； Manager object
 * @return true 继续运行，false 退出； true to continue, false to exit
 */
static bool network_manager_monitor_should_run(network_manager_t *me);

/**
 * @brief 查询链路状态，失败时返回 ERROR
 * @details Get link status, returning ERROR on failure
 * @param[in] link 链路对象； Link object
 * @return 链路状态； Link status
 */
static network_link_status_t network_manager_get_status_or_error(
    network_link_t *link);

/**
 * @brief 切换活动链路并重放订阅
 * @details Switch active link and replay subscriptions
 * @param[in,out] me 管理器对象； Manager object
 * @param[in] link 新活动链路； New active link
 */
static void network_manager_switch_active_locked(network_manager_t *me,
                                                 network_link_t *link);

/**
 * @brief 链路接收桥接回调
 * @details Link RX bridge callback
 * @param[in] rx_data 接收数据； RX data
 * @param[in] user_ctx 桥接上下文； Bridge context
 */
static void network_manager_on_link_rx(const network_rx_data_t *rx_data,
                                       void *user_ctx);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

network_manager_t *network_manager_create(
    const network_manager_config_t *config)
{
    bool primary_cb_registered = false;
    esp_err_t ret = ESP_OK;

    if (network_manager_validate_config(config) != ESP_OK) {
        return NULL;
    }

    network_manager_t *me = calloc(1, sizeof(*me));
    if (me == NULL) {
        ESP_LOGE(TAG, "calloc network manager failed");
        return NULL;
    }

    network_manager_apply_config(config, me);

    me->mutex = xSemaphoreCreateMutex();
    if (me->mutex == NULL) {
        ESP_LOGE(TAG, "create mutex failed");
        network_manager_cleanup_create_failure(me, false, false);
        return NULL;
    }

    me->monitor_task_done_sema = xSemaphoreCreateBinary();
    if (me->monitor_task_done_sema == NULL) {
        ESP_LOGE(TAG, "create monitor done semaphore failed");
        network_manager_cleanup_create_failure(me, false, false);
        return NULL;
    }

    me->sub_table = calloc((size_t)me->sub_table_size,
                           sizeof(me->sub_table[0]));
    if (me->sub_table == NULL) {
        ESP_LOGE(TAG, "calloc subscription table failed");
        network_manager_cleanup_create_failure(me, false, false);
        return NULL;
    }

    me->primary_rx_ctx.manager = me;
    me->primary_rx_ctx.link = me->primary;
    me->backup_rx_ctx.manager = me;
    me->backup_rx_ctx.link = me->backup;

    ret = network_link_register_rx_cb(me->primary, network_manager_on_link_rx,
                                      &me->primary_rx_ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register primary rx callback failed: %s",
                 esp_err_to_name(ret));
        network_manager_cleanup_create_failure(me, false, false);
        return NULL;
    }
    primary_cb_registered = true;

    if (me->backup != NULL) {
        ret = network_link_register_rx_cb(me->backup,
                                          network_manager_on_link_rx,
                                          &me->backup_rx_ctx);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "register backup rx callback failed: %s",
                     esp_err_to_name(ret));
            network_manager_cleanup_create_failure(me, primary_cb_registered,
                                                   false);
            return NULL;
        }
    }

    return me;
}

esp_err_t network_manager_destroy(network_manager_t *me)
{
    esp_err_t first_error = ESP_OK;
    esp_err_t ret = ESP_OK;

    if (me == NULL) {
        return ESP_OK;
    }

    if (me->mutex != NULL &&
        xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
        me->destroying = true;
        (void)xSemaphoreGive(me->mutex);
    }

    first_error = network_manager_stop(me);

    ret = network_manager_clear_link_rx_cb(me->primary, "primary");
    if (ret != ESP_OK && first_error == ESP_OK) {
        first_error = ret;
    }

    if (me->backup != NULL) {
        ret = network_manager_clear_link_rx_cb(me->backup, "backup");
        if (ret != ESP_OK && first_error == ESP_OK) {
            first_error = ret;
        }
    }

    if (first_error != ESP_OK) {
        return first_error;
    }

    network_manager_free_resources(me);
    return ESP_OK;
}

esp_err_t network_manager_start(network_manager_t *me)
{
    esp_err_t ret = ESP_OK;
    esp_err_t primary_ret = ESP_OK;
    network_link_t *selected = NULL;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "manager is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    if (me->started) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_OK;
    }
    if (me->stop_pending) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (me->destroying) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }

    primary_ret = network_link_start(me->primary);
    if (primary_ret == ESP_OK) {
        selected = me->primary;
    } else if (me->backup != NULL) {
        ret = network_link_start(me->backup);
        if (ret == ESP_OK) {
            selected = me->backup;
        }
    }

    if (selected == NULL) {
        (void)xSemaphoreGive(me->mutex);
        return primary_ret;
    }

    me->active = selected;
    me->failback_since_us = 0ULL;
    {
        esp_err_t engage_ret = network_link_set_active(selected, true);
        if (engage_ret != ESP_OK) {
            ESP_LOGW(TAG, "engage selected link failed: %s",
                     esp_err_to_name(engage_ret));
        }
    }

    /* Hot-standby: bring the non-selected link up too (best-effort). For LTE
     * this reaches the network-only DEGRADED standby state; MQTT is engaged
     * only when it becomes active via switch_active_locked. */
    network_link_t *other = (selected == me->primary) ? me->backup : me->primary;
    if (other != NULL) {
        esp_err_t other_ret = network_link_start(other);
        if (other_ret != ESP_OK) {
            ESP_LOGW(TAG, "best-effort standby link start failed: %s",
                     esp_err_to_name(other_ret));
        }
    }

    ret = network_manager_replay_subscriptions_locked(me, selected);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "replay subscriptions after start failed: %s",
                 esp_err_to_name(ret));
    }
    ret = network_manager_start_monitor_locked(me);
    if (ret != ESP_OK) {
        me->active = NULL;
        (void)xSemaphoreGive(me->mutex);
        if (other != NULL) {
            (void)network_link_stop(other);
        }
        (void)network_link_stop(selected);
        return ret;
    }

    me->started = true;
    (void)xSemaphoreGive(me->mutex);
    return ESP_OK;
}

esp_err_t network_manager_stop(network_manager_t *me)
{
    esp_err_t first_error = ESP_OK;
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "manager is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!me->started && !me->monitor_task_running && me->monitor_task == NULL &&
        !me->stop_pending) {
        me->active = NULL;
        me->failback_since_us = 0ULL;
        (void)xSemaphoreGive(me->mutex);
        return ESP_OK;
    }
    (void)xSemaphoreGive(me->mutex);

    ret = network_manager_stop_monitor(me);
    if (ret != ESP_OK) {
        return ret;
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    me->active = NULL;
    me->started = false;
    me->failback_since_us = 0ULL;
    me->stop_pending = true;
    (void)xSemaphoreGive(me->mutex);

    ret = network_link_stop(me->primary);
    if (ret != ESP_OK && first_error == ESP_OK) {
        first_error = ret;
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "stop primary link failed: %s", esp_err_to_name(ret));
    }

    if (me->backup != NULL) {
        ret = network_link_stop(me->backup);
        if (ret != ESP_OK && first_error == ESP_OK) {
            first_error = ret;
        }
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "stop backup link failed: %s",
                     esp_err_to_name(ret));
        }
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return (first_error != ESP_OK) ? first_error : ESP_ERR_TIMEOUT;
    }
    me->active = NULL;
    me->started = false;
    me->failback_since_us = 0ULL;
    me->stop_pending = (first_error != ESP_OK);
    (void)xSemaphoreGive(me->mutex);

    return first_error;
}

esp_err_t network_manager_get_status(network_manager_t *me,
                                      network_manager_status_t *out)
{
    esp_err_t first_error = ESP_OK;
    esp_err_t ret = ESP_OK;
    network_link_status_t primary_status = NETWORK_LINK_STATUS_ERROR;
    network_link_status_t backup_status = NETWORK_LINK_STATUS_IDLE;
    network_link_status_t active_status = NETWORK_LINK_STATUS_IDLE;
    network_link_t *active = NULL;

    ESP_RETURN_ON_FALSE(me != NULL && out != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "invalid argument");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    active = me->active;
    ret = network_link_get_status(me->primary, &primary_status);
    if (ret != ESP_OK) {
        first_error = ret;
        primary_status = NETWORK_LINK_STATUS_ERROR;
    }

    if (me->backup != NULL) {
        ret = network_link_get_status(me->backup, &backup_status);
        if (ret != ESP_OK) {
            if (first_error == ESP_OK) {
                first_error = ret;
            }
            backup_status = NETWORK_LINK_STATUS_ERROR;
        }
    }

    if (active == me->primary) {
        active_status = primary_status;
    } else if (active == me->backup) {
        active_status = backup_status;
    }

    out->active_link = (active != NULL) ? network_link_get_type(active) :
                       NETWORK_LINK_TYPE_NONE;
    out->ready = (active_status == NETWORK_LINK_STATUS_READY);
    out->primary_status = primary_status;
    out->backup_status = backup_status;

    (void)xSemaphoreGive(me->mutex);
    return first_error;
}

esp_err_t network_manager_is_ready(network_manager_t *me, bool *out)
{
    network_manager_status_t status = {0};

    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "ready output is null");

    esp_err_t ret = network_manager_get_status(me, &status);
    if (ret != ESP_OK) {
        return ret;
    }

    *out = status.ready;
    return ESP_OK;
}

esp_err_t network_manager_publish(network_manager_t *me,
                                   const network_publish_request_t *req)
{
    network_link_t *active = NULL;
    network_link_status_t status = NETWORK_LINK_STATUS_IDLE;
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "manager is null");
    ESP_RETURN_ON_FALSE(req != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "publish request is null");
    ESP_RETURN_ON_FALSE(req->topic != NULL && req->topic[0] != '\0',
                        ESP_ERR_INVALID_ARG, TAG, "topic is empty");
    ESP_RETURN_ON_FALSE(req->payload_len == 0U || req->payload != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "payload is null");
    ESP_RETURN_ON_FALSE(network_manager_is_valid_qos(req->qos),
                        ESP_ERR_INVALID_ARG, TAG, "invalid qos");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    active = me->active;
    (void)xSemaphoreGive(me->mutex);

    if (active == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ret = network_link_get_status(active, &status);
    if (ret != ESP_OK) {
        return ret;
    }
    if (status != NETWORK_LINK_STATUS_READY) {
        return ESP_ERR_INVALID_STATE;
    }

    return network_link_publish(active, req);
}

esp_err_t network_manager_subscribe(network_manager_t *me,
                                     const char *topic,
                                     network_mqtt_qos_t qos)
{
    network_link_t *active = NULL;
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "manager is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    ret = network_manager_store_subscription_locked(me, topic, qos);
    if (ret == ESP_OK) {
        active = me->active;
    }
    (void)xSemaphoreGive(me->mutex);

    if (ret != ESP_OK || active == NULL) {
        return ret;
    }

    return network_link_subscribe(active, topic, qos);
}

esp_err_t network_manager_unsubscribe(network_manager_t *me,
                                       const char *topic)
{
    network_link_t *primary = NULL;
    network_link_t *backup = NULL;
    esp_err_t first_error = ESP_OK;
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "manager is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    ret = network_manager_remove_subscription_locked(me, topic);
    if (ret == ESP_OK) {
        primary = me->primary;
        backup = me->backup;
    }
    (void)xSemaphoreGive(me->mutex);

    if (ret != ESP_OK) {
        return ret;
    }

    ret = network_link_unsubscribe(primary, topic);
    if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND) {
        first_error = ret;
    }

    if (backup != NULL) {
        ret = network_link_unsubscribe(backup, topic);
        if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND &&
            first_error == ESP_OK) {
            first_error = ret;
        }
    }

    return first_error;
}

esp_err_t network_manager_register_rx_cb(network_manager_t *me,
                                          network_rx_cb_t cb, void *ctx)
{
    uint32_t waited_ms = 0U;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "manager is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    if (cb == NULL) {
        me->rx_cb = NULL;
        me->rx_ctx = NULL;
        while (me->active_rx_callbacks > 0) {
            if (waited_ms >= NETWORK_MANAGER_RX_CB_TIMEOUT_MS) {
                (void)xSemaphoreGive(me->mutex);
                ESP_LOGE(TAG, "wait for RX callbacks timed out");
                return ESP_ERR_TIMEOUT;
            }
            (void)xSemaphoreGive(me->mutex);
            vTaskDelay(pdMS_TO_TICKS(NETWORK_MANAGER_RX_CB_POLL_MS));
            waited_ms += NETWORK_MANAGER_RX_CB_POLL_MS;
            ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                                ESP_ERR_TIMEOUT, TAG,
                                "take mutex failed");
        }
    } else {
        me->rx_cb = cb;
        me->rx_ctx = ctx;
    }

    (void)xSemaphoreGive(me->mutex);
    return ESP_OK;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static bool network_manager_is_valid_qos(network_mqtt_qos_t qos)
{
    return qos == NETWORK_MQTT_QOS0 || qos == NETWORK_MQTT_QOS1 ||
           qos == NETWORK_MQTT_QOS2;
}

static bool network_manager_status_is_usable(network_link_status_t status)
{
    return status == NETWORK_LINK_STATUS_READY ||
           status == NETWORK_LINK_STATUS_DEGRADED;
}

static esp_err_t network_manager_validate_config(
    const network_manager_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "config is null");
    ESP_RETURN_ON_FALSE(config->primary != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "primary is null");
    ESP_RETURN_ON_FALSE(config->backup == NULL ||
                            config->backup != config->primary,
                        ESP_ERR_INVALID_ARG, TAG,
                        "backup duplicates primary");

    if (config->preferred_primary != NETWORK_LINK_TYPE_NONE) {
        const network_link_type_t primary_type = network_link_get_type(
            config->primary);
        const network_link_type_t backup_type = (config->backup != NULL) ?
                                                network_link_get_type(
                                                    config->backup) :
                                                NETWORK_LINK_TYPE_NONE;
        ESP_RETURN_ON_FALSE(config->preferred_primary == primary_type ||
                                (config->backup != NULL &&
                                 config->preferred_primary == backup_type),
                            ESP_ERR_INVALID_ARG, TAG,
                            "preferred primary does not match links");
    }

    return ESP_OK;
}

static void network_manager_apply_config(const network_manager_config_t *config,
                                         network_manager_t *me)
{
    if (config == NULL || me == NULL) {
        return;
    }

    me->primary = config->primary;
    me->backup = config->backup;
    me->preferred_primary = (config->preferred_primary ==
                             NETWORK_LINK_TYPE_NONE) ?
                            network_link_get_type(config->primary) :
                            config->preferred_primary;
    me->failover_recheck_ms = (config->failover_recheck_ms == 0U) ?
                              NETWORK_MANAGER_DEFAULT_RECHECK_MS :
                              config->failover_recheck_ms;
    me->failback_delay_ms = (config->failback_delay_ms == 0U) ?
                            NETWORK_MANAGER_DEFAULT_FAILBACK_DELAY_MS :
                            config->failback_delay_ms;
    me->sub_table_size = (config->max_subscriptions <= 0) ?
                         NETWORK_MANAGER_DEFAULT_MAX_SUBS :
                         config->max_subscriptions;
}

static bool network_manager_cleanup_create_failure(network_manager_t *me,
                                                   bool clear_primary,
                                                   bool clear_backup)
{
    esp_err_t first_error = ESP_OK;

    if (me == NULL) {
        return true;
    }

    me->destroying = true;

    if (clear_backup && me->backup != NULL) {
        const esp_err_t ret = network_manager_clear_link_rx_cb(me->backup,
                                                               "backup");
        if (ret != ESP_OK && first_error == ESP_OK) {
            first_error = ret;
        }
    }
    if (clear_primary && me->primary != NULL) {
        const esp_err_t ret = network_manager_clear_link_rx_cb(me->primary,
                                                               "primary");
        if (ret != ESP_OK && first_error == ESP_OK) {
            first_error = ret;
        }
    }

    if (first_error != ESP_OK) {
        ESP_LOGE(TAG,
                 "retain manager after create failure to keep rx bridge safe");
        return false;
    }

    network_manager_free_resources(me);
    return true;
}

static esp_err_t network_manager_clear_link_rx_cb(network_link_t *link,
                                                  const char *name)
{
    ESP_RETURN_ON_FALSE(link != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "link is null");

    const esp_err_t ret = network_link_register_rx_cb(link, NULL, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "clear %s rx callback failed: %s",
                 (name != NULL) ? name : "link", esp_err_to_name(ret));
    }
    return ret;
}

static void network_manager_free_resources(network_manager_t *me)
{
    if (me == NULL) {
        return;
    }

    if (me->sub_table != NULL) {
        for (int i = 0; i < me->sub_table_size; i++) {
            free(me->sub_table[i].topic);
            me->sub_table[i].topic = NULL;
            me->sub_table[i].in_use = false;
        }
        free(me->sub_table);
        me->sub_table = NULL;
    }
    if (me->monitor_task_done_sema != NULL) {
        vSemaphoreDelete(me->monitor_task_done_sema);
        me->monitor_task_done_sema = NULL;
    }
    if (me->mutex != NULL) {
        vSemaphoreDelete(me->mutex);
        me->mutex = NULL;
    }
    free(me);
}

static esp_err_t network_manager_find_subscription_locked(
    network_manager_t *me, const char *topic, int *out_index,
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

static esp_err_t network_manager_store_subscription_locked(
    network_manager_t *me, const char *topic, network_mqtt_qos_t qos)
{
    int index = -1;
    int free_index = -1;
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "manager is null");
    ESP_RETURN_ON_FALSE(topic != NULL && topic[0] != '\0',
                        ESP_ERR_INVALID_ARG, TAG, "topic is empty");
    ESP_RETURN_ON_FALSE(network_manager_is_valid_qos(qos),
                        ESP_ERR_INVALID_ARG, TAG, "invalid qos");
    ESP_RETURN_ON_FALSE(strlen(topic) < NETWORK_MANAGER_MAX_TOPIC_LEN,
                        ESP_ERR_INVALID_SIZE, TAG, "topic too long");

    ret = network_manager_find_subscription_locked(me, topic, &index,
                                                   &free_index);
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

    const size_t topic_len = strlen(topic);
    char *topic_copy = malloc(topic_len + 1U);
    if (topic_copy == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(topic_copy, topic, topic_len + 1U);

    me->sub_table[free_index].topic = topic_copy;
    me->sub_table[free_index].qos = qos;
    me->sub_table[free_index].in_use = true;
    return ESP_OK;
}

static esp_err_t network_manager_remove_subscription_locked(
    network_manager_t *me, const char *topic)
{
    int index = -1;
    int free_index = -1;
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "manager is null");
    ESP_RETURN_ON_FALSE(topic != NULL && topic[0] != '\0',
                        ESP_ERR_INVALID_ARG, TAG, "topic is empty");

    ret = network_manager_find_subscription_locked(me, topic, &index,
                                                   &free_index);
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

static esp_err_t network_manager_replay_subscriptions_locked(
    network_manager_t *me, network_link_t *link)
{
    ESP_RETURN_ON_FALSE(me != NULL && link != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "invalid argument");

    for (int i = 0; i < me->sub_table_size; i++) {
        if (!me->sub_table[i].in_use || me->sub_table[i].topic == NULL) {
            continue;
        }

        esp_err_t ret = network_link_subscribe(link, me->sub_table[i].topic,
                                               me->sub_table[i].qos);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return ESP_OK;
}

static esp_err_t network_manager_start_monitor_locked(network_manager_t *me)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "manager is null");
    ESP_RETURN_ON_FALSE(me->monitor_task_done_sema != NULL,
                        ESP_ERR_INVALID_STATE, TAG,
                        "monitor done semaphore is null");

    while (xSemaphoreTake(me->monitor_task_done_sema, 0) == pdTRUE) {
    }

    me->monitor_task_running = true;
    const BaseType_t task_ret = xTaskCreate(network_manager_monitor_task,
                                            NETWORK_MANAGER_TASK_NAME,
                                            NETWORK_MANAGER_TASK_STACK, me,
                                            NETWORK_MANAGER_TASK_PRIORITY,
                                            &me->monitor_task);
    if (task_ret != pdPASS) {
        me->monitor_task_running = false;
        me->monitor_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t network_manager_stop_monitor(network_manager_t *me)
{
    bool wait_for_task = false;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "manager is null");
    ESP_RETURN_ON_FALSE(me->monitor_task_done_sema != NULL,
                        ESP_ERR_INVALID_STATE, TAG,
                        "monitor done semaphore is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    wait_for_task = me->monitor_task_running || me->monitor_task != NULL;
    me->monitor_task_running = false;
    (void)xSemaphoreGive(me->mutex);

    if (!wait_for_task) {
        return ESP_OK;
    }

    if (xSemaphoreTake(me->monitor_task_done_sema,
                       pdMS_TO_TICKS(NETWORK_MANAGER_STOP_TIMEOUT_MS)) !=
        pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void network_manager_monitor_task(void *arg)
{
    network_manager_t *me = (network_manager_t *)arg;

    if (me == NULL) {
        vTaskDelete(NULL);
        return;
    }

    while (network_manager_monitor_should_run(me)) {
        network_manager_monitor_once(me);
        network_manager_monitor_delay(me, me->failover_recheck_ms);
    }

    if (me->mutex != NULL &&
        xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
        me->monitor_task_running = false;
        me->monitor_task = NULL;
        (void)xSemaphoreGive(me->mutex);
    }
    if (me->monitor_task_done_sema != NULL) {
        (void)xSemaphoreGive(me->monitor_task_done_sema);
    }

    vTaskDelete(NULL);
}

static void network_manager_monitor_once(network_manager_t *me)
{
    network_link_t *primary = NULL;
    network_link_t *backup = NULL;
    network_link_t *active = NULL;
    network_link_status_t primary_status = NETWORK_LINK_STATUS_ERROR;
    network_link_status_t backup_status = NETWORK_LINK_STATUS_IDLE;
    bool should_start_backup = false;
    bool should_start_primary = false;
    const uint64_t now_us = (uint64_t)esp_timer_get_time();

    if (me == NULL || me->mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    primary = me->primary;
    backup = me->backup;
    active = me->active;
    (void)xSemaphoreGive(me->mutex);

    primary_status = network_manager_get_status_or_error(primary);
    if (backup != NULL) {
        backup_status = network_manager_get_status_or_error(backup);
    }

    should_start_backup = active == primary && backup != NULL &&
                          !network_manager_status_is_usable(primary_status);
    if (should_start_backup) {
        const esp_err_t start_ret = network_link_start(backup);
        if (start_ret != ESP_OK) {
            ESP_LOGW(TAG, "best-effort backup start failed: %s",
                     esp_err_to_name(start_ret));
        }
        backup_status = network_manager_get_status_or_error(backup);
    }

    should_start_primary = active == backup &&
                           primary_status != NETWORK_LINK_STATUS_READY;
    if (should_start_primary) {
        const esp_err_t start_ret = network_link_start(primary);
        if (start_ret != ESP_OK) {
            ESP_LOGW(TAG, "best-effort primary start failed: %s",
                     esp_err_to_name(start_ret));
        }
        primary_status = network_manager_get_status_or_error(primary);
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (!me->monitor_task_running) {
        (void)xSemaphoreGive(me->mutex);
        return;
    }

    active = me->active;
    if (active == primary) {
        if (!network_manager_status_is_usable(primary_status) &&
            backup != NULL &&
            network_manager_status_is_usable(backup_status)) {
            network_manager_switch_active_locked(me, backup);
        } else if (primary_status == NETWORK_LINK_STATUS_READY) {
            me->failback_since_us = 0ULL;
        }
        (void)xSemaphoreGive(me->mutex);
        return;
    }

    if (active == backup) {
        if (primary_status == NETWORK_LINK_STATUS_READY) {
            if (me->failback_since_us == 0ULL) {
                me->failback_since_us = now_us;
            } else if ((now_us - me->failback_since_us) >=
                       ((uint64_t)me->failback_delay_ms * 1000ULL)) {
                network_manager_switch_active_locked(me, primary);
            }
        } else {
            me->failback_since_us = 0ULL;
        }
    }

    (void)xSemaphoreGive(me->mutex);
}

static void network_manager_monitor_delay(network_manager_t *me,
                                          uint32_t delay_ms)
{
    uint32_t elapsed_ms = 0U;

    while (elapsed_ms < delay_ms && network_manager_monitor_should_run(me)) {
        uint32_t chunk_ms = delay_ms - elapsed_ms;
        if (chunk_ms > NETWORK_MANAGER_STOP_POLL_MS) {
            chunk_ms = NETWORK_MANAGER_STOP_POLL_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(chunk_ms));
        elapsed_ms += chunk_ms;
    }
}

static bool network_manager_monitor_should_run(network_manager_t *me)
{
    bool running = false;

    if (me == NULL || me->mutex == NULL) {
        return false;
    }
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    running = me->monitor_task_running;
    (void)xSemaphoreGive(me->mutex);
    return running;
}

static network_link_status_t network_manager_get_status_or_error(
    network_link_t *link)
{
    network_link_status_t status = NETWORK_LINK_STATUS_ERROR;

    if (link == NULL) {
        return NETWORK_LINK_STATUS_IDLE;
    }
    if (network_link_get_status(link, &status) != ESP_OK) {
        return NETWORK_LINK_STATUS_ERROR;
    }
    return status;
}

static void network_manager_switch_active_locked(network_manager_t *me,
                                                 network_link_t *link)
{
    esp_err_t ret = ESP_OK;
    network_link_t *old = NULL;

    if (me == NULL || link == NULL || me->active == link) {
        return;
    }

    old = me->active;
    me->active = link;
    me->failback_since_us = 0ULL;

    /* Engage the new active link (LTE: brings up MQTT -> aims for READY) and
     * disengage the old (LTE: stops MQTT, keeps network -> DEGRADED standby).
     * Wi-Fi set_active is a no-op. */
    {
        esp_err_t engage_ret = network_link_set_active(link, true);
        if (engage_ret != ESP_OK) {
            ESP_LOGW(TAG, "engage new active link failed: %s",
                     esp_err_to_name(engage_ret));
        }
    }
    if (old != NULL) {
        {
            esp_err_t engage_ret = network_link_set_active(old, false);
            if (engage_ret != ESP_OK) {
                ESP_LOGW(TAG, "disengage old active link failed: %s",
                         esp_err_to_name(engage_ret));
            }
        }
    }

    ret = network_manager_replay_subscriptions_locked(me, link);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "replay subscriptions after switch failed: %s",
                 esp_err_to_name(ret));
    }
}

static void network_manager_on_link_rx(const network_rx_data_t *rx_data,
                                       void *user_ctx)
{
    network_manager_rx_bridge_ctx_t *bridge_ctx =
        (network_manager_rx_bridge_ctx_t *)user_ctx;
    network_manager_t *me = NULL;
    network_link_t *source_link = NULL;
    network_link_t *active = NULL;
    network_rx_cb_t rx_cb = NULL;
    void *rx_ctx = NULL;

    if (bridge_ctx == NULL || bridge_ctx->manager == NULL) {
        return;
    }

    me = bridge_ctx->manager;
    source_link = bridge_ctx->link;
    if (me->mutex == NULL || xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    active = me->active;
    rx_cb = me->rx_cb;
    rx_ctx = me->rx_ctx;
    if (me->destroying) {
        rx_cb = NULL;
    }
    if (source_link == active && rx_cb != NULL) {
        me->active_rx_callbacks++;
    }
    (void)xSemaphoreGive(me->mutex);

    if (source_link == active && rx_cb != NULL) {
        rx_cb(rx_data, rx_ctx);
        if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
            me->active_rx_callbacks--;
            (void)xSemaphoreGive(me->mutex);
        }
    }
}
