/**
 * @file wifi_link_internal.h
 * @brief Wi-Fi 链路内部辅助接口
 * @details Wi-Fi link internal helper interface
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

#include "esp_err.h"
#include "mqtt_client.h"
#include "network_types.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 发布 MQTT 消息
 * @details Publish MQTT message
 * @param[in] client MQTT 客户端； MQTT client
 * @param[in] req 发布请求； Publish request
 * @return
 *         - ESP_OK: 成功； Success
 *         - ESP_FAIL: 发布失败； Publish failed
 */
esp_err_t wifi_link_internal_publish_mqtt(esp_mqtt_client_handle_t client,
                                          const network_publish_request_t *req);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
