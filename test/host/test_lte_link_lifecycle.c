#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_event.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lte_link.h"
#include "lwlte.h"
#include "network_link.h"

typedef struct host_test_mutex {
    bool locked;
} host_test_mutex_t;

struct lwlte {
    int marker;
};

ESP_EVENT_DEFINE_BASE(LWLTE_EVENT);
ESP_EVENT_DEFINE_BASE(LWLTE_MQTT_EVENT);

static network_link_t *s_link_under_test;
static esp_err_t s_lwlte_destroy_result;
static esp_err_t s_nested_destroy_result;
static int s_lwlte_destroy_calls;
static bool s_destroy_during_mqtt_start;
static bool s_destroy_during_mqtt_publish;
static bool s_destroy_during_lwlte_destroy;

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return (SemaphoreHandle_t)calloc(1, sizeof(host_test_mutex_t));
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t ticks_to_wait)
{
    host_test_mutex_t *host_mutex = (host_test_mutex_t *)mutex;

    (void)ticks_to_wait;
    assert(host_mutex != NULL);
    assert(!host_mutex->locked);
    host_mutex->locked = true;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex)
{
    host_test_mutex_t *host_mutex = (host_test_mutex_t *)mutex;

    assert(host_mutex != NULL);
    assert(host_mutex->locked);
    host_mutex->locked = false;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t mutex)
{
    free(mutex);
}

void vTaskDelay(TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
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
    *instance = (esp_event_handler_instance_t)event_handler;
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

const char *esp_err_to_name(esp_err_t err)
{
    (void)err;
    return "host-error";
}

esp_err_t lwlte_air780ep_init(const lwlte_air780ep_config_t *config,
                              lwlte_handle_t *out_lte)
{
    (void)config;
    *out_lte = calloc(1, sizeof(**out_lte));
    return (*out_lte != NULL) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t lwlte_destroy(lwlte_handle_t me)
{
    s_lwlte_destroy_calls++;
    if (s_destroy_during_lwlte_destroy) {
        s_destroy_during_lwlte_destroy = false;
        s_nested_destroy_result = network_link_destroy(s_link_under_test);
    }
    if (s_lwlte_destroy_result != ESP_OK) {
        return s_lwlte_destroy_result;
    }
    free(me);
    return ESP_OK;
}

esp_err_t lwlte_start(lwlte_handle_t me)
{
    return (me != NULL) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t lwlte_stop(lwlte_handle_t me)
{
    return (me != NULL) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t lwlte_get_state(lwlte_handle_t me, lwlte_state_t *state)
{
    assert(me != NULL && state != NULL);
    *state = LWLTE_STATE_ONLINE;
    return ESP_OK;
}

esp_err_t lwlte_mqtt_init(lwlte_handle_t me,
                          const lwlte_mqtt_config_t *config)
{
    return (me != NULL && config != NULL) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t lwlte_mqtt_start(lwlte_handle_t me)
{
    assert(me != NULL);
    if (s_destroy_during_mqtt_start) {
        s_destroy_during_mqtt_start = false;
        s_nested_destroy_result = network_link_destroy(s_link_under_test);
    }
    return ESP_OK;
}

esp_err_t lwlte_mqtt_stop(lwlte_handle_t me)
{
    return (me != NULL) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t lwlte_mqtt_get_state(lwlte_handle_t me,
                               lwlte_mqtt_state_t *state)
{
    assert(me != NULL && state != NULL);
    *state = LWLTE_MQTT_STATE_CONNECTED;
    return ESP_OK;
}

esp_err_t lwlte_mqtt_publish(lwlte_handle_t me, const char *topic,
                             const uint8_t *payload, size_t payload_len,
                             uint8_t qos, bool retain)
{
    (void)payload_len;
    (void)qos;
    (void)retain;
    if (s_destroy_during_mqtt_publish) {
        s_destroy_during_mqtt_publish = false;
        s_nested_destroy_result = network_link_destroy(s_link_under_test);
    }
    return (me != NULL && topic != NULL && payload != NULL) ?
               ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t lwlte_mqtt_subscribe(lwlte_handle_t me, const char *topic,
                               uint8_t qos)
{
    (void)qos;
    return (me != NULL && topic != NULL) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t lwlte_mqtt_unsubscribe(lwlte_handle_t me, const char *topic)
{
    return (me != NULL && topic != NULL) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

void lwlte_mqtt_event_data_release(lwlte_mqtt_event_data_t *data)
{
    (void)data;
}

static network_link_t *create_link(void)
{
    const lte_link_config_t config = {
        .uart_num = UART_NUM_1,
        .tx_gpio = 1,
        .rx_gpio = 2,
        .en_gpio = 3,
        .apn = "internet",
        .mqtt_enabled = true,
        .mqtt_broker_host = "broker",
        .mqtt_broker_port = 1883,
        .mqtt_client_id = "socket",
        .mqtt_keepalive_s = 30,
        .mqtt_clean_session = true,
    };

    return lte_link_create(&config);
}

static void reset_test_state(void)
{
    s_link_under_test = NULL;
    s_lwlte_destroy_result = ESP_OK;
    s_nested_destroy_result = ESP_OK;
    s_lwlte_destroy_calls = 0;
    s_destroy_during_mqtt_start = false;
    s_destroy_during_mqtt_publish = false;
    s_destroy_during_lwlte_destroy = false;
}

static void test_destroy_failure_preserves_handle_for_retry(void)
{
    reset_test_state();
    s_link_under_test = create_link();
    assert(s_link_under_test != NULL);

    s_lwlte_destroy_result = ESP_FAIL;
    assert(network_link_destroy(s_link_under_test) == ESP_FAIL);
    assert(s_lwlte_destroy_calls == 1);
    assert(network_link_set_active(s_link_under_test, true) ==
           ESP_ERR_INVALID_STATE);

    s_lwlte_destroy_result = ESP_OK;
    assert(network_link_destroy(s_link_under_test) == ESP_OK);
    assert(s_lwlte_destroy_calls == 2);
    s_link_under_test = NULL;
}

static void test_destroy_waits_for_inflight_set_active(void)
{
    reset_test_state();
    s_link_under_test = create_link();
    assert(s_link_under_test != NULL);

    s_destroy_during_mqtt_start = true;
    assert(network_link_set_active(s_link_under_test, true) == ESP_OK);
    assert(s_nested_destroy_result == ESP_ERR_TIMEOUT);
    assert(s_lwlte_destroy_calls == 0);

    assert(network_link_destroy(s_link_under_test) == ESP_OK);
    assert(s_lwlte_destroy_calls == 1);
    s_link_under_test = NULL;
}

static void test_destroy_waits_for_inflight_publish(void)
{
    static const char payload[] = "{}";
    const network_publish_request_t request = {
        .topic = "v1/devices/me/telemetry",
        .payload = payload,
        .payload_len = sizeof(payload) - 1U,
        .qos = NETWORK_MQTT_QOS0,
        .retain = false,
    };

    reset_test_state();
    s_link_under_test = create_link();
    assert(s_link_under_test != NULL);

    s_destroy_during_mqtt_publish = true;
    assert(network_link_publish(s_link_under_test, &request) == ESP_OK);
    assert(s_nested_destroy_result == ESP_ERR_TIMEOUT);
    assert(s_lwlte_destroy_calls == 0);

    assert(network_link_destroy(s_link_under_test) == ESP_OK);
    assert(s_lwlte_destroy_calls == 1);
    s_link_under_test = NULL;
}

static void test_reentrant_destroy_is_rejected(void)
{
    reset_test_state();
    s_link_under_test = create_link();
    assert(s_link_under_test != NULL);

    s_destroy_during_lwlte_destroy = true;
    assert(network_link_destroy(s_link_under_test) == ESP_OK);
    assert(s_nested_destroy_result == ESP_ERR_INVALID_STATE);
    assert(s_lwlte_destroy_calls == 1);
    s_link_under_test = NULL;
}

int main(void)
{
    test_destroy_failure_preserves_handle_for_retry();
    test_destroy_waits_for_inflight_set_active();
    test_destroy_waits_for_inflight_publish();
    test_reentrant_destroy_is_rejected();

    printf("lte link lifecycle tests passed\n");
    return 0;
}
