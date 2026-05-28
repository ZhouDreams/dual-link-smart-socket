#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    NETWORK_LINK_TYPE_NONE = 0,
    NETWORK_LINK_TYPE_WIFI,
    NETWORK_LINK_TYPE_LTE,
} network_link_type_t;

typedef enum {
    NETWORK_LINK_STATUS_IDLE = 0,
    NETWORK_LINK_STATUS_STARTING,
    NETWORK_LINK_STATUS_CONNECTING,
    NETWORK_LINK_STATUS_DEGRADED,
    NETWORK_LINK_STATUS_READY,
    NETWORK_LINK_STATUS_ERROR,
} network_link_status_t;

typedef enum {
    NETWORK_MQTT_QOS0 = 0,
    NETWORK_MQTT_QOS1 = 1,
    NETWORK_MQTT_QOS2 = 2,
} network_mqtt_qos_t;

typedef struct {
    const char *topic;
    const void *payload;
    size_t payload_len;
    network_mqtt_qos_t qos;
    bool retain;
} network_publish_request_t;
