#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_controller.h"
#include "app_controller_internal.h"
#include "bl0942.h"
#include "board_pinmap.h"
#include "button.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl_dashboard.h"
#include "metering_service.h"
#include "network_manager.h"
#include "relay.h"
#include "safety_guard.h"
#include "thingsboard_client.h"
#include "thingsboard_client_internal.h"

static void host_log_sink(const char *tag, const char *fmt, ...)
{
    (void)tag;
    (void)fmt;
}

#undef ESP_LOGE
#undef ESP_LOGW
#undef ESP_LOGI
#define ESP_LOGE(tag, fmt, ...) host_log_sink(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) host_log_sink(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) host_log_sink(tag, fmt, ##__VA_ARGS__)

typedef struct host_test_mutex {
    bool locked;
    bool deleted;
} host_test_mutex_t;

typedef struct {
    bool used;
    bool registered_with_loop;
    esp_event_base_t base;
    int32_t id;
    esp_event_loop_handle_t event_loop;
    esp_event_handler_t handler;
    void *arg;
} host_event_handler_slot_t;

struct relay {
    bool on;
};

struct button {
    button_event_cb_t callbacks[BUTTON_EVENT_MAX];
    void *callback_ctx[BUTTON_EVENT_MAX];
};

struct bl0942 {
    bool started;
};

struct metering_service {
    bool started;
    int confirm_calls;
    int discard_calls;
};

struct safety_guard {
    bool started;
    esp_event_handler_instance_t metering_handler;
    safety_guard_snapshot_t latest;
    float overcurrent_a;
    float overpower_w;
};

struct network_manager {
    bool started;
    network_manager_status_t status;
};

struct thingsboard_client {
    bool started;
    tb_command_cb_t cmd_cb;
    void *cmd_ctx;
};

struct lvgl_dashboard {
    bool started;
    bool screen_enabled;
};

ESP_EVENT_DEFINE_BASE(METERING_EVENT_BASE);
ESP_EVENT_DEFINE_BASE(RELAY_EVENT_BASE);
ESP_EVENT_DEFINE_BASE(SAFETY_GUARD_EVENT_BASE);

static void app_controller_on_startup_metering_snapshot(void *handler_args,
                                                        esp_event_base_t event_base,
                                                        int32_t event_id,
                                                        void *event_data);
static void app_controller_on_metering_snapshot(void *handler_args,
                                                esp_event_base_t event_base,
                                                int32_t event_id,
                                                void *event_data);
static void dispatch_event(esp_event_base_t base, int32_t id, void *event_data);

static host_event_handler_slot_t s_handlers[8];
static size_t s_handler_count = 0U;
static const char *s_dispatch_trace[8];
static size_t s_dispatch_trace_count = 0U;
static int s_publish_telemetry_calls = 0;
static bool s_last_telemetry_relay_on = false;
static bool s_last_telemetry_relay_known = false;
static bool s_fail_late_app_metering_register = false;
static unsigned int s_metering_register_count = 0U;
static esp_event_handler_t s_last_failed_metering_handler = NULL;
static bool s_emit_metering_snapshot_during_safety_start = false;
static metering_snapshot_t s_startup_metering_snapshot;
static esp_event_handler_instance_t s_startup_metering_handler_instance = NULL;
static bool s_emit_metering_snapshot_during_startup_unregister = false;
static metering_snapshot_t s_startup_handoff_metering_snapshot;
static uint32_t s_task_delay_calls = 0U;

static void reset_host_state(void)
{
    memset(s_handlers, 0, sizeof(s_handlers));
    s_handler_count = 0U;
    memset(s_dispatch_trace, 0, sizeof(s_dispatch_trace));
    s_dispatch_trace_count = 0U;
    s_publish_telemetry_calls = 0;
    s_last_telemetry_relay_on = false;
    s_last_telemetry_relay_known = false;
    s_fail_late_app_metering_register = false;
    s_metering_register_count = 0U;
    s_last_failed_metering_handler = NULL;
    s_emit_metering_snapshot_during_safety_start = false;
    memset(&s_startup_metering_snapshot, 0, sizeof(s_startup_metering_snapshot));
    s_startup_metering_handler_instance = NULL;
    s_emit_metering_snapshot_during_startup_unregister = false;
    memset(&s_startup_handoff_metering_snapshot, 0,
           sizeof(s_startup_handoff_metering_snapshot));
    s_task_delay_calls = 0U;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return (SemaphoreHandle_t)calloc(1, sizeof(host_test_mutex_t));
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t ticks_to_wait)
{
    host_test_mutex_t *host_mutex = (host_test_mutex_t *)mutex;

    (void)ticks_to_wait;
    assert(host_mutex != NULL);
    assert(host_mutex->deleted == false);
    assert(host_mutex->locked == false);
    host_mutex->locked = true;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex)
{
    host_test_mutex_t *host_mutex = (host_test_mutex_t *)mutex;

    assert(host_mutex != NULL);
    assert(host_mutex->deleted == false);
    assert(host_mutex->locked == true);
    host_mutex->locked = false;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t mutex)
{
    host_test_mutex_t *host_mutex = (host_test_mutex_t *)mutex;

    if (host_mutex == NULL) {
        return;
    }

    host_mutex->deleted = true;
    free(host_mutex);
}

void vTaskDelay(TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    s_task_delay_calls++;
}

static esp_err_t host_register_handler(bool registered_with_loop,
                                       esp_event_loop_handle_t event_loop,
                                       esp_event_base_t event_base,
                                       int32_t event_id,
                                       esp_event_handler_t event_handler,
                                       void *event_handler_arg,
                                       esp_event_handler_instance_t *instance)
{
    assert(event_handler != NULL);
    assert(s_handler_count < (sizeof(s_handlers) / sizeof(s_handlers[0])));

    if (event_base == METERING_EVENT_BASE &&
        event_id == METERING_EVENT_SNAPSHOT) {
        if (s_fail_late_app_metering_register &&
            s_metering_register_count == 2U &&
            event_handler == app_controller_on_metering_snapshot) {
            s_last_failed_metering_handler = event_handler;
            return ESP_FAIL;
        }
        s_metering_register_count++;
    }

    s_handlers[s_handler_count] = (host_event_handler_slot_t){
        .used = true,
        .registered_with_loop = registered_with_loop,
        .base = event_base,
        .id = event_id,
        .event_loop = event_loop,
        .handler = event_handler,
        .arg = event_handler_arg,
    };

    if (instance != NULL) {
        *instance = (esp_event_handler_instance_t)&s_handlers[s_handler_count];
    }

    if (event_base == METERING_EVENT_BASE &&
        event_id == METERING_EVENT_SNAPSHOT &&
        event_handler == app_controller_on_startup_metering_snapshot) {
        s_startup_metering_handler_instance =
            (esp_event_handler_instance_t)&s_handlers[s_handler_count];
    }

    s_handler_count++;
    return ESP_OK;
}

esp_err_t esp_event_handler_instance_register(
    esp_event_base_t event_base, int32_t event_id,
    esp_event_handler_t event_handler, void *event_handler_arg,
    esp_event_handler_instance_t *instance)
{
    return host_register_handler(false, NULL, event_base, event_id,
                                 event_handler,
                                 event_handler_arg, instance);
}

esp_err_t esp_event_handler_instance_register_with(
    esp_event_loop_handle_t event_loop, esp_event_base_t event_base,
    int32_t event_id, esp_event_handler_t event_handler,
    void *event_handler_arg, esp_event_handler_instance_t *instance)
{
    return host_register_handler(true, event_loop, event_base, event_id,
                                 event_handler,
                                 event_handler_arg, instance);
}

static esp_err_t host_unregister_handler(bool registered_with_loop,
                                         esp_event_loop_handle_t event_loop,
                                         esp_event_base_t event_base,
                                         int32_t event_id,
                                         esp_event_handler_instance_t instance)
{
    size_t i = 0U;

    for (i = 0U; i < s_handler_count; ++i) {
        if (&s_handlers[i] == (host_event_handler_slot_t *)instance) {
            if (s_handlers[i].registered_with_loop != registered_with_loop ||
                s_handlers[i].event_loop != event_loop ||
                s_handlers[i].base != event_base ||
                s_handlers[i].id != event_id) {
                return ESP_ERR_NOT_FOUND;
            }
            s_handlers[i].used = false;
            s_handlers[i].registered_with_loop = false;
            s_handlers[i].base = NULL;
            s_handlers[i].id = 0;
            s_handlers[i].event_loop = NULL;
            s_handlers[i].handler = NULL;
            s_handlers[i].arg = NULL;

            if (instance == s_startup_metering_handler_instance) {
                if (s_emit_metering_snapshot_during_startup_unregister) {
                    s_emit_metering_snapshot_during_startup_unregister = false;
                    dispatch_event(METERING_EVENT_BASE,
                                   METERING_EVENT_SNAPSHOT,
                                   &s_startup_handoff_metering_snapshot);
                }
                s_startup_metering_handler_instance = NULL;
            }

            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t esp_event_handler_instance_unregister(
    esp_event_base_t event_base, int32_t event_id,
    esp_event_handler_instance_t instance)
{
    return host_unregister_handler(false, NULL, event_base, event_id,
                                   instance);
}

esp_err_t esp_event_handler_instance_unregister_with(
    esp_event_loop_handle_t event_loop, esp_event_base_t event_base,
    int32_t event_id, esp_event_handler_instance_t instance)
{
    return host_unregister_handler(true, event_loop, event_base, event_id,
                                   instance);
}

esp_err_t esp_event_post(esp_event_base_t event_base, int32_t event_id,
                         const void *event_data, size_t event_data_size,
                         uint32_t ticks_to_wait)
{
    (void)event_base;
    (void)event_id;
    (void)event_data;
    (void)event_data_size;
    (void)ticks_to_wait;
    return ESP_OK;
}

const char *esp_err_to_name(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return "ESP_OK";
    case ESP_FAIL:
        return "ESP_FAIL";
    case ESP_ERR_INVALID_ARG:
        return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE:
        return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_SIZE:
        return "ESP_ERR_INVALID_SIZE";
    case ESP_ERR_NOT_FOUND:
        return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_TIMEOUT:
        return "ESP_ERR_TIMEOUT";
    default:
        return "ESP_ERR_UNKNOWN";
    }
}

esp_err_t tb_internal_format_power_limit_response(char *buf, size_t buf_size,
                                                  float power_limit_w,
                                                  size_t *out_len)
{
    int written = 0;

    if (buf == NULL || buf_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(buf, buf_size, "{\"powerLimit\":%.2f}",
                       (double)power_limit_w);
    if (written < 0) {
        return ESP_FAIL;
    }
    if ((size_t)written >= buf_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (out_len != NULL) {
        *out_len = (size_t)written;
    }

    return ESP_OK;
}

static void host_test_safety_metering_handler(void *handler_args,
                                              esp_event_base_t event_base,
                                              int32_t event_id,
                                              void *event_data)
{
    safety_guard_t *guard = (safety_guard_t *)handler_args;
    metering_snapshot_t *snapshot = (metering_snapshot_t *)event_data;

    assert(event_base == METERING_EVENT_BASE);
    assert(event_id == METERING_EVENT_SNAPSHOT);
    assert(guard != NULL);

    s_dispatch_trace[s_dispatch_trace_count++] = "safety";
    guard->latest.level = (snapshot != NULL && snapshot->power > 1000.0f) ?
                              SAFETY_GUARD_LEVEL_DANGER :
                              SAFETY_GUARD_LEVEL_NORMAL;
    guard->latest.event = (guard->latest.level == SAFETY_GUARD_LEVEL_DANGER) ?
                              SAFETY_GUARD_EVENT_OVERPOWER :
                              SAFETY_GUARD_EVENT_NONE;
    guard->latest.suggested_action =
        (guard->latest.level == SAFETY_GUARD_LEVEL_DANGER) ?
            SAFETY_GUARD_ACTION_RELAY_OFF :
            SAFETY_GUARD_ACTION_NONE;
    guard->latest.timestamp_us = (snapshot != NULL) ? snapshot->timestamp_us : 0U;
    guard->latest.valid = true;
}

static size_t count_active_handlers(esp_event_base_t base, int32_t id)
{
    size_t i = 0U;
    size_t count = 0U;

    for (i = 0U; i < s_handler_count; ++i) {
        if (s_handlers[i].used && s_handlers[i].base == base &&
            s_handlers[i].id == id) {
            count++;
        }
    }

    return count;
}

static esp_event_handler_t nth_handler(esp_event_base_t base, int32_t id,
                                       size_t index)
{
    size_t i = 0U;
    size_t seen = 0U;

    for (i = 0U; i < s_handler_count; ++i) {
        if (s_handlers[i].used && s_handlers[i].base == base &&
            s_handlers[i].id == id) {
            if (seen == index) {
                return s_handlers[i].handler;
            }
            seen++;
        }
    }

    return NULL;
}

static void dispatch_event(esp_event_base_t base, int32_t id, void *event_data)
{
    size_t i = 0U;

    for (i = 0U; i < s_handler_count; ++i) {
        if (s_handlers[i].used && s_handlers[i].base == base &&
            s_handlers[i].id == id) {
            s_handlers[i].handler(s_handlers[i].arg, base, id, event_data);
        }
    }
}

esp_err_t relay_get(const relay_t *me, bool *out_on)
{
    if (me == NULL || out_on == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_on = me->on;
    return ESP_OK;
}

esp_err_t relay_set(relay_t *me, relay_source_t source, bool on)
{
    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (source == RELAY_SOURCE_SAFETY && !on) {
        s_dispatch_trace[s_dispatch_trace_count++] = "relay_off";
    }
    me->on = on;
    return ESP_OK;
}

esp_err_t relay_toggle(relay_t *me, relay_source_t source)
{
    (void)source;

    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    me->on = !me->on;
    return ESP_OK;
}

esp_err_t button_register_cb(button_t *me, button_event_t event,
                             button_event_cb_t cb, void *user_ctx)
{
    if (me == NULL || event >= BUTTON_EVENT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    me->callbacks[event] = cb;
    me->callback_ctx[event] = user_ctx;
    return ESP_OK;
}

esp_err_t bl0942_start(bl0942_t *me)
{
    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    me->started = true;
    return ESP_OK;
}

esp_err_t bl0942_stop(bl0942_t *me)
{
    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    me->started = false;
    return ESP_OK;
}

esp_err_t metering_service_start(metering_service_t *me)
{
    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    me->started = true;
    return ESP_OK;
}

esp_err_t metering_service_stop(metering_service_t *me)
{
    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    me->started = false;
    return ESP_OK;
}

esp_err_t metering_service_confirm_energy_delta(
    metering_service_t *me, const metering_snapshot_t *snapshot)
{
    (void)snapshot;

    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    me->confirm_calls++;
    return ESP_OK;
}

esp_err_t metering_service_discard_energy_delta(
    metering_service_t *me, const metering_snapshot_t *snapshot)
{
    (void)snapshot;

    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    me->discard_calls++;
    return ESP_OK;
}

esp_err_t safety_guard_start(safety_guard_t *me)
{
    esp_event_handler_instance_t instance = NULL;
    esp_err_t ret = ESP_OK;

    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (me->started) {
        return ESP_OK;
    }

    ret = esp_event_handler_instance_register(
        METERING_EVENT_BASE, METERING_EVENT_SNAPSHOT,
        host_test_safety_metering_handler, me, &instance);
    if (ret != ESP_OK) {
        return ret;
    }

    me->metering_handler = instance;
    me->started = true;

    if (s_emit_metering_snapshot_during_safety_start) {
        s_emit_metering_snapshot_during_safety_start = false;
        dispatch_event(METERING_EVENT_BASE, METERING_EVENT_SNAPSHOT,
                       &s_startup_metering_snapshot);
    }

    return ESP_OK;
}

esp_err_t safety_guard_stop(safety_guard_t *me)
{
    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (me->metering_handler != NULL) {
        (void)esp_event_handler_instance_unregister(
            METERING_EVENT_BASE, METERING_EVENT_SNAPSHOT,
            me->metering_handler);
        me->metering_handler = NULL;
    }

    me->started = false;
    me->latest.valid = false;
    return ESP_OK;
}

esp_err_t safety_guard_get_latest(safety_guard_t *me, safety_guard_snapshot_t *out)
{
    if (me == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!me->latest.valid) {
        return ESP_ERR_INVALID_STATE;
    }

    *out = me->latest;
    return ESP_OK;
}

esp_err_t safety_guard_get_thresholds(safety_guard_t *me,
                                      float *out_overcurrent_a,
                                      float *out_overpower_w)
{
    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_overcurrent_a != NULL) {
        *out_overcurrent_a = me->overcurrent_a;
    }
    if (out_overpower_w != NULL) {
        *out_overpower_w = me->overpower_w;
    }

    return ESP_OK;
}

esp_err_t safety_guard_set_thresholds(safety_guard_t *me, float overcurrent_a,
                                      float overpower_w)
{
    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    me->overcurrent_a = overcurrent_a;
    me->overpower_w = overpower_w;
    return ESP_OK;
}

esp_err_t network_manager_start(network_manager_t *me)
{
    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    me->started = true;
    return ESP_OK;
}

esp_err_t network_manager_stop(network_manager_t *me)
{
    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    me->started = false;
    return ESP_OK;
}

esp_err_t network_manager_get_status(network_manager_t *me,
                                     network_manager_status_t *out)
{
    if (me == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out = me->status;
    return ESP_OK;
}

esp_err_t thingsboard_client_start(thingsboard_client_t *me)
{
    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    me->started = true;
    return ESP_OK;
}

esp_err_t thingsboard_client_stop(thingsboard_client_t *me)
{
    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    me->started = false;
    return ESP_OK;
}

esp_err_t thingsboard_client_publish_telemetry(thingsboard_client_t *me,
                                               const tb_telemetry_input_t *input)
{
    (void)me;

    assert(input != NULL);
    s_dispatch_trace[s_dispatch_trace_count++] = "app";
    s_publish_telemetry_calls++;
    s_last_telemetry_relay_on = input->relay_on;
    s_last_telemetry_relay_known = true;
    return ESP_OK;
}

esp_err_t thingsboard_client_report_relay_state(thingsboard_client_t *me, bool on)
{
    (void)me;
    (void)on;
    return ESP_OK;
}

esp_err_t thingsboard_client_report_power_limit(thingsboard_client_t *me,
                                                float power_limit_w)
{
    (void)me;
    (void)power_limit_w;
    return ESP_OK;
}

esp_err_t thingsboard_client_send_rpc_response(thingsboard_client_t *me,
                                               int32_t request_id,
                                               const char *json,
                                               size_t json_len)
{
    (void)me;
    (void)request_id;
    (void)json;
    (void)json_len;
    return ESP_OK;
}

esp_err_t thingsboard_client_register_command_cb(thingsboard_client_t *me,
                                                 tb_command_cb_t cb,
                                                 void *ctx)
{
    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    me->cmd_cb = cb;
    me->cmd_ctx = (cb != NULL) ? ctx : NULL;
    return ESP_OK;
}

esp_err_t lvgl_dashboard_start(lvgl_dashboard_t *me)
{
    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    me->started = true;
    return ESP_OK;
}

esp_err_t lvgl_dashboard_stop(lvgl_dashboard_t *me)
{
    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    me->started = false;
    return ESP_OK;
}

esp_err_t lvgl_dashboard_set_screen_enabled(lvgl_dashboard_t *me, bool enabled)
{
    if (me == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    me->screen_enabled = enabled;
    return ESP_OK;
}

#include "app_controller_internal.c"
#include "app_controller.c"

static const char *handler_label(esp_event_handler_t handler)
{
    if (handler == host_test_safety_metering_handler) {
        return "host_test_safety_metering_handler";
    }
    if (handler == app_controller_on_metering_snapshot) {
        return "app_controller_on_metering_snapshot";
    }
    if (handler == app_controller_on_safety_snapshot) {
        return "app_controller_on_safety_snapshot";
    }
    if (handler == app_controller_on_relay_state_changed) {
        return "app_controller_on_relay_state_changed";
    }

    return "unknown_handler";
}

static void expect_nth_handler(esp_event_base_t base, int32_t id, size_t index,
                               esp_event_handler_t expected)
{
    esp_event_handler_t actual = nth_handler(base, id, index);

    if (actual != expected) {
        fprintf(stderr,
                "expected %s metering handler to be %s, got %s\n",
                (index == 0U) ? "first" : "second",
                handler_label(expected),
                handler_label(actual));
        exit(EXIT_FAILURE);
    }
}

static app_controller_t *create_controller_fixture(relay_t *relay,
                                                   button_t *button,
                                                   bl0942_t *bl0942,
                                                   metering_service_t *metering,
                                                   safety_guard_t *safety,
                                                   thingsboard_client_t *tb,
                                                   network_manager_t *net_mgr,
                                                   lvgl_dashboard_t *dashboard)
{
    const app_controller_config_t cfg = {
        .event_loop = NULL,
        .pinmap = NULL,
        .relay = relay,
        .button = button,
        .bl0942 = bl0942,
        .tft_panel = NULL,
        .metering = metering,
        .safety = safety,
        .tb = tb,
        .net_mgr = net_mgr,
        .dashboard = dashboard,
    };

    memset(button, 0, sizeof(*button));
    memset(bl0942, 0, sizeof(*bl0942));
    memset(metering, 0, sizeof(*metering));
    memset(safety, 0, sizeof(*safety));
    memset(tb, 0, sizeof(*tb));
    memset(net_mgr, 0, sizeof(*net_mgr));
    memset(dashboard, 0, sizeof(*dashboard));
    relay->on = true;
    net_mgr->status = (network_manager_status_t){
        .active_link = NETWORK_LINK_TYPE_WIFI,
        .ready = true,
        .primary_status = NETWORK_LINK_STATUS_READY,
        .backup_status = NETWORK_LINK_STATUS_IDLE,
    };

    return app_controller_create(&cfg);
}

static void test_metering_handler_runs_after_safety_handler(void)
{
    relay_t relay = {0};
    button_t button = {0};
    bl0942_t bl0942 = {0};
    metering_service_t metering = {0};
    safety_guard_t safety = {0};
    thingsboard_client_t tb = {0};
    network_manager_t net_mgr = {0};
    lvgl_dashboard_t dashboard = {0};
    app_controller_t *controller = NULL;
    metering_snapshot_t snapshot = {
        .voltage = 230.0f,
        .current = 1.2f,
        .power = 276.0f,
        .energy_delta = 0.0f,
        .frequency = 50.0f,
        .timestamp_us = 1234U,
        .energy_delta_token = 0U,
        .valid = true,
    };

    reset_host_state();
    controller = create_controller_fixture(&relay, &button, &bl0942, &metering,
                                           &safety, &tb, &net_mgr, &dashboard);
    assert(controller != NULL);

    assert(app_controller_start(controller) == ESP_OK);
    assert(count_active_handlers(METERING_EVENT_BASE,
                                 METERING_EVENT_SNAPSHOT) == 2U);
    assert(count_active_handlers(SAFETY_GUARD_EVENT_BASE,
                                 SAFETY_GUARD_EVENT_SNAPSHOT) == 1U);
    assert(count_active_handlers(RELAY_EVENT_BASE,
                                 RELAY_EVENT_STATE_CHANGED) == 1U);
    expect_nth_handler(METERING_EVENT_BASE, METERING_EVENT_SNAPSHOT, 0U,
                       host_test_safety_metering_handler);
    expect_nth_handler(METERING_EVENT_BASE, METERING_EVENT_SNAPSHOT, 1U,
                       app_controller_on_metering_snapshot);

    dispatch_event(METERING_EVENT_BASE, METERING_EVENT_SNAPSHOT, &snapshot);
    assert(s_publish_telemetry_calls == 1);
    assert(s_dispatch_trace_count == 2U);
    assert(strcmp(s_dispatch_trace[0], "safety") == 0);
    assert(strcmp(s_dispatch_trace[1], "app") == 0);

    assert(app_controller_stop(controller) == ESP_OK);
    assert(count_active_handlers(METERING_EVENT_BASE,
                                 METERING_EVENT_SNAPSHOT) == 0U);
    assert(count_active_handlers(RELAY_EVENT_BASE,
                                 RELAY_EVENT_STATE_CHANGED) == 0U);
    assert(count_active_handlers(SAFETY_GUARD_EVENT_BASE,
                                 SAFETY_GUARD_EVENT_SNAPSHOT) == 0U);

    assert(app_controller_destroy(controller) == ESP_OK);
}

static void test_metering_handler_applies_safety_action_without_safety_event(void)
{
    relay_t relay = {0};
    button_t button = {0};
    bl0942_t bl0942 = {0};
    metering_service_t metering = {0};
    safety_guard_t safety = {0};
    thingsboard_client_t tb = {0};
    network_manager_t net_mgr = {0};
    lvgl_dashboard_t dashboard = {0};
    app_controller_t *controller = NULL;
    const metering_snapshot_t snapshot = {
        .voltage = 230.0f,
        .current = 6.0f,
        .power = 1380.0f,
        .frequency = 50.0f,
        .timestamp_us = 2345U,
        .valid = true,
    };

    reset_host_state();
    controller = create_controller_fixture(&relay, &button, &bl0942, &metering,
                                           &safety, &tb, &net_mgr, &dashboard);
    assert(controller != NULL);
    assert(app_controller_start(controller) == ESP_OK);

    /* The safety stub updates latest but deliberately emits no safety event. */
    dispatch_event(METERING_EVENT_BASE, METERING_EVENT_SNAPSHOT,
                   (void *)&snapshot);

    assert(relay.on == false);
    assert(s_publish_telemetry_calls == 1);
    assert(s_last_telemetry_relay_known == true);
    assert(s_last_telemetry_relay_on == false);
    assert(s_dispatch_trace_count == 3U);
    assert(strcmp(s_dispatch_trace[0], "safety") == 0);
    assert(strcmp(s_dispatch_trace[1], "relay_off") == 0);
    assert(strcmp(s_dispatch_trace[2], "app") == 0);

    assert(app_controller_stop(controller) == ESP_OK);
    assert(app_controller_destroy(controller) == ESP_OK);
}

static void test_metering_register_failure_rolls_back_handlers(void)
{
    relay_t relay = {0};
    button_t button = {0};
    bl0942_t bl0942 = {0};
    metering_service_t metering = {0};
    safety_guard_t safety = {0};
    thingsboard_client_t tb = {0};
    network_manager_t net_mgr = {0};
    lvgl_dashboard_t dashboard = {0};
    app_controller_t *controller = NULL;

    reset_host_state();
    s_fail_late_app_metering_register = true;
    controller = create_controller_fixture(&relay, &button, &bl0942, &metering,
                                           &safety, &tb, &net_mgr, &dashboard);
    assert(controller != NULL);

    assert(app_controller_start(controller) == ESP_FAIL);
    assert(s_metering_register_count == 2U);
    assert(s_last_failed_metering_handler ==
           app_controller_on_metering_snapshot);
    assert(count_active_handlers(METERING_EVENT_BASE,
                                 METERING_EVENT_SNAPSHOT) == 0U);
    assert(count_active_handlers(RELAY_EVENT_BASE,
                                 RELAY_EVENT_STATE_CHANGED) == 0U);
    assert(count_active_handlers(SAFETY_GUARD_EVENT_BASE,
                                 SAFETY_GUARD_EVENT_SNAPSHOT) == 0U);
    assert(controller->startup_metering_handler == NULL);
    assert(controller->metering_handler == NULL);
    assert(controller->relay_handler == NULL);
    assert(controller->safety_handler == NULL);
    assert(controller->button_single_registered == false);
    assert(controller->button_long_registered == false);
    assert(controller->tb_command_registered == false);
    assert(controller->bl0942_started == false);
    assert(controller->metering_started == false);
    assert(controller->safety_started == false);
    assert(controller->net_mgr_started == false);
    assert(controller->tb_started == false);
    assert(controller->dashboard_started == false);
    assert(controller->started == false);
    assert(controller->starting == false);
    assert(controller->stopping == false);
    assert(button.callbacks[BUTTON_EVENT_SINGLE_CLICK] == NULL);
    assert(button.callback_ctx[BUTTON_EVENT_SINGLE_CLICK] == NULL);
    assert(button.callbacks[BUTTON_EVENT_LONG_PRESS_START] == NULL);
    assert(button.callback_ctx[BUTTON_EVENT_LONG_PRESS_START] == NULL);
    assert(tb.cmd_cb == NULL);
    assert(tb.cmd_ctx == NULL);
    assert(bl0942.started == false);
    assert(metering.started == false);
    assert(safety.started == false);
    assert(safety.metering_handler == NULL);
    assert(net_mgr.started == false);
    assert(tb.started == false);
    assert(dashboard.started == false);

    assert(app_controller_destroy(controller) == ESP_OK);
}

static void test_start_discards_startup_window_energy_delta_token(void)
{
    relay_t relay = {0};
    button_t button = {0};
    bl0942_t bl0942 = {0};
    metering_service_t metering = {0};
    safety_guard_t safety = {0};
    thingsboard_client_t tb = {0};
    network_manager_t net_mgr = {0};
    lvgl_dashboard_t dashboard = {0};
    app_controller_t *controller = NULL;
    const metering_snapshot_t startup_snapshot = {
        .voltage = 229.5f,
        .current = 1.8f,
        .power = 413.1f,
        .energy_delta = 0.25f,
        .frequency = 50.0f,
        .timestamp_us = 5678U,
        .energy_delta_token = 17U,
        .valid = true,
    };

    reset_host_state();
    s_emit_metering_snapshot_during_safety_start = true;
    s_startup_metering_snapshot = startup_snapshot;
    controller = create_controller_fixture(&relay, &button, &bl0942, &metering,
                                           &safety, &tb, &net_mgr, &dashboard);
    assert(controller != NULL);

    assert(app_controller_start(controller) == ESP_OK);
    assert(safety.latest.valid == true);
    assert(safety.latest.timestamp_us == startup_snapshot.timestamp_us);
    assert(s_dispatch_trace_count == 1U);
    assert(strcmp(s_dispatch_trace[0], "safety") == 0);
    assert(metering.confirm_calls == 0);

    if (metering.discard_calls != 1) {
        fprintf(stderr,
                "expected startup snapshot token %lu to be discarded once, got %d\n",
                (unsigned long)startup_snapshot.energy_delta_token,
                metering.discard_calls);
        exit(EXIT_FAILURE);
    }

    assert(app_controller_stop(controller) == ESP_OK);
    assert(app_controller_destroy(controller) == ESP_OK);
}

static void test_start_handoff_race_discards_metering_snapshot_token(void)
{
    relay_t relay = {0};
    button_t button = {0};
    bl0942_t bl0942 = {0};
    metering_service_t metering = {0};
    safety_guard_t safety = {0};
    thingsboard_client_t tb = {0};
    network_manager_t net_mgr = {0};
    lvgl_dashboard_t dashboard = {0};
    app_controller_t *controller = NULL;
    const metering_snapshot_t handoff_snapshot = {
        .voltage = 228.4f,
        .current = 2.1f,
        .power = 479.6f,
        .energy_delta = 0.5f,
        .frequency = 50.0f,
        .timestamp_us = 9012U,
        .energy_delta_token = 29U,
        .valid = true,
    };

    reset_host_state();
    s_emit_metering_snapshot_during_startup_unregister = true;
    s_startup_handoff_metering_snapshot = handoff_snapshot;
    controller = create_controller_fixture(&relay, &button, &bl0942, &metering,
                                           &safety, &tb, &net_mgr, &dashboard);
    assert(controller != NULL);

    assert(app_controller_start(controller) == ESP_OK);
    assert(safety.latest.valid == true);
    assert(safety.latest.timestamp_us == handoff_snapshot.timestamp_us);
    assert(s_dispatch_trace_count == 1U);
    assert(strcmp(s_dispatch_trace[0], "safety") == 0);
    assert(metering.confirm_calls == 0);

    if (metering.discard_calls != 1) {
        fprintf(stderr,
                "expected handoff snapshot token %lu to be consumed during startup handoff, got confirm=%d discard=%d\n",
                (unsigned long)handoff_snapshot.energy_delta_token,
                metering.confirm_calls,
                metering.discard_calls);
        exit(EXIT_FAILURE);
    }

    assert(app_controller_stop(controller) == ESP_OK);
    assert(app_controller_destroy(controller) == ESP_OK);
}

static void test_stop_times_out_while_start_remains_in_progress(void)
{
    relay_t relay = {0};
    button_t button = {0};
    bl0942_t bl0942 = {0};
    metering_service_t metering = {0};
    safety_guard_t safety = {0};
    thingsboard_client_t tb = {0};
    network_manager_t net_mgr = {0};
    lvgl_dashboard_t dashboard = {0};
    app_controller_t *controller = NULL;

    reset_host_state();
    controller = create_controller_fixture(&relay, &button, &bl0942, &metering,
                                           &safety, &tb, &net_mgr, &dashboard);
    assert(controller != NULL);
    controller->starting = true;

    assert(app_controller_stop(controller) == ESP_ERR_TIMEOUT);
    assert(s_task_delay_calls ==
           APP_CONTROLLER_LIFECYCLE_TIMEOUT_MS /
               APP_CONTROLLER_LIFECYCLE_POLL_MS);
    assert(controller->starting == true);
    assert(controller->stopping == false);

    controller->starting = false;
    assert(app_controller_destroy(controller) == ESP_OK);
}

int main(void)
{
    test_metering_handler_runs_after_safety_handler();
    test_metering_handler_applies_safety_action_without_safety_event();
    test_metering_register_failure_rolls_back_handlers();
    test_start_discards_startup_window_energy_delta_token();
    test_start_handoff_race_discards_metering_snapshot_token();
    test_stop_times_out_while_start_remains_in_progress();

    printf("app controller event order tests passed\n");
    return 0;
}
