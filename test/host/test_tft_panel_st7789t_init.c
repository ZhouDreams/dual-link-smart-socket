#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/task.h"
#include "tft_panel_st7789t.h"

struct esp_lcd_panel_io {
    int marker;
};

typedef struct {
    int command;
    uint8_t data[16];
    size_t data_size;
} host_param_tx_t;

static host_param_tx_t s_param_txs[32];
static size_t s_param_tx_count;

esp_err_t gpio_config(const gpio_config_t *config)
{
    return (config != NULL) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    (void)gpio_num;
    (void)level;
    return ESP_OK;
}

esp_err_t gpio_reset_pin(gpio_num_t gpio_num)
{
    (void)gpio_num;
    return ESP_OK;
}

void vTaskDelay(TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
}

esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io,
                                    int command, const void *params,
                                    size_t param_size)
{
    host_param_tx_t *tx = NULL;

    assert(io != NULL);
    assert(s_param_tx_count < (sizeof(s_param_txs) / sizeof(s_param_txs[0])));
    assert(param_size <= sizeof(s_param_txs[0].data));
    tx = &s_param_txs[s_param_tx_count++];
    tx->command = command;
    tx->data_size = param_size;
    if (param_size > 0U) {
        assert(params != NULL);
        memcpy(tx->data, params, param_size);
    }
    return ESP_OK;
}

esp_err_t esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t io,
                                    int command, const void *color,
                                    size_t color_size)
{
    (void)command;
    (void)color_size;
    return (io != NULL && color != NULL) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

const char *esp_err_to_name(esp_err_t err)
{
    (void)err;
    return "host-error";
}

static void test_init_preserves_bgr_madctl(void)
{
    struct esp_lcd_panel_io io = {0};
    esp_lcd_panel_handle_t panel = NULL;
    const tft_panel_st7789t_config_t config = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_endian = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };

    memset(s_param_txs, 0, sizeof(s_param_txs));
    s_param_tx_count = 0U;

    assert(tft_panel_new_st7789t(&io, &config, &panel) == ESP_OK);
    assert(panel != NULL);
    assert(panel->init(panel) == ESP_OK);
    assert(s_param_tx_count > 1U);
    assert(s_param_txs[0].command == LCD_CMD_SLPOUT);
    assert(s_param_txs[1].command == LCD_CMD_MADCTL);
    assert(s_param_txs[1].data_size == 1U);
    assert(s_param_txs[1].data[0] == LCD_CMD_BGR_BIT);
    assert(panel->del(panel) == ESP_OK);
}

int main(void)
{
    test_init_preserves_bgr_madctl();

    printf("tft panel ST7789T init tests passed\n");
    return 0;
}
