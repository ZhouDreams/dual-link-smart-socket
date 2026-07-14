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
} metering_config_t;

/**
 * @brief 电参量快照
 * @details Metering snapshot
 */
typedef struct {
    float voltage;              /**< 电压 V； Voltage in volts */
    float current;              /**< 电流 A； Current in amperes */
    float power;                /**< 有功功率 W； Active power in watts */
    float energy_delta;         /**< 上报区间电能增量 mWh； Interval energy delta in milliwatt-hours */
    float frequency;            /**< 电网频率 Hz； Grid frequency in hertz */
    uint64_t timestamp_us;      /**< 快照时间戳； Snapshot timestamp in microseconds */
    uint32_t energy_delta_token;/**< 电能增量确认令牌； Energy delta confirmation token */
    bool valid;                 /**< 快照是否有效； Whether snapshot is valid */
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
    METERING_EVENT_SNAPSHOT = 0, /**< 单次快照； Single sample snapshot */
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
 * @note 仅 ESP_OK 表示句柄已被释放；失败时事件处理器可能仍引用句柄，调用方须保留句柄并重试 stop/destroy。
 *       Only ESP_OK consumes the handle; on failure event handlers may still reference it, so the caller must retain it and retry stop/destroy.
 * @param[in] me 服务句柄，可为 NULL
 * @return
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_STATE: 服务正在执行其它生命周期操作
 *         - ESP_ERR_TIMEOUT: 等待互斥量超时
 *         - 其他: 事件处理器注销失败，句柄未释放
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
 * @note 收尾 mutex 等待被中断时，本函数会先恢复一致状态；若此前无其它清理错误则返回 ESP_ERR_TIMEOUT，调用方可安全重试。
 *       If the final mutex wait is interrupted, this function first restores a consistent state; it returns ESP_ERR_TIMEOUT when no earlier cleanup error takes precedence, and callers may safely retry.
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
 * @brief 重置电能增量基准
 * @details Reset the energy delta baseline
 * @param[in] me 服务句柄
 * @return
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_INVALID_STATE: 服务状态无效
 *         - ESP_ERR_TIMEOUT: 等待资源超时
 */
esp_err_t metering_service_reset_energy(metering_service_t *me);

/**
 * @brief 确认电能增量已成功上报
 * @details Confirm that a snapshot energy delta was published successfully
 * @param[in] me 服务句柄
 * @param[in] snapshot 已成功上报的快照
 * @return
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_INVALID_STATE: 服务状态无效或令牌无效
 *         - ESP_ERR_TIMEOUT: 等待资源超时
 */
esp_err_t metering_service_confirm_energy_delta(
    metering_service_t *me, const metering_snapshot_t *snapshot);

/**
 * @brief 丢弃未成功上报的电能增量
 * @details Release a failed-publish energy delta without advancing the confirmed baseline
 * @param[in] me 服务句柄
 * @param[in] snapshot 未成功上报的快照
 * @return
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_INVALID_STATE: 服务状态无效或令牌无效
 *         - ESP_ERR_TIMEOUT: 等待资源超时
 */
esp_err_t metering_service_discard_energy_delta(
    metering_service_t *me, const metering_snapshot_t *snapshot);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
