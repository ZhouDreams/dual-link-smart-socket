/**
 * @file button.h
 * @brief 本地按键接口
 * @details Local button interface
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

#include "driver/gpio.h"
#include "esp_err.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 按键有效电平
 * @details Button active level
 */
typedef enum {
    BUTTON_ACTIVE_LOW = 0,   /**< 低电平有效； Active low */
    BUTTON_ACTIVE_HIGH = 1,  /**< 高电平有效； Active high */
} button_active_level_t;

/**
 * @brief 按键事件
 * @details Button event
 */
typedef enum {
    BUTTON_EVENT_SINGLE_CLICK = 0,  /**< 单击； Single click */
    BUTTON_EVENT_DOUBLE_CLICK,      /**< 双击； Double click */
    BUTTON_EVENT_LONG_PRESS_START,  /**< 长按开始； Long press start */
    BUTTON_EVENT_LONG_PRESS_HOLD,   /**< 长按保持； Long press hold */
    BUTTON_EVENT_MAX,               /**< 事件数量； Event count */
} button_event_t;

/**
 * @brief 按键初始化配置
 * @details Button initialization configuration
 */
typedef struct {
    gpio_num_t input_gpio;                 /**< 输入 GPIO； Input GPIO */
    button_active_level_t active_level;    /**< 有效电平； Active level */
} button_config_t;

/**
 * @brief 按键事件回调
 * @details Button event callback
 * @param[in] event 按键事件； Button event
 * @param[in] user_ctx 用户上下文； User context
 */
typedef void (*button_event_cb_t)(button_event_t event, void *user_ctx);

/**
 * @brief 按键句柄
 * @details Button handle
 */
typedef struct button button_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 创建按键实例
 * @details Create button instance
 * @param[in] config 按键配置； Button configuration
 * @return 按键句柄，失败返回 NULL； Button handle, NULL on failure
 */
button_t *button_create(const button_config_t *config);

/**
 * @brief 销毁按键实例
 * @details Destroy button instance
 * @note 调用方不得在按键回调中调用本函数； Caller must not call this function from a button callback.
 * @note 调用方必须在外部串行化 destroy 与 button_register_cb 以及同一句柄的其它访问；
 *       Caller must externally serialize destroy against button_register_cb and any other use of the same handle.
 * @note Espressif button 组件头文件应隔离在内部 button_iot_adapter 后面；
 *       Espressif button component headers should stay isolated behind the internal button_iot_adapter.
 * @param[in] me 按键句柄； Button handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_FAIL: 删除底层按键失败； Underlying button delete failed
 */
esp_err_t button_destroy(button_t *me);

/**
 * @brief 注册按键事件回调
 * @details Register button event callback
 * @param[in] me 按键句柄； Button handle
 * @param[in] event 按键事件； Button event
 * @param[in] cb 回调，NULL 表示清除； Callback, NULL clears the slot
 * @param[in] user_ctx 用户上下文； User context
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 获取互斥锁超时； Mutex timeout
 */
esp_err_t button_register_cb(button_t *me, button_event_t event,
                             button_event_cb_t cb, void *user_ctx);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
