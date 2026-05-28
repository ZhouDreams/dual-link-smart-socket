/**
 * @file lvgl_dashboard.h
 * @brief LVGL 本地看板接口
 * @details LVGL local dashboard interface
 * @author OpenCode
 * @date 2026-05-25
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "network_manager.h"
#include "safety_guard.h"
#include "tft_panel.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief LVGL 本地看板句柄
 * @details LVGL local dashboard opaque handle
 */
typedef struct lvgl_dashboard lvgl_dashboard_t;

/**
 * @brief 看板网络状态
 * @details Dashboard network state
 */
typedef enum {
    DASHBOARD_NET_OFFLINE = 0, /**< 离线； Offline */
    DASHBOARD_NET_CONNECTING,  /**< 连接中； Connecting */
    DASHBOARD_NET_WIFI,        /**< Wi-Fi 已连接； Wi-Fi connected */
    DASHBOARD_NET_LTE,         /**< LTE 已连接； LTE connected */
} dashboard_network_t;

/**
 * @brief 看板聚合状态
 * @details Dashboard aggregate state
 */
typedef struct {
    float voltage;                         /**< 电压 V； Voltage in volts */
    float current;                         /**< 电流 A； Current in amperes */
    float power;                           /**< 功率 W； Power in watts */
    float energy_delta;                    /**< 电能增量 mWh； Energy delta in milliwatt-hours */
    bool metering_valid;                   /**< 电参量是否有效； Whether metering data is valid */
    bool relay_on;                         /**< 继电器是否打开； Whether relay is on */
    bool relay_known;                      /**< 继电器状态是否已知； Whether relay state is known */
    dashboard_network_t network;           /**< 网络状态； Network state */
    bool network_ready;                    /**< 网络是否就绪； Whether network is ready */
    safety_guard_level_t safety_level;     /**< 安全等级； Safety level */
    bool safety_valid;                     /**< 安全状态是否有效； Whether safety state is valid */
    bool screen_enabled;                   /**< 屏幕是否启用； Whether screen is enabled */
    uint64_t last_update_us;               /**< 最近电参量更新时间 us； Last metering update time in microseconds */
} dashboard_state_t;

/**
 * @brief LVGL 本地看板初始化配置
 * @details LVGL local dashboard initialization configuration
 * @note panel 和 network_manager 均为借用句柄，调用方负责其生命周期。
 *       panel and network_manager are borrowed handles; the caller owns their lifetime.
 */
typedef struct {
    tft_panel_t *panel;                    /**< 借用的 TFT 面板句柄； Borrowed TFT panel handle */
    network_manager_t *network_manager;    /**< 借用的网络管理器句柄，可为 NULL； Borrowed network manager handle, may be NULL */
    int lvgl_task_stack;                   /**< LVGL 任务栈大小； LVGL task stack size */
    int lvgl_task_priority;                /**< LVGL 任务优先级； LVGL task priority */
    uint32_t lvgl_tick_period_ms;          /**< LVGL tick 周期 ms； LVGL tick period in milliseconds */
    uint32_t update_period_ms;             /**< 看板刷新周期 ms； Dashboard update period in milliseconds */
} lvgl_dashboard_config_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 创建 LVGL 本地看板实例
 * @details Create LVGL local dashboard instance
 * @param[in] config 初始化配置； Initialization configuration
 * @return LVGL 本地看板句柄，失败返回 NULL； LVGL local dashboard handle, NULL on failure
 */
lvgl_dashboard_t *lvgl_dashboard_create(const lvgl_dashboard_config_t *config);

/**
 * @brief 销毁 LVGL 本地看板实例
 * @details Destroy LVGL local dashboard instance
 * @param[in] me LVGL 本地看板句柄，可为 NULL； LVGL local dashboard handle, may be NULL
 * @return
 *         - ESP_OK: 成功； Success
 *         - 其他: 停止或清理资源失败； Stop or cleanup failure
 */
esp_err_t lvgl_dashboard_destroy(lvgl_dashboard_t *me);

/**
 * @brief 启动 LVGL 本地看板
 * @details Start LVGL local dashboard
 * @param[in] me LVGL 本地看板句柄； LVGL local dashboard handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 */
esp_err_t lvgl_dashboard_start(lvgl_dashboard_t *me);

/**
 * @brief 停止 LVGL 本地看板
 * @details Stop LVGL local dashboard
 * @param[in] me LVGL 本地看板句柄； LVGL local dashboard handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 */
esp_err_t lvgl_dashboard_stop(lvgl_dashboard_t *me);

/**
 * @brief 设置屏幕启用状态
 * @details Set screen enabled state
 * @param[in] me LVGL 本地看板句柄； LVGL local dashboard handle
 * @param[in] enabled 是否启用屏幕； Whether to enable the screen
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 获取互斥锁超时； Mutex timeout
 */
esp_err_t lvgl_dashboard_set_screen_enabled(lvgl_dashboard_t *me, bool enabled);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
