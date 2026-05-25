/**
 * @file tft_panel_st7789t.h
 * @brief ST7789T 自定义面板驱动接口
 * @details Custom ST7789T panel driver interface
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

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief ST7789T 面板设备配置
 * @details ST7789T panel device configuration
 */
typedef struct {
    int reset_gpio_num;                    /**< 复位引脚； Reset GPIO */
    lcd_rgb_element_order_t rgb_endian;    /**< RGB/BGR 顺序； RGB element order */
    unsigned int bits_per_pixel;           /**< 像素位宽； Bits per pixel */
    struct {
        unsigned int reset_active_high : 1; /**< 复位是否高有效； Reset active high */
    } flags;
} tft_panel_st7789t_config_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 创建 ST7789T 面板实例
 * @details Create ST7789T panel instance
 * @param[in] io 面板 IO 句柄； Panel IO handle
 * @param[in] panel_dev_config 面板设备配置； Panel device configuration
 * @param[out] ret_panel 输出面板句柄； Output panel handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_NO_MEM: 内存不足； Out of memory
 *         - ESP_ERR_NOT_SUPPORTED: 配置不支持； Unsupported configuration
 */
esp_err_t tft_panel_new_st7789t(const esp_lcd_panel_io_handle_t io,
                                const tft_panel_st7789t_config_t *panel_dev_config,
                                esp_lcd_panel_handle_t *ret_panel);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
