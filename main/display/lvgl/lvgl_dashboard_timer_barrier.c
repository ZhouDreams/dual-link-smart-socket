/**
 * @file lvgl_dashboard_timer_barrier.c
 * @brief LVGL tick 定时器派发屏障私有实现
 * @details Private LVGL tick timer dispatch barrier implementation
 * @author OpenCode
 * @date 2026-07-14
 */

/*********************
 *      INCLUDES
 *********************/

#include "lvgl_dashboard_timer_barrier.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "lvgl_tick_barrier"
#define LVGL_DASHBOARD_TICK_BARRIER_DELAY_US (1ULL)

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**
 * @brief 通知 timer task 派发屏障完成
 * @details Notify that the timer-task dispatch barrier completed
 * @param[in] arg 完成信号量； Completion semaphore
 */
static void lvgl_dashboard_timer_barrier_cb(void *arg);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

esp_err_t lvgl_dashboard_timer_barrier_wait(
    lvgl_dashboard_timer_barrier_t *barrier, uint32_t timeout_ms)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(barrier != NULL && timeout_ms > 0U,
                        ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    if (barrier->completed) {
        goto cleanup;
    }

    if (barrier->done_sema == NULL) {
        barrier->done_sema = xSemaphoreCreateBinary();
        ESP_RETURN_ON_FALSE(barrier->done_sema != NULL, ESP_ERR_NO_MEM, TAG,
                            "create barrier semaphore failed");
    }

    if (barrier->timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = lvgl_dashboard_timer_barrier_cb,
            .arg = barrier->done_sema,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "lvgl_tick_drain",
        };

        ret = esp_timer_create(&timer_args, &barrier->timer);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    if (!barrier->started) {
        while (xSemaphoreTake(barrier->done_sema, 0) == pdTRUE) {
        }
        ret = esp_timer_start_once(barrier->timer,
                                   LVGL_DASHBOARD_TICK_BARRIER_DELAY_US);
        if (ret != ESP_OK) {
            return ret;
        }
        barrier->started = true;
    }

    if (xSemaphoreTake(barrier->done_sema, pdMS_TO_TICKS(timeout_ms)) !=
        pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    barrier->started = false;
    barrier->completed = true;

cleanup:
    ESP_RETURN_ON_FALSE(barrier->timer != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "barrier timer is null");
    ret = esp_timer_delete(barrier->timer);
    if (ret != ESP_OK) {
        return ret;
    }
    barrier->timer = NULL;
    vSemaphoreDelete(barrier->done_sema);
    barrier->done_sema = NULL;
    barrier->completed = false;
    return ESP_OK;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lvgl_dashboard_timer_barrier_cb(void *arg)
{
    SemaphoreHandle_t done_sema = (SemaphoreHandle_t)arg;

    if (done_sema != NULL) {
        (void)xSemaphoreGive(done_sema);
    }
}
