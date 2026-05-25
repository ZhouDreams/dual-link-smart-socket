/**
 * @file tft_panel.c
 * @brief TFT 面板驱动实现
 * @details TFT panel driver implementation
 * @author OpenCode
 * @date 2026-05-25
 */

/*********************
 *      INCLUDES
 *********************/

#include "tft_panel.h"

#include <stdint.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "tft_panel_st7789t.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "tft_panel"

#define TFT_PANEL_SPI_HOST           SPI3_HOST
#define TFT_PANEL_PIXEL_CLOCK_HZ     (12 * 1000 * 1000)
#define TFT_PANEL_BACKLIGHT_ON_LEVEL (1)
#define TFT_PANEL_X_GAP              (34)
#define TFT_PANEL_Y_GAP              (0)
#define TFT_PANEL_BUFFER_LINES       (20)
#define TFT_PANEL_IO_QUEUE_DEPTH     (10)

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief TFT 面板对象
 * @details TFT panel object
 */
struct tft_panel {
    tft_panel_config_t config;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    SemaphoreHandle_t mutex;
    portMUX_TYPE callback_lock;
    tft_panel_flush_done_cb_t flush_done_cb;
    void *flush_done_ctx;
    bool backlight_on;
    bool initialized;
    bool spi_bus_initialized;
    bool destroying;
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**
 * @brief 校验面板配置
 * @details Validate panel configuration
 * @param[in] config 面板配置； Panel configuration
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t tft_panel_validate_config(const tft_panel_config_t *config);

/**
 * @brief 判断 GPIO 是否可作为输出
 * @details Check whether GPIO can be used as output
 * @param[in] gpio_num GPIO 编号； GPIO number
 * @return true 有效，false 无效； true if valid, false otherwise
 */
static bool tft_panel_is_valid_output_gpio(gpio_num_t gpio_num);

/**
 * @brief 配置背光 GPIO
 * @details Configure backlight GPIO
 * @param[in] me TFT 面板对象； TFT panel object
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t tft_panel_configure_backlight_gpio(const tft_panel_t *me);

/**
 * @brief 驱动背光 GPIO
 * @details Drive backlight GPIO
 * @param[in] me TFT 面板对象； TFT panel object
 * @param[in] enabled 是否打开背光； Whether to enable backlight
 * @return ESP-IDF 错误码； ESP-IDF error code
 */
static esp_err_t tft_panel_drive_backlight(const tft_panel_t *me,
                                           bool enabled);

/**
 * @brief 存储刷新完成回调
 * @details Store flush done callback
 * @param[in,out] me TFT 面板对象； TFT panel object
 * @param[in] cb 刷新完成回调； Flush done callback
 * @param[in] user_ctx 用户上下文； User context
 */
static void tft_panel_store_flush_done_cb(tft_panel_t *me,
                                          tft_panel_flush_done_cb_t cb,
                                          void *user_ctx);

/**
 * @brief 读取刷新完成回调
 * @details Load flush done callback
 * @param[in] me TFT 面板对象； TFT panel object
 * @param[out] out_cb 回调输出； Callback output
 * @param[out] out_ctx 上下文输出； Context output
 */
static void tft_panel_load_flush_done_cb(tft_panel_t *me,
                                         tft_panel_flush_done_cb_t *out_cb,
                                         void **out_ctx);

/**
 * @brief 更新销毁中标志
 * @details Update destroying flag
 * @param[in,out] me TFT 面板对象； TFT panel object
 * @param[in] destroying 是否正在销毁； Whether destroy is in progress
 */
static void tft_panel_set_destroying(tft_panel_t *me, bool destroying);

/**
 * @brief 创建失败时清理资源
 * @details Cleanup resources after create failure
 * @param[in,out] me TFT 面板对象； TFT panel object
 */
static void tft_panel_cleanup_create_failure(tft_panel_t *me);

/**
 * @brief 记录清理错误
 * @details Log cleanup error and preserve the first error
 * @param[in] action 清理动作； Cleanup action
 * @param[in] cleanup_ret 清理返回值； Cleanup return value
 * @param[in,out] ret 首个错误码； First error code
 */
static void tft_panel_record_cleanup_error(const char *action,
                                           esp_err_t cleanup_ret,
                                           esp_err_t *ret);

/**
 * @brief 面板颜色传输完成回调
 * @details Panel color transfer done callback
 * @param[in] panel_io 面板 IO 句柄； Panel IO handle
 * @param[in] edata 事件数据； Event data
 * @param[in] user_ctx 用户上下文； User context
 * @return 是否唤醒高优先级任务； Whether a high-priority task was woken
 */
static bool tft_panel_on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                          esp_lcd_panel_io_event_data_t *edata,
                                          void *user_ctx);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

tft_panel_t *tft_panel_create(const tft_panel_config_t *config)
{
    esp_err_t ret = ESP_OK;

    if (tft_panel_validate_config(config) != ESP_OK) {
        return NULL;
    }

    tft_panel_t *me = calloc(1, sizeof(*me));
    if (me == NULL) {
        ESP_LOGE(TAG, "calloc tft panel failed");
        return NULL;
    }
    portMUX_INITIALIZE(&me->callback_lock);
    me->config = *config;

    me->mutex = xSemaphoreCreateMutex();
    if (me->mutex == NULL) {
        ESP_LOGE(TAG, "create mutex failed");
        tft_panel_cleanup_create_failure(me);
        return NULL;
    }

    ret = tft_panel_configure_backlight_gpio(me);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "configure backlight gpio failed: %s",
                 esp_err_to_name(ret));
        tft_panel_cleanup_create_failure(me);
        return NULL;
    }
    ret = tft_panel_drive_backlight(me, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "default backlight off failed: %s",
                 esp_err_to_name(ret));
        tft_panel_cleanup_create_failure(me);
        return NULL;
    }

    const spi_bus_config_t bus_config = {
        .sclk_io_num = me->config.sclk_gpio,
        .mosi_io_num = me->config.mosi_gpio,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
        .max_transfer_sz = me->config.panel_width * TFT_PANEL_BUFFER_LINES *
                           (int)sizeof(uint16_t),
    };
    ret = spi_bus_initialize(TFT_PANEL_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "initialize spi bus failed: %s", esp_err_to_name(ret));
        tft_panel_cleanup_create_failure(me);
        return NULL;
    }
    me->spi_bus_initialized = true;

    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = me->config.cs_gpio,
        .dc_gpio_num = me->config.dc_gpio,
        .spi_mode = 0,
        .pclk_hz = TFT_PANEL_PIXEL_CLOCK_HZ,
        .trans_queue_depth = TFT_PANEL_IO_QUEUE_DEPTH,
        .on_color_trans_done = tft_panel_on_color_trans_done,
        .user_ctx = me,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags = {
            .sio_mode = 0,
        },
    };
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TFT_PANEL_SPI_HOST,
                                   &io_config, &me->io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "create panel io failed: %s", esp_err_to_name(ret));
        tft_panel_cleanup_create_failure(me);
        return NULL;
    }

    const tft_panel_st7789t_config_t panel_config = {
        .reset_gpio_num = me->config.rst_gpio,
        .rgb_endian = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ret = tft_panel_new_st7789t(me->io, &panel_config, &me->panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "create st7789t panel failed: %s", esp_err_to_name(ret));
        tft_panel_cleanup_create_failure(me);
        return NULL;
    }

    ret = esp_lcd_panel_reset(me->panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "reset panel failed: %s", esp_err_to_name(ret));
        tft_panel_cleanup_create_failure(me);
        return NULL;
    }
    ret = esp_lcd_panel_init(me->panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "init panel failed: %s", esp_err_to_name(ret));
        tft_panel_cleanup_create_failure(me);
        return NULL;
    }
    ret = esp_lcd_panel_mirror(me->panel, true, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mirror panel failed: %s", esp_err_to_name(ret));
        tft_panel_cleanup_create_failure(me);
        return NULL;
    }
    ret = esp_lcd_panel_invert_color(me->panel, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "invert color failed: %s", esp_err_to_name(ret));
        tft_panel_cleanup_create_failure(me);
        return NULL;
    }
    ret = esp_lcd_panel_set_gap(me->panel, TFT_PANEL_X_GAP, TFT_PANEL_Y_GAP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set panel gap failed: %s", esp_err_to_name(ret));
        tft_panel_cleanup_create_failure(me);
        return NULL;
    }
    ret = esp_lcd_panel_disp_on_off(me->panel, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "enable display failed: %s", esp_err_to_name(ret));
        tft_panel_cleanup_create_failure(me);
        return NULL;
    }
    ret = tft_panel_drive_backlight(me, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "enable backlight failed: %s", esp_err_to_name(ret));
        tft_panel_cleanup_create_failure(me);
        return NULL;
    }

    me->backlight_on = true;
    me->initialized = true;
    return me;
}

esp_err_t tft_panel_destroy(tft_panel_t *me)
{
    esp_err_t ret = ESP_OK;
    esp_err_t cleanup_ret = ESP_OK;

    if (me == NULL) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (me->destroying) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    me->destroying = true;
    me->initialized = false;
    (void)xSemaphoreGive(me->mutex);

    cleanup_ret = tft_panel_drive_backlight(me, false);
    tft_panel_record_cleanup_error("disable backlight", cleanup_ret, &ret);
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
        me->backlight_on = false;
        (void)xSemaphoreGive(me->mutex);
    }

    if (me->panel != NULL) {
        cleanup_ret = esp_lcd_panel_disp_on_off(me->panel, false);
        tft_panel_record_cleanup_error("disable display", cleanup_ret, &ret);

        cleanup_ret = esp_lcd_panel_del(me->panel);
        if (cleanup_ret != ESP_OK) {
            tft_panel_record_cleanup_error("delete panel", cleanup_ret, &ret);
            tft_panel_set_destroying(me, false);
            return ret;
        }
        me->panel = NULL;
    }

    if (me->io != NULL) {
        cleanup_ret = esp_lcd_panel_io_del(me->io);
        if (cleanup_ret != ESP_OK) {
            tft_panel_record_cleanup_error("delete panel io", cleanup_ret, &ret);
            tft_panel_set_destroying(me, false);
            return ret;
        }
        me->io = NULL;
        tft_panel_store_flush_done_cb(me, NULL, NULL);
    }

    if (me->spi_bus_initialized) {
        cleanup_ret = spi_bus_free(TFT_PANEL_SPI_HOST);
        if (cleanup_ret != ESP_OK) {
            tft_panel_record_cleanup_error("free spi bus", cleanup_ret, &ret);
            tft_panel_set_destroying(me, false);
            return ret;
        }
        me->spi_bus_initialized = false;
    }

    cleanup_ret = gpio_reset_pin(me->config.bl_gpio);
    tft_panel_record_cleanup_error("reset backlight gpio", cleanup_ret, &ret);

    vSemaphoreDelete(me->mutex);
    me->mutex = NULL;
    free(me);
    return ret;
}

esp_err_t tft_panel_register_flush_done_cb(tft_panel_t *me,
                                           tft_panel_flush_done_cb_t cb,
                                           void *user_ctx)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "panel is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    if (!me->initialized || me->destroying) {
        ret = ESP_ERR_INVALID_STATE;
    } else {
        tft_panel_store_flush_done_cb(me, cb, user_ctx);
    }

    (void)xSemaphoreGive(me->mutex);
    return ret;
}

esp_err_t tft_panel_draw_bitmap(tft_panel_t *me, int x1, int y1, int x2, int y2,
                                const void *color_data)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "panel is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    if (!me->initialized || me->destroying || me->panel == NULL) {
        ret = ESP_ERR_INVALID_STATE;
    } else if (color_data == NULL) {
        ret = ESP_ERR_INVALID_ARG;
    } else if (x1 < 0 || y1 < 0 || x2 <= x1 || y2 <= y1 ||
               x2 > me->config.panel_width || y2 > me->config.panel_height) {
        ret = ESP_ERR_INVALID_ARG;
    } else {
        ret = esp_lcd_panel_draw_bitmap(me->panel, x1, y1, x2, y2,
                                        color_data);
    }

    (void)xSemaphoreGive(me->mutex);
    return ret;
}

esp_err_t tft_panel_set_backlight(tft_panel_t *me, bool enabled)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "panel is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    if (!me->initialized || me->destroying) {
        ret = ESP_ERR_INVALID_STATE;
    } else {
        ret = tft_panel_drive_backlight(me, enabled);
        if (ret == ESP_OK) {
            me->backlight_on = enabled;
        }
    }

    (void)xSemaphoreGive(me->mutex);
    return ret;
}

int tft_panel_get_width(const tft_panel_t *me)
{
    if (me == NULL) {
        return 0;
    }

    return me->config.panel_width;
}

int tft_panel_get_height(const tft_panel_t *me)
{
    if (me == NULL) {
        return 0;
    }

    return me->config.panel_height;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static esp_err_t tft_panel_validate_config(const tft_panel_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "config is null");
    ESP_RETURN_ON_FALSE(tft_panel_is_valid_output_gpio(config->sclk_gpio),
                        ESP_ERR_INVALID_ARG, TAG, "invalid sclk gpio");
    ESP_RETURN_ON_FALSE(tft_panel_is_valid_output_gpio(config->mosi_gpio),
                        ESP_ERR_INVALID_ARG, TAG, "invalid mosi gpio");
    ESP_RETURN_ON_FALSE(tft_panel_is_valid_output_gpio(config->dc_gpio),
                        ESP_ERR_INVALID_ARG, TAG, "invalid dc gpio");
    ESP_RETURN_ON_FALSE(tft_panel_is_valid_output_gpio(config->cs_gpio),
                        ESP_ERR_INVALID_ARG, TAG, "invalid cs gpio");
    ESP_RETURN_ON_FALSE(config->rst_gpio == GPIO_NUM_NC ||
                            tft_panel_is_valid_output_gpio(config->rst_gpio),
                        ESP_ERR_INVALID_ARG, TAG, "invalid reset gpio");
    ESP_RETURN_ON_FALSE(tft_panel_is_valid_output_gpio(config->bl_gpio),
                        ESP_ERR_INVALID_ARG, TAG, "invalid backlight gpio");
    ESP_RETURN_ON_FALSE(config->panel_width > 0 && config->panel_height > 0,
                        ESP_ERR_INVALID_ARG, TAG, "invalid panel size");

    return ESP_OK;
}

static bool tft_panel_is_valid_output_gpio(gpio_num_t gpio_num)
{
    return GPIO_IS_VALID_OUTPUT_GPIO(gpio_num);
}

static esp_err_t tft_panel_configure_backlight_gpio(const tft_panel_t *me)
{
    const gpio_config_t backlight_config = {
        .pin_bit_mask = 1ULL << me->config.bl_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&backlight_config);
}

static esp_err_t tft_panel_drive_backlight(const tft_panel_t *me,
                                           bool enabled)
{
    return gpio_set_level(me->config.bl_gpio,
                          enabled ? TFT_PANEL_BACKLIGHT_ON_LEVEL
                                  : !TFT_PANEL_BACKLIGHT_ON_LEVEL);
}

static void tft_panel_store_flush_done_cb(tft_panel_t *me,
                                          tft_panel_flush_done_cb_t cb,
                                          void *user_ctx)
{
    if (me == NULL) {
        return;
    }

    portENTER_CRITICAL_SAFE(&me->callback_lock);
    me->flush_done_cb = cb;
    me->flush_done_ctx = (cb != NULL) ? user_ctx : NULL;
    portEXIT_CRITICAL_SAFE(&me->callback_lock);
}

static void tft_panel_load_flush_done_cb(tft_panel_t *me,
                                         tft_panel_flush_done_cb_t *out_cb,
                                         void **out_ctx)
{
    if (out_cb != NULL) {
        *out_cb = NULL;
    }
    if (out_ctx != NULL) {
        *out_ctx = NULL;
    }
    if (me == NULL || out_cb == NULL || out_ctx == NULL) {
        return;
    }

    portENTER_CRITICAL_SAFE(&me->callback_lock);
    *out_cb = me->flush_done_cb;
    *out_ctx = me->flush_done_ctx;
    portEXIT_CRITICAL_SAFE(&me->callback_lock);
}

static void tft_panel_set_destroying(tft_panel_t *me, bool destroying)
{
    if (me == NULL || me->mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
        me->destroying = destroying;
        (void)xSemaphoreGive(me->mutex);
    }
}

static void tft_panel_cleanup_create_failure(tft_panel_t *me)
{
    if (me == NULL) {
        return;
    }

    if (me->mutex != NULL) {
        if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
            me->initialized = false;
            (void)xSemaphoreGive(me->mutex);
        }
    }
    tft_panel_store_flush_done_cb(me, NULL, NULL);

    (void)tft_panel_drive_backlight(me, false);

    if (me->panel != NULL) {
        (void)esp_lcd_panel_disp_on_off(me->panel, false);
        (void)esp_lcd_panel_del(me->panel);
        me->panel = NULL;
    }
    if (me->io != NULL) {
        (void)esp_lcd_panel_io_del(me->io);
        me->io = NULL;
    }
    if (me->spi_bus_initialized) {
        (void)spi_bus_free(TFT_PANEL_SPI_HOST);
        me->spi_bus_initialized = false;
    }
    if (tft_panel_is_valid_output_gpio(me->config.bl_gpio)) {
        (void)gpio_reset_pin(me->config.bl_gpio);
    }
    if (me->mutex != NULL) {
        vSemaphoreDelete(me->mutex);
        me->mutex = NULL;
    }
    free(me);
}

static void tft_panel_record_cleanup_error(const char *action,
                                           esp_err_t cleanup_ret,
                                           esp_err_t *ret)
{
    if (cleanup_ret == ESP_OK) {
        return;
    }

    ESP_LOGW(TAG, "%s failed: %s", action, esp_err_to_name(cleanup_ret));
    if (ret != NULL && *ret == ESP_OK) {
        *ret = cleanup_ret;
    }
}

static bool tft_panel_on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                          esp_lcd_panel_io_event_data_t *edata,
                                          void *user_ctx)
{
    (void)panel_io;
    (void)edata;

    tft_panel_t *me = (tft_panel_t *)user_ctx;
    tft_panel_flush_done_cb_t cb = NULL;
    void *ctx = NULL;

    tft_panel_load_flush_done_cb(me, &cb, &ctx);
    if (cb != NULL) {
        cb(ctx);
    }

    return false;
}
