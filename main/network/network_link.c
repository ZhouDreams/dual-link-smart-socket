/**
 * @file network_link.c
 * @brief 网络链路基类接口实现
 * @details Network link base interface implementation
 * @author OpenCode
 * @date 2026-05-24
 */

/*********************
 *      INCLUDES
 *********************/

#include "network_link_priv.h"

#include "esp_check.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "network_link"

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

esp_err_t network_link_destroy(network_link_t *me)
{
    if (me == NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(me->ops != NULL && me->ops->destroy != NULL,
                        ESP_ERR_NOT_SUPPORTED, TAG, "destroy not supported");
    return me->ops->destroy(me);
}

esp_err_t network_link_start(network_link_t *me)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");
    ESP_RETURN_ON_FALSE(me->ops != NULL && me->ops->start != NULL,
                        ESP_ERR_NOT_SUPPORTED, TAG, "start not supported");
    return me->ops->start(me);
}

esp_err_t network_link_stop(network_link_t *me)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");
    ESP_RETURN_ON_FALSE(me->ops != NULL && me->ops->stop != NULL,
                        ESP_ERR_NOT_SUPPORTED, TAG, "stop not supported");
    return me->ops->stop(me);
}

esp_err_t network_link_get_status(const network_link_t *me,
                                  network_link_status_t *out)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");
    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "status output is null");
    ESP_RETURN_ON_FALSE(me->ops != NULL && me->ops->get_status != NULL,
                        ESP_ERR_NOT_SUPPORTED, TAG, "get status not supported");
    return me->ops->get_status((network_link_t *)me, out);
}

esp_err_t network_link_publish(network_link_t *me,
                               const network_publish_request_t *req)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");
    ESP_RETURN_ON_FALSE(req != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "publish request is null");
    ESP_RETURN_ON_FALSE(me->ops != NULL && me->ops->publish != NULL,
                        ESP_ERR_NOT_SUPPORTED, TAG, "publish not supported");
    return me->ops->publish(me, req);
}

esp_err_t network_link_subscribe(network_link_t *me, const char *topic,
                                 network_mqtt_qos_t qos)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");
    ESP_RETURN_ON_FALSE(topic != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "topic is null");
    ESP_RETURN_ON_FALSE(me->ops != NULL && me->ops->subscribe != NULL,
                        ESP_ERR_NOT_SUPPORTED, TAG, "subscribe not supported");
    return me->ops->subscribe(me, topic, qos);
}

esp_err_t network_link_unsubscribe(network_link_t *me, const char *topic)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");
    ESP_RETURN_ON_FALSE(topic != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "topic is null");
    ESP_RETURN_ON_FALSE(me->ops != NULL && me->ops->unsubscribe != NULL,
                        ESP_ERR_NOT_SUPPORTED, TAG, "unsubscribe not supported");
    return me->ops->unsubscribe(me, topic);
}

esp_err_t network_link_register_rx_cb(network_link_t *me,
                                      network_rx_cb_t cb, void *ctx)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");
    ESP_RETURN_ON_FALSE(me->ops != NULL && me->ops->register_rx_cb != NULL,
                        ESP_ERR_NOT_SUPPORTED, TAG,
                        "register rx callback not supported");
    return me->ops->register_rx_cb(me, cb, ctx);
}

network_link_type_t network_link_get_type(const network_link_t *me)
{
    if (me == NULL) {
        return NETWORK_LINK_TYPE_NONE;
    }

    return me->type;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
