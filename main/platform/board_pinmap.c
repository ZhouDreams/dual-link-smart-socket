/**
 * @file board_pinmap.c
 * @brief 板级引脚映射
 * @details Board pin mapping
 * @author OpenCode
 * @date 2026-05-24
 */

/*********************
 *      INCLUDES
 *********************/

#include "board_pinmap.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

static const board_pinmap_t s_pinmap = {
    .button_gpio = GPIO_NUM_2,
    .button_active_level = BOARD_ACTIVE_LOW,
    .relay_ctrl_gpio = GPIO_NUM_4,
    .relay_active_level = BOARD_ACTIVE_HIGH,
    .bl0942_en_gpio = GPIO_NUM_8,
    .bl0942_tx_gpio = GPIO_NUM_10,
    .bl0942_rx_gpio = GPIO_NUM_11,
    .lte_en_gpio = GPIO_NUM_5,
    .lte_tx_gpio = GPIO_NUM_6,
    .lte_rx_gpio = GPIO_NUM_7,
    .tft_sclk_gpio = GPIO_NUM_40,
    .tft_mosi_gpio = GPIO_NUM_45,
    .tft_dc_gpio = GPIO_NUM_41,
    .tft_cs_gpio = GPIO_NUM_42,
    .tft_rst_gpio = GPIO_NUM_39,
    .tft_bl_gpio = GPIO_NUM_46,
};

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

const board_pinmap_t *board_pinmap_get(void)
{
    return &s_pinmap;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
