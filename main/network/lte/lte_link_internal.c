/**
 * @file lte_link_internal.c
 * @brief LTE 链路内部辅助实现
 * @details LTE link internal helper implementation
 * @author OpenCode
 * @date 2026-05-28
 */

/*********************
 *      INCLUDES
 *********************/

#include "lte_link_internal.h"

#include <stdlib.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**
 * @brief 复制字符串
 * @details Duplicate string
 * @param[in] str 源字符串
 * @return 复制后的字符串，失败返回 NULL
 */
static char *lte_link_internal_strdup(const char *str);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool lte_link_internal_is_valid_qos(network_mqtt_qos_t qos)
{
    return qos == NETWORK_MQTT_QOS0 || qos == NETWORK_MQTT_QOS1 ||
           qos == NETWORK_MQTT_QOS2;
}

network_link_status_t lte_link_internal_map_status(lwlte_state_t lte_state,
                                                   lwlte_mqtt_state_t mqtt_state,
                                                   bool mqtt_enabled,
                                                   bool query_ok)
{
    if (!query_ok) {
        return NETWORK_LINK_STATUS_ERROR;
    }

    switch (lte_state) {
    case LWLTE_STATE_STOPPED:
        return NETWORK_LINK_STATUS_IDLE;
    case LWLTE_STATE_STARTING:
        return NETWORK_LINK_STATUS_STARTING;
    case LWLTE_STATE_READY:
    case LWLTE_STATE_NET_ACTIVATING:
        return NETWORK_LINK_STATUS_CONNECTING;
    case LWLTE_STATE_ONLINE:
        if (!mqtt_enabled) {
            return NETWORK_LINK_STATUS_DEGRADED;
        }
        return mqtt_state == LWLTE_MQTT_STATE_CONNECTED ? NETWORK_LINK_STATUS_READY :
                                                          NETWORK_LINK_STATUS_DEGRADED;
    default:
        return NETWORK_LINK_STATUS_ERROR;
    }
}

esp_err_t lte_link_internal_find_subscription(lte_link_sub_entry_t *table,
                                              int table_size,
                                              const char *topic,
                                              int *out_index,
                                              int *out_free_index)
{
    if (!table || table_size <= 0 || !topic || !out_index || !out_free_index) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_index = -1;
    *out_free_index = -1;

    for (int i = 0; i < table_size; i++) {
        if (table[i].in_use) {
            if (table[i].topic && strcmp(table[i].topic, topic) == 0) {
                *out_index = i;
            }
            continue;
        }

        if (*out_free_index < 0) {
            *out_free_index = i;
        }
    }

    return ESP_OK;
}

esp_err_t lte_link_internal_store_subscription(lte_link_sub_entry_t *table,
                                               int table_size,
                                               const char *topic,
                                               network_mqtt_qos_t qos,
                                               int max_topic_len)
{
    int found_index = -1;
    int free_index = -1;

    if (!table || table_size <= 0 || !topic || topic[0] == '\0' ||
        !lte_link_internal_is_valid_qos(qos) || max_topic_len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(topic) >= (size_t)max_topic_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = lte_link_internal_find_subscription(table, table_size, topic,
                                                        &found_index, &free_index);
    if (err != ESP_OK) {
        return err;
    }

    if (found_index >= 0) {
        table[found_index].qos = qos;
        return ESP_OK;
    }

    if (free_index < 0) {
        return ESP_ERR_NO_MEM;
    }

    table[free_index].topic = lte_link_internal_strdup(topic);
    if (!table[free_index].topic) {
        return ESP_ERR_NO_MEM;
    }

    table[free_index].qos = qos;
    table[free_index].in_use = true;

    return ESP_OK;
}

esp_err_t lte_link_internal_remove_subscription(lte_link_sub_entry_t *table,
                                                int table_size,
                                                const char *topic)
{
    int found_index = -1;
    int free_index = -1;

    if (!table || table_size <= 0 || !topic || topic[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = lte_link_internal_find_subscription(table, table_size, topic,
                                                        &found_index, &free_index);
    if (err != ESP_OK) {
        return err;
    }

    if (found_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    free(table[found_index].topic);
    table[found_index].topic = NULL;
    table[found_index].qos = NETWORK_MQTT_QOS0;
    table[found_index].in_use = false;

    return ESP_OK;
}

void lte_link_internal_clear_subscriptions(lte_link_sub_entry_t *table,
                                           int table_size)
{
    if (!table || table_size <= 0) {
        return;
    }

    for (int i = 0; i < table_size; i++) {
        free(table[i].topic);
        table[i].topic = NULL;
        table[i].qos = NETWORK_MQTT_QOS0;
        table[i].in_use = false;
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static char *lte_link_internal_strdup(const char *str)
{
    size_t len = strlen(str) + 1U;
    char *copy = malloc(len);

    if (!copy) {
        return NULL;
    }

    memcpy(copy, str, len);

    return copy;
}
