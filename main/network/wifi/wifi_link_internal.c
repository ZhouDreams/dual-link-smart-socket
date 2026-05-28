/**
 * @file wifi_link_internal.c
 * @brief Wi-Fi 链路内部辅助实现
 * @details Wi-Fi link internal helper implementation
 * @author OpenCode
 * @date 2026-05-28
 */

/*********************
 *      INCLUDES
 *********************/

#include "wifi_link_internal.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

esp_err_t wifi_link_internal_publish_mqtt(esp_mqtt_client_handle_t client,
                                          const network_publish_request_t *req)
{
    const int msg_id = esp_mqtt_client_publish(
        client, req->topic, (const char *)req->payload, (int)req->payload_len,
        (int)req->qos, req->retain ? 1 : 0);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
