/**
 * @file network_manager.h
 * @brief 双模网络管理器接口
 * @details Dual-mode network manager interface
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
#include "network_link.h"
#include "network_types.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 双模网络管理器句柄
 * @details Dual-mode network manager handle
 */
typedef struct network_manager network_manager_t;

/**
 * @brief 双模网络管理器初始化配置
 * @details Dual-mode network manager initialization configuration
 * @note primary 与 backup 为借用链路，调用方负责创建和销毁；管理器生命周期内可能启动、停止链路，并注册或清除下行回调。
 *       primary and backup are borrowed links; the caller owns creation and destruction. During the manager lifecycle,
 *       the manager may start or stop links and register or clear RX callbacks.
 * @note preferred_primary 必须为 NETWORK_LINK_TYPE_NONE 以默认使用 primary 的类型，或匹配注入链路之一的类型；不匹配视为无效配置。
 *       preferred_primary must be NETWORK_LINK_TYPE_NONE to default to the primary link type, or match one of the
 *       injected link types; mismatches are invalid configuration.
 */
typedef struct {
    network_link_t *primary;                  /**< 借用的主链路； Borrowed primary link */
    network_link_t *backup;                   /**< 借用的备链路，可为 NULL； Borrowed backup link, may be NULL */
    network_link_type_t preferred_primary;    /**< 首选主链路类型，见上方约束； Preferred primary link type, see invariant above */
    uint32_t failover_recheck_ms;             /**< 故障切换重检周期（毫秒）； Failover recheck interval in milliseconds */
    uint32_t failback_delay_ms;               /**< 主链路恢复后回切延迟（毫秒）； Failback delay after primary recovery in milliseconds */
    int max_subscriptions;                    /**< 订阅意图表容量； Subscription intent table capacity */
} network_manager_config_t;

/**
 * @brief 双模网络管理器聚合状态
 * @details Dual-mode network manager aggregate status
 * @note backup 为 NULL 时，backup_status 固定为 NETWORK_LINK_STATUS_IDLE。
 *       When backup is NULL, backup_status is always NETWORK_LINK_STATUS_IDLE.
 */
typedef struct {
    network_link_type_t active_link;          /**< 当前活动链路类型； Current active link type */
    bool ready;                               /**< 当前是否有可用 MQTT 通道； Whether an MQTT channel is currently available */
    network_link_status_t primary_status;     /**< 主链路状态； Primary link status */
    network_link_status_t backup_status;      /**< 备链路状态，无备链路时为 IDLE； Backup link status, IDLE when no backup link exists */
} network_manager_status_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 创建双模网络管理器
 * @details Create dual-mode network manager
 * @param[in] config 初始化配置； Initialization configuration
 * @return 双模网络管理器句柄，失败返回 NULL； Dual-mode network manager handle, NULL on failure
 */
network_manager_t *network_manager_create(const network_manager_config_t *config);

/**
 * @brief 销毁双模网络管理器
 * @details Destroy dual-mode network manager
 * @note 仅 ESP_OK 表示句柄已被释放；失败时监控任务或链路回调可能仍引用句柄，调用方须保留借用链路并重试 destroy。
 *       Only ESP_OK consumes the handle; on failure a monitor task or link callback may still reference it, so borrowed links must remain alive and destroy must be retried.
 * @param[in] me 双模网络管理器句柄； Dual-mode network manager handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_TIMEOUT: 等待监控任务或回调退出超时； Monitor task or callback drain timed out
 *         - 其他: 下层链路停止或回调清理失败，句柄未释放； Lower link cleanup failed and the handle remains owned
 */
esp_err_t network_manager_destroy(network_manager_t *me);

/**
 * @brief 启动双模网络管理器
 * @details Start dual-mode network manager
 * @param[in] me 双模网络管理器句柄； Dual-mode network manager handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 等待互斥量超时； Mutex timeout
 */
esp_err_t network_manager_start(network_manager_t *me);

/**
 * @brief 停止双模网络管理器
 * @details Stop dual-mode network manager
 * @param[in] me 双模网络管理器句柄； Dual-mode network manager handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_TIMEOUT: 等待监控任务或互斥量超时； Monitor task or mutex timeout
 */
esp_err_t network_manager_stop(network_manager_t *me);

/**
 * @brief 获取双模网络管理器状态
 * @details Get dual-mode network manager status
 * @param[in] me 双模网络管理器句柄； Dual-mode network manager handle
 * @param[out] out 状态输出； Status output
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 */
esp_err_t network_manager_get_status(network_manager_t *me,
                                      network_manager_status_t *out);

/**
 * @brief 查询双模网络管理器是否就绪
 * @details Query whether dual-mode network manager is ready
 * @param[in] me 双模网络管理器句柄； Dual-mode network manager handle
 * @param[out] out 就绪状态输出； Ready state output
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 */
esp_err_t network_manager_is_ready(network_manager_t *me, bool *out);

/**
 * @brief 发布网络消息
 * @details Publish network message
 * @param[in] me 双模网络管理器句柄； Dual-mode network manager handle
 * @param[in] req 发布请求； Publish request
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 */
esp_err_t network_manager_publish(network_manager_t *me,
                                   const network_publish_request_t *req);

/**
 * @brief 订阅主题
 * @details Subscribe topic
 * @param[in] me 双模网络管理器句柄； Dual-mode network manager handle
 * @param[in] topic MQTT 主题； MQTT topic
 * @param[in] qos MQTT 服务质量等级； MQTT QoS level
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 *         - ESP_ERR_NO_MEM: 内存不足； Out of memory
 */
esp_err_t network_manager_subscribe(network_manager_t *me,
                                     const char *topic,
                                     network_mqtt_qos_t qos);

/**
 * @brief 取消订阅主题
 * @details Unsubscribe topic
 * @param[in] me 双模网络管理器句柄； Dual-mode network manager handle
 * @param[in] topic MQTT 主题； MQTT topic
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_INVALID_STATE: 状态无效； Invalid state
 */
esp_err_t network_manager_unsubscribe(network_manager_t *me,
                                       const char *topic);

/**
 * @brief 注册下行消息回调
 * @details Register RX message callback
 * @param[in] me 双模网络管理器句柄； Dual-mode network manager handle
 * @param[in] cb 下行消息回调； RX message callback
 * @param[in] ctx 用户上下文； User context
 * @note cb 为 NULL 时，ESP_OK 保证旧回调及 ctx 已不再被使用；ESP_ERR_TIMEOUT 时旧 ctx 必须继续有效。
 *       When cb is NULL, ESP_OK guarantees the old callback and ctx are no longer used; after ESP_ERR_TIMEOUT the old ctx must remain valid.
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_TIMEOUT: 等待在途回调或互斥量超时； In-flight callback or mutex timeout
 */
esp_err_t network_manager_register_rx_cb(network_manager_t *me,
                                          network_rx_cb_t cb, void *ctx);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
