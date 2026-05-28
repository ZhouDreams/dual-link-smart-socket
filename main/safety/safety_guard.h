/**
 * @file safety_guard.h
 * @brief 本地安全规则接口
 * @details Local safety rule interface
 * @author OpenCode
 * @date 2026-05-25
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
 * @brief 本地安全规则句柄
 * @details Local safety rule opaque handle
 */
typedef struct safety_guard safety_guard_t;

/**
 * @brief 本地安全规则配置
 * @details Local safety rule configuration
 */
typedef struct {
    float overcurrent_threshold_a;    /**< 过流阈值 A； Overcurrent threshold in amperes */
    float overpower_threshold_w;      /**< 过功率阈值 W； Overpower threshold in watts */
    int persistence_samples;          /**< 持续超限样本数； Persistent over-limit sample count */
} safety_guard_config_t;

/**
 * @brief 安全等级
 * @details Safety level
 */
typedef enum {
    SAFETY_GUARD_LEVEL_NORMAL = 0, /**< 正常； Normal */
    SAFETY_GUARD_LEVEL_WARNING,    /**< 告警； Warning */
    SAFETY_GUARD_LEVEL_DANGER,     /**< 危险； Danger */
} safety_guard_level_t;

/**
 * @brief 安全事件类型
 * @details Safety event type
 */
typedef enum {
    SAFETY_GUARD_EVENT_NONE = 0,    /**< 无事件； No event */
    SAFETY_GUARD_EVENT_OVERCURRENT, /**< 过流； Overcurrent */
    SAFETY_GUARD_EVENT_OVERPOWER,   /**< 过功率； Overpower */
} safety_guard_event_t;

/**
 * @brief 安全建议动作
 * @details Safety suggested action
 */
typedef enum {
    SAFETY_GUARD_ACTION_NONE = 0, /**< 无动作； No action */
    SAFETY_GUARD_ACTION_RELAY_OFF, /**< 关闭继电器； Turn relay off */
} safety_guard_action_t;

/**
 * @brief 安全规则快照
 * @details Safety rule snapshot
 */
typedef struct {
    safety_guard_level_t level;              /**< 安全等级； Safety level */
    safety_guard_event_t event;              /**< 安全事件； Safety event */
    safety_guard_action_t suggested_action;  /**< 建议动作； Suggested action */
    uint64_t timestamp_us;                   /**< 时间戳 us； Timestamp in microseconds */
    bool valid;                              /**< 快照是否有效； Whether snapshot is valid */
} safety_guard_snapshot_t;

/**
 * @brief 安全规则事件基
 * @details Safety rule event base
 */
ESP_EVENT_DECLARE_BASE(SAFETY_GUARD_EVENT_BASE);

/**
 * @brief 安全规则事件 ID
 * @details Safety rule event ID
 */
typedef enum {
    SAFETY_GUARD_EVENT_SNAPSHOT = 0, /**< 安全快照； Safety snapshot */
} safety_guard_event_id_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 创建本地安全规则实例
 * @details Create local safety rule instance
 * @param[in] config 配置参数，可为 NULL 使用默认配置； Configuration, NULL uses defaults
 * @return 安全规则句柄，失败返回 NULL； Safety guard handle, NULL on failure
 */
safety_guard_t *safety_guard_create(const safety_guard_config_t *config);

/**
 * @brief 销毁本地安全规则实例
 * @details Destroy local safety rule instance
 * @param[in] me 安全规则句柄，可为 NULL； Safety guard handle, may be NULL
 * @return
 *         - ESP_OK: 成功； Success
 *         - 其他: 停止事件处理失败； Stop event handling failed
 */
esp_err_t safety_guard_destroy(safety_guard_t *me);

/**
 * @brief 启动本地安全规则
 * @details Start local safety rule processing
 * @param[in] me 安全规则句柄； Safety guard handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 获取互斥锁超时； Mutex timeout
 */
esp_err_t safety_guard_start(safety_guard_t *me);

/**
 * @brief 停止本地安全规则
 * @details Stop local safety rule processing
 * @param[in] me 安全规则句柄； Safety guard handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 获取互斥锁超时； Mutex timeout
 */
esp_err_t safety_guard_stop(safety_guard_t *me);

/**
 * @brief 获取最新安全规则快照
 * @details Get latest safety rule snapshot
 * @param[in] me 安全规则句柄； Safety guard handle
 * @param[out] out 快照输出； Snapshot output
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 尚无快照或状态无效； No snapshot or invalid state
 *         - ESP_ERR_TIMEOUT: 获取互斥锁超时； Mutex timeout
 */
esp_err_t safety_guard_get_latest(safety_guard_t *me,
                                  safety_guard_snapshot_t *out);

/**
 * @brief 更新安全阈值
 * @details Update safety thresholds
 * @param[in] me 安全规则句柄； Safety guard handle
 * @param[in] overcurrent_a 过流阈值 A； Overcurrent threshold in amperes
 * @param[in] overpower_w 过功率阈值 W； Overpower threshold in watts
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 获取互斥锁超时； Mutex timeout
 */
esp_err_t safety_guard_set_thresholds(safety_guard_t *me,
                                      float overcurrent_a,
                                      float overpower_w);

/**
 * @brief 获取安全阈值
 * @details Get safety thresholds
 * @param[in] me 安全规则句柄； Safety guard handle
 * @param[out] out_overcurrent_a 过流阈值 A，可为 NULL； Overcurrent threshold, may be NULL
 * @param[out] out_overpower_w 过功率阈值 W，可为 NULL； Overpower threshold, may be NULL
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 获取互斥锁超时； Mutex timeout
 */
esp_err_t safety_guard_get_thresholds(safety_guard_t *me,
                                      float *out_overcurrent_a,
                                      float *out_overpower_w);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
