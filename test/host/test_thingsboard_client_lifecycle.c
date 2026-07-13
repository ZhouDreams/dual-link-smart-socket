#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/semphr.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "thingsboard_client.h"

typedef struct host_test_mutex {
    bool locked;
} host_test_mutex_t;

struct network_manager {
    int unused;
};

static esp_err_t s_clear_rx_ret = ESP_OK;
static esp_err_t s_unsubscribe_ret = ESP_OK;
static unsigned int s_clear_rx_calls = 0U;
static unsigned int s_unsubscribe_calls = 0U;
static unsigned int s_mutex_delete_calls = 0U;
static network_rx_cb_t s_rx_cb = NULL;
static void *s_rx_ctx = NULL;

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return (SemaphoreHandle_t)calloc(1, sizeof(host_test_mutex_t));
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t ticks_to_wait)
{
    host_test_mutex_t *host_mutex = (host_test_mutex_t *)mutex;

    (void)ticks_to_wait;
    assert(host_mutex != NULL);
    assert(host_mutex->locked == false);
    host_mutex->locked = true;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex)
{
    host_test_mutex_t *host_mutex = (host_test_mutex_t *)mutex;

    assert(host_mutex != NULL);
    assert(host_mutex->locked == true);
    host_mutex->locked = false;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t mutex)
{
    assert(mutex != NULL);
    s_mutex_delete_calls++;
    free(mutex);
}

void vTaskDelay(TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
}

const char *esp_err_to_name(esp_err_t err)
{
    (void)err;
    return "host error";
}

esp_err_t network_manager_subscribe(network_manager_t *me, const char *topic,
                                    network_mqtt_qos_t qos)
{
    (void)me;
    (void)topic;
    (void)qos;
    return ESP_OK;
}

esp_err_t network_manager_unsubscribe(network_manager_t *me,
                                      const char *topic)
{
    (void)me;
    (void)topic;
    s_unsubscribe_calls++;
    return s_unsubscribe_ret;
}

esp_err_t network_manager_register_rx_cb(network_manager_t *me,
                                         network_rx_cb_t cb, void *ctx)
{
    (void)me;

    if (cb == NULL) {
        s_clear_rx_calls++;
        s_rx_cb = NULL;
        s_rx_ctx = NULL;
        return s_clear_rx_ret;
    }

    s_rx_cb = cb;
    s_rx_ctx = ctx;
    return ESP_OK;
}

esp_err_t network_manager_publish(network_manager_t *me,
                                  const network_publish_request_t *req)
{
    (void)me;
    (void)req;
    return ESP_OK;
}

#include "thingsboard_client_internal.c"
#include "thingsboard_client.c"

static void test_destroy_retries_network_rx_drain_before_free(void)
{
    network_manager_t manager = {0};
    const tb_client_config_t config = {
        .net_mgr = &manager,
        .enable_rpc = false,
        .enable_attributes = false,
        .json_buf_size = 128,
    };
    thingsboard_client_t *client = NULL;

    s_clear_rx_ret = ESP_OK;
    s_unsubscribe_ret = ESP_OK;
    s_clear_rx_calls = 0U;
    s_unsubscribe_calls = 0U;
    s_mutex_delete_calls = 0U;
    s_rx_cb = NULL;
    s_rx_ctx = NULL;

    client = thingsboard_client_create(&config);
    assert(client != NULL);
    assert(thingsboard_client_start(client) == ESP_OK);
    assert(client->network_rx_registered == true);
    assert(s_rx_cb == thingsboard_client_on_rx);
    assert(s_rx_ctx == client);

    s_clear_rx_ret = ESP_ERR_TIMEOUT;
    assert(thingsboard_client_destroy(client) == ESP_ERR_TIMEOUT);
    assert(s_clear_rx_calls == 1U);
    assert(s_mutex_delete_calls == 0U);
    assert(client->destroying == true);
    assert(client->stopping == false);
    assert(client->network_rx_registered == true);

    s_clear_rx_ret = ESP_OK;
    assert(thingsboard_client_destroy(client) == ESP_OK);
    assert(s_clear_rx_calls == 2U);
    assert(s_mutex_delete_calls == 1U);
}

static void test_destroy_retries_failed_unsubscribe_before_free(void)
{
    network_manager_t manager = {0};
    const tb_client_config_t config = {
        .net_mgr = &manager,
        .enable_rpc = true,
        .enable_attributes = false,
        .json_buf_size = 128,
    };
    thingsboard_client_t *client = NULL;

    s_clear_rx_ret = ESP_OK;
    s_unsubscribe_ret = ESP_OK;
    s_clear_rx_calls = 0U;
    s_unsubscribe_calls = 0U;
    s_mutex_delete_calls = 0U;
    s_rx_cb = NULL;
    s_rx_ctx = NULL;

    client = thingsboard_client_create(&config);
    assert(client != NULL);
    assert(thingsboard_client_start(client) == ESP_OK);

    s_unsubscribe_ret = ESP_FAIL;
    assert(thingsboard_client_destroy(client) == ESP_FAIL);
    assert(s_clear_rx_calls == 1U);
    assert(s_unsubscribe_calls == 1U);
    assert(s_mutex_delete_calls == 0U);
    assert(client->network_rx_registered == false);
    assert(client->cleanup_pending == true);

    s_unsubscribe_ret = ESP_OK;
    assert(thingsboard_client_destroy(client) == ESP_OK);
    assert(s_clear_rx_calls == 1U);
    assert(s_unsubscribe_calls == 2U);
    assert(s_mutex_delete_calls == 1U);
}

int main(void)
{
    test_destroy_retries_network_rx_drain_before_free();
    test_destroy_retries_failed_unsubscribe_before_free();

    printf("thingsboard client lifecycle tests passed\n");
    return 0;
}
