/**
 * @file wifi_link.h
 * @brief Wi-Fi 链路子类接口
 * @details Wi-Fi link subclass interface
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

#include "network_link.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief Wi-Fi 链路配置
 * @details Wi-Fi link configuration
 */
typedef struct {
    const char *ssid;             /**< Wi-Fi SSID； Wi-Fi SSID */
    const char *password;         /**< Wi-Fi 密码； Wi-Fi password */
    const char *mqtt_broker_host; /**< MQTT 服务器主机； MQTT broker host */
    uint16_t mqtt_broker_port;    /**< MQTT 服务器端口； MQTT broker port */
    const char *mqtt_client_id;   /**< MQTT 客户端 ID； MQTT client ID */
    const char *mqtt_username;    /**< MQTT 用户名； MQTT username */
    const char *mqtt_password;    /**< MQTT 密码； MQTT password */
    uint16_t mqtt_keepalive_s;    /**< MQTT 保活时间（秒）； MQTT keepalive in seconds */
    bool mqtt_clean_session;      /**< MQTT 清理会话标志； MQTT clean session flag */
    bool mqtt_use_tls;            /**< MQTT TLS 启用标志； MQTT TLS enable flag */
    int max_subscriptions;        /**< 最大订阅数量； Maximum subscription count */
    int max_topic_len;            /**< 最大主题长度； Maximum topic length */
} wifi_link_config_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 创建 Wi-Fi 链路
 * @details Create Wi-Fi link
 * @param[in] config Wi-Fi 链路配置； Wi-Fi link configuration
 * @return 网络链路句柄，失败返回 NULL； Network link handle, or NULL on failure
 */
network_link_t *wifi_link_create(const wifi_link_config_t *config);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
