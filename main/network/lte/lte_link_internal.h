/**
 * @file lte_link_internal.h
 * @brief LTE 链路内部辅助接口
 * @details LTE link internal helper interface
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

#include "esp_err.h"
#include "lwlte.h"
#include "network_types.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief LTE 订阅表项
 * @details LTE subscription table entry
 */
typedef struct {
    char *topic;            /**< MQTT 主题； MQTT topic */
    network_mqtt_qos_t qos; /**< MQTT 服务质量等级； MQTT QoS level */
    bool in_use;            /**< 使用标志； In-use flag */
} lte_link_sub_entry_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 检查 MQTT QoS 是否有效
 * @details Check whether MQTT QoS is valid
 * @param[in] qos MQTT 服务质量等级
 * @return true 有效； false 无效
 */
bool lte_link_internal_is_valid_qos(network_mqtt_qos_t qos);

/**
 * @brief 映射 LTE 和 MQTT 状态
 * @details Map LTE and MQTT states to network link status
 * @param[in] lte_state LTE 状态
 * @param[in] mqtt_state MQTT 状态
 * @param[in] mqtt_enabled MQTT 是否启用
 * @param[in] query_ok 状态查询是否成功
 * @return 网络链路状态
 */
network_link_status_t lte_link_internal_map_status(lwlte_state_t lte_state,
                                                   lwlte_mqtt_state_t mqtt_state,
                                                   bool mqtt_enabled,
                                                   bool query_ok);

/**
 * @brief 查找订阅表项
 * @details Find subscription entry
 * @param[in,out] table 订阅表
 * @param[in] table_size 订阅表大小
 * @param[in] topic MQTT 主题
 * @param[out] out_index 匹配表项索引
 * @param[out] out_free_index 首个空闲表项索引
 * @return
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t lte_link_internal_find_subscription(lte_link_sub_entry_t *table,
                                              int table_size,
                                              const char *topic,
                                              int *out_index,
                                              int *out_free_index);

/**
 * @brief 存储订阅表项
 * @details Store subscription entry
 * @param[in,out] table 订阅表
 * @param[in] table_size 订阅表大小
 * @param[in] topic MQTT 主题
 * @param[in] qos MQTT 服务质量等级
 * @param[in] max_topic_len 最大主题长度
 * @return
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_INVALID_SIZE: 主题过长
 *         - ESP_ERR_NO_MEM: 表已满或内存不足
 */
esp_err_t lte_link_internal_store_subscription(lte_link_sub_entry_t *table,
                                               int table_size,
                                               const char *topic,
                                               network_mqtt_qos_t qos,
                                               int max_topic_len);

/**
 * @brief 删除订阅表项
 * @details Remove subscription entry
 * @param[in,out] table 订阅表
 * @param[in] table_size 订阅表大小
 * @param[in] topic MQTT 主题
 * @return
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_NOT_FOUND: 未找到
 */
esp_err_t lte_link_internal_remove_subscription(lte_link_sub_entry_t *table,
                                                int table_size,
                                                const char *topic);

/**
 * @brief 清空订阅表
 * @details Clear subscription table
 * @param[in,out] table 订阅表
 * @param[in] table_size 订阅表大小
 */
void lte_link_internal_clear_subscriptions(lte_link_sub_entry_t *table,
                                           int table_size);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
