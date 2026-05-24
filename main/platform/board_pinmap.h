/**
 * @file board_pinmap.h
 * @brief 板级引脚映射
 * @details Board pin mapping
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

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 板级有效电平
 * @details Board active level
 */
typedef enum {
    BOARD_ACTIVE_LOW = 0,   /**< 低电平有效； Active low */
    BOARD_ACTIVE_HIGH = 1,  /**< 高电平有效； Active high */
} board_active_level_t;

/**
 * @brief 板级引脚映射
 * @details Board pin mapping
 */
typedef struct {
    gpio_num_t button_gpio;                         /**< 按键输入 GPIO； Button input GPIO */
    board_active_level_t button_active_level;       /**< 按键有效电平； Button active level */
    gpio_num_t relay_ctrl_gpio;                     /**< 继电器控制 GPIO； Relay control GPIO */
    board_active_level_t relay_active_level;        /**< 继电器有效电平； Relay active level */
    gpio_num_t bl0942_en_gpio;                      /**< BL0942 使能 GPIO； BL0942 enable GPIO */
    gpio_num_t bl0942_tx_gpio;                      /**< BL0942 UART TX GPIO； BL0942 UART TX GPIO */
    gpio_num_t bl0942_rx_gpio;                      /**< BL0942 UART RX GPIO； BL0942 UART RX GPIO */
    gpio_num_t lte_en_gpio;                         /**< LTE 使能 GPIO； LTE enable GPIO */
    gpio_num_t lte_tx_gpio;                         /**< LTE UART TX GPIO； LTE UART TX GPIO */
    gpio_num_t lte_rx_gpio;                         /**< LTE UART RX GPIO； LTE UART RX GPIO */
    gpio_num_t tft_sclk_gpio;                       /**< TFT SPI SCLK GPIO； TFT SPI SCLK GPIO */
    gpio_num_t tft_mosi_gpio;                       /**< TFT SPI MOSI GPIO； TFT SPI MOSI GPIO */
    gpio_num_t tft_dc_gpio;                         /**< TFT 数据命令 GPIO； TFT data/command GPIO */
    gpio_num_t tft_cs_gpio;                         /**< TFT 片选 GPIO； TFT chip-select GPIO */
    gpio_num_t tft_rst_gpio;                        /**< TFT 复位 GPIO； TFT reset GPIO */
    gpio_num_t tft_bl_gpio;                         /**< TFT 背光 GPIO； TFT backlight GPIO */
} board_pinmap_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 获取只读板级引脚映射
 * @details Get read-only board pin mapping
 * @return 板级引脚映射只读指针； Read-only board pin mapping pointer
 */
const board_pinmap_t *board_pinmap_get(void);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
