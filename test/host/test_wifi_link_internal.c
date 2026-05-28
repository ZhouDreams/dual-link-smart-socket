#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "wifi_link_internal.h"

static int s_publish_calls;
static esp_mqtt_client_handle_t s_publish_client;
static const char *s_publish_topic;
static const char *s_publish_data;
static int s_publish_len;
static int s_publish_qos;
static int s_publish_retain;

int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char *topic,
                            const char *data, int len, int qos, int retain)
{
    s_publish_calls++;
    s_publish_client = client;
    s_publish_topic = topic;
    s_publish_data = data;
    s_publish_len = len;
    s_publish_qos = qos;
    s_publish_retain = retain;
    return 0;
}

static void reset_publish_spy(void)
{
    s_publish_calls = 0;
    s_publish_client = NULL;
    s_publish_topic = NULL;
    s_publish_data = NULL;
    s_publish_len = 0;
    s_publish_qos = 0;
    s_publish_retain = 0;
}

static void test_qos0_publish_uses_immediate_mqtt_publish(void)
{
    const char payload[] = "{\"power\":12.3}";
    const network_publish_request_t req = {
        .topic = "v1/devices/me/telemetry",
        .payload = payload,
        .payload_len = strlen(payload),
        .qos = NETWORK_MQTT_QOS0,
        .retain = false,
    };
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)&s_publish_calls;

    reset_publish_spy();

    assert(wifi_link_internal_publish_mqtt(client, &req) == ESP_OK);
    assert(s_publish_calls == 1);
    assert(s_publish_client == client);
    assert(strcmp(s_publish_topic, "v1/devices/me/telemetry") == 0);
    assert(s_publish_data == payload);
    assert(s_publish_len == (int)strlen(payload));
    assert(s_publish_qos == 0);
    assert(s_publish_retain == 0);
}

int main(void)
{
    test_qos0_publish_uses_immediate_mqtt_publish();

    printf("wifi link internal tests passed\n");

    return 0;
}
