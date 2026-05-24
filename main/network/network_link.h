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
 * @param[in] me 网络链路句柄； Network link handle
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_NOT_SUPPORTED: 不支持销毁； Destroy not supported
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
 * @param[in] me 网络链路句柄； Network link handle
 * @param[in] cb 下行消息回调； RX message callback
 * @param[in] ctx 用户上下文； User context
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_ERR_INVALID_ARG: 参数无效； Invalid argument
 *         - ESP_ERR_NOT_SUPPORTED: 不支持回调注册； Register callback not supported
 */
esp_err_t network_link_register_rx_cb(network_link_t *me,
                                      network_rx_cb_t cb, void *ctx);

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
