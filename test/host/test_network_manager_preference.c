#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "network_link_priv.h"
#include "network_manager.h"

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

typedef struct host_test_semaphore {
    bool locked;
    bool signaled;
    bool is_mutex;
} host_test_semaphore_t;

typedef struct {
    network_link_t base;
    network_rx_cb_t rx_cb;
    void *rx_ctx;
    network_link_status_t status;
    esp_err_t start_ret;
    esp_err_t stop_ret;
    int start_calls;
    int stop_calls;
    int set_active_calls;
    bool last_active;
    int subscribe_calls;
} fake_link_t;

static int s_semaphore_delete_calls = 0;
static SemaphoreHandle_t s_manager_state_mutex = NULL;
static TickType_t s_last_contended_take_ticks = 0U;
static int s_state_lock_violations = 0;
static int s_rx_callback_calls = 0;
static BaseType_t s_task_create_result = pdFALSE;
static atomic_int *s_drain_counter = NULL;
static int s_delay_calls = 0;
static network_manager_t *s_start_probe_manager = NULL;
static esp_err_t s_publish_during_start_ret = ESP_OK;
static esp_err_t s_ready_during_start_ret = ESP_FAIL;
static bool s_ready_during_start = true;

static void record_link_call(void)
{
    host_test_semaphore_t *state =
        (host_test_semaphore_t *)s_manager_state_mutex;

    if (state != NULL && state->locked) {
        s_state_lock_violations++;
    }
}

static esp_err_t fake_start(network_link_t *base)
{
    fake_link_t *link = (fake_link_t *)base;

    record_link_call();
    link->start_calls++;
    return link->start_ret;
}

static esp_err_t fake_stop(network_link_t *base)
{
    fake_link_t *link = (fake_link_t *)base;

    record_link_call();
    link->stop_calls++;
    return link->stop_ret;
}

static esp_err_t fake_get_status(network_link_t *base,
                                 network_link_status_t *out)
{
    fake_link_t *link = (fake_link_t *)base;

    record_link_call();
    *out = link->status;
    return ESP_OK;
}

static esp_err_t fake_subscribe(network_link_t *base, const char *topic,
                                network_mqtt_qos_t qos)
{
    fake_link_t *link = (fake_link_t *)base;

    (void)topic;
    (void)qos;
    record_link_call();
    link->subscribe_calls++;
    return ESP_OK;
}

static esp_err_t fake_unsubscribe(network_link_t *base, const char *topic)
{
    (void)base;
    (void)topic;
    record_link_call();
    return ESP_OK;
}

static esp_err_t fake_register_rx_cb(network_link_t *base,
                                     network_rx_cb_t cb, void *ctx)
{
    fake_link_t *link = (fake_link_t *)base;

    link->rx_cb = cb;
    link->rx_ctx = (cb != NULL) ? ctx : NULL;
    return ESP_OK;
}

static esp_err_t fake_set_active(network_link_t *base, bool active)
{
    fake_link_t *link = (fake_link_t *)base;

    record_link_call();
    link->set_active_calls++;
    link->last_active = active;
    if (active && s_start_probe_manager != NULL) {
        const network_publish_request_t request = {
            .topic = "v1/devices/me/telemetry",
            .payload = "{}",
            .payload_len = 2U,
            .qos = NETWORK_MQTT_QOS1,
        };

        s_publish_during_start_ret = network_manager_publish(
            s_start_probe_manager, &request);
        s_ready_during_start_ret = network_manager_is_ready(
            s_start_probe_manager, &s_ready_during_start);
    }
    return ESP_OK;
}

static const network_link_ops_t s_fake_ops = {
    .start = fake_start,
    .stop = fake_stop,
    .get_status = fake_get_status,
    .subscribe = fake_subscribe,
    .unsubscribe = fake_unsubscribe,
    .register_rx_cb = fake_register_rx_cb,
    .set_active = fake_set_active,
};

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    host_test_semaphore_t *host = calloc(1, sizeof(*host));

    if (host != NULL) {
        host->is_mutex = true;
    }
    return (SemaphoreHandle_t)host;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    return (SemaphoreHandle_t)calloc(1, sizeof(host_test_semaphore_t));
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore,
                          TickType_t ticks_to_wait)
{
    host_test_semaphore_t *host = (host_test_semaphore_t *)semaphore;

    assert(host != NULL);
    if (host->is_mutex) {
        if (host->locked) {
            s_last_contended_take_ticks = ticks_to_wait;
            return pdFALSE;
        }
        host->locked = true;
        return pdTRUE;
    }
    if (host->signaled) {
        host->signaled = false;
        return pdTRUE;
    }
    if (ticks_to_wait == 0U) {
        return pdFALSE;
    }
    return pdFALSE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    host_test_semaphore_t *host = (host_test_semaphore_t *)semaphore;

    assert(host != NULL);
    if (host->is_mutex) {
        assert(host->locked);
        host->locked = false;
    } else {
        host->signaled = true;
    }
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore)
{
    s_semaphore_delete_calls++;
    free(semaphore);
}

BaseType_t xTaskCreate(TaskFunction_t task, const char *name,
                       uint32_t stack_depth, void *arg,
                       UBaseType_t priority, TaskHandle_t *out_handle)
{
    (void)task;
    (void)name;
    (void)stack_depth;
    (void)arg;
    (void)priority;
    if (s_task_create_result == pdPASS && out_handle != NULL) {
        *out_handle = (TaskHandle_t)(uintptr_t)1U;
    }
    return s_task_create_result;
}

void vTaskDelay(TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    s_delay_calls++;
    if (s_drain_counter != NULL && s_delay_calls == 1) {
        atomic_store(s_drain_counter, 0);
    }
}

void vTaskDelete(TaskHandle_t task)
{
    (void)task;
}

int64_t esp_timer_get_time(void)
{
    return 0;
}

const char *esp_err_to_name(esp_err_t err)
{
    (void)err;
    return "host-error";
}

#include "network_manager.c"

static void init_fake_link(fake_link_t *link, network_link_type_t type)
{
    *link = (fake_link_t){
        .base = {
            .ops = &s_fake_ops,
            .type = type,
        },
        .status = NETWORK_LINK_STATUS_IDLE,
    };
}

static void reset_spies(void)
{
    s_semaphore_delete_calls = 0;
    s_manager_state_mutex = NULL;
    s_last_contended_take_ticks = 0U;
    s_state_lock_violations = 0;
    s_rx_callback_calls = 0;
    s_task_create_result = pdFALSE;
    s_drain_counter = NULL;
    s_delay_calls = 0;
    s_start_probe_manager = NULL;
    s_publish_during_start_ret = ESP_OK;
    s_ready_during_start_ret = ESP_FAIL;
    s_ready_during_start = true;
}

static void test_backup_type_can_be_selected_as_preferred_primary(void)
{
    fake_link_t wifi = {0};
    fake_link_t lte = {0};
    network_manager_t *manager = NULL;

    reset_spies();
    init_fake_link(&wifi, NETWORK_LINK_TYPE_WIFI);
    init_fake_link(&lte, NETWORK_LINK_TYPE_LTE);
    const network_manager_config_t config = {
        .primary = &wifi.base,
        .backup = &lte.base,
        .preferred_primary = NETWORK_LINK_TYPE_LTE,
    };

    manager = network_manager_create(&config);
    assert(manager != NULL);
    assert(manager->primary == &lte.base);
    assert(manager->backup == &wifi.base);
    assert(lte.rx_cb != NULL);
    assert(wifi.rx_cb != NULL);
    assert(network_manager_destroy(manager) == ESP_OK);
    assert(lte.rx_cb == NULL);
    assert(wifi.rx_cb == NULL);
    assert(s_semaphore_delete_calls == 3);
}

static void test_none_preserves_injected_primary(void)
{
    fake_link_t wifi = {0};
    fake_link_t lte = {0};
    network_manager_t *manager = NULL;

    reset_spies();
    init_fake_link(&wifi, NETWORK_LINK_TYPE_WIFI);
    init_fake_link(&lte, NETWORK_LINK_TYPE_LTE);
    const network_manager_config_t config = {
        .primary = &wifi.base,
        .backup = &lte.base,
        .preferred_primary = NETWORK_LINK_TYPE_NONE,
    };

    manager = network_manager_create(&config);
    assert(manager != NULL);
    assert(manager->primary == &wifi.base);
    assert(manager->backup == &lte.base);
    assert(network_manager_destroy(manager) == ESP_OK);
    assert(s_semaphore_delete_calls == 3);
}

static void test_destroy_failure_preserves_handle_for_retry(void)
{
    fake_link_t wifi = {0};
    fake_link_t lte = {0};
    network_manager_t *manager = NULL;

    reset_spies();
    init_fake_link(&wifi, NETWORK_LINK_TYPE_WIFI);
    init_fake_link(&lte, NETWORK_LINK_TYPE_LTE);
    const network_manager_config_t config = {
        .primary = &wifi.base,
        .backup = &lte.base,
        .preferred_primary = NETWORK_LINK_TYPE_WIFI,
    };

    manager = network_manager_create(&config);
    assert(manager != NULL);
    manager->started = true;
    wifi.stop_ret = ESP_FAIL;

    assert(network_manager_destroy(manager) == ESP_FAIL);
    assert(s_semaphore_delete_calls == 0);
    assert(manager->destroying == true);
    assert(manager->stop_pending == true);
    assert(wifi.stop_calls == 1);
    assert(lte.stop_calls == 1);
    assert(wifi.rx_cb == NULL);
    assert(lte.rx_cb == NULL);

    wifi.stop_ret = ESP_OK;
    assert(network_manager_destroy(manager) == ESP_OK);
    assert(wifi.stop_calls == 2);
    assert(lte.stop_calls == 2);
    assert(s_semaphore_delete_calls == 3);
}

static void test_switch_calls_links_without_state_mutex(void)
{
    fake_link_t wifi = {0};
    fake_link_t lte = {0};
    network_manager_t *manager = NULL;

    reset_spies();
    init_fake_link(&wifi, NETWORK_LINK_TYPE_WIFI);
    init_fake_link(&lte, NETWORK_LINK_TYPE_LTE);
    wifi.status = NETWORK_LINK_STATUS_ERROR;
    lte.status = NETWORK_LINK_STATUS_READY;
    const network_manager_config_t config = {
        .primary = &wifi.base,
        .backup = &lte.base,
        .preferred_primary = NETWORK_LINK_TYPE_WIFI,
    };

    manager = network_manager_create(&config);
    assert(manager != NULL);
    s_manager_state_mutex = manager->mutex;
    assert(network_manager_subscribe(manager, "v1/devices/me/rpc/request/+",
                                     NETWORK_MQTT_QOS1) == ESP_OK);

    manager->active = &wifi.base;
    manager->monitor_task_running = true;
    network_manager_monitor_once(manager);

    assert(s_state_lock_violations == 0);
    assert(manager->active == &lte.base);
    assert(lte.start_calls == 1);
    assert(lte.set_active_calls == 1);
    assert(lte.last_active == true);
    assert(wifi.set_active_calls == 1);
    assert(wifi.last_active == false);
    assert(lte.subscribe_calls == 1);

    manager->monitor_task_running = false;
    s_manager_state_mutex = NULL;
    assert(network_manager_destroy(manager) == ESP_OK);
}

static void test_start_calls_links_without_state_mutex(void)
{
    fake_link_t wifi = {0};
    fake_link_t lte = {0};
    network_manager_t *manager = NULL;

    reset_spies();
    init_fake_link(&wifi, NETWORK_LINK_TYPE_WIFI);
    init_fake_link(&lte, NETWORK_LINK_TYPE_LTE);
    const network_manager_config_t config = {
        .primary = &wifi.base,
        .backup = &lte.base,
        .preferred_primary = NETWORK_LINK_TYPE_WIFI,
    };

    manager = network_manager_create(&config);
    assert(manager != NULL);
    s_manager_state_mutex = manager->mutex;
    assert(network_manager_subscribe(manager, "v1/devices/me/attributes",
                                     NETWORK_MQTT_QOS1) == ESP_OK);

    assert(network_manager_start(manager) == ESP_ERR_NO_MEM);
    assert(s_state_lock_violations == 0);
    assert(wifi.start_calls == 1);
    assert(lte.start_calls == 1);
    assert(wifi.set_active_calls == 1);
    assert(wifi.subscribe_calls == 1);
    assert(wifi.stop_calls == 1);
    assert(lte.stop_calls == 1);
    assert(manager->active == NULL);
    assert(manager->started == false);

    s_manager_state_mutex = NULL;
    assert(network_manager_destroy(manager) == ESP_OK);
}

static void test_successful_start_and_stop_use_control_lock(void)
{
    fake_link_t wifi = {0};
    fake_link_t lte = {0};
    network_manager_t *manager = NULL;

    reset_spies();
    init_fake_link(&wifi, NETWORK_LINK_TYPE_WIFI);
    init_fake_link(&lte, NETWORK_LINK_TYPE_LTE);
    s_task_create_result = pdPASS;
    const network_manager_config_t config = {
        .primary = &wifi.base,
        .backup = &lte.base,
        .preferred_primary = NETWORK_LINK_TYPE_WIFI,
    };

    manager = network_manager_create(&config);
    assert(manager != NULL);
    s_manager_state_mutex = manager->mutex;
    wifi.status = NETWORK_LINK_STATUS_READY;
    s_start_probe_manager = manager;
    assert(network_manager_start(manager) == ESP_OK);
    s_start_probe_manager = NULL;
    assert(manager->started == true);
    assert(manager->active == &wifi.base);
    assert(s_publish_during_start_ret == ESP_ERR_INVALID_STATE);
    assert(s_ready_during_start_ret == ESP_OK);
    assert(s_ready_during_start == false);
    assert(s_state_lock_violations == 0);

    manager->monitor_task = NULL;
    assert(xSemaphoreGive(manager->monitor_task_done_sema) == pdTRUE);
    assert(network_manager_stop(manager) == ESP_OK);
    assert(manager->started == false);
    assert(manager->active == NULL);
    assert(manager->stop_pending == false);
    assert(s_state_lock_violations == 0);

    s_task_create_result = pdFALSE;
    s_manager_state_mutex = NULL;
    assert(network_manager_destroy(manager) == ESP_OK);
}

static void hold_state_mutex_rx_cb(const network_rx_data_t *rx_data,
                                   void *user_ctx)
{
    (void)rx_data;
    (void)user_ctx;

    s_rx_callback_calls++;
    assert(xSemaphoreTake(s_manager_state_mutex, portMAX_DELAY) == pdTRUE);
}

static void test_rx_callback_uses_bounded_entry_and_atomic_exit(void)
{
    fake_link_t wifi = {0};
    network_manager_t *manager = NULL;
    const network_rx_data_t rx_data = {
        .topic = "v1/devices/me/rpc/request/1",
        .data = "{}",
        .data_len = 2,
    };

    reset_spies();
    init_fake_link(&wifi, NETWORK_LINK_TYPE_WIFI);
    const network_manager_config_t config = {
        .primary = &wifi.base,
        .preferred_primary = NETWORK_LINK_TYPE_WIFI,
    };

    manager = network_manager_create(&config);
    assert(manager != NULL);
    s_manager_state_mutex = manager->mutex;
    manager->active = &wifi.base;
    assert(network_manager_register_rx_cb(manager, hold_state_mutex_rx_cb,
                                          NULL) == ESP_OK);

    assert(xSemaphoreTake(manager->mutex, portMAX_DELAY) == pdTRUE);
    wifi.rx_cb(&rx_data, wifi.rx_ctx);
    assert(s_last_contended_take_ticks ==
           pdMS_TO_TICKS(NETWORK_MANAGER_RX_MUTEX_TIMEOUT_MS));
    assert(s_last_contended_take_ticks != portMAX_DELAY);
    assert(s_rx_callback_calls == 0);
    (void)xSemaphoreGive(manager->mutex);

    wifi.rx_cb(&rx_data, wifi.rx_ctx);
    assert(s_rx_callback_calls == 1);
    assert(atomic_load(&manager->active_rx_callbacks) == 0);
    assert(((host_test_semaphore_t *)manager->mutex)->locked);
    (void)xSemaphoreGive(manager->mutex);

    atomic_store(&manager->active_rx_callbacks, 1);
    s_drain_counter = &manager->active_rx_callbacks;
    s_delay_calls = 0;
    assert(network_manager_register_rx_cb(manager, NULL, NULL) == ESP_OK);
    assert(s_delay_calls == 1);
    assert(atomic_load(&manager->active_rx_callbacks) == 0);
    s_drain_counter = NULL;

    atomic_store(&manager->active_rx_callbacks, 1);
    s_delay_calls = 0;
    assert(network_manager_register_rx_cb(manager, NULL, NULL) ==
           ESP_ERR_TIMEOUT);
    assert(s_delay_calls ==
           (int)(NETWORK_MANAGER_RX_CB_TIMEOUT_MS /
                 NETWORK_MANAGER_RX_CB_POLL_MS));
    atomic_store(&manager->active_rx_callbacks, 0);

    s_manager_state_mutex = NULL;
    assert(network_manager_destroy(manager) == ESP_OK);
}

int main(void)
{
    test_backup_type_can_be_selected_as_preferred_primary();
    test_none_preserves_injected_primary();
    test_destroy_failure_preserves_handle_for_retry();
    test_switch_calls_links_without_state_mutex();
    test_start_calls_links_without_state_mutex();
    test_successful_start_and_stop_use_control_lock();
    test_rx_callback_uses_bounded_entry_and_atomic_exit();

    printf("network manager preference tests passed\n");
    return 0;
}
