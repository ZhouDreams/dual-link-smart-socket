#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_event.h"

typedef struct lwlte *lwlte_handle_t;

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

typedef enum {
    LWLTE_EVENT_NET_ONLINE = 0,
    LWLTE_EVENT_NET_OFFLINE,
    LWLTE_EVENT_NET_ERROR,
    LWLTE_EVENT_ERROR,
} lwlte_event_id_t;

typedef enum {
    LWLTE_MQTT_EVENT_CONNECTED = 0,
    LWLTE_MQTT_EVENT_DISCONNECTED,
    LWLTE_MQTT_EVENT_DATA,
    LWLTE_MQTT_EVENT_ERROR,
} lwlte_mqtt_event_id_t;

typedef struct {
    struct {
        uart_port_t num;
        gpio_num_t tx_pin;
        gpio_num_t rx_pin;
        int baud_rate;
    } uart;
    struct {
        int unused;
    } at_engine;
    struct {
        gpio_num_t en_pin;
        uint32_t reset_pulse_ms;
        uint32_t ready_timeout_ms;
        uint32_t default_cmd_timeout_ms;
        int event_queue_size;
        int event_task_stack;
        int event_task_priority;
    } modem;
    struct {
        const char *apn;
        int primary_cid;
        uint32_t net_activate_timeout_ms;
        uint32_t reconnect_delay_ms;
        int fsm_queue_size;
        int fsm_task_stack;
        int fsm_task_priority;
    } core;
    struct {
        esp_event_loop_handle_t loop;
    } event;
} lwlte_base_config_t;

typedef struct {
    lwlte_base_config_t base;
} lwlte_air780ep_config_t;

typedef struct {
    const char *host;
    uint16_t port;
    const char *client_id;
    const char *username;
    const char *password;
    uint16_t keepalive_s;
    bool clean_session;
    int fsm_queue_size;
    int fsm_task_stack;
    int fsm_task_priority;
} lwlte_mqtt_config_t;

typedef struct {
    struct {
        const char *topic;
        size_t topic_len;
        const void *payload;
        size_t payload_len;
    } msg;
} lwlte_mqtt_event_data_t;

ESP_EVENT_DECLARE_BASE(LWLTE_EVENT);
ESP_EVENT_DECLARE_BASE(LWLTE_MQTT_EVENT);

esp_err_t lwlte_air780ep_init(const lwlte_air780ep_config_t *config,
                              lwlte_handle_t *out_lte);
esp_err_t lwlte_destroy(lwlte_handle_t me);
esp_err_t lwlte_start(lwlte_handle_t me);
esp_err_t lwlte_stop(lwlte_handle_t me);
esp_err_t lwlte_get_state(lwlte_handle_t me, lwlte_state_t *state);
esp_err_t lwlte_mqtt_init(lwlte_handle_t me,
                          const lwlte_mqtt_config_t *config);
esp_err_t lwlte_mqtt_start(lwlte_handle_t me);
esp_err_t lwlte_mqtt_stop(lwlte_handle_t me);
esp_err_t lwlte_mqtt_get_state(lwlte_handle_t me,
                               lwlte_mqtt_state_t *state);
esp_err_t lwlte_mqtt_publish(lwlte_handle_t me, const char *topic,
                             const uint8_t *payload, size_t payload_len,
                             uint8_t qos, bool retain);
esp_err_t lwlte_mqtt_subscribe(lwlte_handle_t me, const char *topic,
                               uint8_t qos);
esp_err_t lwlte_mqtt_unsubscribe(lwlte_handle_t me, const char *topic);
void lwlte_mqtt_event_data_release(lwlte_mqtt_event_data_t *data);
