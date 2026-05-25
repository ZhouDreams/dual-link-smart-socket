/**
 * @file tft_panel.h
 * @brief TFT 面板驱动接口
 * @details TFT panel driver interface
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

#include "driver/gpio.h"
#include "esp_err.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief TFT 面板句柄
 * @details TFT panel opaque handle
 */
typedef struct tft_panel tft_panel_t;

/**
 * @brief TFT 面板初始化配置
 * @details TFT panel initialization configuration
 */
typedef struct {
    gpio_num_t sclk_gpio;   /**< SPI 时钟引脚； SPI clock GPIO */
    gpio_num_t mosi_gpio;   /**< SPI MOSI 引脚； SPI MOSI GPIO */
    gpio_num_t dc_gpio;     /**< 数据命令选择引脚； Data/command GPIO */
    gpio_num_t cs_gpio;     /**< SPI 片选引脚； SPI chip-select GPIO */
    gpio_num_t rst_gpio;    /**< 复位引脚，可为 GPIO_NUM_NC； Reset GPIO, may be GPIO_NUM_NC */
    gpio_num_t bl_gpio;     /**< 背光控制引脚； Backlight control GPIO */
    int panel_width;        /**< 面板宽度像素； Panel width in pixels */
    int panel_height;       /**< 面板高度像素； Panel height in pixels */
} tft_panel_config_t;

/**
 * @brief TFT 刷新完成回调
 * @details TFT flush done callback
 * @param[in] user_ctx 用户上下文； User context
 */
typedef void (*tft_panel_flush_done_cb_t)(void *user_ctx);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 创建 TFT 面板实例
 * @details Create TFT panel instance
 * @param[in] config 初始化配置； Initialization configuration
 * @return TFT 面板句柄，失败返回 NULL； TFT panel handle, NULL on failure
 */
tft_panel_t *tft_panel_create(const tft_panel_config_t *config);

/**
 * @brief 销毁 TFT 面板实例
 * @details Destroy TFT panel instance
 * @param[in] me TFT 面板句柄，可为 NULL； TFT panel handle, may be NULL
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_STATE: 状态无效或资源释放失败； Invalid state or cleanup failure
 */
esp_err_t tft_panel_destroy(tft_panel_t *me);

/**
 * @brief 注册刷新完成回调
 * @details Register flush done callback
 * @param[in] me TFT 面板句柄； TFT panel handle
 * @param[in] cb 刷新完成回调，NULL 表示清除； Flush done callback, NULL clears callback
 * @param[in] user_ctx 用户上下文； User context
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 获取互斥锁超时； Mutex timeout
 */
esp_err_t tft_panel_register_flush_done_cb(tft_panel_t *me,
                                           tft_panel_flush_done_cb_t cb,
                                           void *user_ctx);

/**
 * @brief 向面板写入位图区域
 * @details Draw bitmap region to panel
 * @param[in] me TFT 面板句柄； TFT panel handle
 * @param[in] x1 起始列，包含； Inclusive start x coordinate
 * @param[in] y1 起始行，包含； Inclusive start y coordinate
 * @param[in] x2 结束列，不包含； Exclusive end x coordinate
 * @param[in] y2 结束行，不包含； Exclusive end y coordinate
 * @param[in] color_data RGB565 像素数据； RGB565 pixel data
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 获取互斥锁超时； Mutex timeout
 */
esp_err_t tft_panel_draw_bitmap(tft_panel_t *me, int x1, int y1, int x2, int y2,
                                const void *color_data);

/**
 * @brief 设置背光开关
 * @details Set backlight on/off state
 * @param[in] me TFT 面板句柄； TFT panel handle
 * @param[in] enabled 是否打开背光； Whether to enable backlight
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 获取互斥锁超时； Mutex timeout
 */
esp_err_t tft_panel_set_backlight(tft_panel_t *me, bool enabled);

/**
 * @brief 获取面板宽度
 * @details Get panel width
 * @param[in] me TFT 面板句柄，可为 NULL； TFT panel handle, may be NULL
 * @return 面板宽度像素，NULL 返回 0； Panel width in pixels, 0 for NULL
 */
int tft_panel_get_width(const tft_panel_t *me);

/**
 * @brief 获取面板高度
 * @details Get panel height
 * @param[in] me TFT 面板句柄，可为 NULL； TFT panel handle, may be NULL
 * @return 面板高度像素，NULL 返回 0； Panel height in pixels, 0 for NULL
 */
int tft_panel_get_height(const tft_panel_t *me);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
