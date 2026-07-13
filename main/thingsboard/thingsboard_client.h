/**
 * @file thingsboard_client.h
 * @brief ThingsBoard 客户端公共接口
 * @details ThingsBoard client public interface
 * @author OpenCode
 * @date 2026-05-28
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "network_manager.h"
#include "safety_guard.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief ThingsBoard 客户端句柄
 * @details ThingsBoard client handle
 */
typedef struct thingsboard_client thingsboard_client_t;

/**
 * @brief ThingsBoard 客户端配置
 * @details ThingsBoard client configuration
 */
typedef struct {
    network_manager_t *net_mgr;  /**< 借用的网络管理器； Borrowed network manager */
    const char *device_token;    /**< 设备令牌，当前由底层连接管理； Device token, currently managed by lower layers */
    bool enable_rpc;             /**< 是否订阅 RPC； Whether to subscribe to RPC */
    bool enable_attributes;      /**< 是否订阅属性； Whether to subscribe to attributes */
    int json_buf_size;           /**< JSON 缓冲区大小，非正数使用默认值； JSON buffer size, non-positive uses default */
} tb_client_config_t;

/**
 * @brief ThingsBoard 遥测输入
 * @details ThingsBoard telemetry input
 */
typedef struct {
    float voltage;                         /**< 电压 V； Voltage in volts */
    float current;                         /**< 电流 A； Current in amperes */
    float power;                           /**< 功率 W； Power in watts */
    float energy_delta;                    /**< 上报区间电能增量 mWh； Interval energy delta in milliwatt-hours */
    float frequency;                       /**< 电网频率 Hz； Grid frequency in hertz */
    bool relay_on;                         /**< 继电器状态； Relay state */
    const char *active_link;               /**< 当前活动链路； Active link */
    safety_guard_level_t safety_level;     /**< 安全等级； Safety level */
    bool valid;                            /**< 数据是否有效； Whether data is valid */
} tb_telemetry_input_t;

/**
 * @brief ThingsBoard 命令类型
 * @details ThingsBoard command type
 */
typedef enum {
    TB_COMMAND_SET_RELAY = 0,       /**< 设置继电器； Set relay */
    TB_COMMAND_GET_POWER_LIMIT,     /**< 获取功率限制； Get power limit */
    TB_COMMAND_SET_POWER_LIMIT,     /**< 设置功率限制； Set power limit */
} tb_command_type_t;

/**
 * @brief ThingsBoard 命令
 * @details ThingsBoard command
 */
typedef struct {
    tb_command_type_t type; /**< 命令类型； Command type */
    int32_t request_id;     /**< 请求 ID； Request ID */
    bool relay_on;          /**< 继电器目标状态； Target relay state */
    float power_limit_w;    /**< 功率限制 W； Power limit in watts */
} tb_command_t;

/**
 * @brief ThingsBoard 命令回调
 * @details ThingsBoard command callback
 * @param[in] cmd 命令； Command
 * @param[in] user_ctx 用户上下文； User context
 */
typedef void (*tb_command_cb_t)(const tb_command_t *cmd, void *user_ctx);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 创建 ThingsBoard 客户端
 * @details Create ThingsBoard client
 * @param[in] config 客户端配置； Client configuration
 * @return ThingsBoard 客户端句柄，失败返回 NULL； ThingsBoard client handle, NULL on failure
 */
thingsboard_client_t *thingsboard_client_create(const tb_client_config_t *config);

/**
 * @brief 销毁 ThingsBoard 客户端
 * @details Destroy ThingsBoard client
 * @note 仅 ESP_OK 表示句柄已被释放；失败时网络或命令回调可能仍引用句柄，调用方须保留并重试 destroy。
 *       Only ESP_OK consumes the handle; on failure network or command callbacks may still reference it, so the caller must retain it and retry destroy.
 * @param[in] me ThingsBoard 客户端句柄； ThingsBoard client handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_TIMEOUT: 等待停止或回调退出超时； Stop or callback drain timed out
 *         - 其他: 下层清理失败，句柄未释放； Lower cleanup failed and the handle remains owned
 */
esp_err_t thingsboard_client_destroy(thingsboard_client_t *me);

/**
 * @brief 启动 ThingsBoard 客户端
 * @details Start ThingsBoard client
 * @param[in] me ThingsBoard 客户端句柄； ThingsBoard client handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 等待互斥量超时； Mutex timeout
 */
esp_err_t thingsboard_client_start(thingsboard_client_t *me);

/**
 * @brief 停止 ThingsBoard 客户端
 * @details Stop ThingsBoard client
 * @param[in] me ThingsBoard 客户端句柄； ThingsBoard client handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 等待并发停止或回调退出超时； Concurrent stop or callback drain timed out
 */
esp_err_t thingsboard_client_stop(thingsboard_client_t *me);

/**
 * @brief 发布遥测数据
 * @details Publish telemetry data
 * @param[in] me ThingsBoard 客户端句柄； ThingsBoard client handle
 * @param[in] input 遥测输入； Telemetry input
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 */
esp_err_t thingsboard_client_publish_telemetry(thingsboard_client_t *me,
                                               const tb_telemetry_input_t *input);

/**
 * @brief 上报继电器状态
 * @details Report relay state
 * @param[in] me ThingsBoard 客户端句柄； ThingsBoard client handle
 * @param[in] on 继电器是否开启； Whether relay is on
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 */
esp_err_t thingsboard_client_report_relay_state(thingsboard_client_t *me,
                                                bool on);

/**
 * @brief 上报功率限制
 * @details Report power limit
 * @param[in] me ThingsBoard 客户端句柄； ThingsBoard client handle
 * @param[in] power_limit_w 功率限制 W； Power limit in watts
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 */
esp_err_t thingsboard_client_report_power_limit(thingsboard_client_t *me,
                                                float power_limit_w);

/**
 * @brief 发送 RPC 响应
 * @details Send RPC response
 * @param[in] me ThingsBoard 客户端句柄； ThingsBoard client handle
 * @param[in] request_id RPC 请求 ID； RPC request ID
 * @param[in] json JSON 响应载荷； JSON response payload
 * @param[in] json_len JSON 响应长度； JSON response length
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 */
esp_err_t thingsboard_client_send_rpc_response(thingsboard_client_t *me,
                                               int32_t request_id,
                                               const char *json,
                                               size_t json_len);

/**
 * @brief 注册命令回调
 * @details Register command callback
 * @param[in] me ThingsBoard 客户端句柄； ThingsBoard client handle
 * @param[in] cb 命令回调，NULL 表示清除； Command callback, NULL to clear
 * @param[in] ctx 用户上下文； User context
 * @note cb 为 NULL 时，ESP_OK 保证旧回调及 ctx 已不再被使用；ESP_ERR_TIMEOUT 时旧 ctx 必须继续有效。
 *       When cb is NULL, ESP_OK guarantees the old callback and ctx are no longer used; after ESP_ERR_TIMEOUT the old ctx must remain valid.
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 等待在途命令回调超时； In-flight command callback drain timed out
 */
esp_err_t thingsboard_client_register_command_cb(thingsboard_client_t *me,
                                                 tb_command_cb_t cb,
                                                 void *ctx);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
