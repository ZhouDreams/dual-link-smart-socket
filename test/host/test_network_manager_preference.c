#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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
} host_test_semaphore_t;

typedef struct {
    network_link_t base;
    network_rx_cb_t rx_cb;
    void *rx_ctx;
} fake_link_t;

static esp_err_t fake_register_rx_cb(network_link_t *base,
                                     network_rx_cb_t cb, void *ctx)
{
    fake_link_t *link = (fake_link_t *)base;

    link->rx_cb = cb;
    link->rx_ctx = (cb != NULL) ? ctx : NULL;
    return ESP_OK;
}

static const network_link_ops_t s_fake_ops = {
    .register_rx_cb = fake_register_rx_cb,
};

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return (SemaphoreHandle_t)calloc(1, sizeof(host_test_semaphore_t));
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
    if (ticks_to_wait == 0U && host->signaled) {
        host->signaled = false;
        return pdTRUE;
    }
    if (ticks_to_wait == 0U) {
        return pdFALSE;
    }
    assert(!host->locked);
    host->locked = true;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    host_test_semaphore_t *host = (host_test_semaphore_t *)semaphore;

    assert(host != NULL);
    if (host->locked) {
        host->locked = false;
    } else {
        host->signaled = true;
    }
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore)
{
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
    (void)out_handle;
    return pdFALSE;
}

void vTaskDelay(TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
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
    };
}

static void test_backup_type_can_be_selected_as_preferred_primary(void)
{
    fake_link_t wifi = {0};
    fake_link_t lte = {0};
    network_manager_t *manager = NULL;

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
}

static void test_none_preserves_injected_primary(void)
{
    fake_link_t wifi = {0};
    fake_link_t lte = {0};
    network_manager_t *manager = NULL;

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
}

int main(void)
{
    test_backup_type_can_be_selected_as_preferred_primary();
    test_none_preserves_injected_primary();

    printf("network manager preference tests passed\n");
    return 0;
}
