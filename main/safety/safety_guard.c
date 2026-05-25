/**
 * @file safety_guard.c
 * @brief 本地安全规则实现
 * @details Local safety rule implementation
 * @author OpenCode
 * @date 2026-05-25
 */

/*********************
 *      INCLUDES
 *********************/

#include "safety_guard.h"

#include <stdlib.h>
#include <string.h>

#include "metering_service.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "safety_guard"
#define SAFETY_GUARD_DEFAULT_OVERCURRENT_A (10.0f)
#define SAFETY_GUARD_DEFAULT_OVERPOWER_W   (2200.0f)
#define SAFETY_GUARD_DEFAULT_PERSISTENCE   (3)
#define SAFETY_GUARD_EVENT_POST_TIMEOUT_MS (10)

ESP_EVENT_DEFINE_BASE(SAFETY_GUARD_EVENT_BASE);

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 本地安全规则对象
 * @details Local safety rule object
 */
struct safety_guard {
    safety_guard_config_t config;
    SemaphoreHandle_t mutex;
    safety_guard_snapshot_t latest;
    bool has_latest;
    int overcurrent_persistence;
    int overpower_persistence;
    esp_event_handler_instance_t metering_handler;
    bool started;
    bool stopping;
    bool initialized;
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**
 * @brief 应用安全规则默认配置
 * @details Apply safety rule default configuration
 * @param[in] config 输入配置，可为 NULL； Input configuration, may be NULL
 * @param[out] out 输出配置； Output configuration
 */
static void safety_guard_apply_defaults(const safety_guard_config_t *config,
                                        safety_guard_config_t *out);

/**
 * @brief 计算快照时间戳
 * @details Resolve snapshot timestamp
 * @param[in] metering 电参量快照； Metering snapshot
 * @return 安全快照时间戳； Safety snapshot timestamp
 */
static uint64_t safety_guard_resolve_timestamp(
    const metering_snapshot_t *metering);

/**
 * @brief 评估电参量快照
 * @details Evaluate metering snapshot while locked
 * @param[in,out] me 安全规则对象； Safety guard object
 * @param[in] metering 电参量快照； Metering snapshot
 * @param[out] out 安全规则快照； Safety snapshot
 */
static void safety_guard_evaluate_locked(safety_guard_t *me,
                                         const metering_snapshot_t *metering,
                                         safety_guard_snapshot_t *out);

/**
 * @brief 处理电参量快照事件
 * @details Handle metering snapshot event
 * @param[in] arg 事件处理上下文； Event handler context
 * @param[in] base 事件基； Event base
 * @param[in] id 事件 ID； Event ID
 * @param[in] event_data 事件载荷； Event payload
 */
static void safety_guard_on_metering_snapshot(void *arg,
                                              esp_event_base_t base,
                                              int32_t id, void *event_data);

/**
 * @brief 发布安全规则快照事件
 * @details Post safety rule snapshot event
 * @param[in] snapshot 安全规则快照； Safety snapshot
 */
static void safety_guard_post_snapshot(
    const safety_guard_snapshot_t *snapshot);

/**
 * @brief 清理运行态规则状态
 * @details Clear runtime rule state
 * @param[in,out] me 安全规则对象； Safety guard object
 */
static void safety_guard_clear_rule_state_locked(safety_guard_t *me);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

safety_guard_t *safety_guard_create(const safety_guard_config_t *config)
{
    safety_guard_config_t resolved = {0};

    safety_guard_apply_defaults(config, &resolved);

    safety_guard_t *me = calloc(1, sizeof(*me));
    if (me == NULL) {
        ESP_LOGE(TAG, "calloc safety guard failed");
        return NULL;
    }

    me->config = resolved;
    me->mutex = xSemaphoreCreateMutex();
    if (me->mutex == NULL) {
        ESP_LOGE(TAG, "create mutex failed");
        free(me);
        return NULL;
    }

    me->initialized = true;
    return me;
}

esp_err_t safety_guard_destroy(safety_guard_t *me)
{
    esp_err_t stop_ret = ESP_OK;

    if (me == NULL) {
        return ESP_OK;
    }

    stop_ret = safety_guard_stop(me);
    if (stop_ret != ESP_OK) {
        return stop_ret;
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (me->metering_handler != NULL || me->started || me->stopping) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    (void)xSemaphoreGive(me->mutex);

    if (me->mutex != NULL) {
        vSemaphoreDelete(me->mutex);
        me->mutex = NULL;
    }
    memset(me, 0, sizeof(*me));
    free(me);

    return stop_ret;
}

esp_err_t safety_guard_start(safety_guard_t *me)
{
    esp_event_handler_instance_t handler = NULL;
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "guard is null");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "guard is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    if (me->stopping) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (me->started) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_OK;
    }
    if (me->metering_handler != NULL) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }

    ret = esp_event_handler_instance_register(METERING_EVENT_BASE,
                                              METERING_EVENT_SNAPSHOT,
                                              safety_guard_on_metering_snapshot,
                                              me, &handler);
    if (ret != ESP_OK) {
        (void)xSemaphoreGive(me->mutex);
        ESP_LOGE(TAG, "register metering handler failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    me->metering_handler = handler;
    me->started = true;
    (void)xSemaphoreGive(me->mutex);
    return ESP_OK;
}

esp_err_t safety_guard_stop(safety_guard_t *me)
{
    esp_event_handler_instance_t handler = NULL;
    esp_err_t ret = ESP_OK;
    esp_err_t state_ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "guard is null");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "guard is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    if (me->stopping) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (!me->started && me->metering_handler == NULL) {
        safety_guard_clear_rule_state_locked(me);
        (void)xSemaphoreGive(me->mutex);
        return ESP_OK;
    }

    handler = me->metering_handler;
    if (handler == NULL) {
        safety_guard_clear_rule_state_locked(me);
        me->started = false;
        (void)xSemaphoreGive(me->mutex);
        return ESP_OK;
    }
    me->stopping = true;
    (void)xSemaphoreGive(me->mutex);

    ret = esp_event_handler_instance_unregister(METERING_EVENT_BASE,
                                                METERING_EVENT_SNAPSHOT,
                                                handler);

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "take mutex failed after unregister");
        if (ret == ESP_OK) {
            me->metering_handler = NULL;
            me->started = false;
        }
        me->stopping = false;
        return ESP_ERR_TIMEOUT;
    }

    if (ret == ESP_OK) {
        if (me->metering_handler == handler) {
            me->metering_handler = NULL;
        }
        safety_guard_clear_rule_state_locked(me);
        me->started = false;
    } else {
        ESP_LOGW(TAG, "unregister metering handler failed: %s",
                 esp_err_to_name(ret));
        state_ret = ret;
    }
    me->stopping = false;
    (void)xSemaphoreGive(me->mutex);

    return state_ret;
}

esp_err_t safety_guard_get_latest(safety_guard_t *me,
                                  safety_guard_snapshot_t *out)
{
    ESP_RETURN_ON_FALSE(me != NULL && out != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "invalid argument");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "guard is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    if (!me->has_latest) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }

    *out = me->latest;
    (void)xSemaphoreGive(me->mutex);
    return ESP_OK;
}

esp_err_t safety_guard_set_thresholds(safety_guard_t *me,
                                      float overcurrent_a,
                                      float overpower_w)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "guard is null");
    ESP_RETURN_ON_FALSE(overcurrent_a > 0.0f && overpower_w > 0.0f,
                        ESP_ERR_INVALID_ARG, TAG, "invalid threshold");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "guard is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    me->config.overcurrent_threshold_a = overcurrent_a;
    me->config.overpower_threshold_w = overpower_w;
    me->overcurrent_persistence = 0;
    me->overpower_persistence = 0;

    (void)xSemaphoreGive(me->mutex);
    return ESP_OK;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void safety_guard_apply_defaults(const safety_guard_config_t *config,
                                        safety_guard_config_t *out)
{
    if (out == NULL) {
        return;
    }

    if (config != NULL) {
        *out = *config;
    } else {
        memset(out, 0, sizeof(*out));
    }

    if (!(out->overcurrent_threshold_a > 0.0f)) {
        out->overcurrent_threshold_a = SAFETY_GUARD_DEFAULT_OVERCURRENT_A;
    }
    if (!(out->overpower_threshold_w > 0.0f)) {
        out->overpower_threshold_w = SAFETY_GUARD_DEFAULT_OVERPOWER_W;
    }
    if (out->persistence_samples <= 0) {
        out->persistence_samples = SAFETY_GUARD_DEFAULT_PERSISTENCE;
    }
}

static void safety_guard_clear_rule_state_locked(safety_guard_t *me)
{
    if (me == NULL) {
        return;
    }

    memset(&me->latest, 0, sizeof(me->latest));
    me->has_latest = false;
    me->overcurrent_persistence = 0;
    me->overpower_persistence = 0;
}

static uint64_t safety_guard_resolve_timestamp(
    const metering_snapshot_t *metering)
{
    if (metering != NULL && metering->timestamp_us != 0ULL) {
        return metering->timestamp_us;
    }

    return (uint64_t)esp_timer_get_time();
}

static void safety_guard_evaluate_locked(safety_guard_t *me,
                                         const metering_snapshot_t *metering,
                                         safety_guard_snapshot_t *out)
{
    bool overcurrent = false;
    bool overpower = false;
    int winning_count = 0;

    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    out->timestamp_us = safety_guard_resolve_timestamp(metering);

    if (me == NULL || metering == NULL || !metering->valid) {
        if (me != NULL) {
            me->overcurrent_persistence = 0;
            me->overpower_persistence = 0;
        }
        out->level = SAFETY_GUARD_LEVEL_WARNING;
        out->event = SAFETY_GUARD_EVENT_NONE;
        out->suggested_action = SAFETY_GUARD_ACTION_NONE;
        out->valid = false;
        return;
    }

    out->valid = true;
    overcurrent = (metering->current > me->config.overcurrent_threshold_a);
    overpower = (metering->power > me->config.overpower_threshold_w);

    if (overcurrent) {
        me->overcurrent_persistence++;
    } else {
        me->overcurrent_persistence = 0;
    }

    if (overpower) {
        me->overpower_persistence++;
    } else {
        me->overpower_persistence = 0;
    }

    if (!overcurrent && !overpower) {
        out->level = SAFETY_GUARD_LEVEL_NORMAL;
        out->event = SAFETY_GUARD_EVENT_NONE;
        out->suggested_action = SAFETY_GUARD_ACTION_NONE;
        return;
    }

    if (overcurrent &&
        (!overpower || me->overcurrent_persistence >=
            me->overpower_persistence)) {
        out->event = SAFETY_GUARD_EVENT_OVERCURRENT;
        winning_count = me->overcurrent_persistence;
    } else {
        out->event = SAFETY_GUARD_EVENT_OVERPOWER;
        winning_count = me->overpower_persistence;
    }

    if (winning_count >= me->config.persistence_samples) {
        out->level = SAFETY_GUARD_LEVEL_DANGER;
        out->suggested_action = SAFETY_GUARD_ACTION_RELAY_OFF;
    } else {
        out->level = SAFETY_GUARD_LEVEL_WARNING;
        out->suggested_action = SAFETY_GUARD_ACTION_NONE;
    }
}

static void safety_guard_on_metering_snapshot(void *arg,
                                              esp_event_base_t base,
                                              int32_t id, void *event_data)
{
    safety_guard_t *me = (safety_guard_t *)arg;
    const metering_snapshot_t *metering =
        (const metering_snapshot_t *)event_data;
    safety_guard_snapshot_t snapshot = {0};

    if (me == NULL || base != METERING_EVENT_BASE ||
        id != METERING_EVENT_SNAPSHOT) {
        return;
    }
    if (me->mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(TAG, "take mutex failed while handling metering snapshot");
        return;
    }
    if (!me->initialized || !me->started || me->stopping) {
        (void)xSemaphoreGive(me->mutex);
        return;
    }

    safety_guard_evaluate_locked(me, metering, &snapshot);
    me->latest = snapshot;
    me->has_latest = true;
    (void)xSemaphoreGive(me->mutex);

    safety_guard_post_snapshot(&snapshot);
}

static void safety_guard_post_snapshot(
    const safety_guard_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    const esp_err_t ret = esp_event_post(SAFETY_GUARD_EVENT_BASE,
                                         SAFETY_GUARD_EVENT_SNAPSHOT,
                                         snapshot, sizeof(*snapshot),
                                         pdMS_TO_TICKS(
                                             SAFETY_GUARD_EVENT_POST_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "post snapshot event failed: %s", esp_err_to_name(ret));
    }
}
