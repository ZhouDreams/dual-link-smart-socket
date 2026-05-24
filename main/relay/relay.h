/**
 * @file relay.h
 * @brief 继电器控制接口
 * @details Relay control interface
 * @author OpenCode
 * @date 2026-05-24
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 继电器有效电平
 * @details Relay active level
 */
typedef enum {
    RELAY_ACTIVE_LOW = 0,   /**< 低电平有效； Active low */
    RELAY_ACTIVE_HIGH = 1,  /**< 高电平有效； Active high */
} relay_active_level_t;

/**
 * @brief 继电器操作来源
 * @details Relay operation source
 */
typedef enum {
    RELAY_SOURCE_INTERNAL = 0,  /**< 内部来源； Internal source */
    RELAY_SOURCE_LOCAL_BUTTON,  /**< 本地按键来源； Local button source */
    RELAY_SOURCE_CLOUD,         /**< 云端来源； Cloud source */
    RELAY_SOURCE_SAFETY,        /**< 安全保护来源； Safety source */
    RELAY_SOURCE_MAX,           /**< 来源数量； Source count */
} relay_source_t;

/**
 * @brief 继电器初始化配置
 * @details Relay initialization configuration
 */
typedef struct {
    gpio_num_t ctrl_gpio;                 /**< 继电器控制 GPIO； Relay control GPIO */
    relay_active_level_t active_level;    /**< 继电器有效电平； Relay active level */
} relay_config_t;

/**
 * @brief 继电器状态变化事件载荷
 * @details Relay state changed event payload
 */
typedef struct {
    bool on;                  /**< 继电器是否打开； Whether relay is on */
    relay_source_t source;    /**< 状态变化来源； State change source */
} relay_state_changed_event_t;

/**
 * @brief 继电器句柄
 * @details Relay handle
 */
typedef struct relay relay_t;

ESP_EVENT_DECLARE_BASE(RELAY_EVENT_BASE);

/**
 * @brief 继电器事件 ID
 * @details Relay event ID
 */
typedef enum {
    RELAY_EVENT_STATE_CHANGED = 0,  /**< 状态已变化； State changed */
} relay_event_id_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 创建继电器实例
 * @details Create relay instance
 * @param[in] config 继电器配置； Relay configuration
 * @return 继电器句柄，失败返回 NULL； Relay handle, NULL on failure
 */
relay_t *relay_create(const relay_config_t *config);

/**
 * @brief 销毁继电器实例
 * @details Destroy relay instance
 * @param[in] me 继电器句柄； Relay handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - 其他: 关闭 GPIO 失败； GPIO off failed
 */
esp_err_t relay_destroy(relay_t *me);

/**
 * @brief 设置继电器状态
 * @details Set relay state
 * @param[in] me 继电器句柄； Relay handle
 * @param[in] source 操作来源； Operation source
 * @param[in] on 是否打开； Whether to turn on
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 获取互斥锁超时； Mutex timeout
 */
esp_err_t relay_set(relay_t *me, relay_source_t source, bool on);

/**
 * @brief 切换继电器状态
 * @details Toggle relay state
 * @param[in] me 继电器句柄； Relay handle
 * @param[in] source 操作来源； Operation source
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 获取互斥锁超时； Mutex timeout
 */
esp_err_t relay_toggle(relay_t *me, relay_source_t source);

/**
 * @brief 获取继电器状态
 * @details Get relay state
 * @param[in] me 继电器句柄； Relay handle
 * @param[out] out_on 状态输出； State output
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 获取互斥锁超时； Mutex timeout
 */
esp_err_t relay_get(const relay_t *me, bool *out_on);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
