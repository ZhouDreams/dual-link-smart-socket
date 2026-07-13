/**
 * @file network_link.h
 * @brief 网络链路基类接口
 * @details Network link base interface
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

#include "esp_err.h"
#include "network_types.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 网络链路句柄
 * @details Network link handle
 */
typedef struct network_link network_link_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 销毁网络链路
 * @details Destroy network link
 * @note 仅 ESP_OK 表示句柄已被释放；任何错误返回都保留句柄所有权，调用方不得释放其内存。
 *       Only ESP_OK consumes the handle; any error preserves caller ownership and the caller must not free its memory.
 * @note 调用方必须在外部串行化 destroy 与同一句柄的所有其它访问，包括另一次 destroy。
 *       The caller must externally serialize destroy against every other access to the same handle, including another destroy.
 * @param[in] me 网络链路句柄； Network link handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_NOT_SUPPORTED: 不支持销毁； Destroy not supported
 *         - ESP_ERR_TIMEOUT: 等待在途操作或回调退出超时； In-flight operation or callback drain timed out
 */
esp_err_t network_link_destroy(network_link_t *me);

/**
 * @brief 启动网络链路
 * @details Start network link
 * @param[in] me 网络链路句柄； Network link handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_NOT_SUPPORTED: 不支持启动； Start not supported
 */
esp_err_t network_link_start(network_link_t *me);

/**
 * @brief 停止网络链路
 * @details Stop network link
 * @param[in] me 网络链路句柄； Network link handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_NOT_SUPPORTED: 不支持停止； Stop not supported
 */
esp_err_t network_link_stop(network_link_t *me);

/**
 * @brief 获取网络链路状态
 * @details Get network link status
 * @param[in] me 网络链路句柄； Network link handle
 * @param[out] out 状态输出； Status output
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_NOT_SUPPORTED: 不支持状态查询； Get status not supported
 */
esp_err_t network_link_get_status(const network_link_t *me,
                                  network_link_status_t *out);

/**
 * @brief 发布网络消息
 * @details Publish network message
 * @param[in] me 网络链路句柄； Network link handle
 * @param[in] req 发布请求； Publish request
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_NOT_SUPPORTED: 不支持发布； Publish not supported
 */
esp_err_t network_link_publish(network_link_t *me,
                               const network_publish_request_t *req);

/**
 * @brief 订阅主题
 * @details Subscribe topic
 * @param[in] me 网络链路句柄； Network link handle
 * @param[in] topic MQTT 主题； MQTT topic
 * @param[in] qos MQTT 服务质量等级； MQTT QoS level
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_NOT_SUPPORTED: 不支持订阅； Subscribe not supported
 */
esp_err_t network_link_subscribe(network_link_t *me, const char *topic,
                                 network_mqtt_qos_t qos);

/**
 * @brief 取消订阅主题
 * @details Unsubscribe topic
 * @param[in] me 网络链路句柄； Network link handle
 * @param[in] topic MQTT 主题； MQTT topic
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_NOT_SUPPORTED: 不支持取消订阅； Unsubscribe not supported
 */
esp_err_t network_link_unsubscribe(network_link_t *me, const char *topic);

/**
 * @brief 注册下行消息回调
 * @details Register RX message callback
 * @note cb 为 NULL 时，ESP_OK 保证旧回调及 ctx 已不再被使用；ESP_ERR_TIMEOUT 时旧 ctx 必须继续有效。
 *       When cb is NULL, ESP_OK guarantees the old callback and ctx are no longer used; after ESP_ERR_TIMEOUT the old ctx must remain valid.
 * @param[in] me 网络链路句柄； Network link handle
 * @param[in] cb 下行消息回调； RX message callback
 * @param[in] ctx 用户上下文； User context
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_NOT_SUPPORTED: 不支持回调注册； Register callback not supported
 *         - ESP_ERR_TIMEOUT: 等待在途回调超时； In-flight callback drain timed out
 */
esp_err_t network_link_register_rx_cb(network_link_t *me,
                                      network_rx_cb_t cb, void *ctx);

/**
 * @brief 设置链路上岗/卸岗状态
 * @details Set link active/inactive role
 * @note 选填方法：子类未实现（ops->set_active 为 NULL）时返回 ESP_OK，按 no-op 处理。
 * @param[in] me 网络链路句柄； Network link handle
 * @param[in] active true=上岗（冲向 READY），false=卸岗（退回值守态）； true=engage, false=disengage
 * @return
 *         - ESP_OK: 成功或子类未实现（no-op）； Success or not implemented (no-op)
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 */
esp_err_t network_link_set_active(network_link_t *me, bool active);

/**
 * @brief 获取网络链路类型
 * @details Get network link type
 * @param[in] me 网络链路句柄； Network link handle
 * @return 网络链路类型； Network link type
 */
network_link_type_t network_link_get_type(const network_link_t *me);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
