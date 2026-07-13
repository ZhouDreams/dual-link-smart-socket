#pragma once

#include <stddef.h>

#include "esp_err.h"

typedef struct esp_lcd_panel_io *esp_lcd_panel_io_handle_t;

esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io,
                                    int command, const void *params,
                                    size_t param_size);
esp_err_t esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t io,
                                    int command, const void *color,
                                    size_t color_size);
