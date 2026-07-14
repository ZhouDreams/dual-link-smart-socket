#pragma once

#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *arg);

#define pdPASS 1

BaseType_t xTaskCreate(TaskFunction_t task, const char *name,
                       uint32_t stack_depth, void *arg,
                       UBaseType_t priority, TaskHandle_t *out_handle);
void vTaskDelay(TickType_t ticks_to_wait);
void vTaskDelete(TaskHandle_t task);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t xTaskGetTickCount(void);
void vTaskDelayUntil(TickType_t *previous_wake_time,
                     TickType_t time_increment);
