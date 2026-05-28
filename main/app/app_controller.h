/**
 * @file app_controller.h
 * @brief 应用控制器公共接口
 * @details App controller public interface
 * @author OpenCode
 * @date 2026-05-28
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "bl0942.h"
#include "board_pinmap.h"
#include "button.h"
#include "esp_err.h"
#include "esp_event.h"
#include "lvgl_dashboard.h"
#include "metering_service.h"
#include "network_manager.h"
#include "relay.h"
#include "safety_guard.h"
#include "tft_panel.h"
#include "thingsboard_client.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 应用控制器句柄
 * @details App controller handle
 */
typedef struct app_controller app_controller_t;

/**
 * @brief 应用控制器配置
 * @details App controller configuration
 */
typedef struct {
    esp_event_loop_handle_t event_loop;     /**< 借用事件循环句柄，可为 NULL 使用默认循环； Borrowed event loop handle, may be NULL to use default loop */
    const board_pinmap_t *pinmap;           /**< 借用板级引脚表，可为 NULL； Borrowed board pinmap, may be NULL */
    relay_t *relay;                         /**< 借用继电器句柄； Borrowed relay handle */
    button_t *button;                       /**< 借用按键句柄； Borrowed button handle */
    bl0942_t *bl0942;                       /**< 借用 BL0942 句柄； Borrowed BL0942 handle */
    tft_panel_t *tft_panel;                 /**< 借用 TFT 面板句柄，可为 NULL； Borrowed TFT panel handle, may be NULL */
    metering_service_t *metering;           /**< 借用电参量服务句柄； Borrowed metering service handle */
    safety_guard_t *safety;                 /**< 借用安全规则句柄； Borrowed safety guard handle */
    thingsboard_client_t *tb;               /**< 借用 ThingsBoard 客户端句柄； Borrowed ThingsBoard client handle */
    network_manager_t *net_mgr;             /**< 借用网络管理器句柄； Borrowed network manager handle */
    lvgl_dashboard_t *dashboard;            /**< 借用 LVGL 看板句柄； Borrowed LVGL dashboard handle */
} app_controller_config_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 创建应用控制器
 * @details Create app controller
 * @param[in] config 控制器配置； Controller configuration
 * @return 应用控制器句柄，失败返回 NULL； App controller handle, NULL on failure
 */
app_controller_t *app_controller_create(const app_controller_config_t *config);

/**
 * @brief 销毁应用控制器
 * @details Destroy app controller
 * @note 本函数不会销毁配置中借用的模块句柄； This function never destroys borrowed module handles.
 * @param[in] me 应用控制器句柄，可为 NULL； App controller handle, may be NULL
 * @return
 *         - ESP_OK: 成功； Success
 *         - 其他: 停止失败； Stop failed
 */
esp_err_t app_controller_destroy(app_controller_t *me);

/**
 * @brief 启动应用控制器
 * @details Start app controller
 * @param[in] me 应用控制器句柄； App controller handle
 * @return
 *         - ESP_OK: 成功或已启动； Success or already started
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_NO_MEM: 内存不足； Out of memory
 *         - 其他: 下层模块启动失败； Lower module start failed
 */
esp_err_t app_controller_start(app_controller_t *me);

/**
 * @brief 停止应用控制器
 * @details Stop app controller
 * @param[in] me 应用控制器句柄； App controller handle
 * @return
 *         - ESP_OK: 成功或已停止； Success or already stopped
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - 其他: 下层模块停止或回调清理失败； Lower module stop or callback cleanup failed
 */
esp_err_t app_controller_stop(app_controller_t *me);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
