#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bl0942.c"

struct host_test_mutex {
    bool locked;
    bool signaled;
    bool is_mutex;
};

static bl0942_t *s_subject = NULL;
static bool s_uart_installed = true;
static int s_delay_calls = 0;
static bool s_state_locked_during_delay = false;
static bool s_io_unlocked_during_delay = false;
static bool s_io_unlocked_during_hardware_call = false;
static esp_err_t s_latest_during_reset_ret = ESP_FAIL;
static bl0942_measurement_t s_latest_during_reset = {0};

static void record_hardware_call(void)
{
    if (s_subject != NULL &&
        !((struct host_test_mutex *)s_subject->io_mutex)->locked) {
        s_io_unlocked_during_hardware_call = true;
    }
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    struct host_test_mutex *host = calloc(1, sizeof(*host));

    if (host != NULL) {
        host->is_mutex = true;
    }
    return host;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    return calloc(1, sizeof(struct host_test_mutex));
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore,
                          TickType_t ticks_to_wait)
{
    struct host_test_mutex *host = semaphore;

    assert(host != NULL);
    if (host->is_mutex) {
        if (host->locked) {
            return pdFALSE;
        }
        host->locked = true;
        return pdTRUE;
    }
    if (host->signaled) {
        host->signaled = false;
        return pdTRUE;
    }
    (void)ticks_to_wait;
    return pdFALSE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    struct host_test_mutex *host = semaphore;

    assert(host != NULL);
    if (host->is_mutex) {
        assert(host->locked);
        host->locked = false;
    } else {
        host->signaled = true;
    }
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore)
{
    free(semaphore);
}

BaseType_t xTaskCreate(TaskFunction_t task, const char *name,
                       uint32_t stack_depth, void *arg,
                       UBaseType_t priority, TaskHandle_t *out_handle)
{
    (void)task;
    (void)name;
    (void)stack_depth;
    (void)arg;
    (void)priority;
    (void)out_handle;
    return pdFALSE;
}

void vTaskDelay(TickType_t ticks_to_wait)
{
    struct host_test_mutex *state = s_subject->mutex;
    struct host_test_mutex *io = s_subject->io_mutex;

    (void)ticks_to_wait;
    s_delay_calls++;
    s_state_locked_during_delay |= state->locked;
    s_io_unlocked_during_delay |= !io->locked;
    if (s_delay_calls == 1) {
        s_latest_during_reset_ret = bl0942_get_latest(
            s_subject, &s_latest_during_reset);
    }
}

void vTaskDelete(TaskHandle_t task)
{
    (void)task;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return NULL;
}

TickType_t xTaskGetTickCount(void)
{
    return 0U;
}

void vTaskDelayUntil(TickType_t *previous_wake_time,
                     TickType_t time_increment)
{
    *previous_wake_time += time_increment;
}

esp_err_t gpio_config(const gpio_config_t *config)
{
    (void)config;
    record_hardware_call();
    return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    (void)gpio_num;
    (void)level;
    record_hardware_call();
    return ESP_OK;
}

esp_err_t gpio_reset_pin(gpio_num_t gpio_num)
{
    (void)gpio_num;
    return ESP_OK;
}

bool uart_is_driver_installed(uart_port_t uart_num)
{
    (void)uart_num;
    record_hardware_call();
    return s_uart_installed;
}

esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size,
                              int tx_buffer_size, int queue_size,
                              void *uart_queue, int intr_alloc_flags)
{
    (void)uart_num;
    (void)rx_buffer_size;
    (void)tx_buffer_size;
    (void)queue_size;
    (void)uart_queue;
    (void)intr_alloc_flags;
    record_hardware_call();
    s_uart_installed = true;
    return ESP_OK;
}

esp_err_t uart_driver_delete(uart_port_t uart_num)
{
    (void)uart_num;
    record_hardware_call();
    s_uart_installed = false;
    return ESP_OK;
}

esp_err_t uart_param_config(uart_port_t uart_num,
                            const uart_config_t *uart_config)
{
    (void)uart_num;
    (void)uart_config;
    record_hardware_call();
    return ESP_OK;
}

esp_err_t uart_set_pin(uart_port_t uart_num, int tx_io_num, int rx_io_num,
                       int rts_io_num, int cts_io_num)
{
    (void)uart_num;
    (void)tx_io_num;
    (void)rx_io_num;
    (void)rts_io_num;
    (void)cts_io_num;
    record_hardware_call();
    return ESP_OK;
}

esp_err_t uart_flush_input(uart_port_t uart_num)
{
    (void)uart_num;
    return ESP_OK;
}

int uart_write_bytes(uart_port_t uart_num, const void *src, size_t size)
{
    (void)uart_num;
    (void)src;
    return (int)size;
}

esp_err_t uart_wait_tx_done(uart_port_t uart_num, uint32_t ticks_to_wait)
{
    (void)uart_num;
    (void)ticks_to_wait;
    return ESP_OK;
}

int uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length,
                    uint32_t ticks_to_wait)
{
    (void)uart_num;
    (void)buf;
    (void)length;
    (void)ticks_to_wait;
    return 0;
}

esp_err_t esp_event_post(esp_event_base_t event_base, int32_t event_id,
                         const void *event_data, size_t event_data_size,
                         uint32_t ticks_to_wait)
{
    (void)event_base;
    (void)event_id;
    (void)event_data;
    (void)event_data_size;
    (void)ticks_to_wait;
    return ESP_OK;
}

int64_t esp_timer_get_time(void)
{
    return 1234;
}

const char *esp_err_to_name(esp_err_t err)
{
    (void)err;
    return "host-error";
}

static void test_hard_reset_does_not_hold_state_mutex(void)
{
    bl0942_t subject = {
        .config = {
            .uart_num = UART_NUM_1,
            .en_gpio = 10,
            .tx_gpio = 11,
            .rx_gpio = 12,
            .baud_rate = 9600,
            .rx_buf_size = 256,
        },
        .latest = {
            .v_rms_raw = 230U,
            .capture_time_us = 42U,
            .valid = true,
        },
        .has_latest = true,
        .initialized = true,
    };

    subject.mutex = xSemaphoreCreateMutex();
    subject.io_mutex = xSemaphoreCreateMutex();
    subject.lifecycle_mutex = xSemaphoreCreateMutex();
    subject.active_ops_done_sema = xSemaphoreCreateBinary();
    assert(subject.mutex != NULL);
    assert(subject.io_mutex != NULL);
    assert(subject.lifecycle_mutex != NULL);
    assert(subject.active_ops_done_sema != NULL);
    s_subject = &subject;

    assert(bl0942_hard_reset(&subject) == ESP_OK);
    assert(s_delay_calls == 2);
    assert(s_state_locked_during_delay == false);
    assert(s_io_unlocked_during_delay == false);
    assert(s_io_unlocked_during_hardware_call == false);
    assert(s_latest_during_reset_ret == ESP_OK);
    assert(s_latest_during_reset.v_rms_raw == 230U);
    assert(s_latest_during_reset.capture_time_us == 42U);
    assert(subject.active_ops == 0);

    vSemaphoreDelete(subject.active_ops_done_sema);
    vSemaphoreDelete(subject.lifecycle_mutex);
    vSemaphoreDelete(subject.io_mutex);
    vSemaphoreDelete(subject.mutex);
    s_subject = NULL;
}

int main(void)
{
    test_hard_reset_does_not_hold_state_mutex();

    printf("BL0942 reset locking tests passed\n");
    return 0;
}
