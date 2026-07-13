/**
 * @file thingsboard_client.c
 * @brief ThingsBoard 客户端公共实现
 * @details ThingsBoard client public implementation
 * @author OpenCode
 * @date 2026-05-28
 */

/*********************
 *      INCLUDES
 *********************/

#include "thingsboard_client.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "network_types.h"
#include "thingsboard_client_internal.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "thingsboard_client"
#define TB_DEFAULT_JSON_BUF_SIZE (512)
#define TB_STOP_POLL_MS          (10U)
#define TB_STOP_TIMEOUT_MS       (5000U)
#define TB_COMMAND_DRAIN_POLL_MS (10U)
#define TB_COMMAND_DRAIN_TIMEOUT_MS (5000U)

/**********************
 *      TYPEDEFS
 **********************/

struct thingsboard_client {
    tb_client_config_t config;
    SemaphoreHandle_t mutex;
    char *json_buf;
    int json_buf_size;
    tb_command_cb_t cmd_cb;
    void *cmd_ctx;
    uint32_t active_cmd_callbacks;
    bool started;
    bool stopping;
    bool destroying;
    bool network_rx_registered;
    bool cleanup_pending;
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**
 * @brief 校验客户端配置
 * @details Validate client configuration
 * @param[in] config 客户端配置； Client configuration
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 */
static esp_err_t thingsboard_client_validate_config(
    const tb_client_config_t *config);

/**
 * @brief 解析 JSON 缓冲区大小
 * @details Resolve JSON buffer size
 * @param[in] config 客户端配置； Client configuration
 * @return JSON 缓冲区大小； JSON buffer size
 */
static int thingsboard_client_resolve_json_buf_size(
    const tb_client_config_t *config);

/**
 * @brief 处理网络下行数据
 * @details Handle network RX data
 * @param[in] rx_data 下行数据； RX data
 * @param[in] user_ctx 用户上下文； User context
 */
static void thingsboard_client_on_rx(const network_rx_data_t *rx_data,
                                     void *user_ctx);

/**
 * @brief 发布 JSON 数据
 * @details Publish JSON data
 * @param[in] net_mgr 网络管理器； Network manager
 * @param[in] topic MQTT 主题； MQTT topic
 * @param[in] json JSON 载荷； JSON payload
 * @param[in] json_len JSON 载荷长度； JSON payload length
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 */
static esp_err_t thingsboard_client_publish_json(network_manager_t *net_mgr,
                                                 const char *topic,
                                                 const char *json,
                                                 size_t json_len);

/**
 * @brief 复制内部命令到公共命令
 * @details Copy internal command to public command
 * @param[in] internal_cmd 内部命令； Internal command
 * @param[out] out_cmd 公共命令输出； Public command output
 */
static void thingsboard_client_copy_command(
    const tb_internal_command_t *internal_cmd, tb_command_t *out_cmd);

/**
 * @brief 等待命令回调退出
 * @details Wait for command callbacks to drain
 * @param[in] me 客户端句柄； Client handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_TIMEOUT: 获取互斥锁超时； Mutex timeout
 */
static esp_err_t thingsboard_client_wait_command_callbacks_drained(
    thingsboard_client_t *me);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

thingsboard_client_t *thingsboard_client_create(const tb_client_config_t *config)
{
    thingsboard_client_t *me;
    int json_buf_size;

    if (thingsboard_client_validate_config(config) != ESP_OK) {
        return NULL;
    }

    me = calloc(1, sizeof(*me));
    if (me == NULL) {
        return NULL;
    }

    me->config = *config;
    json_buf_size = thingsboard_client_resolve_json_buf_size(config);
    me->json_buf_size = json_buf_size;

    me->json_buf = calloc((size_t)json_buf_size, 1U);
    if (me->json_buf == NULL) {
        free(me);
        return NULL;
    }

    me->mutex = xSemaphoreCreateMutex();
    if (me->mutex == NULL) {
        free(me->json_buf);
        free(me);
        return NULL;
    }

    return me;
}

esp_err_t thingsboard_client_destroy(thingsboard_client_t *me)
{
    esp_err_t ret = ESP_OK;

    if (me == NULL) {
        return ESP_OK;
    }

    if (me->mutex != NULL) {
        if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
        me->destroying = true;
        (void)xSemaphoreGive(me->mutex);
    }

    ret = thingsboard_client_stop(me);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = thingsboard_client_register_command_cb(me, NULL, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    if (me->mutex != NULL) {
        vSemaphoreDelete(me->mutex);
    }
    free(me->json_buf);
    free(me);

    return ESP_OK;
}

esp_err_t thingsboard_client_start(thingsboard_client_t *me)
{
    esp_err_t ret;
    esp_err_t cleanup_ret;
    bool subscribed_rpc = false;
    bool subscribed_attributes = false;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "client is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (me->started) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_OK;
    }
    if (me->destroying || me->stopping || me->cleanup_pending) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (me->config.enable_rpc) {
        ret = network_manager_subscribe(me->config.net_mgr,
                                        TB_TOPIC_RPC_REQUEST_SUB,
                                        NETWORK_MQTT_QOS0);
        if (ret != ESP_OK) {
            (void)xSemaphoreGive(me->mutex);
            return ret;
        }
        subscribed_rpc = true;
    }

    if (me->config.enable_attributes) {
        ret = network_manager_subscribe(me->config.net_mgr, TB_TOPIC_ATTRIBUTES,
                                        NETWORK_MQTT_QOS0);
        if (ret != ESP_OK) {
            if (subscribed_rpc) {
                cleanup_ret = network_manager_unsubscribe(
                    me->config.net_mgr, TB_TOPIC_RPC_REQUEST_SUB);
                if (cleanup_ret != ESP_OK && cleanup_ret != ESP_ERR_NOT_FOUND) {
                    me->cleanup_pending = true;
                }
            }
            (void)xSemaphoreGive(me->mutex);
            return ret;
        }
        subscribed_attributes = true;
    }

    ret = network_manager_register_rx_cb(me->config.net_mgr,
                                         thingsboard_client_on_rx, me);
    if (ret != ESP_OK) {
        if (subscribed_attributes) {
            cleanup_ret = network_manager_unsubscribe(me->config.net_mgr,
                                                       TB_TOPIC_ATTRIBUTES);
            if (cleanup_ret != ESP_OK && cleanup_ret != ESP_ERR_NOT_FOUND) {
                me->cleanup_pending = true;
            }
        }
        if (subscribed_rpc) {
            cleanup_ret = network_manager_unsubscribe(
                me->config.net_mgr, TB_TOPIC_RPC_REQUEST_SUB);
            if (cleanup_ret != ESP_OK && cleanup_ret != ESP_ERR_NOT_FOUND) {
                me->cleanup_pending = true;
            }
        }
        (void)xSemaphoreGive(me->mutex);
        return ret;
    }
    me->network_rx_registered = true;

    if (me->destroying || me->stopping) {
        ret = network_manager_register_rx_cb(me->config.net_mgr, NULL, NULL);
        if (ret == ESP_OK || ret == ESP_ERR_NOT_FOUND) {
            me->network_rx_registered = false;
        } else {
            me->cleanup_pending = true;
        }
        if (subscribed_attributes) {
            cleanup_ret = network_manager_unsubscribe(me->config.net_mgr,
                                                       TB_TOPIC_ATTRIBUTES);
            if (cleanup_ret != ESP_OK && cleanup_ret != ESP_ERR_NOT_FOUND) {
                me->cleanup_pending = true;
            }
        }
        if (subscribed_rpc) {
            cleanup_ret = network_manager_unsubscribe(
                me->config.net_mgr, TB_TOPIC_RPC_REQUEST_SUB);
            if (cleanup_ret != ESP_OK && cleanup_ret != ESP_ERR_NOT_FOUND) {
                me->cleanup_pending = true;
            }
        }
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    me->started = true;
    me->cleanup_pending = false;
    (void)xSemaphoreGive(me->mutex);

    return ESP_OK;
}

esp_err_t thingsboard_client_stop(thingsboard_client_t *me)
{
    esp_err_t ret;
    esp_err_t first_error = ESP_OK;
    network_manager_t *net_mgr;
    bool enable_rpc;
    bool enable_attributes;
    bool network_rx_registered;
    bool network_rx_cleared = false;
    uint32_t waited_ms = 0U;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "client is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    while (!me->started && me->stopping) {
        if (waited_ms >= TB_STOP_TIMEOUT_MS) {
            (void)xSemaphoreGive(me->mutex);
            ESP_LOGE(TAG, "wait for concurrent stop timed out");
            return ESP_ERR_TIMEOUT;
        }
        (void)xSemaphoreGive(me->mutex);
        vTaskDelay(pdMS_TO_TICKS(TB_STOP_POLL_MS));
        waited_ms += TB_STOP_POLL_MS;
        ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                            ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    }
    if (!me->started && !me->network_rx_registered && !me->cleanup_pending) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_OK;
    }
    me->stopping = true;
    me->started = false;
    me->cleanup_pending = true;
    net_mgr = me->config.net_mgr;
    enable_rpc = me->config.enable_rpc;
    enable_attributes = me->config.enable_attributes;
    network_rx_registered = me->network_rx_registered;
    (void)xSemaphoreGive(me->mutex);

    if (network_rx_registered) {
        ret = network_manager_register_rx_cb(net_mgr, NULL, NULL);
        if (ret == ESP_OK || ret == ESP_ERR_NOT_FOUND) {
            network_rx_cleared = true;
        } else {
            first_error = ret;
        }
    }

    if (enable_rpc) {
        ret = network_manager_unsubscribe(net_mgr, TB_TOPIC_RPC_REQUEST_SUB);
        if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND &&
            first_error == ESP_OK) {
            first_error = ret;
        }
    }

    if (enable_attributes) {
        ret = network_manager_unsubscribe(net_mgr, TB_TOPIC_ATTRIBUTES);
        if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND &&
            first_error == ESP_OK) {
            first_error = ret;
        }
    }

    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (network_rx_cleared) {
        me->network_rx_registered = false;
    }
    if (first_error == ESP_OK) {
        me->cleanup_pending = false;
    }
    me->stopping = false;
    (void)xSemaphoreGive(me->mutex);

    return first_error;
}

esp_err_t thingsboard_client_publish_telemetry(thingsboard_client_t *me,
                                               const tb_telemetry_input_t *input)
{
    esp_err_t ret;
    size_t json_len = 0;
    char *json_copy;
    network_manager_t *net_mgr;
    tb_internal_telemetry_t internal_input;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "client is null");
    ESP_RETURN_ON_FALSE(input != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "telemetry input is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");

    internal_input.voltage = input->voltage;
    internal_input.current = input->current;
    internal_input.power = input->power;
    internal_input.energy_delta = input->energy_delta;
    internal_input.frequency = input->frequency;
    internal_input.relay_on = input->relay_on;
    internal_input.active_link = input->active_link;
    internal_input.safety_level = input->safety_level;
    internal_input.valid = input->valid;

    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (me->destroying) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    ret = tb_internal_format_telemetry(me->json_buf, (size_t)me->json_buf_size,
                                       &internal_input, &json_len);
    if (ret != ESP_OK) {
        (void)xSemaphoreGive(me->mutex);
        return ret;
    }
    json_copy = calloc(json_len + 1U, 1U);
    if (json_copy == NULL) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_NO_MEM;
    }
    memcpy(json_copy, me->json_buf, json_len);
    net_mgr = me->config.net_mgr;
    (void)xSemaphoreGive(me->mutex);

    ret = thingsboard_client_publish_json(net_mgr, TB_TOPIC_TELEMETRY,
                                          json_copy, json_len);
    free(json_copy);

    return ret;
}

esp_err_t thingsboard_client_report_relay_state(thingsboard_client_t *me,
                                                bool on)
{
    esp_err_t ret;
    size_t json_len = 0;
    char *json_copy;
    network_manager_t *net_mgr;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "client is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (me->destroying) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    ret = tb_internal_format_relay_attribute(me->json_buf,
                                             (size_t)me->json_buf_size, on,
                                             &json_len);
    if (ret != ESP_OK) {
        (void)xSemaphoreGive(me->mutex);
        return ret;
    }
    json_copy = calloc(json_len + 1U, 1U);
    if (json_copy == NULL) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_NO_MEM;
    }
    memcpy(json_copy, me->json_buf, json_len);
    net_mgr = me->config.net_mgr;
    (void)xSemaphoreGive(me->mutex);

    ret = thingsboard_client_publish_json(net_mgr, TB_TOPIC_ATTRIBUTES,
                                          json_copy, json_len);
    free(json_copy);

    return ret;
}

esp_err_t thingsboard_client_report_power_limit(thingsboard_client_t *me,
                                                float power_limit_w)
{
    esp_err_t ret;
    size_t json_len = 0;
    char *json_copy;
    network_manager_t *net_mgr;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "client is null");
    ESP_RETURN_ON_FALSE(power_limit_w > 0.0f, ESP_ERR_INVALID_ARG, TAG,
                        "power limit must be positive");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (me->destroying) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    ret = tb_internal_format_power_limit_attribute(me->json_buf,
                                                   (size_t)me->json_buf_size,
                                                   power_limit_w, &json_len);
    if (ret != ESP_OK) {
        (void)xSemaphoreGive(me->mutex);
        return ret;
    }
    json_copy = calloc(json_len + 1U, 1U);
    if (json_copy == NULL) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_NO_MEM;
    }
    memcpy(json_copy, me->json_buf, json_len);
    net_mgr = me->config.net_mgr;
    (void)xSemaphoreGive(me->mutex);

    ret = thingsboard_client_publish_json(net_mgr, TB_TOPIC_ATTRIBUTES,
                                          json_copy, json_len);
    free(json_copy);

    return ret;
}

esp_err_t thingsboard_client_send_rpc_response(thingsboard_client_t *me,
                                               int32_t request_id,
                                               const char *json,
                                               size_t json_len)
{
    char topic[96];
    esp_err_t ret;
    network_manager_t *net_mgr;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "client is null");
    ESP_RETURN_ON_FALSE(json_len == 0U || json != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "json is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (me->destroying) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    net_mgr = me->config.net_mgr;
    (void)xSemaphoreGive(me->mutex);

    ret = tb_internal_format_rpc_response_topic(topic, sizeof(topic),
                                                request_id, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    return thingsboard_client_publish_json(net_mgr, topic, json, json_len);
}

esp_err_t thingsboard_client_register_command_cb(thingsboard_client_t *me,
                                                 tb_command_cb_t cb,
                                                 void *ctx)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "client is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    me->cmd_cb = cb;
    me->cmd_ctx = (cb != NULL) ? ctx : NULL;

    (void)xSemaphoreGive(me->mutex);

    if (cb == NULL) {
        return thingsboard_client_wait_command_callbacks_drained(me);
    }

    return ESP_OK;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static esp_err_t thingsboard_client_validate_config(
    const tb_client_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "config is null");
    ESP_RETURN_ON_FALSE(config->net_mgr != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "network manager is null");

    return ESP_OK;
}

static int thingsboard_client_resolve_json_buf_size(
    const tb_client_config_t *config)
{
    return (config->json_buf_size > 0) ? config->json_buf_size :
           TB_DEFAULT_JSON_BUF_SIZE;
}

static void thingsboard_client_on_rx(const network_rx_data_t *rx_data,
                                     void *user_ctx)
{
    thingsboard_client_t *me = (thingsboard_client_t *)user_ctx;
    tb_internal_command_t internal_cmd = { 0 };
    tb_command_t cmd = { 0 };
    tb_command_cb_t cb = NULL;
    void *ctx = NULL;
    esp_err_t ret;

    if ((rx_data == NULL) || (rx_data->topic == NULL) ||
        (rx_data->data == NULL) || (user_ctx == NULL)) {
        return;
    }
    if (rx_data->data_len < 0) {
        ESP_LOGW(TAG, "malformed rx data length");
        return;
    }

    ret = tb_internal_parse_rpc(rx_data->topic, rx_data->data,
                                (size_t)rx_data->data_len, &internal_cmd);
    if (ret == ESP_ERR_NOT_FOUND) {
        return;
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "malformed rpc message: %s", esp_err_to_name(ret));
        return;
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (!me->destroying && !me->stopping && me->started) {
        cb = me->cmd_cb;
        ctx = me->cmd_ctx;
        if (cb != NULL) {
            me->active_cmd_callbacks++;
        }
    }
    (void)xSemaphoreGive(me->mutex);

    thingsboard_client_copy_command(&internal_cmd, &cmd);
    if (cb != NULL) {
        cb(&cmd, ctx);

        if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
            if (me->active_cmd_callbacks > 0U) {
                me->active_cmd_callbacks--;
            }
            (void)xSemaphoreGive(me->mutex);
        }
    }
}

static esp_err_t thingsboard_client_publish_json(network_manager_t *net_mgr,
                                                 const char *topic,
                                                 const char *json,
                                                 size_t json_len)
{
    network_publish_request_t req = {
        .topic = topic,
        .payload = json,
        .payload_len = json_len,
        .qos = NETWORK_MQTT_QOS0,
        .retain = false,
    };

    return network_manager_publish(net_mgr, &req);
}

static void thingsboard_client_copy_command(
    const tb_internal_command_t *internal_cmd, tb_command_t *out_cmd)
{
    switch (internal_cmd->type) {
    case TB_INTERNAL_COMMAND_SET_RELAY:
        out_cmd->type = TB_COMMAND_SET_RELAY;
        break;
    case TB_INTERNAL_COMMAND_GET_POWER_LIMIT:
        out_cmd->type = TB_COMMAND_GET_POWER_LIMIT;
        break;
    case TB_INTERNAL_COMMAND_SET_POWER_LIMIT:
        out_cmd->type = TB_COMMAND_SET_POWER_LIMIT;
        break;
    default:
        out_cmd->type = TB_COMMAND_GET_POWER_LIMIT;
        break;
    }

    out_cmd->request_id = internal_cmd->request_id;
    out_cmd->relay_on = internal_cmd->relay_on;
    out_cmd->power_limit_w = internal_cmd->power_limit_w;
}

static esp_err_t thingsboard_client_wait_command_callbacks_drained(
    thingsboard_client_t *me)
{
    bool drained = false;
    uint32_t waited_ms = 0U;

    while (!drained) {
        if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
        drained = me->active_cmd_callbacks == 0U;
        (void)xSemaphoreGive(me->mutex);

        if (!drained) {
            if (waited_ms >= TB_COMMAND_DRAIN_TIMEOUT_MS) {
                ESP_LOGE(TAG, "wait for command callbacks timed out");
                return ESP_ERR_TIMEOUT;
            }
            vTaskDelay(pdMS_TO_TICKS(TB_COMMAND_DRAIN_POLL_MS));
            waited_ms += TB_COMMAND_DRAIN_POLL_MS;
        }
    }

    return ESP_OK;
}
