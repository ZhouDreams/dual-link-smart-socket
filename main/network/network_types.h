/**
 * @file network_types.h
 * @brief 网络公共类型
 * @details Network shared types
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
#include <stddef.h>
#include <stdint.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 网络链路类型
 * @details Network link type
 */
typedef enum {
    NETWORK_LINK_TYPE_NONE = 0,  /**< 无链路； No link */
    NETWORK_LINK_TYPE_WIFI,      /**< Wi-Fi 链路； Wi-Fi link */
    NETWORK_LINK_TYPE_LTE,       /**< LTE 链路； LTE link */
} network_link_type_t;

/**
 * @brief 网络链路状态
 * @details Network link status
 */
typedef enum {
    NETWORK_LINK_STATUS_IDLE = 0,   /**< 空闲； Idle */
    NETWORK_LINK_STATUS_STARTING,   /**< 启动中； Starting */
    NETWORK_LINK_STATUS_CONNECTING, /**< 连接中； Connecting */
    NETWORK_LINK_STATUS_DEGRADED,   /**< 降级可用； Degraded */
    NETWORK_LINK_STATUS_READY,      /**< 就绪； Ready */
    NETWORK_LINK_STATUS_ERROR,      /**< 错误； Error */
} network_link_status_t;

/**
 * @brief MQTT 服务质量等级
 * @details MQTT quality of service level
 */
typedef enum {
    NETWORK_MQTT_QOS0 = 0, /**< 最多一次； At most once */
    NETWORK_MQTT_QOS1 = 1, /**< 至少一次； At least once */
    NETWORK_MQTT_QOS2 = 2, /**< 仅一次； Exactly once */
} network_mqtt_qos_t;

/**
 * @brief 网络发布请求
 * @details Network publish request
 */
typedef struct {
    const char *topic;          /**< MQTT 主题； MQTT topic */
    const void *payload;        /**< 发布负载； Publish payload */
    size_t payload_len;         /**< 发布负载长度； Publish payload length */
    network_mqtt_qos_t qos;     /**< MQTT 服务质量等级； MQTT QoS level */
    bool retain;                /**< 保留消息标志； Retain message flag */
} network_publish_request_t;

/**
 * @brief 网络接收数据
 * @details Network received data
 */
typedef struct {
    const char *topic; /**< MQTT 主题； MQTT topic */
    const char *data;  /**< 接收数据； Received data */
    int data_len;      /**< 接收数据长度； Received data length */
} network_rx_data_t;

/**
 * @brief 网络下行数据回调
 * @details Network RX data callback
 * @param[in] rx_data 接收数据； Received data
 * @param[in] user_ctx 用户上下文； User context
 */
typedef void (*network_rx_cb_t)(const network_rx_data_t *rx_data, void *user_ctx);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
