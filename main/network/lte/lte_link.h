/**
 * @file lte_link.h
 * @brief LTE 链路子类接口
 * @details LTE link subclass interface
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
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "network_link.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief LTE 链路配置
 * @details LTE link configuration
 */
typedef struct {
    uart_port_t uart_num;              /**< UART 端口号； UART port number */
    gpio_num_t tx_gpio;                /**< UART TX GPIO； UART TX GPIO */
    gpio_num_t rx_gpio;                /**< UART RX GPIO； UART RX GPIO */
    int baud_rate;                     /**< UART 波特率，<=0 使用默认值； UART baud rate, <=0 uses default */
    gpio_num_t en_gpio;                /**< 模组 EN GPIO，GPIO_NUM_NC 表示不控制； Module EN GPIO, GPIO_NUM_NC disables control */
    const char *apn;                   /**< APN 字符串，可为 NULL； APN string, nullable */
    bool mqtt_enabled;                 /**< MQTT 启用标志； MQTT enable flag */
    const char *mqtt_broker_host;      /**< MQTT 服务器主机； MQTT broker host */
    uint16_t mqtt_broker_port;         /**< MQTT 服务器端口； MQTT broker port */
    const char *mqtt_client_id;        /**< MQTT 客户端 ID； MQTT client ID */
    const char *mqtt_username;         /**< MQTT 用户名，可为 NULL； MQTT username, nullable */
    const char *mqtt_password;         /**< MQTT 密码，可为 NULL； MQTT password, nullable */
    uint16_t mqtt_keepalive_s;         /**< MQTT 保活时间（秒）； MQTT keepalive in seconds */
    bool mqtt_clean_session;           /**< MQTT 清理会话标志； MQTT clean session flag */
    uint32_t init_ready_timeout_ms;    /**< 初始化就绪超时（毫秒）； Init ready timeout in milliseconds */
    uint32_t net_activate_timeout_ms;  /**< 网络激活超时（毫秒）； Network activation timeout in milliseconds */
    int max_subscriptions;             /**< 最大订阅数量，<=0 使用默认值； Maximum subscription count, <=0 uses default */
    int max_topic_len;                 /**< 最大主题长度，<=0 使用默认值； Maximum topic length, <=0 uses default */
} lte_link_config_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 创建 LTE 链路
 * @details Create LTE link
 * @param[in] config LTE 链路配置； LTE link configuration
 * @return 网络链路句柄，失败返回 NULL； Network link handle, or NULL on failure
 */
network_link_t *lte_link_create(const lte_link_config_t *config);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
