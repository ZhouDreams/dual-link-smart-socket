/**
 * @file app_controller.c
 * @brief 应用控制器实现
 * @details App controller implementation
 * @author OpenCode
 * @date 2026-05-28
 */

/*********************
 *      INCLUDES
 *********************/

#include "app_controller.h"

#include <stdlib.h>
#include <string.h>

#include "app_controller_internal.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "app_controller"
#define APP_CONTROLLER_RPC_BUF_SIZE (96)
#define APP_CONTROLLER_LIFECYCLE_POLL_MS (10U)

/**********************
 *      TYPEDEFS
 **********************/

struct app_controller {
    app_controller_config_t cfg;
    SemaphoreHandle_t mutex;
    esp_event_handler_instance_t safety_handler;
    esp_event_handler_instance_t metering_handler;
    esp_event_handler_instance_t relay_handler;
    bool relay_on;
    bool relay_known;
    bool screen_enabled;
    bool started;
    bool starting;
    bool stopping;
    bool button_single_registered;
    bool button_long_registered;
    bool tb_command_registered;
    bool bl0942_started;
    bool metering_started;
    bool safety_started;
    bool net_mgr_started;
    bool tb_started;
    bool dashboard_started;
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**
 * @brief 处理按键单击
 * @details Handle button single click
 */
static void app_controller_on_button_single_click(button_event_t event,
                                                  void *user_ctx);

/**
 * @brief 处理按键长按
 * @details Handle button long press
 */
static void app_controller_on_button_long_press(button_event_t event,
                                                void *user_ctx);

/**
 * @brief 处理安全快照事件
 * @details Handle safety snapshot event
 */
static void app_controller_on_safety_snapshot(void *handler_args,
                                              esp_event_base_t event_base,
                                              int32_t event_id,
                                              void *event_data);

/**
 * @brief 处理电参量快照事件
 * @details Handle metering snapshot event
 */
static void app_controller_on_metering_snapshot(void *handler_args,
                                                esp_event_base_t event_base,
                                                int32_t event_id,
                                                void *event_data);

/**
 * @brief 处理继电器状态事件
 * @details Handle relay state event
 */
static void app_controller_on_relay_state_changed(void *handler_args,
                                                  esp_event_base_t event_base,
                                                  int32_t event_id,
                                                  void *event_data);

/**
 * @brief 处理 ThingsBoard 命令
 * @details Handle ThingsBoard command
 */
static void app_controller_on_tb_command(const tb_command_t *cmd,
                                         void *user_ctx);

/**
 * @brief 注册按键回调
 * @details Register button callbacks
 */
static esp_err_t app_controller_register_button_callbacks(app_controller_t *me);

/**
 * @brief 清除按键回调
 * @details Clear button callbacks
 */
static esp_err_t app_controller_clear_button_callbacks(app_controller_t *me);

/**
 * @brief 注册事件处理器
 * @details Register event handlers
 */
static esp_err_t app_controller_register_event_handlers(app_controller_t *me);

/**
 * @brief 注销事件处理器
 * @details Unregister event handlers
 */
static esp_err_t app_controller_unregister_event_handlers(app_controller_t *me);

/**
 * @brief 停止控制器内部实现
 * @details Internal controller stop implementation
 */
static esp_err_t app_controller_stop_impl(app_controller_t *me,
                                          bool from_start_rollback);

/**
 * @brief 设置生命周期标志
 * @details Set lifecycle flag
 */
static esp_err_t app_controller_set_flag(app_controller_t *me, bool *flag,
                                         bool value);

/**
 * @brief 读取生命周期标志
 * @details Get lifecycle flag
 */
static bool app_controller_get_flag(app_controller_t *me, const bool *flag);

/**
 * @brief 查询是否完全停止
 * @details Query whether fully stopped
 */
static bool app_controller_is_fully_stopped_locked(const app_controller_t *me);

/**
 * @brief 启动下层模块
 * @details Start lower modules
 */
static esp_err_t app_controller_start_modules(app_controller_t *me);

/**
 * @brief 停止下层模块
 * @details Stop lower modules
 */
static esp_err_t app_controller_stop_modules(app_controller_t *me);

/**
 * @brief 查询运行状态
 * @details Query running state
 */
static bool app_controller_is_running(app_controller_t *me);

/**
 * @brief 发布遥测
 * @details Publish telemetry
 */
static esp_err_t app_controller_publish_telemetry(
    app_controller_t *me, const metering_snapshot_t *snapshot);

/**
 * @brief 记录首个错误
 * @details Capture first error
 */
static void app_controller_capture_first_error(esp_err_t ret,
                                               esp_err_t *first_error);

/**
 * @brief 停止清理时过滤本地状态无效错误
 * @details Filter invalid state errors during stop cleanup
 */
static esp_err_t app_controller_filter_stop_error(esp_err_t ret);

/**********************
 *  STATIC VARIABLES
 **********************/

static uint32_t s_telemetry_publish_count;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

app_controller_t *app_controller_create(const app_controller_config_t *config)
{
    if (config == NULL || config->relay == NULL || config->button == NULL ||
        config->bl0942 == NULL || config->metering == NULL ||
        config->safety == NULL || config->tb == NULL ||
        config->net_mgr == NULL || config->dashboard == NULL) {
        return NULL;
    }

    app_controller_t *me = calloc(1, sizeof(*me));
    if (me == NULL) {
        ESP_LOGE(TAG, "calloc controller failed");
        return NULL;
    }

    me->cfg = *config;
    me->screen_enabled = true;
    me->mutex = xSemaphoreCreateMutex();
    if (me->mutex == NULL) {
        ESP_LOGE(TAG, "create mutex failed");
        free(me);
        return NULL;
    }

    bool relay_on = false;
    if (relay_get(me->cfg.relay, &relay_on) == ESP_OK) {
        me->relay_on = relay_on;
        me->relay_known = true;
    }

    return me;
}

esp_err_t app_controller_destroy(app_controller_t *me)
{
    esp_err_t ret = ESP_OK;

    if (me == NULL) {
        return ESP_OK;
    }

    ret = app_controller_stop(me);
    if (ret != ESP_OK) {
        return ret;
    }
    if (me->mutex != NULL) {
        vSemaphoreDelete(me->mutex);
    }

    free(me);
    return ret;
}

esp_err_t app_controller_start(app_controller_t *me)
{
    esp_err_t ret = ESP_OK;
    esp_err_t original_error = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "controller is null");
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
    me->starting = true;
    (void)xSemaphoreGive(me->mutex);

    ret = app_controller_register_button_callbacks(me);
    if (ret != ESP_OK) {
        original_error = ret;
        goto err;
    }

    ret = app_controller_register_event_handlers(me);
    if (ret != ESP_OK) {
        original_error = ret;
        goto err;
    }

    ret = thingsboard_client_register_command_cb(me->cfg.tb,
                                                 app_controller_on_tb_command,
                                                 me);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register ThingsBoard command callback failed: %s",
                 esp_err_to_name(ret));
        original_error = ret;
        goto err;
    }
    ret = app_controller_set_flag(me, &me->tb_command_registered, true);
    if (ret != ESP_OK) {
        original_error = ret;
        goto err;
    }

    ret = app_controller_start_modules(me);
    if (ret != ESP_OK) {
        original_error = ret;
        goto err;
    }

    ESP_GOTO_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                      ESP_ERR_TIMEOUT, err, TAG, "take mutex failed");
    me->started = true;
    me->starting = false;
    (void)xSemaphoreGive(me->mutex);
    return ESP_OK;

err:
    if (original_error == ESP_OK) {
        original_error = ret;
    }
    (void)app_controller_stop_impl(me, true);
    return original_error;
}

esp_err_t app_controller_stop(app_controller_t *me)
{
    return app_controller_stop_impl(me, false);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static esp_err_t app_controller_stop_impl(app_controller_t *me,
                                          bool from_start_rollback)
{
    esp_err_t first_error = ESP_OK;
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "controller is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    while (!from_start_rollback && me->starting) {
        (void)xSemaphoreGive(me->mutex);
        vTaskDelay(pdMS_TO_TICKS(APP_CONTROLLER_LIFECYCLE_POLL_MS));
        ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                            ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    }

    if (me->stopping) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (app_controller_is_fully_stopped_locked(me)) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_OK;
    }

    me->stopping = true;
    me->started = false;
    (void)xSemaphoreGive(me->mutex);

    app_controller_capture_first_error(app_controller_stop_modules(me),
                                       &first_error);

    if (app_controller_get_flag(me, &me->tb_command_registered)) {
        ret = thingsboard_client_register_command_cb(me->cfg.tb, NULL, NULL);
        ret = app_controller_filter_stop_error(ret);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "clear ThingsBoard command callback failed: %s",
                     esp_err_to_name(ret));
        } else {
            (void)app_controller_set_flag(me, &me->tb_command_registered,
                                          false);
        }
        app_controller_capture_first_error(ret, &first_error);
    }

    app_controller_capture_first_error(app_controller_clear_button_callbacks(me),
                                       &first_error);
    app_controller_capture_first_error(app_controller_unregister_event_handlers(me),
                                       &first_error);

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "take mutex failed after stop");
        return first_error == ESP_OK ? ESP_ERR_TIMEOUT : first_error;
    }
    if (from_start_rollback) {
        me->starting = false;
    }
    me->stopping = false;
    (void)xSemaphoreGive(me->mutex);

    return first_error;
}

static void app_controller_on_button_single_click(button_event_t event,
                                                  void *user_ctx)
{
    app_controller_t *me = (app_controller_t *)user_ctx;

    if (event != BUTTON_EVENT_SINGLE_CLICK || !app_controller_is_running(me)) {
        return;
    }

    esp_err_t ret = relay_toggle(me->cfg.relay, RELAY_SOURCE_LOCAL_BUTTON);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "toggle relay failed: %s", esp_err_to_name(ret));
    }
}

static void app_controller_on_button_long_press(button_event_t event,
                                                void *user_ctx)
{
    app_controller_t *me = (app_controller_t *)user_ctx;
    bool next = false;

    if (event != BUTTON_EVENT_LONG_PRESS_START ||
        !app_controller_is_running(me)) {
        return;
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(TAG, "take mutex failed in long press");
        return;
    }
    if (!me->started || me->stopping) {
        (void)xSemaphoreGive(me->mutex);
        return;
    }
    next = app_controller_internal_toggle_screen(me->screen_enabled);
    me->screen_enabled = next;
    (void)xSemaphoreGive(me->mutex);

    esp_err_t ret = lvgl_dashboard_set_screen_enabled(me->cfg.dashboard, next);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "set screen enabled failed: %s", esp_err_to_name(ret));
    }
}

static void app_controller_on_safety_snapshot(void *handler_args,
                                              esp_event_base_t event_base,
                                              int32_t event_id,
                                              void *event_data)
{
    app_controller_t *me = (app_controller_t *)handler_args;
    safety_guard_snapshot_t *snapshot = (safety_guard_snapshot_t *)event_data;

    if (event_base != SAFETY_GUARD_EVENT_BASE ||
        event_id != SAFETY_GUARD_EVENT_SNAPSHOT || snapshot == NULL ||
        !app_controller_is_running(me)) {
        return;
    }

    if (snapshot->suggested_action == SAFETY_GUARD_ACTION_RELAY_OFF) {
        esp_err_t ret = relay_set(me->cfg.relay, RELAY_SOURCE_SAFETY, false);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "safety relay off failed: %s", esp_err_to_name(ret));
        }
    }
}

static void app_controller_on_metering_snapshot(void *handler_args,
                                                esp_event_base_t event_base,
                                                int32_t event_id,
                                                void *event_data)
{
    app_controller_t *me = (app_controller_t *)handler_args;
    metering_snapshot_t *snapshot = (metering_snapshot_t *)event_data;

    if (event_base != METERING_EVENT_BASE || event_id != METERING_EVENT_SNAPSHOT ||
        snapshot == NULL || !app_controller_is_running(me)) {
        return;
    }

    esp_err_t ret = app_controller_publish_telemetry(me, snapshot);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "publish telemetry failed: %s", esp_err_to_name(ret));
    }
}

static void app_controller_on_relay_state_changed(void *handler_args,
                                                  esp_event_base_t event_base,
                                                  int32_t event_id,
                                                  void *event_data)
{
    app_controller_t *me = (app_controller_t *)handler_args;
    relay_state_changed_event_t *changed =
        (relay_state_changed_event_t *)event_data;
    bool running = false;

    if (event_base != RELAY_EVENT_BASE ||
        event_id != RELAY_EVENT_STATE_CHANGED || changed == NULL ||
        me == NULL) {
        return;
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(TAG, "take mutex failed in relay event");
        return;
    }
    me->relay_on = changed->on;
    me->relay_known = true;
    running = me->started && !me->stopping;
    (void)xSemaphoreGive(me->mutex);

    if (running && changed->source != RELAY_SOURCE_CLOUD) {
        esp_err_t ret = thingsboard_client_report_relay_state(me->cfg.tb,
                                                              changed->on);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "report relay state failed: %s", esp_err_to_name(ret));
        }
    }
}

static void app_controller_on_tb_command(const tb_command_t *cmd,
                                         void *user_ctx)
{
    app_controller_t *me = (app_controller_t *)user_ctx;
    float current_a = 0.0f;
    float power = 0.0f;
    char response[APP_CONTROLLER_RPC_BUF_SIZE];
    size_t response_len = 0U;
    esp_err_t ret = ESP_OK;

    if (cmd == NULL || !app_controller_is_running(me)) {
        return;
    }

    switch (cmd->type) {
    case TB_COMMAND_SET_RELAY:
        ret = relay_set(me->cfg.relay, RELAY_SOURCE_CLOUD, cmd->relay_on);
        if (ret == ESP_OK) {
            ret = thingsboard_client_report_relay_state(me->cfg.tb,
                                                        cmd->relay_on);
        }
        break;
    case TB_COMMAND_GET_POWER_LIMIT:
        ret = safety_guard_get_thresholds(me->cfg.safety, NULL, &power);
        if (ret == ESP_OK) {
            ret = app_controller_internal_format_power_limit_response(
                response, sizeof(response), power, &response_len);
        }
        if (ret == ESP_OK) {
            ret = thingsboard_client_send_rpc_response(me->cfg.tb,
                                                       cmd->request_id,
                                                       response,
                                                       response_len);
        }
        break;
    case TB_COMMAND_SET_POWER_LIMIT:
        ret = safety_guard_get_thresholds(me->cfg.safety, &current_a, NULL);
        if (ret == ESP_OK) {
            ret = safety_guard_set_thresholds(me->cfg.safety, current_a,
                                              cmd->power_limit_w);
        }
        if (ret == ESP_OK) {
            ret = thingsboard_client_report_power_limit(me->cfg.tb,
                                                        cmd->power_limit_w);
        }
        break;
    default:
        ret = ESP_ERR_INVALID_ARG;
        break;
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "handle ThingsBoard command failed: %s",
                 esp_err_to_name(ret));
    }
}

static esp_err_t app_controller_register_button_callbacks(app_controller_t *me)
{
    esp_err_t ret = button_register_cb(me->cfg.button,
                                       BUTTON_EVENT_SINGLE_CLICK,
                                       app_controller_on_button_single_click,
                                       me);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register single click callback failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }
    ret = app_controller_set_flag(me, &me->button_single_registered, true);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = button_register_cb(me->cfg.button, BUTTON_EVENT_LONG_PRESS_START,
                             app_controller_on_button_long_press, me);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register long press callback failed: %s",
                 esp_err_to_name(ret));
    } else {
        ret = app_controller_set_flag(me, &me->button_long_registered, true);
    }

    return ret;
}

static esp_err_t app_controller_clear_button_callbacks(app_controller_t *me)
{
    esp_err_t first_error = ESP_OK;
    esp_err_t ret = ESP_OK;

    if (app_controller_get_flag(me, &me->button_single_registered)) {
        ret = button_register_cb(me->cfg.button, BUTTON_EVENT_SINGLE_CLICK,
                                 NULL, NULL);
        ret = app_controller_filter_stop_error(ret);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "clear single click callback failed: %s",
                     esp_err_to_name(ret));
        } else {
            (void)app_controller_set_flag(me, &me->button_single_registered,
                                          false);
        }
        app_controller_capture_first_error(ret, &first_error);
    }

    if (app_controller_get_flag(me, &me->button_long_registered)) {
        ret = button_register_cb(me->cfg.button, BUTTON_EVENT_LONG_PRESS_START,
                                 NULL, NULL);
        ret = app_controller_filter_stop_error(ret);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "clear long press callback failed: %s",
                     esp_err_to_name(ret));
        } else {
            (void)app_controller_set_flag(me, &me->button_long_registered,
                                          false);
        }
        app_controller_capture_first_error(ret, &first_error);
    }

    return first_error;
}

static esp_err_t app_controller_set_flag(app_controller_t *me, bool *flag,
                                         bool value)
{
    ESP_RETURN_ON_FALSE(me != NULL && flag != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "flag args are null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    *flag = value;
    (void)xSemaphoreGive(me->mutex);

    return ESP_OK;
}

static bool app_controller_get_flag(app_controller_t *me, const bool *flag)
{
    bool value = false;

    if (me == NULL || flag == NULL || me->mutex == NULL) {
        return false;
    }
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    value = *flag;
    (void)xSemaphoreGive(me->mutex);

    return value;
}

static esp_err_t app_controller_register_event_handlers(app_controller_t *me)
{
    esp_err_t ret = ESP_OK;

    if (me->cfg.event_loop != NULL) {
        ret = esp_event_handler_instance_register_with(
            me->cfg.event_loop, SAFETY_GUARD_EVENT_BASE,
            SAFETY_GUARD_EVENT_SNAPSHOT, app_controller_on_safety_snapshot, me,
            &me->safety_handler);
    } else {
        ret = esp_event_handler_instance_register(
            SAFETY_GUARD_EVENT_BASE, SAFETY_GUARD_EVENT_SNAPSHOT,
            app_controller_on_safety_snapshot, me, &me->safety_handler);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register safety handler failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    if (me->cfg.event_loop != NULL) {
        ret = esp_event_handler_instance_register_with(
            me->cfg.event_loop, METERING_EVENT_BASE, METERING_EVENT_SNAPSHOT,
            app_controller_on_metering_snapshot, me, &me->metering_handler);
    } else {
        ret = esp_event_handler_instance_register(
            METERING_EVENT_BASE, METERING_EVENT_SNAPSHOT,
            app_controller_on_metering_snapshot, me, &me->metering_handler);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register metering handler failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    if (me->cfg.event_loop != NULL) {
        ret = esp_event_handler_instance_register_with(
            me->cfg.event_loop, RELAY_EVENT_BASE, RELAY_EVENT_STATE_CHANGED,
            app_controller_on_relay_state_changed, me, &me->relay_handler);
    } else {
        ret = esp_event_handler_instance_register(
            RELAY_EVENT_BASE, RELAY_EVENT_STATE_CHANGED,
            app_controller_on_relay_state_changed, me, &me->relay_handler);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register relay handler failed: %s",
                 esp_err_to_name(ret));
    }

    return ret;
}

static esp_err_t app_controller_unregister_event_handlers(app_controller_t *me)
{
    esp_err_t first_error = ESP_OK;
    esp_err_t ret = ESP_OK;

    if (me->relay_handler != NULL) {
        if (me->cfg.event_loop != NULL) {
            ret = esp_event_handler_instance_unregister_with(
                me->cfg.event_loop, RELAY_EVENT_BASE, RELAY_EVENT_STATE_CHANGED,
                me->relay_handler);
        } else {
            ret = esp_event_handler_instance_unregister(RELAY_EVENT_BASE,
                                                        RELAY_EVENT_STATE_CHANGED,
                                                        me->relay_handler);
        }
        ret = app_controller_filter_stop_error(ret);
        if (ret == ESP_OK) {
            me->relay_handler = NULL;
        } else {
            ESP_LOGW(TAG, "unregister relay handler failed: %s",
                     esp_err_to_name(ret));
        }
        app_controller_capture_first_error(ret, &first_error);
    }

    if (me->metering_handler != NULL) {
        if (me->cfg.event_loop != NULL) {
            ret = esp_event_handler_instance_unregister_with(
                me->cfg.event_loop, METERING_EVENT_BASE, METERING_EVENT_SNAPSHOT,
                me->metering_handler);
        } else {
            ret = esp_event_handler_instance_unregister(METERING_EVENT_BASE,
                                                        METERING_EVENT_SNAPSHOT,
                                                        me->metering_handler);
        }
        ret = app_controller_filter_stop_error(ret);
        if (ret == ESP_OK) {
            me->metering_handler = NULL;
        } else {
            ESP_LOGW(TAG, "unregister metering handler failed: %s",
                     esp_err_to_name(ret));
        }
        app_controller_capture_first_error(ret, &first_error);
    }

    if (me->safety_handler != NULL) {
        if (me->cfg.event_loop != NULL) {
            ret = esp_event_handler_instance_unregister_with(
                me->cfg.event_loop, SAFETY_GUARD_EVENT_BASE,
                SAFETY_GUARD_EVENT_SNAPSHOT, me->safety_handler);
        } else {
            ret = esp_event_handler_instance_unregister(SAFETY_GUARD_EVENT_BASE,
                                                        SAFETY_GUARD_EVENT_SNAPSHOT,
                                                        me->safety_handler);
        }
        ret = app_controller_filter_stop_error(ret);
        if (ret == ESP_OK) {
            me->safety_handler = NULL;
        } else {
            ESP_LOGW(TAG, "unregister safety handler failed: %s",
                     esp_err_to_name(ret));
        }
        app_controller_capture_first_error(ret, &first_error);
    }

    return first_error;
}

static esp_err_t app_controller_start_modules(app_controller_t *me)
{
    esp_err_t ret = bl0942_start(me->cfg.bl0942);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "start BL0942 failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = app_controller_set_flag(me, &me->bl0942_started, true);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = metering_service_start(me->cfg.metering);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "start metering failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = app_controller_set_flag(me, &me->metering_started, true);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = safety_guard_start(me->cfg.safety);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "start safety failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = app_controller_set_flag(me, &me->safety_started, true);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = network_manager_start(me->cfg.net_mgr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "start network manager failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = app_controller_set_flag(me, &me->net_mgr_started, true);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = thingsboard_client_start(me->cfg.tb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "start ThingsBoard client failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }
    ret = app_controller_set_flag(me, &me->tb_started, true);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = lvgl_dashboard_start(me->cfg.dashboard);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "start dashboard failed: %s", esp_err_to_name(ret));
    } else {
        ret = app_controller_set_flag(me, &me->dashboard_started, true);
    }

    return ret;
}

static esp_err_t app_controller_stop_modules(app_controller_t *me)
{
    esp_err_t first_error = ESP_OK;
    esp_err_t ret = ESP_OK;

    if (app_controller_get_flag(me, &me->dashboard_started)) {
        ret = app_controller_filter_stop_error(
            lvgl_dashboard_stop(me->cfg.dashboard));
        if (ret == ESP_OK) {
            (void)app_controller_set_flag(me, &me->dashboard_started, false);
        }
        app_controller_capture_first_error(ret, &first_error);
    }

    if (app_controller_get_flag(me, &me->tb_started)) {
        ret = app_controller_filter_stop_error(thingsboard_client_stop(me->cfg.tb));
        if (ret == ESP_OK) {
            (void)app_controller_set_flag(me, &me->tb_started, false);
        }
        app_controller_capture_first_error(ret, &first_error);
    }

    if (app_controller_get_flag(me, &me->net_mgr_started)) {
        ret = app_controller_filter_stop_error(network_manager_stop(me->cfg.net_mgr));
        if (ret == ESP_OK) {
            (void)app_controller_set_flag(me, &me->net_mgr_started, false);
        }
        app_controller_capture_first_error(ret, &first_error);
    }

    if (app_controller_get_flag(me, &me->safety_started)) {
        ret = app_controller_filter_stop_error(safety_guard_stop(me->cfg.safety));
        if (ret == ESP_OK) {
            (void)app_controller_set_flag(me, &me->safety_started, false);
        }
        app_controller_capture_first_error(ret, &first_error);
    }

    if (app_controller_get_flag(me, &me->metering_started)) {
        ret = app_controller_filter_stop_error(
            metering_service_stop(me->cfg.metering));
        if (ret == ESP_OK) {
            (void)app_controller_set_flag(me, &me->metering_started, false);
        }
        app_controller_capture_first_error(ret, &first_error);
    }

    if (app_controller_get_flag(me, &me->bl0942_started)) {
        ret = app_controller_filter_stop_error(bl0942_stop(me->cfg.bl0942));
        if (ret == ESP_OK) {
            (void)app_controller_set_flag(me, &me->bl0942_started, false);
        }
        app_controller_capture_first_error(ret, &first_error);
    }

    return first_error;
}

static bool app_controller_is_fully_stopped_locked(const app_controller_t *me)
{
    return !me->started && !me->starting && !me->button_single_registered &&
           !me->button_long_registered && !me->tb_command_registered &&
           !me->bl0942_started && !me->metering_started &&
           !me->safety_started && !me->net_mgr_started && !me->tb_started &&
           !me->dashboard_started && me->safety_handler == NULL &&
           me->metering_handler == NULL && me->relay_handler == NULL;
}

static bool app_controller_is_running(app_controller_t *me)
{
    bool running = false;

    if (me == NULL || me->mutex == NULL) {
        return false;
    }
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    running = me->started && !me->stopping;
    (void)xSemaphoreGive(me->mutex);

    return running;
}

static esp_err_t app_controller_publish_telemetry(
    app_controller_t *me, const metering_snapshot_t *snapshot)
{
    esp_err_t ret = ESP_OK;
    app_controller_telemetry_source_t source = { 0 };
    app_controller_telemetry_output_t output = { 0 };
    tb_telemetry_input_t input = { 0 };
    network_manager_status_t net_status = {
        .active_link = NETWORK_LINK_TYPE_NONE,
    };
    safety_guard_snapshot_t safety = {
        .level = SAFETY_GUARD_LEVEL_WARNING,
        .valid = false,
    };
    bool relay_on = false;
    bool relay_known = false;

    ESP_RETURN_ON_FALSE(me != NULL && snapshot != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "telemetry args are null");

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
        relay_on = me->relay_on;
        relay_known = me->relay_known;
        (void)xSemaphoreGive(me->mutex);
    }

    if (!relay_known && relay_get(me->cfg.relay, &relay_on) == ESP_OK) {
        relay_known = true;
        if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
            me->relay_on = relay_on;
            me->relay_known = true;
            (void)xSemaphoreGive(me->mutex);
        }
    }

    (void)network_manager_get_status(me->cfg.net_mgr, &net_status);
    (void)safety_guard_get_latest(me->cfg.safety, &safety);

    source.voltage = snapshot->voltage;
    source.current = snapshot->current;
    source.power = snapshot->power;
    source.total_energy = snapshot->total_energy;
    source.metering_valid = snapshot->valid;
    source.relay_on = relay_on;
    source.relay_known = relay_known;
    source.active_link = net_status.active_link;
    source.safety_level = safety.level;
    source.safety_valid = safety.valid;
    app_controller_internal_build_telemetry(&source, &output);

    input.voltage = output.voltage;
    input.current = output.current;
    input.power = output.power;
    input.total_energy = output.total_energy;
    input.relay_on = output.relay_on;
    input.active_link = output.active_link;
    input.safety_level = output.safety_level;
    input.valid = output.valid;

    ret = thingsboard_client_publish_telemetry(me->cfg.tb, &input);
    if (ret == ESP_OK) {
        s_telemetry_publish_count++;
        ESP_LOGI(TAG, "telemetry publish #%lu ok: energy=%.3f Wh link=%s",
                 (unsigned long)s_telemetry_publish_count,
                 (double)input.total_energy,
                 input.active_link);
    }

    return ret;
}

static void app_controller_capture_first_error(esp_err_t ret,
                                               esp_err_t *first_error)
{
    if (first_error != NULL && *first_error == ESP_OK && ret != ESP_OK) {
        *first_error = ret;
    }
}

static esp_err_t app_controller_filter_stop_error(esp_err_t ret)
{
    return ret == ESP_ERR_INVALID_STATE ? ESP_OK : ret;
}
