#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef const char *esp_event_base_t;
typedef void *esp_event_loop_handle_t;
typedef void (*esp_event_handler_t)(void *arg, esp_event_base_t base,
                                    int32_t id, void *event_data);
typedef void *esp_event_handler_instance_t;

#define ESP_EVENT_DECLARE_BASE(id) extern esp_event_base_t id
#define ESP_EVENT_DEFINE_BASE(id) esp_event_base_t id = #id
#define ESP_EVENT_ANY_ID (-1)

esp_err_t esp_event_post(esp_event_base_t event_base, int32_t event_id,
                         const void *event_data, size_t event_data_size,
                         uint32_t ticks_to_wait);

esp_err_t esp_event_handler_instance_register(
    esp_event_base_t event_base, int32_t event_id,
    esp_event_handler_t event_handler, void *event_handler_arg,
    esp_event_handler_instance_t *instance);

esp_err_t esp_event_handler_instance_unregister(
    esp_event_base_t event_base, int32_t event_id,
    esp_event_handler_instance_t instance);

esp_err_t esp_event_handler_instance_register_with(
    esp_event_loop_handle_t event_loop, esp_event_base_t event_base,
    int32_t event_id, esp_event_handler_t event_handler,
    void *event_handler_arg, esp_event_handler_instance_t *instance);

esp_err_t esp_event_handler_instance_unregister_with(
    esp_event_loop_handle_t event_loop, esp_event_base_t event_base,
    int32_t event_id, esp_event_handler_instance_t instance);
