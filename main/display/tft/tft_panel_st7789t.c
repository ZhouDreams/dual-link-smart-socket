/**
 * @file tft_panel_st7789t.c
 * @brief ST7789T 自定义面板驱动实现
 * @details Custom ST7789T panel driver implementation
 * @author OpenCode
 * @date 2026-05-25
 */

/*********************
 *      INCLUDES
 *********************/

#include "tft_panel_st7789t.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "lcd_panel.st7789t"

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief ST7789T 面板运行时对象
 * @details ST7789T panel runtime object
 */
typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    bool reset_level;
    int x_gap;
    int y_gap;
    uint8_t fb_bits_per_pixel;
    uint8_t madctl_val;
    uint8_t colmod_val;
} tft_panel_st7789t_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**
 * @brief 从基类指针获取 ST7789T 对象
 * @details Get ST7789T object from base panel pointer
 * @param[in] panel 面板基类指针； Base panel pointer
 * @return ST7789T 对象指针； ST7789T object pointer
 */
static tft_panel_st7789t_t *tft_panel_st7789t_from_base(
    esp_lcd_panel_t *panel);

/**
 * @brief 删除面板实例
 * @details Delete panel instance
 * @param[in] panel 面板基类指针； Base panel pointer
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t tft_panel_st7789t_del(esp_lcd_panel_t *panel);

/**
 * @brief 复位面板
 * @details Reset panel
 * @param[in] panel 面板基类指针； Base panel pointer
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t tft_panel_st7789t_reset(esp_lcd_panel_t *panel);

/**
 * @brief 初始化面板寄存器
 * @details Initialize panel registers
 * @param[in] panel 面板基类指针； Base panel pointer
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t tft_panel_st7789t_init(esp_lcd_panel_t *panel);

/**
 * @brief 绘制位图到面板
 * @details Draw bitmap to panel
 * @param[in] panel 面板基类指针； Base panel pointer
 * @param[in] x_start 起始列，包含； Inclusive start x coordinate
 * @param[in] y_start 起始行，包含； Inclusive start y coordinate
 * @param[in] x_end 结束列，不包含； Exclusive end x coordinate
 * @param[in] y_end 结束行，不包含； Exclusive end y coordinate
 * @param[in] color_data 像素数据； Pixel data
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t tft_panel_st7789t_draw_bitmap(esp_lcd_panel_t *panel,
                                               int x_start,
                                               int y_start,
                                               int x_end,
                                               int y_end,
                                               const void *color_data);

/**
 * @brief 设置颜色反转
 * @details Set color inversion
 * @param[in] panel 面板基类指针； Base panel pointer
 * @param[in] invert_color_data 是否反转颜色； Whether to invert colors
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t tft_panel_st7789t_invert_color(esp_lcd_panel_t *panel,
                                                bool invert_color_data);

/**
 * @brief 设置镜像
 * @details Set panel mirroring
 * @param[in] panel 面板基类指针； Base panel pointer
 * @param[in] mirror_x 是否 X 轴镜像； Whether to mirror X axis
 * @param[in] mirror_y 是否 Y 轴镜像； Whether to mirror Y axis
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t tft_panel_st7789t_mirror(esp_lcd_panel_t *panel,
                                          bool mirror_x, bool mirror_y);

/**
 * @brief 设置 XY 交换
 * @details Set XY swap
 * @param[in] panel 面板基类指针； Base panel pointer
 * @param[in] swap_axes 是否交换 XY 轴； Whether to swap axes
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t tft_panel_st7789t_swap_xy(esp_lcd_panel_t *panel,
                                           bool swap_axes);

/**
 * @brief 设置绘制偏移
 * @details Set draw gap offsets
 * @param[in] panel 面板基类指针； Base panel pointer
 * @param[in] x_gap X 轴偏移； X-axis gap
 * @param[in] y_gap Y 轴偏移； Y-axis gap
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t tft_panel_st7789t_set_gap(esp_lcd_panel_t *panel,
                                           int x_gap, int y_gap);

/**
 * @brief 设置显示开关
 * @details Turn display output on or off
 * @param[in] panel 面板基类指针； Base panel pointer
 * @param[in] on_off 是否打开显示； Whether to turn display on
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t tft_panel_st7789t_disp_on_off(esp_lcd_panel_t *panel,
                                               bool on_off);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

esp_err_t tft_panel_new_st7789t(const esp_lcd_panel_io_handle_t io,
                                const tft_panel_st7789t_config_t *panel_dev_config,
                                esp_lcd_panel_handle_t *ret_panel)
{
    esp_err_t ret = ESP_OK;
    tft_panel_st7789t_t *panel = NULL;

    ESP_GOTO_ON_FALSE(io != NULL && panel_dev_config != NULL && ret_panel != NULL,
                      ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    *ret_panel = NULL;
    ESP_GOTO_ON_FALSE(panel_dev_config->reset_gpio_num == GPIO_NUM_NC ||
                          GPIO_IS_VALID_OUTPUT_GPIO(panel_dev_config->reset_gpio_num),
                      ESP_ERR_INVALID_ARG, err, TAG, "invalid reset gpio");

    panel = calloc(1, sizeof(*panel));
    ESP_GOTO_ON_FALSE(panel != NULL, ESP_ERR_NO_MEM, err, TAG,
                      "no memory for st7789t panel");

    if (panel_dev_config->reset_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG,
                          "configure reset gpio failed");
    }

    switch (panel_dev_config->rgb_endian) {
        case LCD_RGB_ELEMENT_ORDER_RGB:
            panel->madctl_val = 0x00;
            break;
        case LCD_RGB_ELEMENT_ORDER_BGR:
            panel->madctl_val = LCD_CMD_BGR_BIT;
            break;
        default:
            ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG,
                              "unsupported rgb endian");
            break;
    }

    switch (panel_dev_config->bits_per_pixel) {
        case 16:
            panel->colmod_val = 0x55;
            panel->fb_bits_per_pixel = 16;
            break;
        case 18:
            panel->colmod_val = 0x66;
            panel->fb_bits_per_pixel = 24;
            break;
        default:
            ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG,
                              "unsupported pixel width");
            break;
    }

    panel->io = io;
    panel->reset_gpio_num = panel_dev_config->reset_gpio_num;
    panel->reset_level = panel_dev_config->flags.reset_active_high;
    panel->base.del = tft_panel_st7789t_del;
    panel->base.reset = tft_panel_st7789t_reset;
    panel->base.init = tft_panel_st7789t_init;
    panel->base.draw_bitmap = tft_panel_st7789t_draw_bitmap;
    panel->base.invert_color = tft_panel_st7789t_invert_color;
    panel->base.mirror = tft_panel_st7789t_mirror;
    panel->base.swap_xy = tft_panel_st7789t_swap_xy;
    panel->base.set_gap = tft_panel_st7789t_set_gap;
    panel->base.disp_on_off = tft_panel_st7789t_disp_on_off;
    *ret_panel = &panel->base;

    return ESP_OK;

err:
    if (panel != NULL) {
        if (panel_dev_config != NULL && panel_dev_config->reset_gpio_num != GPIO_NUM_NC &&
            GPIO_IS_VALID_GPIO(panel_dev_config->reset_gpio_num)) {
            (void)gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(panel);
    }
    return ret;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static tft_panel_st7789t_t *tft_panel_st7789t_from_base(
    esp_lcd_panel_t *panel)
{
    if (panel == NULL) {
        return NULL;
    }

    return (tft_panel_st7789t_t *)((char *)panel -
                                  offsetof(tft_panel_st7789t_t, base));
}

static esp_err_t tft_panel_st7789t_del(esp_lcd_panel_t *panel)
{
    tft_panel_st7789t_t *st7789t = tft_panel_st7789t_from_base(panel);

    ESP_RETURN_ON_FALSE(st7789t != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "panel is null");

    if (st7789t->reset_gpio_num != GPIO_NUM_NC) {
        (void)gpio_reset_pin(st7789t->reset_gpio_num);
    }

    free(st7789t);
    return ESP_OK;
}

static esp_err_t tft_panel_st7789t_reset(esp_lcd_panel_t *panel)
{
    tft_panel_st7789t_t *st7789t = tft_panel_st7789t_from_base(panel);

    ESP_RETURN_ON_FALSE(st7789t != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "panel is null");

    if (st7789t->reset_gpio_num != GPIO_NUM_NC) {
        ESP_RETURN_ON_ERROR(gpio_set_level(st7789t->reset_gpio_num,
                                           st7789t->reset_level),
                            TAG, "set reset active level failed");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(gpio_set_level(st7789t->reset_gpio_num,
                                           !st7789t->reset_level),
                            TAG, "set reset inactive level failed");
        vTaskDelay(pdMS_TO_TICKS(10));
    } else {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io,
                                                      LCD_CMD_SWRESET,
                                                      NULL, 0),
                            TAG, "software reset failed");
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return ESP_OK;
}

static esp_err_t tft_panel_st7789t_init(esp_lcd_panel_t *panel)
{
    tft_panel_st7789t_t *st7789t = tft_panel_st7789t_from_base(panel);

    ESP_RETURN_ON_FALSE(st7789t != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "panel is null");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, LCD_CMD_SLPOUT,
                                                  NULL, 0),
                        TAG, "sleep out failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(
                            st7789t->io, LCD_CMD_MADCTL,
                            (uint8_t[]){st7789t->madctl_val}, 1),
                         TAG, "madctl init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, LCD_CMD_COLMOD,
                                                   (uint8_t[]){st7789t->colmod_val}, 1),
                         TAG, "colmod init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, 0xB0,
                                                  (uint8_t[]){0x00, 0xE8}, 2),
                        TAG, "ramctrl init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, 0xB2,
                                                  (uint8_t[]){0x0C, 0x0C, 0x00,
                                                              0x33, 0x33},
                                                  5),
                        TAG, "B2 init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, 0xB7,
                                                  (uint8_t[]){0x75}, 1),
                        TAG, "B7 init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, 0xBB,
                                                  (uint8_t[]){0x1A}, 1),
                        TAG, "BB init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, 0xC0,
                                                  (uint8_t[]){0x80}, 1),
                        TAG, "C0 init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, 0xC2,
                                                  (uint8_t[]){0x01, 0xFF}, 2),
                        TAG, "C2 init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, 0xC3,
                                                  (uint8_t[]){0x13}, 1),
                        TAG, "C3 init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, 0xC4,
                                                  (uint8_t[]){0x20}, 1),
                        TAG, "C4 init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, 0xC6,
                                                  (uint8_t[]){0x0F}, 1),
                        TAG, "C6 init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, 0xD0,
                                                  (uint8_t[]){0xA4}, 1),
                        TAG, "D0 init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, 0xE0,
                                                  (uint8_t[]){0xD0, 0x0D, 0x14,
                                                              0x0D, 0x0D, 0x09,
                                                              0x38, 0x44, 0x4E,
                                                              0x3A, 0x17, 0x18,
                                                              0x2F, 0x30},
                                                  14),
                        TAG, "E0 init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, 0xE1,
                                                  (uint8_t[]){0xD0, 0x09, 0x0F,
                                                              0x08, 0x07, 0x14,
                                                              0x37, 0x44, 0x4D,
                                                              0x38, 0x15, 0x16,
                                                              0x2C, 0x2E},
                                                  14),
                        TAG, "E1 init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, 0x21, NULL, 0),
                        TAG, "invert on init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, 0x29, NULL, 0),
                        TAG, "display on init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, 0x2C, NULL, 0),
                        TAG, "ram write init failed");

    return ESP_OK;
}

static esp_err_t tft_panel_st7789t_draw_bitmap(esp_lcd_panel_t *panel,
                                               int x_start,
                                               int y_start,
                                               int x_end,
                                               int y_end,
                                               const void *color_data)
{
    tft_panel_st7789t_t *st7789t = tft_panel_st7789t_from_base(panel);
    size_t len = 0;

    ESP_RETURN_ON_FALSE(st7789t != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "panel is null");
    ESP_RETURN_ON_FALSE(color_data != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "color data is null");
    ESP_RETURN_ON_FALSE((x_start < x_end) && (y_start < y_end),
                        ESP_ERR_INVALID_ARG, TAG, "invalid draw area");

    x_start += st7789t->x_gap;
    x_end += st7789t->x_gap;
    y_start += st7789t->y_gap;
    y_end += st7789t->y_gap;

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, LCD_CMD_CASET,
                                                  (uint8_t[]){
                                                      (x_start >> 8) & 0xFF,
                                                      x_start & 0xFF,
                                                      ((x_end - 1) >> 8) & 0xFF,
                                                      (x_end - 1) & 0xFF,
                                                  },
                                                  4),
                        TAG, "set column range failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, LCD_CMD_RASET,
                                                  (uint8_t[]){
                                                      (y_start >> 8) & 0xFF,
                                                      y_start & 0xFF,
                                                      ((y_end - 1) >> 8) & 0xFF,
                                                      (y_end - 1) & 0xFF,
                                                  },
                                                  4),
                        TAG, "set row range failed");

    len = (size_t)(x_end - x_start) * (size_t)(y_end - y_start) *
          st7789t->fb_bits_per_pixel / 8U;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(st7789t->io, LCD_CMD_RAMWR,
                                                  color_data, len),
                        TAG, "write color buffer failed");

    return ESP_OK;
}

static esp_err_t tft_panel_st7789t_invert_color(esp_lcd_panel_t *panel,
                                                bool invert_color_data)
{
    tft_panel_st7789t_t *st7789t = tft_panel_st7789t_from_base(panel);
    const int command = invert_color_data ? LCD_CMD_INVON : LCD_CMD_INVOFF;

    ESP_RETURN_ON_FALSE(st7789t != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "panel is null");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, command,
                                                  NULL, 0),
                        TAG, "invert color failed");
    return ESP_OK;
}

static esp_err_t tft_panel_st7789t_mirror(esp_lcd_panel_t *panel,
                                          bool mirror_x, bool mirror_y)
{
    tft_panel_st7789t_t *st7789t = tft_panel_st7789t_from_base(panel);

    ESP_RETURN_ON_FALSE(st7789t != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "panel is null");

    if (mirror_x) {
        st7789t->madctl_val |= LCD_CMD_MX_BIT;
    } else {
        st7789t->madctl_val &= ~LCD_CMD_MX_BIT;
    }
    if (mirror_y) {
        st7789t->madctl_val |= LCD_CMD_MY_BIT;
    } else {
        st7789t->madctl_val &= ~LCD_CMD_MY_BIT;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, LCD_CMD_MADCTL,
                                                  (uint8_t[]){st7789t->madctl_val}, 1),
                        TAG, "mirror command failed");
    return ESP_OK;
}

static esp_err_t tft_panel_st7789t_swap_xy(esp_lcd_panel_t *panel,
                                           bool swap_axes)
{
    tft_panel_st7789t_t *st7789t = tft_panel_st7789t_from_base(panel);

    ESP_RETURN_ON_FALSE(st7789t != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "panel is null");

    if (swap_axes) {
        st7789t->madctl_val |= LCD_CMD_MV_BIT;
    } else {
        st7789t->madctl_val &= ~LCD_CMD_MV_BIT;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, LCD_CMD_MADCTL,
                                                  (uint8_t[]){st7789t->madctl_val}, 1),
                        TAG, "swap xy command failed");
    return ESP_OK;
}

static esp_err_t tft_panel_st7789t_set_gap(esp_lcd_panel_t *panel,
                                           int x_gap, int y_gap)
{
    tft_panel_st7789t_t *st7789t = tft_panel_st7789t_from_base(panel);

    ESP_RETURN_ON_FALSE(st7789t != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "panel is null");

    st7789t->x_gap = x_gap;
    st7789t->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t tft_panel_st7789t_disp_on_off(esp_lcd_panel_t *panel,
                                               bool on_off)
{
    tft_panel_st7789t_t *st7789t = tft_panel_st7789t_from_base(panel);
    const int command = on_off ? LCD_CMD_DISPON : LCD_CMD_DISPOFF;

    ESP_RETURN_ON_FALSE(st7789t != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "panel is null");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, command,
                                                  NULL, 0),
                        TAG, "display on/off command failed");
    return ESP_OK;
}
