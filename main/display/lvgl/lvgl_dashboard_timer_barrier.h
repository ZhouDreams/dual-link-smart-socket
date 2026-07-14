/**
 * @file lvgl_dashboard_timer_barrier.h
 * @brief LVGL tick 定时器派发屏障私有接口
 * @details Private LVGL tick timer dispatch barrier interface
 * @author OpenCode
 * @date 2026-07-14
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief timer task 派发屏障状态
 * @details Timer-task dispatch barrier state
 */
typedef struct {
    esp_timer_handle_t timer;       /**< 屏障 timer； Barrier timer */
    SemaphoreHandle_t done_sema;    /**< 完成信号量； Completion semaphore */
    bool started;                   /**< timer 已启动； Timer started */
    bool completed;                 /**< 回调已确认； Callback acknowledged */
} lvgl_dashboard_timer_barrier_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 等待 timer task 越过派发屏障
 * @details Wait for the timer task to pass the dispatch barrier
 * @note 失败时保留内部资源和进度，调用方必须保留 barrier 并重试。
 *       Internal resources and progress are retained on failure; the caller must retain and retry the barrier.
 * @param[in,out] barrier 屏障状态； Barrier state
 * @param[in] timeout_ms 等待超时毫秒数； Wait timeout in milliseconds
 * @return
 *         - ESP_OK: 屏障完成且内部资源已释放； Barrier completed and resources released
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_NO_MEM: 资源分配失败； Resource allocation failed
 *         - ESP_ERR_TIMEOUT: 等待回调超时； Callback wait timed out
 *         - 其他: esp_timer 错误； Other esp_timer error
 */
esp_err_t lvgl_dashboard_timer_barrier_wait(
    lvgl_dashboard_timer_barrier_t *barrier, uint32_t timeout_ms);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
