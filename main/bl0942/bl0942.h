/**
 * @file bl0942.h
 * @brief BL0942 电能计量接口
 * @details BL0942 power metering interface
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

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_event.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief BL0942 句柄
 * @details BL0942 handle
 */
typedef struct bl0942 bl0942_t;

/**
 * @brief BL0942 初始化配置
 * @details BL0942 initialization configuration
 * @note 本驱动启动和运行使用同一个 baud_rate；使用 EN 上下电或硬复位时，
 *       baud_rate 应配置为硬件上电后的 BL0942 波特率，当前 Smart_Socket
 *       硬件使用 9600。
 */
typedef struct {
    uart_port_t uart_num;             /**< UART 端口号； UART port number */
    gpio_num_t en_gpio;               /**< 模块使能 GPIO，不使用填 GPIO_NUM_NC； Module enable GPIO, GPIO_NUM_NC if unused */
    gpio_num_t tx_gpio;               /**< ESP TX 到 BL0942 RX； ESP TX to BL0942 RX */
    gpio_num_t rx_gpio;               /**< ESP RX 来自 BL0942 TX； ESP RX from BL0942 TX */
    int baud_rate;                    /**< UART 波特率； UART baud rate */
    uint8_t device_address;           /**< 器件地址 0~3； Device address 0-3 */
    int rx_buf_size;                  /**< UART RX 缓冲区大小； UART RX buffer size */
    int read_timeout_ms;              /**< 单次读取超时； Single read timeout in milliseconds */
    int sample_period_ms;             /**< 采样任务周期； Sampling task period in milliseconds */
    int fault_threshold;              /**< 连续失败阈值； Consecutive failure threshold */
    int hard_reset_max_attempts;      /**< 硬复位最大次数； Maximum hard reset attempts */
} bl0942_config_t;

/**
 * @brief BL0942 测量快照
 * @details BL0942 measurement snapshot
 */
typedef struct {
    uint32_t i_rms_raw;        /**< 电流有效值原始寄存器； Raw current RMS register */
    uint32_t v_rms_raw;        /**< 电压有效值原始寄存器； Raw voltage RMS register */
    uint32_t i_fast_rms_raw;   /**< 快速电流有效值原始寄存器； Raw fast current RMS register */
    int32_t watt_raw;          /**< 有功功率原始寄存器； Raw active power register */
    uint32_t cf_cnt_raw;       /**< 电能脉冲计数原始寄存器； Raw CF pulse counter register */
    uint16_t freq_raw;         /**< 频率原始寄存器； Raw frequency register */
    uint16_t status_raw;       /**< 状态原始寄存器； Raw status register */
    uint64_t capture_time_us;  /**< 采集时间戳； Capture timestamp in microseconds */
    bool valid;                /**< 快照是否有效； Whether snapshot is valid */
} bl0942_measurement_t;

/**
 * @brief BL0942 故障事件载荷
 * @details BL0942 fault event payload
 */
typedef struct {
    uint32_t consecutive_failures;    /**< 连续失败次数； Consecutive failure count */
    uint32_t fault_cycles;            /**< 故障周期累计次数； Cumulative fault cycles */
    bool hard_reset_attempted;        /**< 是否尝试硬复位； Whether hard reset was attempted */
    esp_err_t last_error;             /**< 最近一次错误； Most recent error */
} bl0942_fault_info_t;

ESP_EVENT_DECLARE_BASE(BL0942_EVENT_BASE);

/**
 * @brief BL0942 事件 ID
 * @details BL0942 event ID
 */
typedef enum {
    BL0942_EVENT_MEASUREMENT = 0,  /**< 测量完成； Measurement completed */
    BL0942_EVENT_FAULT,            /**< 读取故障； Read fault */
} bl0942_event_id_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 创建 BL0942 实例
 * @details Create BL0942 instance
 * @param[in] config 初始化配置； Initialization configuration
 * @return BL0942 句柄，失败返回 NULL； BL0942 handle, NULL on failure
 */
bl0942_t *bl0942_create(const bl0942_config_t *config);

/**
 * @brief 销毁 BL0942 实例
 * @details Destroy BL0942 instance
 * @note 调用方必须在外部串行化句柄生命周期；bl0942_destroy() 返回 ESP_OK 后，
 *       不得再使用该句柄。
 * @note 如果销毁返回错误，句柄仍由调用方持有，可用于重试销毁或诊断。
 * @param[in] me BL0942 句柄； BL0942 handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_TIMEOUT: 采样任务停止超时； Sampling task stop timeout
 *         - 其他: UART 清理失败； UART cleanup failed
 */
esp_err_t bl0942_destroy(bl0942_t *me);

/**
 * @brief 启动采样任务
 * @details Start sampling task
 * @param[in] me BL0942 句柄； BL0942 handle
 * @return
 *         - ESP_OK: 成功或已启动； Success or already started
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_NO_MEM: 任务创建失败； Task creation failed
 */
esp_err_t bl0942_start(bl0942_t *me);

/**
 * @brief 停止采样任务
 * @details Stop sampling task
 * @param[in] me BL0942 句柄； BL0942 handle
 * @return
 *         - ESP_OK: 成功或未启动； Success or already stopped
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 停止超时； Stop timeout
 */
esp_err_t bl0942_stop(bl0942_t *me);

/**
 * @brief 同步读取一次测量
 * @details Synchronously read one measurement
 * @param[in] me BL0942 句柄； BL0942 handle
 * @param[out] out 测量结果输出； Measurement output
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 读取超时； Read timeout
 *         - ESP_ERR_INVALID_RESPONSE: 响应无效； Invalid response
 */
esp_err_t bl0942_read(bl0942_t *me, bl0942_measurement_t *out);

/**
 * @brief 获取最近一次测量
 * @details Get latest measurement
 * @note 仅读取缓存，不等待 UART 访问或硬复位完成。
 *       Reads only the cache and does not wait for UART access or hard reset completion.
 * @param[in] me BL0942 句柄； BL0942 handle
 * @param[out] out 测量结果输出； Measurement output
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效或暂无数据； Invalid state or no data
 */
esp_err_t bl0942_get_latest(bl0942_t *me, bl0942_measurement_t *out);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
