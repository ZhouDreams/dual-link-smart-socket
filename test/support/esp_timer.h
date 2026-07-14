#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct host_test_esp_timer *esp_timer_handle_t;
typedef void (*esp_timer_cb_t)(void *arg);

typedef enum {
    ESP_TIMER_TASK = 0,
    ESP_TIMER_MAX,
} esp_timer_dispatch_t;

typedef struct {
    esp_timer_cb_t callback;
    void *arg;
    esp_timer_dispatch_t dispatch_method;
    const char *name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;

esp_err_t esp_timer_create(const esp_timer_create_args_t *create_args,
                           esp_timer_handle_t *out_handle);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us);
esp_err_t esp_timer_delete(esp_timer_handle_t timer);
bool esp_timer_is_active(esp_timer_handle_t timer);
int64_t esp_timer_get_time(void);
