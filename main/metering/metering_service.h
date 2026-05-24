/**
 * @file metering_service.h
 * @brief 电参量业务聚合接口
 * @details Electrical metering service interface
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

#include "esp_err.h"
#include "esp_event.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 电参量服务句柄
 * @details Metering service opaque handle
 */
typedef struct metering_service metering_service_t;

/**
 * @brief 电参量服务配置
 * @details Metering service configuration
 */
typedef struct {
    float v_rms_coeff;       /**< 电压转换系数； Voltage conversion coefficient */
    float i_rms_coeff;       /**< 电流转换系数； Current conversion coefficient */
    float watt_coeff;        /**< 功率转换系数； Power conversion coefficient */
    float cf_coeff;          /**< 电能转换系数 Wh/pulse； Energy coefficient in Wh per pulse */
    int window_samples;      /**< 聚合窗口样本数； Aggregation window sample count */
    int publish_period_ms;   /**< 快照发布周期； Snapshot publish period in milliseconds */
} metering_config_t;

/**
 * @brief 电参量快照
 * @details Metering snapshot
 */
typedef struct {
    float voltage;            /**< 电压 V； Voltage in volts */
    float current;            /**< 电流 A； Current in amperes */
    float power;              /**< 有功功率 W； Active power in watts */
    float total_energy;       /**< 累计电能 Wh； Total energy in watt-hours */
    float frequency;          /**< 电网频率 Hz； Grid frequency in hertz */
    uint64_t timestamp_us;    /**< 快照时间戳； Snapshot timestamp in microseconds */
    bool valid;               /**< 快照是否有效； Whether snapshot is valid */
} metering_snapshot_t;

/**
 * @brief 电参量事件基
 * @details Metering event base
 */
ESP_EVENT_DECLARE_BASE(METERING_EVENT_BASE);

/**
 * @brief 电参量事件 ID
 * @details Metering event ID
 */
typedef enum {
    METERING_EVENT_SNAPSHOT = 0, /**< 聚合快照； Aggregated snapshot */
} metering_event_id_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 创建电参量服务
 * @details Create a metering service
 * @param[in] config 配置参数，可为 NULL 使用默认配置
 * @return
 *         - 非 NULL: 服务句柄
 *         - NULL: 创建失败
 */
metering_service_t *metering_service_create(const metering_config_t *config);

/**
 * @brief 销毁电参量服务
 * @details Destroy a metering service
 * @param[in] me 服务句柄，可为 NULL
 * @return
 *         - ESP_OK: 成功
 */
esp_err_t metering_service_destroy(metering_service_t *me);

/**
 * @brief 启动电参量服务
 * @details Start a metering service
 * @param[in] me 服务句柄
 * @return
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_INVALID_STATE: 服务状态无效
 *         - ESP_ERR_TIMEOUT: 等待资源超时
 */
esp_err_t metering_service_start(metering_service_t *me);

/**
 * @brief 停止电参量服务
 * @details Stop a metering service
 * @param[in] me 服务句柄
 * @return
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_TIMEOUT: 等待资源超时
 */
esp_err_t metering_service_stop(metering_service_t *me);

/**
 * @brief 获取最新电参量快照
 * @details Get the latest metering snapshot
 * @param[in] me 服务句柄
 * @param[out] out 快照输出缓冲区
 * @return
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_INVALID_STATE: 尚无有效快照或服务状态无效
 *         - ESP_ERR_TIMEOUT: 等待资源超时
 */
esp_err_t metering_service_get_latest(metering_service_t *me,
                                      metering_snapshot_t *out);

/**
 * @brief 重置累计电能
 * @details Reset accumulated energy
 * @param[in] me 服务句柄
 * @return
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_INVALID_STATE: 服务状态无效
 *         - ESP_ERR_TIMEOUT: 等待资源超时
 */
esp_err_t metering_service_reset_energy(metering_service_t *me);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
