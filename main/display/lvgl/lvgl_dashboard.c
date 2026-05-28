/**
 * @file lvgl_dashboard.c
 * @brief LVGL 本地看板实现
 * @details LVGL local dashboard implementation
 * @author OpenCode
 * @date 2026-05-25
 */

/*********************
 *      INCLUDES
 *********************/

#include "lvgl_dashboard.h"
#include "lvgl_dashboard_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "metering_service.h"
#include "relay.h"
#include "safety_guard.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "lvgl_dashboard"
#define LVGL_DASHBOARD_DEFAULT_TASK_STACK     (6144U)
#define LVGL_DASHBOARD_DEFAULT_TASK_PRIORITY  (4)
#define LVGL_DASHBOARD_DEFAULT_TICK_PERIOD_MS (10U)
#define LVGL_DASHBOARD_DEFAULT_UPDATE_MS      (20U)
#define LVGL_DASHBOARD_TASK_NAME              "lvgl_dash"

#define LVGL_DASHBOARD_STOP_TIMEOUT_MS        (3000U)
#define LVGL_DASHBOARD_FLUSH_TIMEOUT_MS       (3000U)
#define LVGL_DASHBOARD_FLUSH_POLL_MS          (10U)
#define LVGL_DASHBOARD_TICK_TIMEOUT_MS        (3000U)
#define LVGL_DASHBOARD_TICK_POLL_MS           (10U)
#define LVGL_DASHBOARD_POWER_CARD_WIDTH       (156)
#define LVGL_DASHBOARD_METRIC_CARD_WIDTH      (74)
#define LVGL_DASHBOARD_METRIC_CARD_HEIGHT     (60)
#define LVGL_DASHBOARD_METRIC_MARGIN_X        (8)
#define LVGL_DASHBOARD_POWER_VALUE_Y          (38)
#define LVGL_DASHBOARD_POWER_TITLE_Y          (12)
#define LVGL_DASHBOARD_BOTTOM_ENERGY_Y        (-32)
#define LVGL_DASHBOARD_BOTTOM_SAFETY_Y        (-12)
#define LVGL_DASHBOARD_PLACEHOLDER_POWER      "--.-- W"
#define LVGL_DASHBOARD_PLACEHOLDER_VOLTAGE    "--.- V"
#define LVGL_DASHBOARD_PLACEHOLDER_CURRENT    "--.--- A"
#define LVGL_DASHBOARD_PLACEHOLDER_ENERGY     "--.--- mWh"
#define LVGL_DASHBOARD_NEUTRAL_BG_HEX         (0x5C6370U)
#define LVGL_DASHBOARD_CONNECTING_BG_HEX      (0xF9A825U)
#define LVGL_DASHBOARD_OFFLINE_BG_HEX         (0xC62828U)

#if LV_FONT_MONTSERRAT_36
#define LVGL_DASHBOARD_POWER_FONT (&lv_font_montserrat_36)
#else
#define LVGL_DASHBOARD_POWER_FONT LV_FONT_DEFAULT
#endif

#if LV_FONT_MONTSERRAT_22
#define LVGL_DASHBOARD_METRIC_FONT (&lv_font_montserrat_22)
#else
#define LVGL_DASHBOARD_METRIC_FONT LV_FONT_DEFAULT
#endif

#define LVGL_DASHBOARD_TITLE_FONT LV_FONT_DEFAULT

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief LVGL tick 定时器上下文
 * @details LVGL tick timer callback context
 */
typedef struct {
    portMUX_TYPE lock;
    uint32_t period_ms;
    uint32_t active_callbacks;
    bool accepting;
} lvgl_dashboard_tick_ctx_t;

/**
 * @brief LVGL 本地看板对象
 * @details LVGL local dashboard object
 */
struct lvgl_dashboard {
    lvgl_dashboard_config_t config;
    SemaphoreHandle_t mutex;
    dashboard_state_t state_cache;
    lv_display_t *display;
    TaskHandle_t lvgl_task;
    SemaphoreHandle_t lvgl_task_done_sema;
    portMUX_TYPE flush_lock;
    esp_timer_handle_t tick_timer;
    lvgl_dashboard_tick_ctx_t *tick_ctx;
    void *draw_buf_1;
    void *draw_buf_2;
    lv_obj_t *screen;
    lv_obj_t *network_pill;
    lv_obj_t *relay_pill;
    lv_obj_t *power_card;
    lv_obj_t *voltage_card;
    lv_obj_t *current_card;
    lv_obj_t *label_voltage;
    lv_obj_t *label_current;
    lv_obj_t *label_power;
    lv_obj_t *label_energy;
    lv_obj_t *label_relay;
    lv_obj_t *label_network;
    lv_obj_t *label_safety;
    esp_event_handler_instance_t metering_handler;
    esp_event_handler_instance_t relay_handler;
    esp_event_handler_instance_t safety_handler;
    dashboard_state_t rendered_state;
    uint32_t pending_flushes;
    bool rendered_stale;
    bool has_rendered_state;
    bool lvgl_task_running;
    bool started;
    bool initialized;
    bool destroying;
    bool lvgl_initialized_by_me;
    bool flush_cb_registered;
    bool tick_timer_started;
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**
 * @brief 应用看板默认配置
 * @details Apply dashboard default configuration
 * @param[in] config 输入配置； Input configuration
 * @param[out] out 输出配置； Output configuration
 */
static void lvgl_dashboard_apply_defaults(
    const lvgl_dashboard_config_t *config,
    lvgl_dashboard_config_t *out);

/**
 * @brief 清理创建失败资源
 * @details Cleanup resources after create failure
 * @param[in,out] me 看板对象； Dashboard object
 */
static void lvgl_dashboard_cleanup_create_failure(lvgl_dashboard_t *me);

/**
 * @brief 释放本地资源
 * @details Release locally owned resources
 * @param[in,out] me 看板对象； Dashboard object
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lvgl_dashboard_release_resources(lvgl_dashboard_t *me);

/**
 * @brief 创建控件树
 * @details Create LVGL widget tree
 * @param[in,out] me 看板对象； Dashboard object
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_NO_MEM: 内存不足； Out of memory
 *         - ESP_FAIL: LVGL 屏幕不可用； LVGL screen unavailable
 */
static esp_err_t lvgl_dashboard_create_widgets(lvgl_dashboard_t *me);

/**
 * @brief 注销业务事件处理器
 * @details Unregister business event handlers
 * @param[in,out] me 看板对象； Dashboard object
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lvgl_dashboard_unregister_handlers(lvgl_dashboard_t *me);

/**
 * @brief 记录一次待完成刷新
 * @details Track one pending display flush before submitting it to TFT
 * @param[in,out] me 看板对象； Dashboard object
 * @return true 已记录，false 不应提交； true if tracked
 */
static bool lvgl_dashboard_begin_flush(lvgl_dashboard_t *me);

/**
 * @brief 完成一次待刷新
 * @details Complete one tracked flush after LVGL has been notified
 * @param[in,out] me 看板对象； Dashboard object
 */
static void lvgl_dashboard_complete_flush(lvgl_dashboard_t *me);

/**
 * @brief 等待所有 TFT 刷新完成
 * @details Wait for all pending TFT flush callbacks to finish
 * @param[in,out] me 看板对象； Dashboard object
 * @return
 *         - ESP_OK: 全部完成； All flushes completed
 *         - ESP_ERR_TIMEOUT: 等待超时； Timed out
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 */
static esp_err_t lvgl_dashboard_wait_for_pending_flushes(
    lvgl_dashboard_t *me);

/**
 * @brief 创建 tick 定时器上下文
 * @details Create tick timer context
 * @param[in] period_ms tick 周期毫秒； Tick period in milliseconds
 * @return tick 上下文，失败返回 NULL； Tick context, NULL on failure
 */
static lvgl_dashboard_tick_ctx_t *lvgl_dashboard_tick_ctx_create(
    uint32_t period_ms);

/**
 * @brief 设置 tick 回调接收状态
 * @details Set whether tick callbacks are accepted
 * @param[in,out] ctx tick 上下文； Tick context
 * @param[in] accepting 是否接收回调； Whether callbacks are accepted
 */
static void lvgl_dashboard_tick_ctx_set_accepting(
    lvgl_dashboard_tick_ctx_t *ctx, bool accepting);

/**
 * @brief 开始处理 tick 回调
 * @details Begin one tick callback and copy its period
 * @param[in,out] ctx tick 上下文； Tick context
 * @param[out] out_period_ms tick 周期输出； Tick period output
 * @return true 执行 tick，false 跳过； true to run tick
 */
static bool lvgl_dashboard_tick_ctx_begin(lvgl_dashboard_tick_ctx_t *ctx,
                                          uint32_t *out_period_ms);

/**
 * @brief 完成 tick 回调
 * @details Complete one active tick callback
 * @param[in,out] ctx tick 上下文； Tick context
 */
static void lvgl_dashboard_tick_ctx_complete(lvgl_dashboard_tick_ctx_t *ctx);

/**
 * @brief 获取活跃 tick 回调数量
 * @details Get active tick callback count
 * @param[in,out] ctx tick 上下文； Tick context
 * @return 活跃回调数量； Active callback count
 */
static uint32_t lvgl_dashboard_tick_ctx_get_active(
    lvgl_dashboard_tick_ctx_t *ctx);

/**
 * @brief 等待 tick 回调退出
 * @details Wait until active tick callbacks are drained
 * @param[in,out] ctx tick 上下文； Tick context
 * @return
 *         - ESP_OK: 已退出； Drained
 *         - ESP_ERR_TIMEOUT: 等待超时； Timed out
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 */
static esp_err_t lvgl_dashboard_wait_for_tick_callbacks(
    lvgl_dashboard_tick_ctx_t *ctx);

/**
 * @brief 停止 tick 定时器并等待回调退出
 * @details Stop tick timer and drain callbacks
 * @param[in,out] me 看板对象； Dashboard object
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lvgl_dashboard_stop_tick_timer(lvgl_dashboard_t *me);

/**
 * @brief 删除 tick 定时器
 * @details Delete tick timer after callbacks are drained
 * @param[in,out] me 看板对象； Dashboard object
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t lvgl_dashboard_delete_tick_timer(lvgl_dashboard_t *me);

/**
 * @brief 通知 LVGL 刷新完成
 * @details Notify LVGL that a display flush completed
 * @param[in] user_ctx 用户上下文； User context
 */
static void lvgl_dashboard_flush_ready(void *user_ctx);

/**
 * @brief LVGL tick 定时器回调
 * @details LVGL tick timer callback
 * @param[in] arg 用户上下文； User context
 */
static void lvgl_dashboard_tick_timer_cb(void *arg);

/**
 * @brief LVGL 显示刷新回调
 * @details LVGL display flush callback
 * @param[in] display LVGL 显示对象； LVGL display object
 * @param[in] area 刷新区域； Flush area
 * @param[in] px_map RGB565 像素数据； RGB565 pixel data
 */
static void lvgl_dashboard_display_flush(lv_display_t *display,
                                         const lv_area_t *area,
                                         uint8_t *px_map);

/**
 * @brief LVGL 看板任务入口
 * @details LVGL dashboard task entry
 * @param[in] arg 看板对象； Dashboard object
 */
static void lvgl_dashboard_task(void *arg);

/**
 * @brief 处理电参量快照事件
 * @details Handle metering snapshot event
 * @param[in] arg 看板对象； Dashboard object
 * @param[in] base 事件基； Event base
 * @param[in] id 事件 ID； Event ID
 * @param[in] event_data 事件载荷； Event payload
 */
static void lvgl_dashboard_on_metering_snapshot(void *arg,
                                                esp_event_base_t base,
                                                int32_t id,
                                                void *event_data);

/**
 * @brief 处理继电器状态变化事件
 * @details Handle relay state changed event
 * @param[in] arg 看板对象； Dashboard object
 * @param[in] base 事件基； Event base
 * @param[in] id 事件 ID； Event ID
 * @param[in] event_data 事件载荷； Event payload
 */
static void lvgl_dashboard_on_relay_state_changed(void *arg,
                                                  esp_event_base_t base,
                                                  int32_t id,
                                                  void *event_data);

/**
 * @brief 处理安全快照事件
 * @details Handle safety snapshot event
 * @param[in] arg 看板对象； Dashboard object
 * @param[in] base 事件基； Event base
 * @param[in] id 事件 ID； Event ID
 * @param[in] event_data 事件载荷； Event payload
 */
static void lvgl_dashboard_on_safety_snapshot(void *arg,
                                             esp_event_base_t base,
                                             int32_t id,
                                             void *event_data);

/**
 * @brief 轮询网络状态
 * @details Poll network manager status into a render state
 * @param[in] me 看板对象； Dashboard object
 * @param[in,out] state 渲染状态； Render state
 */
static void lvgl_dashboard_poll_network(lvgl_dashboard_t *me,
                                        dashboard_state_t *state);

/**
 * @brief 判断链路是否正在连接
 * @details Check whether a link status is starting or connecting
 * @param[in] status 链路状态； Link status
 * @return true 正在连接，false 未连接； true if connecting
 */
static bool lvgl_dashboard_link_is_connecting(network_link_status_t status);

/**
 * @brief 应用状态到控件
 * @details Apply one dashboard state to LVGL widgets
 * @param[in,out] me 看板对象； Dashboard object
 * @param[in] state 看板状态； Dashboard state
 * @param[in] stale 电参量是否过期； Whether metering data is stale
 */
static void lvgl_dashboard_apply_state(lvgl_dashboard_t *me,
                                       const dashboard_state_t *state,
                                       bool stale);

/**
 * @brief 设置电参量控件透明度
 * @details Set metering widget opacity
 * @param[in,out] me 看板对象； Dashboard object
 * @param[in] stale 电参量是否过期； Whether metering data is stale
 */
static void lvgl_dashboard_set_metering_opacity(lvgl_dashboard_t *me,
                                                bool stale);

/**
 * @brief 获取继电器 pill 背景色
 * @details Get relay pill background color
 * @param[in] relay_known 继电器状态是否已知； Whether relay state is known
 * @param[in] relay_on 继电器是否打开； Whether relay is on
 * @return LVGL 颜色； LVGL color
 */
static lv_color_t lvgl_dashboard_relay_bg_color(bool relay_known,
                                                bool relay_on);

/**
 * @brief 获取网络 pill 背景色
 * @details Get network pill background color
 * @param[in] network 网络状态； Network state
 * @return LVGL 颜色； LVGL color
 */
static lv_color_t lvgl_dashboard_network_bg_color(
    dashboard_network_t network);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lvgl_dashboard_t *lvgl_dashboard_create(const lvgl_dashboard_config_t *config)
{
    esp_err_t ret = ESP_OK;
    lvgl_dashboard_config_t resolved = {0};
    int width = 0;
    int height = 0;
    size_t draw_buf_size = 0U;
    esp_timer_create_args_t tick_timer_args = {0};

    if (config == NULL || config->panel == NULL) {
        ESP_LOGE(TAG, "invalid dashboard config");
        return NULL;
    }

    lvgl_dashboard_apply_defaults(config, &resolved);
    width = tft_panel_get_width(resolved.panel);
    height = tft_panel_get_height(resolved.panel);
    if (width <= 0 || height <= 0) {
        ESP_LOGE(TAG, "invalid panel size");
        return NULL;
    }

    lvgl_dashboard_t *me = calloc(1, sizeof(*me));
    if (me == NULL) {
        ESP_LOGE(TAG, "calloc dashboard failed");
        return NULL;
    }
    me->config = resolved;

    portMUX_INITIALIZE(&me->flush_lock);

    me->mutex = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(me->mutex != NULL, ESP_ERR_NO_MEM, err, TAG,
                      "create mutex failed");
    me->lvgl_task_done_sema = xSemaphoreCreateBinary();
    ESP_GOTO_ON_FALSE(me->lvgl_task_done_sema != NULL, ESP_ERR_NO_MEM, err,
                      TAG, "create task done semaphore failed");
    me->tick_ctx = lvgl_dashboard_tick_ctx_create(
        me->config.lvgl_tick_period_ms);
    ESP_GOTO_ON_FALSE(me->tick_ctx != NULL, ESP_ERR_NO_MEM, err, TAG,
                      "create tick context failed");

    if (!lv_is_initialized()) {
        lv_init();
        me->lvgl_initialized_by_me = true;
    }

    me->display = lv_display_create(width, height);
    ESP_GOTO_ON_FALSE(me->display != NULL, ESP_FAIL, err, TAG,
                      "create lvgl display failed");

    ESP_GOTO_ON_ERROR(
        tft_panel_register_flush_done_cb(me->config.panel,
                                         lvgl_dashboard_flush_ready, me),
        err, TAG, "register panel flush callback failed");
    me->flush_cb_registered = true;

    draw_buf_size = (size_t)width * LVGL_DASHBOARD_DRAW_BUF_LINES *
                    (size_t)LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565);
    me->draw_buf_1 = heap_caps_malloc(draw_buf_size,
                                      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    me->draw_buf_2 = heap_caps_malloc(draw_buf_size,
                                      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_GOTO_ON_FALSE(me->draw_buf_1 != NULL && me->draw_buf_2 != NULL,
                      ESP_ERR_NO_MEM, err, TAG,
                      "allocate lvgl draw buffers failed");

    lv_display_set_default(me->display);
    lv_display_set_color_format(me->display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(me->display, lvgl_dashboard_display_flush);
    lv_display_set_user_data(me->display, me);
    lv_display_set_buffers(me->display, me->draw_buf_1, me->draw_buf_2,
                           draw_buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    ESP_GOTO_ON_ERROR(lvgl_dashboard_create_widgets(me), err, TAG,
                      "create dashboard widgets failed");

    tick_timer_args.callback = lvgl_dashboard_tick_timer_cb;
    tick_timer_args.arg = me->tick_ctx;
    tick_timer_args.name = "lvgl_tick";
    ESP_GOTO_ON_ERROR(esp_timer_create(&tick_timer_args, &me->tick_timer), err,
                      TAG, "create lvgl tick timer failed");

    me->state_cache.screen_enabled = true;
    me->state_cache.network = DASHBOARD_NET_OFFLINE;
    me->state_cache.safety_level = SAFETY_GUARD_LEVEL_NORMAL;
    me->initialized = true;

    return me;

err:
    ESP_LOGD(TAG, "create dashboard failed: %s", esp_err_to_name(ret));
    lvgl_dashboard_cleanup_create_failure(me);
    return NULL;
}

esp_err_t lvgl_dashboard_destroy(lvgl_dashboard_t *me)
{
    esp_err_t ret = ESP_OK;

    if (me == NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "dashboard is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    me->destroying = true;
    (void)xSemaphoreGive(me->mutex);

    ret = lvgl_dashboard_stop(me);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = lvgl_dashboard_wait_for_pending_flushes(me);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = tft_panel_register_flush_done_cb(me->config.panel, NULL, NULL);
    if (ret != ESP_OK) {
        return ret;
    }
    me->flush_cb_registered = false;

    return lvgl_dashboard_release_resources(me);
}

esp_err_t lvgl_dashboard_start(lvgl_dashboard_t *me)
{
    esp_err_t ret = ESP_OK;
    BaseType_t task_ret = pdFAIL;
    bool timer_started = false;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "dashboard is null");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "dashboard is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(me->tick_timer != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "tick timer is null");
    ESP_RETURN_ON_FALSE(me->tick_ctx != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "tick context is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    if (me->started) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_OK;
    }
    if (me->destroying || me->metering_handler != NULL ||
        me->relay_handler != NULL || me->safety_handler != NULL ||
        me->tick_timer_started || me->lvgl_task != NULL) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }

    memset(&me->rendered_state, 0, sizeof(me->rendered_state));
    me->rendered_stale = false;
    me->has_rendered_state = false;
    (void)xSemaphoreGive(me->mutex);

    ret = esp_event_handler_instance_register(METERING_EVENT_BASE,
                                              METERING_EVENT_SNAPSHOT,
                                              lvgl_dashboard_on_metering_snapshot,
                                              me, &me->metering_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register metering handler failed: %s",
                 esp_err_to_name(ret));
        goto err;
    }

    ret = esp_event_handler_instance_register(RELAY_EVENT_BASE,
                                              RELAY_EVENT_STATE_CHANGED,
                                              lvgl_dashboard_on_relay_state_changed,
                                              me, &me->relay_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register relay handler failed: %s",
                 esp_err_to_name(ret));
        goto err;
    }

    ret = esp_event_handler_instance_register(SAFETY_GUARD_EVENT_BASE,
                                              SAFETY_GUARD_EVENT_SNAPSHOT,
                                              lvgl_dashboard_on_safety_snapshot,
                                              me, &me->safety_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register safety handler failed: %s",
                 esp_err_to_name(ret));
        goto err;
    }

    lvgl_dashboard_tick_ctx_set_accepting(me->tick_ctx, true);
    ret = esp_timer_start_periodic(
        me->tick_timer, (uint64_t)me->config.lvgl_tick_period_ms * 1000ULL);
    if (ret != ESP_OK) {
        lvgl_dashboard_tick_ctx_set_accepting(me->tick_ctx, false);
        ESP_LOGE(TAG, "start tick timer failed: %s", esp_err_to_name(ret));
        goto err;
    }
    timer_started = true;

    ESP_GOTO_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                      ESP_ERR_TIMEOUT, err, TAG, "take mutex failed");
    me->tick_timer_started = true;
    me->started = true;
    me->lvgl_task_running = true;
    (void)xSemaphoreGive(me->mutex);

    task_ret = xTaskCreate(lvgl_dashboard_task, LVGL_DASHBOARD_TASK_NAME,
                           (uint32_t)me->config.lvgl_task_stack, me,
                           (UBaseType_t)me->config.lvgl_task_priority,
                           &me->lvgl_task);
    if (task_ret != pdPASS) {
        ret = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "create lvgl task failed");
        goto err;
    }

    return ESP_OK;

err:
    if (timer_started || me->tick_timer_started) {
        const esp_err_t stop_ret = lvgl_dashboard_stop_tick_timer(me);
        if (stop_ret != ESP_OK) {
            ESP_LOGW(TAG, "stop tick timer after start failure failed: %s",
                     esp_err_to_name(stop_ret));
        }
    } else {
        lvgl_dashboard_tick_ctx_set_accepting(me->tick_ctx, false);
    }
    (void)lvgl_dashboard_unregister_handlers(me);
    if (me->mutex != NULL && xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
        me->lvgl_task_running = false;
        me->started = false;
        (void)xSemaphoreGive(me->mutex);
    }
    return ret;
}

esp_err_t lvgl_dashboard_stop(lvgl_dashboard_t *me)
{
    esp_err_t first_error = ESP_OK;
    esp_err_t ret = ESP_OK;
    bool wait_for_task = false;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "dashboard is null");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "dashboard is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    if (!me->started && !me->lvgl_task_running && me->lvgl_task == NULL &&
        !me->tick_timer_started && me->metering_handler == NULL &&
        me->relay_handler == NULL && me->safety_handler == NULL) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_OK;
    }

    me->lvgl_task_running = false;
    wait_for_task = (me->lvgl_task != NULL);
    (void)xSemaphoreGive(me->mutex);

    if (wait_for_task) {
        ESP_RETURN_ON_FALSE(me->lvgl_task_done_sema != NULL,
                            ESP_ERR_INVALID_STATE, TAG,
                            "task done semaphore is null");
        if (xSemaphoreTake(me->lvgl_task_done_sema,
                           pdMS_TO_TICKS(LVGL_DASHBOARD_STOP_TIMEOUT_MS)) !=
            pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }

        ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                            ESP_ERR_TIMEOUT, TAG, "take mutex failed");
        me->lvgl_task = NULL;
        (void)xSemaphoreGive(me->mutex);
    }

    if (me->tick_timer != NULL) {
        ret = lvgl_dashboard_stop_tick_timer(me);
        if (ret != ESP_OK) {
            first_error = ret;
        }
    }

    ret = lvgl_dashboard_unregister_handlers(me);
    if (first_error == ESP_OK && ret != ESP_OK) {
        first_error = ret;
    }

    if (first_error == ESP_OK) {
        ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                            ESP_ERR_TIMEOUT, TAG, "take mutex failed");
        me->started = false;
        me->lvgl_task_running = false;
        me->tick_timer_started = false;
        (void)xSemaphoreGive(me->mutex);
    }

    return first_error;
}

esp_err_t lvgl_dashboard_set_screen_enabled(lvgl_dashboard_t *me, bool enabled)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "dashboard is null");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "dashboard is not initialized");
    ESP_RETURN_ON_FALSE(me->config.panel != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "panel is null");

    ret = tft_panel_set_backlight(me->config.panel, enabled);

    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return (ret != ESP_OK) ? ret : ESP_ERR_TIMEOUT;
    }
    if (!me->destroying) {
        me->state_cache.screen_enabled = enabled;
    }
    (void)xSemaphoreGive(me->mutex);

    return ret;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lvgl_dashboard_apply_defaults(
    const lvgl_dashboard_config_t *config,
    lvgl_dashboard_config_t *out)
{
    if (config == NULL || out == NULL) {
        return;
    }

    *out = *config;
    if (out->lvgl_task_stack <= 0) {
        out->lvgl_task_stack = (int)LVGL_DASHBOARD_DEFAULT_TASK_STACK;
    }
    if (out->lvgl_task_priority <= 0) {
        out->lvgl_task_priority = LVGL_DASHBOARD_DEFAULT_TASK_PRIORITY;
    }
    if (out->lvgl_tick_period_ms == 0U) {
        out->lvgl_tick_period_ms = LVGL_DASHBOARD_DEFAULT_TICK_PERIOD_MS;
    }
    if (out->update_period_ms == 0U) {
        out->update_period_ms = LVGL_DASHBOARD_DEFAULT_UPDATE_MS;
    }
}

static void lvgl_dashboard_cleanup_create_failure(lvgl_dashboard_t *me)
{
    if (me == NULL) {
        return;
    }

    if (me->flush_cb_registered && me->config.panel != NULL) {
        const esp_err_t ret = tft_panel_register_flush_done_cb(
            me->config.panel, NULL, NULL);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "clear panel flush callback failed: %s",
                     esp_err_to_name(ret));
        }
        me->flush_cb_registered = false;
    }

    const esp_err_t release_ret = lvgl_dashboard_release_resources(me);
    if (release_ret != ESP_OK) {
        ESP_LOGW(TAG, "release resources after create failure failed: %s",
                 esp_err_to_name(release_ret));
    }
}

static esp_err_t lvgl_dashboard_release_resources(lvgl_dashboard_t *me)
{
    esp_err_t ret = ESP_OK;

    if (me == NULL) {
        return ESP_OK;
    }

    if (me->tick_timer != NULL) {
        ret = lvgl_dashboard_delete_tick_timer(me);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "delete tick timer failed: %s",
                     esp_err_to_name(ret));
            return ret;
        }
    }
    if (me->tick_ctx != NULL) {
        lvgl_dashboard_tick_ctx_set_accepting(me->tick_ctx, false);
        /* Do not free: esp_timer may already have copied the callback arg
         * before stop/delete, and active-callback draining cannot see callbacks
         * that have not entered yet. Keep this tombstone to avoid callback UAF.
         */
        me->tick_ctx = NULL;
    }

    if (me->display != NULL) {
        lv_display_delete(me->display);
        me->display = NULL;
    }

    if (me->draw_buf_1 != NULL) {
        heap_caps_free(me->draw_buf_1);
        me->draw_buf_1 = NULL;
    }
    if (me->draw_buf_2 != NULL) {
        heap_caps_free(me->draw_buf_2);
        me->draw_buf_2 = NULL;
    }

    if (me->lvgl_task_done_sema != NULL) {
        vSemaphoreDelete(me->lvgl_task_done_sema);
        me->lvgl_task_done_sema = NULL;
    }
    if (me->mutex != NULL) {
        vSemaphoreDelete(me->mutex);
        me->mutex = NULL;
    }

    if (me->lvgl_initialized_by_me && lv_is_initialized()) {
        lv_deinit();
        me->lvgl_initialized_by_me = false;
    }

    free(me);
    return ESP_OK;
}

static esp_err_t lvgl_dashboard_create_widgets(lvgl_dashboard_t *me)
{
    lv_obj_t *power_title = NULL;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "dashboard is null");

    me->screen = lv_screen_active();
    ESP_RETURN_ON_FALSE(me->screen != NULL, ESP_FAIL, TAG,
                        "active screen is null");

    lv_obj_set_style_bg_color(me->screen,
                              lv_color_hex(LVGL_DASHBOARD_SCREEN_BG_HEX), 0);
    lv_obj_set_style_bg_opa(me->screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(me->screen,
                                lv_color_hex(LVGL_DASHBOARD_SCREEN_TEXT_HEX),
                                0);

    me->network_pill = lv_obj_create(me->screen);
    ESP_RETURN_ON_FALSE(me->network_pill != NULL, ESP_ERR_NO_MEM, TAG,
                        "create network pill failed");
    lv_obj_set_size(me->network_pill, LVGL_DASHBOARD_STATUS_PILL_WIDTH,
                    LVGL_DASHBOARD_STATUS_PILL_HEIGHT);
    lv_obj_align(me->network_pill, LV_ALIGN_TOP_MID, 0,
                 LVGL_DASHBOARD_NETWORK_BOX_Y);
    lv_obj_set_style_radius(me->network_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(me->network_pill, 0, 0);
    lv_obj_set_style_pad_all(me->network_pill, 0, 0);
    lv_obj_set_style_bg_opa(me->network_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(
        me->network_pill,
        lvgl_dashboard_network_bg_color(DASHBOARD_NET_OFFLINE), 0);

    me->relay_pill = lv_obj_create(me->screen);
    ESP_RETURN_ON_FALSE(me->relay_pill != NULL, ESP_ERR_NO_MEM, TAG,
                        "create relay pill failed");
    lv_obj_set_size(me->relay_pill, LVGL_DASHBOARD_STATUS_PILL_WIDTH,
                    LVGL_DASHBOARD_STATUS_PILL_HEIGHT);
    lv_obj_align(me->relay_pill, LV_ALIGN_TOP_MID, 0,
                 LVGL_DASHBOARD_RELAY_BOX_Y);
    lv_obj_set_style_radius(me->relay_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(me->relay_pill, 0, 0);
    lv_obj_set_style_pad_all(me->relay_pill, 0, 0);
    lv_obj_set_style_bg_opa(me->relay_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(me->relay_pill,
                              lvgl_dashboard_relay_bg_color(false, false), 0);

    me->label_network = lv_label_create(me->network_pill);
    ESP_RETURN_ON_FALSE(me->label_network != NULL, ESP_ERR_NO_MEM, TAG,
                        "create network label failed");
    lv_obj_center(me->label_network);

    me->label_relay = lv_label_create(me->relay_pill);
    ESP_RETURN_ON_FALSE(me->label_relay != NULL, ESP_ERR_NO_MEM, TAG,
                        "create relay label failed");
    lv_obj_center(me->label_relay);

    me->power_card = lv_obj_create(me->screen);
    ESP_RETURN_ON_FALSE(me->power_card != NULL, ESP_ERR_NO_MEM, TAG,
                        "create power card failed");
    lv_obj_set_size(me->power_card, LVGL_DASHBOARD_POWER_CARD_WIDTH,
                    LVGL_DASHBOARD_POWER_BOX_HEIGHT);
    lv_obj_align(me->power_card, LV_ALIGN_TOP_MID, 0,
                 LVGL_DASHBOARD_POWER_BOX_Y);
    lv_obj_set_style_bg_opa(me->power_card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(me->power_card,
                              lv_color_hex(LVGL_DASHBOARD_POWER_BG_HEX), 0);
    lv_obj_set_style_border_width(me->power_card, 0, 0);
    lv_obj_set_style_pad_all(me->power_card, 0, 0);

    me->label_power = lv_label_create(me->power_card);
    ESP_RETURN_ON_FALSE(me->label_power != NULL, ESP_ERR_NO_MEM, TAG,
                        "create power label failed");
    lv_obj_align(me->label_power, LV_ALIGN_TOP_MID, 0,
                 LVGL_DASHBOARD_POWER_VALUE_Y);
    lv_obj_set_style_text_font(me->label_power, LVGL_DASHBOARD_POWER_FONT, 0);

    power_title = lv_label_create(me->power_card);
    ESP_RETURN_ON_FALSE(power_title != NULL, ESP_ERR_NO_MEM, TAG,
                        "create power title failed");
    lv_label_set_text(power_title, "POWER");
    lv_obj_align(power_title, LV_ALIGN_TOP_MID, 0,
                 LVGL_DASHBOARD_POWER_TITLE_Y);
    lv_obj_set_style_text_font(power_title, LVGL_DASHBOARD_TITLE_FONT, 0);

    me->voltage_card = lv_obj_create(me->screen);
    ESP_RETURN_ON_FALSE(me->voltage_card != NULL, ESP_ERR_NO_MEM, TAG,
                        "create voltage card failed");
    lv_obj_set_size(me->voltage_card, LVGL_DASHBOARD_METRIC_CARD_WIDTH,
                    LVGL_DASHBOARD_METRIC_CARD_HEIGHT);
    lv_obj_align(me->voltage_card, LV_ALIGN_TOP_LEFT,
                 LVGL_DASHBOARD_METRIC_MARGIN_X,
                 LVGL_DASHBOARD_METRIC_CARD_Y);
    lv_obj_set_style_radius(me->voltage_card, 12, 0);
    lv_obj_set_style_border_width(me->voltage_card, 0, 0);
    lv_obj_set_style_bg_opa(me->voltage_card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(me->voltage_card,
                              lv_color_hex(LVGL_DASHBOARD_VOLTAGE_BG_HEX), 0);
    lv_obj_set_style_pad_all(me->voltage_card, 0, 0);

    me->label_voltage = lv_label_create(me->voltage_card);
    ESP_RETURN_ON_FALSE(me->label_voltage != NULL, ESP_ERR_NO_MEM, TAG,
                        "create voltage label failed");
    lv_obj_center(me->label_voltage);
    lv_obj_set_style_text_font(me->label_voltage, LVGL_DASHBOARD_METRIC_FONT,
                               0);

    me->current_card = lv_obj_create(me->screen);
    ESP_RETURN_ON_FALSE(me->current_card != NULL, ESP_ERR_NO_MEM, TAG,
                        "create current card failed");
    lv_obj_set_size(me->current_card, LVGL_DASHBOARD_METRIC_CARD_WIDTH,
                    LVGL_DASHBOARD_METRIC_CARD_HEIGHT);
    lv_obj_align(me->current_card, LV_ALIGN_TOP_RIGHT,
                 -LVGL_DASHBOARD_METRIC_MARGIN_X,
                 LVGL_DASHBOARD_METRIC_CARD_Y);
    lv_obj_set_style_radius(me->current_card, 12, 0);
    lv_obj_set_style_border_width(me->current_card, 0, 0);
    lv_obj_set_style_bg_opa(me->current_card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(me->current_card,
                              lv_color_hex(LVGL_DASHBOARD_CURRENT_BG_HEX), 0);
    lv_obj_set_style_pad_all(me->current_card, 0, 0);

    me->label_current = lv_label_create(me->current_card);
    ESP_RETURN_ON_FALSE(me->label_current != NULL, ESP_ERR_NO_MEM, TAG,
                        "create current label failed");
    lv_obj_center(me->label_current);
    lv_obj_set_style_text_font(me->label_current, LVGL_DASHBOARD_METRIC_FONT,
                               0);

    me->label_energy = lv_label_create(me->screen);
    ESP_RETURN_ON_FALSE(me->label_energy != NULL, ESP_ERR_NO_MEM, TAG,
                        "create energy label failed");
    lv_obj_align(me->label_energy, LV_ALIGN_BOTTOM_MID, 0,
                 LVGL_DASHBOARD_BOTTOM_ENERGY_Y);

    me->label_safety = lv_label_create(me->screen);
    ESP_RETURN_ON_FALSE(me->label_safety != NULL, ESP_ERR_NO_MEM, TAG,
                        "create safety label failed");
    lv_obj_align(me->label_safety, LV_ALIGN_BOTTOM_MID, 0,
                 LVGL_DASHBOARD_BOTTOM_SAFETY_Y);

    lv_label_set_text(me->label_network, "OFFLINE");
    lv_label_set_text(me->label_relay, "RELAY ?");
    lv_label_set_text(me->label_power, LVGL_DASHBOARD_PLACEHOLDER_POWER);
    lv_label_set_text(me->label_voltage, LVGL_DASHBOARD_PLACEHOLDER_VOLTAGE);
    lv_label_set_text(me->label_current, LVGL_DASHBOARD_PLACEHOLDER_CURRENT);
    lv_label_set_text(me->label_energy, LVGL_DASHBOARD_PLACEHOLDER_ENERGY);
    lv_label_set_text(me->label_safety, "SAFETY ?");

    return ESP_OK;
}

static esp_err_t lvgl_dashboard_unregister_handlers(lvgl_dashboard_t *me)
{
    esp_err_t first_error = ESP_OK;
    esp_err_t ret = ESP_OK;

    if (me == NULL) {
        return ESP_OK;
    }

    if (me->metering_handler != NULL) {
        ret = esp_event_handler_instance_unregister(METERING_EVENT_BASE,
                                                    METERING_EVENT_SNAPSHOT,
                                                    me->metering_handler);
        if (ret == ESP_OK) {
            me->metering_handler = NULL;
        } else {
            first_error = ret;
            ESP_LOGW(TAG, "unregister metering handler failed: %s",
                     esp_err_to_name(ret));
        }
    }

    if (me->relay_handler != NULL) {
        ret = esp_event_handler_instance_unregister(RELAY_EVENT_BASE,
                                                    RELAY_EVENT_STATE_CHANGED,
                                                    me->relay_handler);
        if (ret == ESP_OK) {
            me->relay_handler = NULL;
        } else {
            if (first_error == ESP_OK) {
                first_error = ret;
            }
            ESP_LOGW(TAG, "unregister relay handler failed: %s",
                     esp_err_to_name(ret));
        }
    }

    if (me->safety_handler != NULL) {
        ret = esp_event_handler_instance_unregister(SAFETY_GUARD_EVENT_BASE,
                                                    SAFETY_GUARD_EVENT_SNAPSHOT,
                                                    me->safety_handler);
        if (ret == ESP_OK) {
            me->safety_handler = NULL;
        } else {
            if (first_error == ESP_OK) {
                first_error = ret;
            }
            ESP_LOGW(TAG, "unregister safety handler failed: %s",
                     esp_err_to_name(ret));
        }
    }

    return first_error;
}

static bool lvgl_dashboard_begin_flush(lvgl_dashboard_t *me)
{
    bool tracked = false;

    if (me == NULL) {
        return false;
    }

    portENTER_CRITICAL_SAFE(&me->flush_lock);
    if (me->initialized && !me->destroying &&
        me->pending_flushes < UINT32_MAX) {
        me->pending_flushes++;
        tracked = true;
    }
    portEXIT_CRITICAL_SAFE(&me->flush_lock);

    return tracked;
}

static void lvgl_dashboard_complete_flush(lvgl_dashboard_t *me)
{
    if (me == NULL) {
        return;
    }

    portENTER_CRITICAL_SAFE(&me->flush_lock);
    if (me->pending_flushes > 0U) {
        me->pending_flushes--;
    }
    portEXIT_CRITICAL_SAFE(&me->flush_lock);
}

static esp_err_t lvgl_dashboard_wait_for_pending_flushes(
    lvgl_dashboard_t *me)
{
    const TickType_t timeout_ticks =
        pdMS_TO_TICKS(LVGL_DASHBOARD_FLUSH_TIMEOUT_MS);
    const TickType_t default_poll_ticks =
        pdMS_TO_TICKS(LVGL_DASHBOARD_FLUSH_POLL_MS);
    const TickType_t start_ticks = xTaskGetTickCount();

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "dashboard is null");

    while (true) {
        TickType_t delay_ticks = 0;
        TickType_t elapsed_ticks = 0;
        TickType_t remaining_ticks = 0;
        uint32_t pending_flushes = 0U;

        portENTER_CRITICAL_SAFE(&me->flush_lock);
        pending_flushes = me->pending_flushes;
        portEXIT_CRITICAL_SAFE(&me->flush_lock);

        if (pending_flushes == 0U) {
            return ESP_OK;
        }

        elapsed_ticks = xTaskGetTickCount() - start_ticks;
        if (elapsed_ticks >= timeout_ticks) {
            return ESP_ERR_TIMEOUT;
        }
        remaining_ticks = timeout_ticks - elapsed_ticks;
        delay_ticks = (default_poll_ticks > 0U) ? default_poll_ticks : 1U;
        if (delay_ticks > remaining_ticks) {
            delay_ticks = remaining_ticks;
        }
        if (delay_ticks == 0U) {
            delay_ticks = 1U;
        }
        vTaskDelay(delay_ticks);
    }
}

static lvgl_dashboard_tick_ctx_t *lvgl_dashboard_tick_ctx_create(
    uint32_t period_ms)
{
    lvgl_dashboard_tick_ctx_t *ctx = calloc(1, sizeof(*ctx));

    if (ctx == NULL) {
        return NULL;
    }

    portMUX_INITIALIZE(&ctx->lock);
    ctx->period_ms = period_ms;
    ctx->accepting = false;

    return ctx;
}

static void lvgl_dashboard_tick_ctx_set_accepting(
    lvgl_dashboard_tick_ctx_t *ctx, bool accepting)
{
    if (ctx == NULL) {
        return;
    }

    portENTER_CRITICAL_SAFE(&ctx->lock);
    ctx->accepting = accepting;
    portEXIT_CRITICAL_SAFE(&ctx->lock);
}

static bool lvgl_dashboard_tick_ctx_begin(lvgl_dashboard_tick_ctx_t *ctx,
                                          uint32_t *out_period_ms)
{
    bool accepting = false;
    bool active = false;

    if (ctx == NULL || out_period_ms == NULL) {
        return false;
    }

    *out_period_ms = 0U;
    portENTER_CRITICAL_SAFE(&ctx->lock);
    if (ctx->active_callbacks < UINT32_MAX) {
        ctx->active_callbacks++;
        active = true;
        accepting = ctx->accepting;
        *out_period_ms = ctx->period_ms;
    }
    portEXIT_CRITICAL_SAFE(&ctx->lock);

    if (!active) {
        return false;
    }
    if (!accepting) {
        lvgl_dashboard_tick_ctx_complete(ctx);
        return false;
    }

    return true;
}

static void lvgl_dashboard_tick_ctx_complete(lvgl_dashboard_tick_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    portMUX_TYPE *lock = &ctx->lock;
    portENTER_CRITICAL_SAFE(lock);
    if (ctx->active_callbacks > 0U) {
        ctx->active_callbacks--;
    }
    portEXIT_CRITICAL_SAFE(lock);
}

static uint32_t lvgl_dashboard_tick_ctx_get_active(
    lvgl_dashboard_tick_ctx_t *ctx)
{
    uint32_t active_callbacks = 0U;

    if (ctx == NULL) {
        return 0U;
    }

    portENTER_CRITICAL_SAFE(&ctx->lock);
    active_callbacks = ctx->active_callbacks;
    portEXIT_CRITICAL_SAFE(&ctx->lock);

    return active_callbacks;
}

static esp_err_t lvgl_dashboard_wait_for_tick_callbacks(
    lvgl_dashboard_tick_ctx_t *ctx)
{
    const TickType_t timeout_ticks =
        pdMS_TO_TICKS(LVGL_DASHBOARD_TICK_TIMEOUT_MS);
    const TickType_t default_poll_ticks =
        pdMS_TO_TICKS(LVGL_DASHBOARD_TICK_POLL_MS);
    const TickType_t start_ticks = xTaskGetTickCount();

    ESP_RETURN_ON_FALSE(ctx != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "tick context is null");

    while (true) {
        TickType_t delay_ticks = 0;
        TickType_t elapsed_ticks = 0;
        TickType_t remaining_ticks = 0;

        if (lvgl_dashboard_tick_ctx_get_active(ctx) == 0U) {
            return ESP_OK;
        }

        elapsed_ticks = xTaskGetTickCount() - start_ticks;
        if (elapsed_ticks >= timeout_ticks) {
            return ESP_ERR_TIMEOUT;
        }
        remaining_ticks = timeout_ticks - elapsed_ticks;
        delay_ticks = (default_poll_ticks > 0U) ? default_poll_ticks : 1U;
        if (delay_ticks > remaining_ticks) {
            delay_ticks = remaining_ticks;
        }
        if (delay_ticks == 0U) {
            delay_ticks = 1U;
        }
        vTaskDelay(delay_ticks);
    }
}

static esp_err_t lvgl_dashboard_stop_tick_timer(lvgl_dashboard_t *me)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "dashboard is null");
    ESP_RETURN_ON_FALSE(me->tick_ctx != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "tick context is null");

    lvgl_dashboard_tick_ctx_set_accepting(me->tick_ctx, false);

    if (me->tick_timer != NULL && me->tick_timer_started) {
        ret = esp_timer_stop(me->tick_timer);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }
    }

    ret = lvgl_dashboard_wait_for_tick_callbacks(me->tick_ctx);
    if (ret == ESP_OK) {
        me->tick_timer_started = false;
    }

    return ret;
}

static esp_err_t lvgl_dashboard_delete_tick_timer(lvgl_dashboard_t *me)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "dashboard is null");

    ret = lvgl_dashboard_stop_tick_timer(me);
    if (ret != ESP_OK) {
        return ret;
    }

    if (me->tick_timer != NULL) {
        ret = esp_timer_delete(me->tick_timer);
        if (ret != ESP_OK) {
            return ret;
        }
        me->tick_timer = NULL;
    }

    return lvgl_dashboard_wait_for_tick_callbacks(me->tick_ctx);
}

static void lvgl_dashboard_flush_ready(void *user_ctx)
{
    lvgl_dashboard_t *me = (lvgl_dashboard_t *)user_ctx;

    if (me == NULL) {
        return;
    }

    if (me->display != NULL) {
        lv_display_flush_ready(me->display);
    }
    lvgl_dashboard_complete_flush(me);
}

static void lvgl_dashboard_tick_timer_cb(void *arg)
{
    lvgl_dashboard_tick_ctx_t *ctx = (lvgl_dashboard_tick_ctx_t *)arg;
    uint32_t period_ms = 0U;

    if (!lvgl_dashboard_tick_ctx_begin(ctx, &period_ms)) {
        return;
    }

    lv_tick_inc(period_ms);
    lvgl_dashboard_tick_ctx_complete(ctx);
}

static void lvgl_dashboard_display_flush(lv_display_t *display,
                                         const lv_area_t *area,
                                         uint8_t *px_map)
{
    lvgl_dashboard_t *me = NULL;
    esp_err_t ret = ESP_OK;

    if (display == NULL || area == NULL || px_map == NULL) {
        if (display != NULL) {
            lv_display_flush_ready(display);
        }
        return;
    }

    me = (lvgl_dashboard_t *)lv_display_get_user_data(display);
    if (me == NULL || me->config.panel == NULL) {
        lv_display_flush_ready(display);
        return;
    }

    if (!lvgl_dashboard_begin_flush(me)) {
        lv_display_flush_ready(display);
        return;
    }

    ret = tft_panel_draw_bitmap(me->config.panel, (int)area->x1,
                                (int)area->y1, (int)area->x2 + 1,
                                (int)area->y2 + 1, px_map);
    if (ret != ESP_OK) {
        lv_display_flush_ready(display);
        lvgl_dashboard_complete_flush(me);
    }
}

static void lvgl_dashboard_task(void *arg)
{
    lvgl_dashboard_t *me = (lvgl_dashboard_t *)arg;
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t delay_ticks = 1;

    if (me == NULL) {
        vTaskDelete(NULL);
        return;
    }

    delay_ticks = pdMS_TO_TICKS(me->config.update_period_ms);
    if (delay_ticks == 0) {
        delay_ticks = 1;
    }

    while (true) {
        dashboard_state_t state = {0};
        bool running = false;
        bool stale = false;

        if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
            running = me->lvgl_task_running;
            state = me->state_cache;
            (void)xSemaphoreGive(me->mutex);
        }
        if (!running) {
            break;
        }

        lvgl_dashboard_poll_network(me, &state);
        stale = lvgl_dashboard_internal_is_stale(
            state.metering_valid, state.last_update_us,
            (uint64_t)esp_timer_get_time());

        if (lvgl_dashboard_internal_should_apply_state(
                me->has_rendered_state, &me->rendered_state,
                me->rendered_stale, &state, stale)) {
            lvgl_dashboard_apply_state(me, &state, stale);
            me->rendered_state = state;
            me->rendered_stale = stale;
            me->has_rendered_state = true;
        }

        (void)lv_timer_handler();
        vTaskDelayUntil(&last_wake, delay_ticks);
    }

    if (me->lvgl_task_done_sema != NULL) {
        (void)xSemaphoreGive(me->lvgl_task_done_sema);
    }
    vTaskDelete(NULL);
}

static void lvgl_dashboard_on_metering_snapshot(void *arg,
                                                esp_event_base_t base,
                                                int32_t id,
                                                void *event_data)
{
    lvgl_dashboard_t *me = (lvgl_dashboard_t *)arg;
    const metering_snapshot_t *snapshot =
        (const metering_snapshot_t *)event_data;

    if (me == NULL || base != METERING_EVENT_BASE ||
        id != METERING_EVENT_SNAPSHOT || snapshot == NULL ||
        me->mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (!me->initialized || me->destroying) {
        (void)xSemaphoreGive(me->mutex);
        return;
    }

    me->state_cache.voltage = snapshot->voltage;
    me->state_cache.current = snapshot->current;
    me->state_cache.power = snapshot->power;
    me->state_cache.energy_delta = snapshot->energy_delta;
    me->state_cache.metering_valid = snapshot->valid;
    me->state_cache.last_update_us =
        (snapshot->timestamp_us != 0ULL) ? snapshot->timestamp_us
                                        : (uint64_t)esp_timer_get_time();
    (void)xSemaphoreGive(me->mutex);
}

static void lvgl_dashboard_on_relay_state_changed(void *arg,
                                                  esp_event_base_t base,
                                                  int32_t id,
                                                  void *event_data)
{
    lvgl_dashboard_t *me = (lvgl_dashboard_t *)arg;
    const relay_state_changed_event_t *event =
        (const relay_state_changed_event_t *)event_data;

    if (me == NULL || base != RELAY_EVENT_BASE ||
        id != RELAY_EVENT_STATE_CHANGED || event == NULL ||
        me->mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (!me->initialized || me->destroying) {
        (void)xSemaphoreGive(me->mutex);
        return;
    }

    me->state_cache.relay_on = event->on;
    me->state_cache.relay_known = true;
    (void)xSemaphoreGive(me->mutex);
}

static void lvgl_dashboard_on_safety_snapshot(void *arg,
                                             esp_event_base_t base,
                                             int32_t id,
                                             void *event_data)
{
    lvgl_dashboard_t *me = (lvgl_dashboard_t *)arg;
    const safety_guard_snapshot_t *snapshot =
        (const safety_guard_snapshot_t *)event_data;

    if (me == NULL || base != SAFETY_GUARD_EVENT_BASE ||
        id != SAFETY_GUARD_EVENT_SNAPSHOT || snapshot == NULL ||
        me->mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (!me->initialized || me->destroying) {
        (void)xSemaphoreGive(me->mutex);
        return;
    }

    me->state_cache.safety_level = snapshot->level;
    me->state_cache.safety_valid = snapshot->valid;
    (void)xSemaphoreGive(me->mutex);
}

static void lvgl_dashboard_poll_network(lvgl_dashboard_t *me,
                                        dashboard_state_t *state)
{
    network_manager_status_t status = {0};

    if (me == NULL || state == NULL) {
        return;
    }

    state->network = DASHBOARD_NET_OFFLINE;
    state->network_ready = false;

    if (me->config.network_manager == NULL) {
        return;
    }
    if (network_manager_get_status(me->config.network_manager, &status) !=
        ESP_OK) {
        return;
    }

    if (status.ready && status.active_link == NETWORK_LINK_TYPE_WIFI) {
        state->network = DASHBOARD_NET_WIFI;
        state->network_ready = true;
    } else if (status.ready && status.active_link == NETWORK_LINK_TYPE_LTE) {
        state->network = DASHBOARD_NET_LTE;
        state->network_ready = true;
    } else if (!status.ready &&
               (lvgl_dashboard_link_is_connecting(status.primary_status) ||
                lvgl_dashboard_link_is_connecting(status.backup_status))) {
        state->network = DASHBOARD_NET_CONNECTING;
        state->network_ready = false;
    }
}

static bool lvgl_dashboard_link_is_connecting(network_link_status_t status)
{
    return status == NETWORK_LINK_STATUS_STARTING ||
           status == NETWORK_LINK_STATUS_CONNECTING;
}

static void lvgl_dashboard_apply_state(lvgl_dashboard_t *me,
                                       const dashboard_state_t *state,
                                       bool stale)
{
    char power_text[32] = LVGL_DASHBOARD_PLACEHOLDER_POWER;
    char voltage_text[32] = LVGL_DASHBOARD_PLACEHOLDER_VOLTAGE;
    char current_text[32] = LVGL_DASHBOARD_PLACEHOLDER_CURRENT;
    char energy_text[32] = LVGL_DASHBOARD_PLACEHOLDER_ENERGY;
    char safety_text[48] = {0};
    const char *relay_text = "RELAY ?";
    const char *safety_state_text = NULL;
    const char *bottom_status_text = NULL;
    const char *safety_label_text = NULL;
    int written = 0;

    if (me == NULL || state == NULL) {
        return;
    }

    if (state->metering_valid) {
        if (lvgl_dashboard_internal_format_power(
                power_text, sizeof(power_text), state->power) != ESP_OK) {
            strcpy(power_text, LVGL_DASHBOARD_PLACEHOLDER_POWER);
        }
        if (lvgl_dashboard_internal_format_voltage(
                voltage_text, sizeof(voltage_text), state->voltage) != ESP_OK) {
            strcpy(voltage_text, LVGL_DASHBOARD_PLACEHOLDER_VOLTAGE);
        }
        if (lvgl_dashboard_internal_format_current(
                current_text, sizeof(current_text), state->current) != ESP_OK) {
            strcpy(current_text, LVGL_DASHBOARD_PLACEHOLDER_CURRENT);
        }
        if (lvgl_dashboard_internal_format_energy(
                energy_text, sizeof(energy_text), state->energy_delta) !=
            ESP_OK) {
            strcpy(energy_text, LVGL_DASHBOARD_PLACEHOLDER_ENERGY);
        }
    }

    if (state->relay_known) {
        relay_text = state->relay_on ? "RELAY ON" : "RELAY OFF";
    }
    safety_state_text = lvgl_dashboard_internal_safety_text(
        state->safety_level, state->safety_valid);
    bottom_status_text = lvgl_dashboard_internal_bottom_status_text(
        state->metering_valid, stale);
    safety_label_text = safety_state_text;
    written = snprintf(safety_text, sizeof(safety_text), "%s | %s",
                       safety_state_text, bottom_status_text);
    if (written > 0 && (size_t)written < sizeof(safety_text)) {
        safety_label_text = safety_text;
    }

    lv_obj_set_style_bg_color(
        me->relay_pill,
        lvgl_dashboard_relay_bg_color(state->relay_known, state->relay_on),
        0);
    lv_obj_set_style_bg_color(
        me->network_pill, lvgl_dashboard_network_bg_color(state->network), 0);

    lv_label_set_text(me->label_network,
                      lvgl_dashboard_internal_network_text(state->network));
    lv_label_set_text(me->label_relay, relay_text);
    lv_label_set_text(me->label_power, power_text);
    lv_label_set_text(me->label_voltage, voltage_text);
    lv_label_set_text(me->label_current, current_text);
    lv_label_set_text(me->label_energy, energy_text);
    lv_label_set_text(me->label_safety, safety_label_text);

    lvgl_dashboard_set_metering_opacity(me, stale);
}

static void lvgl_dashboard_set_metering_opacity(lvgl_dashboard_t *me,
                                                bool stale)
{
    const lv_opa_t opacity = stale ? LV_OPA_70 : LV_OPA_COVER;

    if (me == NULL) {
        return;
    }

    lv_obj_set_style_opa(me->power_card, opacity, 0);
    lv_obj_set_style_opa(me->voltage_card, opacity, 0);
    lv_obj_set_style_opa(me->current_card, opacity, 0);
    lv_obj_set_style_text_opa(me->label_power, opacity, 0);
    lv_obj_set_style_text_opa(me->label_voltage, opacity, 0);
    lv_obj_set_style_text_opa(me->label_current, opacity, 0);
    lv_obj_set_style_text_opa(me->label_energy, opacity, 0);
}

static lv_color_t lvgl_dashboard_relay_bg_color(bool relay_known,
                                                bool relay_on)
{
    if (!relay_known) {
        return lv_color_hex(LVGL_DASHBOARD_NEUTRAL_BG_HEX);
    }

    return relay_on ? lv_color_hex(LVGL_DASHBOARD_RELAY_ON_BG_HEX)
                    : lv_color_hex(LVGL_DASHBOARD_RELAY_OFF_BG_HEX);
}

static lv_color_t lvgl_dashboard_network_bg_color(
    dashboard_network_t network)
{
    switch (network) {
        case DASHBOARD_NET_WIFI:
            return lv_color_hex(LVGL_DASHBOARD_NETWORK_WIFI_BG_HEX);
        case DASHBOARD_NET_LTE:
            return lv_color_hex(LVGL_DASHBOARD_NETWORK_LTE_BG_HEX);
        case DASHBOARD_NET_CONNECTING:
            return lv_color_hex(LVGL_DASHBOARD_CONNECTING_BG_HEX);
        case DASHBOARD_NET_OFFLINE:
        default:
            return lv_color_hex(LVGL_DASHBOARD_OFFLINE_BG_HEX);
    }
}
