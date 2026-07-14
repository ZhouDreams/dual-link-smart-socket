#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/semphr.h"
#include "lvgl_dashboard_timer_barrier.h"

struct host_test_mutex {
    bool signaled;
};

struct host_test_esp_timer {
    esp_timer_cb_t callback;
    void *arg;
    bool active;
};

static bool s_auto_fire = false;
static esp_err_t s_delete_ret = ESP_OK;
static unsigned int s_timer_create_calls = 0U;
static unsigned int s_timer_delete_calls = 0U;
static unsigned int s_semaphore_delete_calls = 0U;
static TickType_t s_last_wait_ticks = 0U;

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return NULL;
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
    s_last_wait_ticks = ticks_to_wait;
    if (!host->signaled) {
        return pdFALSE;
    }
    host->signaled = false;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    struct host_test_mutex *host = semaphore;

    assert(host != NULL);
    host->signaled = true;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore)
{
    assert(semaphore != NULL);
    s_semaphore_delete_calls++;
    free(semaphore);
}

esp_err_t esp_timer_create(const esp_timer_create_args_t *create_args,
                           esp_timer_handle_t *out_handle)
{
    struct host_test_esp_timer *timer = NULL;

    assert(create_args != NULL);
    assert(create_args->callback != NULL);
    assert(create_args->dispatch_method == ESP_TIMER_TASK);
    assert(out_handle != NULL);
    timer = calloc(1, sizeof(*timer));
    if (timer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    timer->callback = create_args->callback;
    timer->arg = create_args->arg;
    *out_handle = timer;
    s_timer_create_calls++;
    return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us)
{
    assert(timer != NULL);
    assert(timeout_us > 0U);
    assert(timer->active == false);
    timer->active = true;
    if (s_auto_fire) {
        timer->active = false;
        timer->callback(timer->arg);
    }
    return ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t timer)
{
    assert(timer != NULL);
    assert(timer->active == false);
    if (s_delete_ret != ESP_OK) {
        const esp_err_t ret = s_delete_ret;
        s_delete_ret = ESP_OK;
        return ret;
    }
    s_timer_delete_calls++;
    free(timer);
    return ESP_OK;
}

bool esp_timer_is_active(esp_timer_handle_t timer)
{
    assert(timer != NULL);
    return timer->active;
}

int64_t esp_timer_get_time(void)
{
    return 0;
}

static void fire_barrier_timer(lvgl_dashboard_timer_barrier_t *barrier)
{
    assert(barrier != NULL);
    assert(barrier->timer != NULL);
    assert(barrier->timer->active == true);
    barrier->timer->active = false;
    barrier->timer->callback(barrier->timer->arg);
}

static void reset_spies(void)
{
    s_auto_fire = false;
    s_delete_ret = ESP_OK;
    s_timer_create_calls = 0U;
    s_timer_delete_calls = 0U;
    s_semaphore_delete_calls = 0U;
    s_last_wait_ticks = 0U;
}

static void test_completed_barrier_releases_resources(void)
{
    lvgl_dashboard_timer_barrier_t barrier = {0};

    reset_spies();
    s_auto_fire = true;
    assert(lvgl_dashboard_timer_barrier_wait(&barrier, 3000U) == ESP_OK);
    assert(barrier.timer == NULL);
    assert(barrier.done_sema == NULL);
    assert(barrier.started == false);
    assert(barrier.completed == false);
    assert(s_timer_create_calls == 1U);
    assert(s_timer_delete_calls == 1U);
    assert(s_semaphore_delete_calls == 1U);
    assert(s_last_wait_ticks == pdMS_TO_TICKS(3000U));
}

static void test_timeout_retains_resources_for_late_callback_retry(void)
{
    lvgl_dashboard_timer_barrier_t barrier = {0};
    esp_timer_handle_t retained_timer = NULL;
    SemaphoreHandle_t retained_sema = NULL;

    reset_spies();
    assert(lvgl_dashboard_timer_barrier_wait(&barrier, 3000U) ==
           ESP_ERR_TIMEOUT);
    retained_timer = barrier.timer;
    retained_sema = barrier.done_sema;
    assert(retained_timer != NULL);
    assert(retained_sema != NULL);
    assert(barrier.started == true);
    assert(s_timer_delete_calls == 0U);
    assert(s_semaphore_delete_calls == 0U);

    assert(lvgl_dashboard_timer_barrier_wait(&barrier, 3000U) ==
           ESP_ERR_TIMEOUT);
    assert(barrier.timer == retained_timer);
    assert(barrier.done_sema == retained_sema);
    assert(s_timer_create_calls == 1U);
    assert(s_timer_delete_calls == 0U);

    fire_barrier_timer(&barrier);
    assert(lvgl_dashboard_timer_barrier_wait(&barrier, 3000U) == ESP_OK);
    assert(barrier.timer == NULL);
    assert(barrier.done_sema == NULL);
    assert(s_timer_create_calls == 1U);
    assert(s_timer_delete_calls == 1U);
    assert(s_semaphore_delete_calls == 1U);
}

static void test_delete_failure_retries_without_another_callback(void)
{
    lvgl_dashboard_timer_barrier_t barrier = {0};
    esp_timer_handle_t retained_timer = NULL;

    reset_spies();
    s_auto_fire = true;
    s_delete_ret = ESP_ERR_INVALID_STATE;
    assert(lvgl_dashboard_timer_barrier_wait(&barrier, 3000U) ==
           ESP_ERR_INVALID_STATE);
    retained_timer = barrier.timer;
    assert(retained_timer != NULL);
    assert(barrier.completed == true);
    assert(s_timer_delete_calls == 0U);
    assert(s_semaphore_delete_calls == 0U);

    s_auto_fire = false;
    assert(lvgl_dashboard_timer_barrier_wait(&barrier, 3000U) == ESP_OK);
    assert(s_timer_create_calls == 1U);
    assert(s_timer_delete_calls == 1U);
    assert(s_semaphore_delete_calls == 1U);
}

int main(void)
{
    test_completed_barrier_releases_resources();
    test_timeout_retains_resources_for_late_callback_retry();
    test_delete_failure_retries_without_another_callback();

    printf("LVGL dashboard timer barrier tests passed\n");
    return 0;
}
