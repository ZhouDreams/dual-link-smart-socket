#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct lwlte lwlte_t;

typedef enum {
    LWLTE_STATE_STOPPED = 0,
    LWLTE_STATE_STARTING,
    LWLTE_STATE_READY,
    LWLTE_STATE_NET_ACTIVATING,
    LWLTE_STATE_ONLINE,
    LWLTE_STATE_ERROR,
    LWLTE_STATE_DESTROYING,
} lwlte_state_t;

typedef enum {
    LWLTE_MQTT_STATE_STOPPED = 0,
    LWLTE_MQTT_STATE_WAITING_NET,
    LWLTE_MQTT_STATE_CONNECTING,
    LWLTE_MQTT_STATE_CONNECTED,
    LWLTE_MQTT_STATE_DISCONNECTING,
    LWLTE_MQTT_STATE_ERROR,
} lwlte_mqtt_state_t;
