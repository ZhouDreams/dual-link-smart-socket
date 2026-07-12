#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/semphr.h"

typedef struct host_test_mutex {
    bool locked;
    bool deleted;
} host_test_mutex_t;

static esp_err_t s_next_event_post_ret = ESP_OK;
static int s_event_post_calls = 0;
static unsigned char s_last_event_data[128];
static size_t s_last_event_size = 0;
static int64_t s_fake_time_us = 0;
static void (*s_event_post_hook)(void) = NULL;
static void *s_event_post_hook_ctx = NULL;

esp_event_base_t BL0942_EVENT_BASE = "BL0942_EVENT_BASE";

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

esp_err_t gpio_config(const gpio_config_t *config)
{
    (void)config;
    return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    (void)gpio_num;
    (void)level;
    return ESP_OK;
}

esp_err_t esp_event_handler_instance_register(
    esp_event_base_t event_base, int32_t event_id,
    esp_event_handler_t event_handler, void *event_handler_arg,
    esp_event_handler_instance_t *instance)
{
    (void)event_base;
    (void)event_id;
    (void)event_handler;
    (void)event_handler_arg;

    if (instance != NULL) {
        *instance = (void *)event_handler;
    }

    return ESP_OK;
}

esp_err_t esp_event_handler_instance_unregister(
    esp_event_base_t event_base, int32_t event_id,
    esp_event_handler_instance_t instance)
{
    (void)event_base;
    (void)event_id;
    (void)instance;
    return ESP_OK;
}

esp_err_t esp_event_post(esp_event_base_t event_base, int32_t event_id,
                         const void *event_data, size_t event_data_size,
                         uint32_t ticks_to_wait)
{
    (void)event_base;
    (void)event_id;
    (void)ticks_to_wait;

    s_event_post_calls++;
    s_last_event_size = 0;
    memset(s_last_event_data, 0, sizeof(s_last_event_data));

    if (event_data != NULL && event_data_size <= sizeof(s_last_event_data)) {
        memcpy(s_last_event_data, event_data, event_data_size);
        s_last_event_size = event_data_size;
    }

    if (s_event_post_hook != NULL) {
        s_event_post_hook();
    }

    return s_next_event_post_ret;
}

int64_t esp_timer_get_time(void)
{
    return s_fake_time_us;
}

const char *esp_err_to_name(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return "ESP_OK";
    case ESP_FAIL:
        return "ESP_FAIL";
    case ESP_ERR_NO_MEM:
        return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG:
        return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE:
        return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_SIZE:
        return "ESP_ERR_INVALID_SIZE";
    case ESP_ERR_NOT_FOUND:
        return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_NOT_SUPPORTED:
        return "ESP_ERR_NOT_SUPPORTED";
    case ESP_ERR_TIMEOUT:
        return "ESP_ERR_TIMEOUT";
    case ESP_ERR_INVALID_RESPONSE:
        return "ESP_ERR_INVALID_RESPONSE";
    default:
        return "ESP_ERR_UNKNOWN";
    }
}

#include "metering_service_internal.c"
#include "metering_service.c"

static void set_service_stopping_on_event_post(void)
{
    metering_service_t *service = (metering_service_t *)s_event_post_hook_ctx;

    assert(service != NULL);
    service->stopping = true;
}

static void assert_float_near(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.001f);
}

static void reset_spies(void)
{
    s_next_event_post_ret = ESP_OK;
    s_event_post_calls = 0;
    s_last_event_size = 0;
    s_fake_time_us = 0;
    s_event_post_hook = NULL;
    s_event_post_hook_ctx = NULL;
    memset(s_last_event_data, 0, sizeof(s_last_event_data));
}

static void init_service_fixture(metering_service_t *service)
{
    memset(service, 0, sizeof(*service));
    service->mutex = xSemaphoreCreateMutex();
    assert(service->mutex != NULL);
    service->initialized = true;
    service->started = true;
    metering_energy_delta_state_init(&service->energy_delta_state);
}

static void destroy_service_fixture(metering_service_t *service)
{
    vSemaphoreDelete(service->mutex);
    service->mutex = NULL;
}

static bl0942_measurement_t make_valid_measurement(uint32_t cf_cnt_raw,
                                                   uint64_t capture_time_us)
{
    const bl0942_measurement_t measurement = {
        .i_rms_raw = 753639U,
        .v_rms_raw = 3494335U,
        .i_fast_rms_raw = 0U,
        .watt_raw = 411438,
        .cf_cnt_raw = cf_cnt_raw,
        .freq_raw = 20000U,
        .status_raw = 0U,
        .capture_time_us = capture_time_us,
        .valid = true,
    };

    return measurement;
}

static void test_post_failure_discards_pending_token(void)
{
    metering_service_t service;
    metering_snapshot_t latest = {0};
    metering_energy_delta_result_t retry = {0};
    const bl0942_measurement_t measurement = make_valid_measurement(100U, 1111U);

    reset_spies();
    init_service_fixture(&service);
    s_next_event_post_ret = ESP_FAIL;

    metering_on_bl0942_measurement(&service, BL0942_EVENT_BASE,
                                   BL0942_EVENT_MEASUREMENT,
                                   (void *)&measurement);

    assert(s_event_post_calls == 1);
    assert(service.energy_delta_state.have_pending == false);
    assert(metering_service_get_latest(&service, &latest) == ESP_OK);
    assert_float_near(latest.energy_delta, 0.0f);
    assert(latest.energy_delta_token == 0U);
    assert(metering_energy_delta_prepare(&service.energy_delta_state, 101U,
                                         &retry) == ESP_OK);
    assert_float_near(retry.energy_delta_mwh, 62.297f);

    destroy_service_fixture(&service);
}

static void test_post_failure_discards_pending_token_while_stop_begins(void)
{
    metering_service_t service;
    const bl0942_measurement_t measurement = make_valid_measurement(100U, 1111U);

    reset_spies();
    init_service_fixture(&service);
    s_next_event_post_ret = ESP_FAIL;
    s_event_post_hook = set_service_stopping_on_event_post;
    s_event_post_hook_ctx = &service;

    metering_on_bl0942_measurement(&service, BL0942_EVENT_BASE,
                                   BL0942_EVENT_MEASUREMENT,
                                   (void *)&measurement);

    assert(s_event_post_calls == 1);
    assert(service.stopping == true);
    assert(service.energy_delta_state.have_pending == false);
    assert(service.latest.energy_delta_token == 0U);

    destroy_service_fixture(&service);
}

static void seed_confirmed_baseline(metering_service_t *service,
                                    uint32_t confirmed_cf_cnt_raw)
{
    metering_energy_delta_result_t baseline = {0};

    assert(metering_energy_delta_prepare(&service->energy_delta_state,
                                         confirmed_cf_cnt_raw,
                                         &baseline) == ESP_OK);
    assert(metering_energy_delta_confirm(&service->energy_delta_state,
                                         baseline.token) == ESP_OK);
}

static void test_fault_without_hard_reset_preserves_confirmed_baseline(void)
{
    metering_service_t service;
    metering_energy_delta_result_t after_fault = {0};
    const bl0942_fault_info_t fault = {
        .consecutive_failures = 1U,
        .fault_cycles = 1U,
        .hard_reset_attempted = false,
        .last_error = ESP_FAIL,
    };

    reset_spies();
    init_service_fixture(&service);
    service.has_latest = true;
    service.latest = (metering_snapshot_t){
        .voltage = 220.0f,
        .current = 1.0f,
        .power = 220.0f,
        .energy_delta = 0.5f,
        .frequency = 50.0f,
        .timestamp_us = 999U,
        .energy_delta_token = 9U,
        .valid = true,
    };

    seed_confirmed_baseline(&service, 100U);

    metering_on_bl0942_fault(&service, BL0942_EVENT_BASE,
                             BL0942_EVENT_FAULT, (void *)&fault);

    assert(s_event_post_calls == 1);
    assert(service.latest.valid == false);
    assert(service.latest.energy_delta == 0.0f);
    assert(service.latest.energy_delta_token == 0U);
    assert(service.energy_delta_state.have_confirmed_cf_cnt_raw == true);
    assert(service.energy_delta_state.confirmed_cf_cnt_raw == 100U);
    assert(service.energy_delta_state.have_pending == false);
    assert(metering_energy_delta_prepare(&service.energy_delta_state, 200U,
                                         &after_fault) == ESP_OK);
    assert(after_fault.baseline_established == false);
    assert_float_near(after_fault.energy_delta_mwh, 6229.793f);

    destroy_service_fixture(&service);
}

static void test_fault_without_hard_reset_clears_pending_token_only(void)
{
    metering_service_t service;
    metering_energy_delta_result_t pending = {0};
    metering_energy_delta_result_t after_fault = {0};
    const bl0942_fault_info_t fault = {
        .consecutive_failures = 1U,
        .fault_cycles = 1U,
        .hard_reset_attempted = false,
        .last_error = ESP_FAIL,
    };

    reset_spies();
    init_service_fixture(&service);
    seed_confirmed_baseline(&service, 100U);

    assert(metering_energy_delta_prepare(&service.energy_delta_state, 101U,
                                         &pending) == ESP_OK);

    metering_on_bl0942_fault(&service, BL0942_EVENT_BASE,
                             BL0942_EVENT_FAULT, (void *)&fault);

    assert(s_event_post_calls == 1);
    assert(service.energy_delta_state.have_confirmed_cf_cnt_raw == true);
    assert(service.energy_delta_state.confirmed_cf_cnt_raw == 100U);
    assert(service.energy_delta_state.have_pending == false);
    assert(metering_energy_delta_confirm(&service.energy_delta_state,
                                         pending.token) == ESP_ERR_INVALID_STATE);
    assert(metering_energy_delta_prepare(&service.energy_delta_state, 200U,
                                         &after_fault) == ESP_OK);
    assert(after_fault.baseline_established == false);
    assert_float_near(after_fault.energy_delta_mwh, 6229.793f);

    destroy_service_fixture(&service);
}

static void test_fault_with_hard_reset_rebaselines(void)
{
    metering_service_t service;
    metering_energy_delta_result_t pending = {0};
    metering_energy_delta_result_t after_fault = {0};
    const bl0942_fault_info_t fault = {
        .consecutive_failures = 3U,
        .fault_cycles = 1U,
        .hard_reset_attempted = true,
        .last_error = ESP_FAIL,
    };

    reset_spies();
    init_service_fixture(&service);
    seed_confirmed_baseline(&service, 100U);

    assert(metering_energy_delta_prepare(&service.energy_delta_state, 101U,
                                         &pending) == ESP_OK);

    metering_on_bl0942_fault(&service, BL0942_EVENT_BASE,
                             BL0942_EVENT_FAULT, (void *)&fault);

    assert(s_event_post_calls == 1);
    assert(service.energy_delta_state.have_confirmed_cf_cnt_raw == false);
    assert(service.energy_delta_state.have_pending == false);
    assert(metering_energy_delta_confirm(&service.energy_delta_state,
                                         pending.token) == ESP_ERR_INVALID_STATE);
    assert(metering_energy_delta_prepare(&service.energy_delta_state, 200U,
                                         &after_fault) == ESP_OK);
    assert(after_fault.baseline_established == true);
    assert_float_near(after_fault.energy_delta_mwh, 0.0f);

    destroy_service_fixture(&service);
}

int main(void)
{
    test_post_failure_discards_pending_token();
    test_post_failure_discards_pending_token_while_stop_begins();
    test_fault_without_hard_reset_preserves_confirmed_baseline();
    test_fault_without_hard_reset_clears_pending_token_only();
    test_fault_with_hard_reset_rebaselines();

    printf("metering event flow tests passed\n");
    return 0;
}
