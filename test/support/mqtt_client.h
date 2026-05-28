#pragma once

typedef void *esp_mqtt_client_handle_t;

int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char *topic,
                            const char *data, int len, int qos, int retain);
