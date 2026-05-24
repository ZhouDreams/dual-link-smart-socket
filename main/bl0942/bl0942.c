/**
 * @file bl0942.c
 * @brief BL0942 电能计量实现
 * @details BL0942 power metering implementation
 * @author OpenCode
 * @date 2026-05-24
 */

/*********************
 *      INCLUDES
 *********************/

#include "bl0942.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "bl0942"

#define BL0942_DEFAULT_BAUD_RATE              (9600)
#define BL0942_DEFAULT_RX_BUF_SIZE            (256)
#define BL0942_DEFAULT_READ_TIMEOUT_MS        (500)
#define BL0942_DEFAULT_SAMPLE_PERIOD_MS       (100)
#define BL0942_DEFAULT_FAULT_THRESHOLD        (10)
#define BL0942_DEFAULT_HARD_RESET_ATTEMPTS    (3)

#define BL0942_FRAME_SIZE              (23)
#define BL0942_FRAME_HEADER            (0x55U)
#define BL0942_PACKET_READ_ADDR        (0xAAU)
#define BL0942_MAX_DEVICE_ADDRESS      (3U)
#define BL0942_EN_LOW_DELAY_MS         (1000)
#define BL0942_EN_SETTLE_DELAY_MS      (1000)
#define BL0942_SAMPLE_TASK_NAME        "bl0942_sample"
#define BL0942_SAMPLE_TASK_STACK       (4096)
#define BL0942_SAMPLE_TASK_PRIORITY    (5)

#define BL0942_UART_TX_BUF_SIZE        (0)
#define BL0942_EVENT_POST_TIMEOUT_MS   (10)
#define BL0942_MIN_RX_BUF_SIZE         (128)
#define BL0942_MIN_TIMEOUT_MS          (10)
#define BL0942_STOP_EXTRA_TIMEOUT_MS   (3500)

/**********************
 *      TYPEDEFS
 **********************/

struct bl0942 {
    bl0942_config_t config;
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t lifecycle_mutex;
    bl0942_measurement_t latest;
    bool has_latest;
    TaskHandle_t sample_task;
    SemaphoreHandle_t sample_task_done_sema;
    int active_ops;
    SemaphoreHandle_t active_ops_done_sema;
    volatile bool sample_task_running;
    bool stop_in_progress;
    uint32_t fault_cycles;
    int consecutive_failures;
    int hard_reset_count;
    bool destroying;
    bool initialized;
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**
 * @brief 应用默认配置
 * @details Apply default configuration
 * @param[in] config 输入配置； Input configuration
 * @param[out] out 输出配置； Output configuration
 */
static void bl0942_apply_defaults(const bl0942_config_t *config,
                                  bl0942_config_t *out);

/**
 * @brief 校验 BL0942 配置
 * @details Validate BL0942 configuration
 * @param[in] config 已应用默认值的配置； Configuration with defaults applied
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 */
static esp_err_t bl0942_validate_config(const bl0942_config_t *config);

/**
 * @brief 判断波特率是否受支持
 * @details Check whether baud rate is supported
 * @param[in] baud_rate 波特率； Baud rate
 * @return true 表示支持； true if supported
 */
static bool bl0942_is_supported_baud_rate(int baud_rate);

/**
 * @brief 构建读取命令字节
 * @details Build read command byte
 * @param[in] device_address 器件地址； Device address
 * @return 命令字节； Command byte
 */
static uint8_t bl0942_build_read_cmd(uint8_t device_address);

/**
 * @brief 构建整包读取命令
 * @details Build packet read command
 * @param[in] device_address 器件地址； Device address
 * @param[out] out_cmd 两字节命令输出； Two-byte command output
 */
static void bl0942_build_packet_read_cmd(uint8_t device_address,
                                         uint8_t out_cmd[2]);

/**
 * @brief 计算整包校验和
 * @details Calculate packet checksum
 * @param[in] cmd 命令字节； Command byte
 * @param[in] frame 响应帧； Response frame
 * @return 校验和； Checksum
 */
static uint8_t bl0942_calculate_packet_checksum(
    uint8_t cmd, const uint8_t frame[BL0942_FRAME_SIZE]);

/**
 * @brief 解码 24 位无符号小端数
 * @details Decode unsigned 24-bit little-endian value
 * @param[in] data 数据指针； Data pointer
 * @return 解码结果； Decoded value
 */
static uint32_t bl0942_decode_u24_le(const uint8_t *data);

/**
 * @brief 解码 24 位有符号小端数
 * @details Decode signed 24-bit little-endian value
 * @param[in] data 数据指针； Data pointer
 * @return 解码结果； Decoded value
 */
static int32_t bl0942_decode_s24_le(const uint8_t *data);

/**
 * @brief 解析测量帧
 * @details Parse measurement frame
 * @param[in] frame 响应帧； Response frame
 * @param[out] out 测量结果输出； Measurement output
 */
static void bl0942_parse_packet(const uint8_t frame[BL0942_FRAME_SIZE],
                                bl0942_measurement_t *out);

/**
 * @brief 发布测量事件
 * @details Post measurement event
 * @param[in] measurement 测量结果； Measurement
 */
static void bl0942_post_measurement(
    const bl0942_measurement_t *measurement);

/**
 * @brief 发布故障事件
 * @details Post fault event
 * @param[in] me BL0942 句柄； BL0942 handle
 * @param[in] last_error 最近错误； Most recent error
 * @param[in] hard_reset_attempted 是否尝试硬复位； Whether hard reset was attempted
 */
static void bl0942_post_fault(const bl0942_t *me, esp_err_t last_error,
                              bool hard_reset_attempted);

/**
 * @brief 通过 EN 引脚上下电
 * @details Power-cycle through EN pin
 * @param[in] en_gpio EN GPIO； EN GPIO
 * @return
 *         - ESP_OK: 成功或跳过； Success or skipped
 *         - 其他: GPIO 错误； GPIO error
 */
static esp_err_t bl0942_power_cycle(gpio_num_t en_gpio);

/**
 * @brief 安装 UART 驱动
 * @details Install UART driver
 * @param[in] config BL0942 配置； BL0942 configuration
 * @return
 *         - ESP_OK: 成功； Success
 *         - 其他: UART 错误； UART error
 */
static esp_err_t bl0942_uart_install(const bl0942_config_t *config);

/**
 * @brief 配置 UART 参数和引脚
 * @details Configure UART parameters and pins
 * @param[in] config BL0942 配置； BL0942 configuration
 * @return
 *         - ESP_OK: 成功； Success
 *         - 其他: UART 错误； UART error
 */
static esp_err_t bl0942_uart_configure(const bl0942_config_t *config);

/**
 * @brief 删除 UART 驱动
 * @details Delete UART driver
 * @param[in] uart_num UART 端口号； UART port number
 * @return
 *         - ESP_OK: 成功或未安装； Success or not installed
 *         - 其他: UART 错误； UART error
 */
static esp_err_t bl0942_uart_delete(uart_port_t uart_num);

/**
 * @brief 执行硬复位
 * @details Perform hard reset
 * @param[in] me BL0942 句柄； BL0942 handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - 其他: EN 或 UART 错误； EN or UART error
 */
static esp_err_t bl0942_hard_reset(bl0942_t *me);

/**
 * @brief 进入一次公开操作
 * @details Enter one public operation
 * @param[in] me BL0942 句柄； BL0942 handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 正在销毁或状态无效； Destroying or invalid state
 *         - ESP_ERR_TIMEOUT: 互斥锁获取失败； Mutex take failed
 */
static esp_err_t bl0942_enter_op(bl0942_t *me);

/**
 * @brief 离开一次公开操作
 * @details Leave one public operation
 * @param[in] me BL0942 句柄； BL0942 handle
 */
static void bl0942_leave_op(bl0942_t *me);

/**
 * @brief 等待公开操作清空
 * @details Wait until public operations are drained
 * @param[in] me BL0942 句柄； BL0942 handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 互斥锁获取失败； Mutex take failed
 */
static esp_err_t bl0942_wait_active_ops_drained(bl0942_t *me);

/**
 * @brief 停止采样任务内部实现
 * @details Stop sampling task internal implementation
 * @param[in] me BL0942 句柄； BL0942 handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 停止超时； Stop timeout
 */
static esp_err_t bl0942_stop_impl(bl0942_t *me);

/**
 * @brief 采样任务入口
 * @details Sampling task entry
 * @param[in] user_ctx BL0942 句柄； BL0942 handle
 */
static void bl0942_sample_task_entry(void *user_ctx);

/**********************
 *  STATIC VARIABLES
 **********************/

ESP_EVENT_DEFINE_BASE(BL0942_EVENT_BASE);

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bl0942_t *bl0942_create(const bl0942_config_t *config)
{
    bl0942_config_t resolved_config = {0};
    bool uart_installed = false;
    esp_err_t ret = ESP_OK;

    if (config == NULL) {
        ESP_LOGE(TAG, "config is null");
        return NULL;
    }

    bl0942_apply_defaults(config, &resolved_config);
    if (bl0942_validate_config(&resolved_config) != ESP_OK) {
        return NULL;
    }

    bl0942_t *me = calloc(1, sizeof(*me));
    if (me == NULL) {
        ESP_LOGE(TAG, "calloc bl0942 failed");
        return NULL;
    }

    me->config = resolved_config;

    me->mutex = xSemaphoreCreateMutex();
    if (me->mutex == NULL) {
        ESP_LOGE(TAG, "create mutex failed");
        goto err;
    }

    me->lifecycle_mutex = xSemaphoreCreateMutex();
    if (me->lifecycle_mutex == NULL) {
        ESP_LOGE(TAG, "create lifecycle mutex failed");
        goto err;
    }

    me->sample_task_done_sema = xSemaphoreCreateBinary();
    if (me->sample_task_done_sema == NULL) {
        ESP_LOGE(TAG, "create sample task done semaphore failed");
        goto err;
    }

    me->active_ops_done_sema = xSemaphoreCreateBinary();
    if (me->active_ops_done_sema == NULL) {
        ESP_LOGE(TAG, "create active ops done semaphore failed");
        goto err;
    }

    ret = bl0942_power_cycle(me->config.en_gpio);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "power-cycle BL0942 failed: %s", esp_err_to_name(ret));
        goto err;
    }

    if (uart_is_driver_installed(me->config.uart_num)) {
        ESP_LOGE(TAG, "uart port %d already installed",
                 (int)me->config.uart_num);
        goto err;
    }

    ret = bl0942_uart_install(&me->config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "install uart driver failed: %s", esp_err_to_name(ret));
        goto err;
    }
    uart_installed = true;

    ret = bl0942_uart_configure(&me->config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "configure uart failed: %s", esp_err_to_name(ret));
        goto err;
    }

    memset(&me->latest, 0, sizeof(me->latest));
    me->has_latest = false;
    me->sample_task = NULL;
    me->active_ops = 0;
    me->sample_task_running = false;
    me->stop_in_progress = false;
    me->fault_cycles = 0;
    me->consecutive_failures = 0;
    me->hard_reset_count = 0;
    me->destroying = false;
    me->initialized = true;

    return me;

err:
    if (uart_installed) {
        const esp_err_t delete_ret = bl0942_uart_delete(me->config.uart_num);
        if (delete_ret != ESP_OK) {
            ESP_LOGW(TAG, "delete uart after create failure failed: %s",
                     esp_err_to_name(delete_ret));
        }
    }
    if (me->sample_task_done_sema != NULL) {
        vSemaphoreDelete(me->sample_task_done_sema);
    }
    if (me->active_ops_done_sema != NULL) {
        vSemaphoreDelete(me->active_ops_done_sema);
    }
    if (me->lifecycle_mutex != NULL) {
        vSemaphoreDelete(me->lifecycle_mutex);
    }
    if (me->mutex != NULL) {
        vSemaphoreDelete(me->mutex);
    }
    free(me);
    return NULL;
}

esp_err_t bl0942_destroy(bl0942_t *me)
{
    if (me == NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(me->lifecycle_mutex != NULL, ESP_ERR_INVALID_STATE,
                        TAG, "lifecycle mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->lifecycle_mutex,
                                       portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take lifecycle mutex failed");

    me->destroying = true;
    (void)xSemaphoreGive(me->lifecycle_mutex);

    esp_err_t ret = bl0942_wait_active_ops_drained(me);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = bl0942_stop_impl(me);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    ret = bl0942_uart_delete(me->config.uart_num);
    if (ret != ESP_OK) {
        (void)xSemaphoreGive(me->mutex);
        ESP_LOGW(TAG, "delete uart driver failed: %s", esp_err_to_name(ret));
        return ret;
    }

    me->initialized = false;
    (void)xSemaphoreGive(me->mutex);

    if (me->sample_task_done_sema != NULL) {
        vSemaphoreDelete(me->sample_task_done_sema);
    }
    if (me->active_ops_done_sema != NULL) {
        vSemaphoreDelete(me->active_ops_done_sema);
    }
    if (me->lifecycle_mutex != NULL) {
        vSemaphoreDelete(me->lifecycle_mutex);
    }
    if (me->mutex != NULL) {
        vSemaphoreDelete(me->mutex);
    }

    free(me);
    return ESP_OK;
}

esp_err_t bl0942_start(bl0942_t *me)
{
    esp_err_t ret = bl0942_enter_op(me);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!me->initialized) {
        ret = ESP_ERR_INVALID_STATE;
        goto out;
    }
    if (me->sample_task_done_sema == NULL) {
        ret = ESP_ERR_INVALID_STATE;
        goto out;
    }

    if (xSemaphoreTake(me->lifecycle_mutex, portMAX_DELAY) != pdTRUE) {
        ret = ESP_ERR_TIMEOUT;
        goto out;
    }

    if (me->destroying || me->stop_in_progress) {
        ret = ESP_ERR_INVALID_STATE;
        goto unlock;
    }

    if (me->sample_task != NULL && me->sample_task_running) {
        goto unlock;
    }

    if (me->sample_task != NULL) {
        if (xSemaphoreTake(me->sample_task_done_sema, 0) == pdTRUE) {
            me->sample_task = NULL;
        } else {
            ret = ESP_ERR_INVALID_STATE;
            goto unlock;
        }
    }

    me->consecutive_failures = 0;
    me->fault_cycles = 0;
    me->hard_reset_count = 0;

    while (xSemaphoreTake(me->sample_task_done_sema, 0) == pdTRUE) {
    }

    me->sample_task_running = true;

    const BaseType_t task_ret = xTaskCreate(bl0942_sample_task_entry,
                                            BL0942_SAMPLE_TASK_NAME,
                                            BL0942_SAMPLE_TASK_STACK,
                                            me,
                                            BL0942_SAMPLE_TASK_PRIORITY,
                                            &me->sample_task);
    if (task_ret != pdPASS) {
        me->sample_task_running = false;
        me->sample_task = NULL;
        ESP_LOGE(TAG, "create sample task failed");
        ret = ESP_ERR_NO_MEM;
        goto unlock;
    }

unlock:
    (void)xSemaphoreGive(me->lifecycle_mutex);
out:
    bl0942_leave_op(me);
    return ret;
}

esp_err_t bl0942_stop(bl0942_t *me)
{
    esp_err_t ret = bl0942_enter_op(me);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = bl0942_stop_impl(me);
    bl0942_leave_op(me);
    return ret;
}

esp_err_t bl0942_read(bl0942_t *me, bl0942_measurement_t *out)
{
    esp_err_t ret = bl0942_enter_op(me);
    if (ret != ESP_OK) {
        return ret;
    }

    if (out == NULL) {
        ret = ESP_ERR_INVALID_ARG;
        goto leave;
    }
    if (!me->initialized || me->mutex == NULL) {
        ret = ESP_ERR_INVALID_STATE;
        goto leave;
    }
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        ret = ESP_ERR_TIMEOUT;
        goto leave;
    }

    uint8_t cmd[2] = {0};
    uint8_t frame[BL0942_FRAME_SIZE] = {0};
    bl0942_measurement_t measurement = {0};
    const TickType_t timeout_ticks = pdMS_TO_TICKS(me->config.read_timeout_ms);

    bl0942_build_packet_read_cmd(me->config.device_address, cmd);

    if (me->destroying) {
        ret = ESP_ERR_INVALID_STATE;
        goto out;
    }

    if (!uart_is_driver_installed(me->config.uart_num)) {
        ret = ESP_ERR_INVALID_STATE;
        goto out;
    }

    ret = uart_flush_input(me->config.uart_num);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "flush uart input failed: %s", esp_err_to_name(ret));
        goto out;
    }

    const int written = uart_write_bytes(me->config.uart_num, cmd, sizeof(cmd));
    if (written != (int)sizeof(cmd)) {
        ESP_LOGW(TAG, "write read command failed, written=%d", written);
        ret = ESP_FAIL;
        goto out;
    }

    ret = uart_wait_tx_done(me->config.uart_num, timeout_ticks);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "uart tx wait failed: %s", esp_err_to_name(ret));
        goto out;
    }

    const int read_len = uart_read_bytes(me->config.uart_num, frame,
                                         sizeof(frame), timeout_ticks);
    if (read_len != (int)sizeof(frame)) {
        ESP_LOGW(TAG, "expected %d bytes, received %d", (int)sizeof(frame),
                 read_len);
        ret = ESP_ERR_TIMEOUT;
        goto out;
    }

    if (frame[0] != BL0942_FRAME_HEADER) {
        ESP_LOGW(TAG, "invalid frame header: 0x%02X", frame[0]);
        ret = ESP_ERR_INVALID_RESPONSE;
        goto out;
    }

    const uint8_t expected_checksum =
        bl0942_calculate_packet_checksum(cmd[0], frame);
    if (frame[BL0942_FRAME_SIZE - 1] != expected_checksum) {
        ESP_LOGW(TAG, "checksum mismatch, expected=0x%02X actual=0x%02X",
                 expected_checksum, frame[BL0942_FRAME_SIZE - 1]);
        ret = ESP_ERR_INVALID_RESPONSE;
        goto out;
    }

    bl0942_parse_packet(frame, &measurement);
    me->latest = measurement;
    me->has_latest = true;
    *out = measurement;

out:
    (void)xSemaphoreGive(me->mutex);
leave:
    bl0942_leave_op(me);
    return ret;
}

esp_err_t bl0942_get_latest(bl0942_t *me, bl0942_measurement_t *out)
{
    esp_err_t ret = bl0942_enter_op(me);
    if (ret != ESP_OK) {
        return ret;
    }

    if (out == NULL) {
        ret = ESP_ERR_INVALID_ARG;
        goto leave;
    }
    if (!me->initialized || me->mutex == NULL) {
        ret = ESP_ERR_INVALID_STATE;
        goto leave;
    }
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        ret = ESP_ERR_TIMEOUT;
        goto leave;
    }

    if (me->destroying) {
        ret = ESP_ERR_INVALID_STATE;
    } else if (!me->has_latest) {
        ret = ESP_ERR_INVALID_STATE;
    } else {
        *out = me->latest;
    }

    (void)xSemaphoreGive(me->mutex);
leave:
    bl0942_leave_op(me);
    return ret;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static esp_err_t bl0942_enter_op(bl0942_t *me)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "bl0942 is null");
    ESP_RETURN_ON_FALSE(me->lifecycle_mutex != NULL, ESP_ERR_INVALID_STATE,
                        TAG, "lifecycle mutex is null");
    ESP_RETURN_ON_FALSE(me->active_ops_done_sema != NULL,
                        ESP_ERR_INVALID_STATE, TAG,
                        "active ops done semaphore is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->lifecycle_mutex,
                                       portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG,
                        "take lifecycle mutex failed");

    esp_err_t ret = ESP_OK;
    if (me->destroying) {
        ret = ESP_ERR_INVALID_STATE;
    } else {
        me->active_ops++;
    }

    (void)xSemaphoreGive(me->lifecycle_mutex);
    return ret;
}

static void bl0942_leave_op(bl0942_t *me)
{
    if (me == NULL || me->lifecycle_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(me->lifecycle_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    if (me->active_ops > 0) {
        me->active_ops--;
        if (me->destroying && me->active_ops == 0 &&
            me->active_ops_done_sema != NULL) {
            (void)xSemaphoreGive(me->active_ops_done_sema);
        }
    }

    (void)xSemaphoreGive(me->lifecycle_mutex);
}

static esp_err_t bl0942_wait_active_ops_drained(bl0942_t *me)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "bl0942 is null");
    ESP_RETURN_ON_FALSE(me->lifecycle_mutex != NULL, ESP_ERR_INVALID_STATE,
                        TAG, "lifecycle mutex is null");
    ESP_RETURN_ON_FALSE(me->active_ops_done_sema != NULL,
                        ESP_ERR_INVALID_STATE, TAG,
                        "active ops done semaphore is null");

    while (xSemaphoreTake(me->active_ops_done_sema, 0) == pdTRUE) {
    }

    while (true) {
        ESP_RETURN_ON_FALSE(xSemaphoreTake(me->lifecycle_mutex,
                                           portMAX_DELAY) == pdTRUE,
                            ESP_ERR_TIMEOUT, TAG,
                            "take lifecycle mutex failed");
        const bool drained = (me->active_ops == 0);
        (void)xSemaphoreGive(me->lifecycle_mutex);

        if (drained) {
            return ESP_OK;
        }

        ESP_RETURN_ON_FALSE(xSemaphoreTake(me->active_ops_done_sema,
                                           portMAX_DELAY) == pdTRUE,
                            ESP_ERR_TIMEOUT, TAG,
                            "wait active ops drained failed");
    }
}

static esp_err_t bl0942_stop_impl(bl0942_t *me)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "bl0942 is null");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "bl0942 is not initialized");
    ESP_RETURN_ON_FALSE(me->sample_task_done_sema != NULL,
                        ESP_ERR_INVALID_STATE, TAG,
                        "sample task done semaphore is null");
    ESP_RETURN_ON_FALSE(me->lifecycle_mutex != NULL, ESP_ERR_INVALID_STATE,
                        TAG, "lifecycle mutex is null");

    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->lifecycle_mutex,
                                       portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG,
                        "take lifecycle mutex failed");

    esp_err_t ret = ESP_OK;
    int wait_timeout_ms = 0;

    if (me->sample_task == NULL) {
        me->sample_task_running = false;
        goto out;
    }

    if (xTaskGetCurrentTaskHandle() == me->sample_task) {
        me->sample_task_running = false;
        ret = ESP_ERR_INVALID_STATE;
        goto out;
    }

    if (me->stop_in_progress) {
        ret = ESP_ERR_INVALID_STATE;
        goto out;
    }

    me->sample_task_running = false;
    me->stop_in_progress = true;

    wait_timeout_ms = me->config.read_timeout_ms +
                      me->config.sample_period_ms +
                      BL0942_STOP_EXTRA_TIMEOUT_MS;
    (void)xSemaphoreGive(me->lifecycle_mutex);

    const BaseType_t sema_ret = xSemaphoreTake(
        me->sample_task_done_sema, pdMS_TO_TICKS(wait_timeout_ms));

    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->lifecycle_mutex,
                                       portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG,
                        "take lifecycle mutex failed");

    me->stop_in_progress = false;

    if (sema_ret != pdTRUE) {
        ret = ESP_ERR_TIMEOUT;
        ESP_LOGW(TAG, "stop sample task timed out");
        goto out;
    }

    me->sample_task = NULL;

out:
    (void)xSemaphoreGive(me->lifecycle_mutex);
    return ret;
}

static void bl0942_apply_defaults(const bl0942_config_t *config,
                                  bl0942_config_t *out)
{
    if (out == NULL) {
        return;
    }

    *out = *config;

    if (out->baud_rate <= 0) {
        out->baud_rate = BL0942_DEFAULT_BAUD_RATE;
    }
    if (out->rx_buf_size <= 0) {
        out->rx_buf_size = BL0942_DEFAULT_RX_BUF_SIZE;
    }
    if (out->read_timeout_ms <= 0) {
        out->read_timeout_ms = BL0942_DEFAULT_READ_TIMEOUT_MS;
    }
    if (out->sample_period_ms <= 0) {
        out->sample_period_ms = BL0942_DEFAULT_SAMPLE_PERIOD_MS;
    }
    if (out->fault_threshold <= 0) {
        out->fault_threshold = BL0942_DEFAULT_FAULT_THRESHOLD;
    }
    if (out->hard_reset_max_attempts < 0) {
        out->hard_reset_max_attempts = BL0942_DEFAULT_HARD_RESET_ATTEMPTS;
    }
}

static esp_err_t bl0942_validate_config(const bl0942_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "config is null");
    ESP_RETURN_ON_FALSE(config->uart_num >= UART_NUM_0 &&
                            config->uart_num < UART_NUM_MAX,
                        ESP_ERR_INVALID_ARG, TAG, "invalid uart port");
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(config->tx_gpio),
                        ESP_ERR_INVALID_ARG, TAG, "invalid tx gpio");
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_GPIO(config->rx_gpio),
                        ESP_ERR_INVALID_ARG, TAG, "invalid rx gpio");
    ESP_RETURN_ON_FALSE(config->tx_gpio != config->rx_gpio,
                        ESP_ERR_INVALID_ARG, TAG,
                        "tx and rx gpio must be distinct");
    ESP_RETURN_ON_FALSE(config->en_gpio == GPIO_NUM_NC ||
                            GPIO_IS_VALID_OUTPUT_GPIO(config->en_gpio),
                        ESP_ERR_INVALID_ARG, TAG, "invalid enable gpio");
    ESP_RETURN_ON_FALSE(config->device_address <= BL0942_MAX_DEVICE_ADDRESS,
                        ESP_ERR_INVALID_ARG, TAG, "invalid device address");
    ESP_RETURN_ON_FALSE(bl0942_is_supported_baud_rate(config->baud_rate),
                        ESP_ERR_INVALID_ARG, TAG, "unsupported baud rate");
    ESP_RETURN_ON_FALSE(config->rx_buf_size >= BL0942_MIN_RX_BUF_SIZE,
                        ESP_ERR_INVALID_ARG, TAG, "rx buffer too small");
    ESP_RETURN_ON_FALSE(config->read_timeout_ms >= BL0942_MIN_TIMEOUT_MS,
                        ESP_ERR_INVALID_ARG, TAG, "read timeout too small");
    ESP_RETURN_ON_FALSE(config->sample_period_ms >= BL0942_MIN_TIMEOUT_MS,
                        ESP_ERR_INVALID_ARG, TAG, "sample period too small");
    ESP_RETURN_ON_FALSE(config->fault_threshold >= 1,
                        ESP_ERR_INVALID_ARG, TAG, "fault threshold too small");
    ESP_RETURN_ON_FALSE(config->hard_reset_max_attempts >= 0,
                        ESP_ERR_INVALID_ARG, TAG,
                        "hard reset attempts must be non-negative");

    return ESP_OK;
}

static bool bl0942_is_supported_baud_rate(int baud_rate)
{
    switch (baud_rate) {
    case 4800:
    case 9600:
    case 19200:
    case 38400:
        return true;
    default:
        return false;
    }
}

static uint8_t bl0942_build_read_cmd(uint8_t device_address)
{
    return (uint8_t)(0x58U | (device_address & 0x03U));
}

static void bl0942_build_packet_read_cmd(uint8_t device_address,
                                         uint8_t out_cmd[2])
{
    out_cmd[0] = bl0942_build_read_cmd(device_address);
    out_cmd[1] = BL0942_PACKET_READ_ADDR;
}

static uint8_t bl0942_calculate_packet_checksum(
    uint8_t cmd, const uint8_t frame[BL0942_FRAME_SIZE])
{
    uint32_t checksum_acc = cmd;

    for (size_t i = 0; i < BL0942_FRAME_SIZE - 1; ++i) {
        checksum_acc += frame[i];
    }

    return (uint8_t)(~(checksum_acc & 0xFFU));
}

static uint32_t bl0942_decode_u24_le(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16);
}

static int32_t bl0942_decode_s24_le(const uint8_t *data)
{
    uint32_t raw = bl0942_decode_u24_le(data);

    if ((raw & 0x00800000UL) != 0U) {
        raw |= 0xFF000000UL;
    }

    return (int32_t)raw;
}

static void bl0942_parse_packet(const uint8_t frame[BL0942_FRAME_SIZE],
                                bl0942_measurement_t *out)
{
    out->i_rms_raw = bl0942_decode_u24_le(&frame[1]);
    out->v_rms_raw = bl0942_decode_u24_le(&frame[4]);
    out->i_fast_rms_raw = bl0942_decode_u24_le(&frame[7]);
    out->watt_raw = bl0942_decode_s24_le(&frame[10]);
    out->cf_cnt_raw = bl0942_decode_u24_le(&frame[13]);
    out->freq_raw = (uint16_t)(((uint16_t)frame[17] << 8) | frame[16]);
    out->status_raw = (uint16_t)frame[19];
    out->capture_time_us = (uint64_t)esp_timer_get_time();
    out->valid = true;
}

static void bl0942_post_measurement(
    const bl0942_measurement_t *measurement)
{
    const esp_err_t ret = esp_event_post(BL0942_EVENT_BASE,
                                         BL0942_EVENT_MEASUREMENT,
                                         measurement, sizeof(*measurement),
                                         pdMS_TO_TICKS(
                                             BL0942_EVENT_POST_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "post measurement event failed: %s",
                 esp_err_to_name(ret));
    }
}

static void bl0942_post_fault(const bl0942_t *me, esp_err_t last_error,
                              bool hard_reset_attempted)
{
    const bl0942_fault_info_t payload = {
        .consecutive_failures = (uint32_t)me->consecutive_failures,
        .fault_cycles = me->fault_cycles,
        .hard_reset_attempted = hard_reset_attempted,
        .last_error = last_error,
    };

    const esp_err_t ret = esp_event_post(BL0942_EVENT_BASE, BL0942_EVENT_FAULT,
                                         &payload, sizeof(payload),
                                         pdMS_TO_TICKS(
                                             BL0942_EVENT_POST_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "post fault event failed: %s", esp_err_to_name(ret));
    }
}

static esp_err_t bl0942_power_cycle(gpio_num_t en_gpio)
{
    if (en_gpio == GPIO_NUM_NC) {
        return ESP_OK;
    }

    const gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << (uint32_t)en_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG,
                        "configure enable gpio failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(en_gpio, 0), TAG,
                        "drive enable gpio low failed");
    vTaskDelay(pdMS_TO_TICKS(BL0942_EN_LOW_DELAY_MS));
    ESP_RETURN_ON_ERROR(gpio_set_level(en_gpio, 1), TAG,
                        "drive enable gpio high failed");
    vTaskDelay(pdMS_TO_TICKS(BL0942_EN_SETTLE_DELAY_MS));

    return ESP_OK;
}

static esp_err_t bl0942_uart_install(const bl0942_config_t *config)
{
    return uart_driver_install(config->uart_num, config->rx_buf_size,
                               BL0942_UART_TX_BUF_SIZE, 0, NULL, 0);
}

static esp_err_t bl0942_uart_configure(const bl0942_config_t *config)
{
    const uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {
            .allow_pd = 0,
            .backup_before_sleep = 0,
        },
    };

    ESP_RETURN_ON_ERROR(uart_param_config(config->uart_num, &uart_config), TAG,
                        "configure uart parameters failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(config->uart_num, config->tx_gpio,
                                     config->rx_gpio, UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE),
                        TAG, "set uart pins failed");

    return ESP_OK;
}

static esp_err_t bl0942_uart_delete(uart_port_t uart_num)
{
    if (!uart_is_driver_installed(uart_num)) {
        return ESP_OK;
    }

    return uart_driver_delete(uart_num);
}

static esp_err_t bl0942_hard_reset(bl0942_t *me)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "bl0942 is null");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    esp_err_t ret = bl0942_power_cycle(me->config.en_gpio);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "hard-reset power cycle failed: %s",
                 esp_err_to_name(ret));
        goto out;
    }

    ret = bl0942_uart_delete(me->config.uart_num);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "hard-reset uart delete failed: %s",
                 esp_err_to_name(ret));
        goto out;
    }

    ret = bl0942_uart_install(&me->config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "hard-reset uart install failed: %s",
                 esp_err_to_name(ret));
        goto out;
    }

    ret = bl0942_uart_configure(&me->config);
    if (ret != ESP_OK) {
        (void)bl0942_uart_delete(me->config.uart_num);
        ESP_LOGE(TAG, "hard-reset uart configure failed: %s",
                 esp_err_to_name(ret));
        goto out;
    }

out:
    (void)xSemaphoreGive(me->mutex);
    return ret;
}

static void bl0942_sample_task_entry(void *user_ctx)
{
    bl0942_t *me = (bl0942_t *)user_ctx;
    TickType_t last_wake = xTaskGetTickCount();

    while (me->sample_task_running) {
        bl0942_measurement_t measurement = {0};
        esp_err_t ret = bl0942_read(me, &measurement);

        if (ret == ESP_OK) {
            me->consecutive_failures = 0;
            me->hard_reset_count = 0;
            bl0942_post_measurement(&measurement);
        } else {
            me->consecutive_failures++;
            if (me->consecutive_failures >= me->config.fault_threshold) {
                me->fault_cycles++;
                bool reset_attempted = false;
                esp_err_t fault_error = ret;
                if (me->config.en_gpio != GPIO_NUM_NC &&
                    me->hard_reset_count < me->config.hard_reset_max_attempts) {
                    reset_attempted = true;
                    me->hard_reset_count++;
                    const esp_err_t reset_ret = bl0942_hard_reset(me);
                    if (reset_ret != ESP_OK) {
                        fault_error = reset_ret;
                    }
                }
                bl0942_post_fault(me, fault_error, reset_attempted);
                me->consecutive_failures = 0;
                if (me->hard_reset_count >= me->config.hard_reset_max_attempts &&
                    me->config.hard_reset_max_attempts > 0) {
                    me->sample_task_running = false;
                }
            }
        }

        if (!me->sample_task_running) {
            break;
        }

        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(me->config.sample_period_ms));
    }

    me->sample_task_running = false;
    if (me->lifecycle_mutex != NULL &&
        xSemaphoreTake(me->lifecycle_mutex, portMAX_DELAY) == pdTRUE) {
        me->sample_task = NULL;
        (void)xSemaphoreGive(me->sample_task_done_sema);
        (void)xSemaphoreGive(me->lifecycle_mutex);
    } else {
        (void)xSemaphoreGive(me->sample_task_done_sema);
    }
    vTaskDelete(NULL);
}
