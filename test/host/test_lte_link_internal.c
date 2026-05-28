#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "lte_link_internal.h"

static void test_status_mapping(void)
{
    assert(lte_link_internal_map_status(LWLTE_STATE_STOPPED,
                                        LWLTE_MQTT_STATE_STOPPED,
                                        true,
                                        true) == NETWORK_LINK_STATUS_IDLE);
    assert(lte_link_internal_map_status(LWLTE_STATE_STARTING,
                                        LWLTE_MQTT_STATE_STOPPED,
                                        true,
                                        true) == NETWORK_LINK_STATUS_STARTING);
    assert(lte_link_internal_map_status(LWLTE_STATE_READY,
                                        LWLTE_MQTT_STATE_STOPPED,
                                        true,
                                        true) == NETWORK_LINK_STATUS_CONNECTING);
    assert(lte_link_internal_map_status(LWLTE_STATE_NET_ACTIVATING,
                                        LWLTE_MQTT_STATE_STOPPED,
                                        true,
                                        true) == NETWORK_LINK_STATUS_CONNECTING);
    assert(lte_link_internal_map_status(LWLTE_STATE_ONLINE,
                                        LWLTE_MQTT_STATE_CONNECTED,
                                        true,
                                        true) == NETWORK_LINK_STATUS_READY);
    assert(lte_link_internal_map_status(LWLTE_STATE_ONLINE,
                                        LWLTE_MQTT_STATE_CONNECTING,
                                        true,
                                        true) == NETWORK_LINK_STATUS_DEGRADED);
    assert(lte_link_internal_map_status(LWLTE_STATE_ONLINE,
                                        LWLTE_MQTT_STATE_CONNECTED,
                                        false,
                                        true) == NETWORK_LINK_STATUS_DEGRADED);
    assert(lte_link_internal_map_status(LWLTE_STATE_ERROR,
                                        LWLTE_MQTT_STATE_ERROR,
                                        true,
                                        true) == NETWORK_LINK_STATUS_ERROR);
    assert(lte_link_internal_map_status(LWLTE_STATE_ONLINE,
                                        LWLTE_MQTT_STATE_CONNECTED,
                                        true,
                                        false) == NETWORK_LINK_STATUS_ERROR);
}

static void test_qos_validation(void)
{
    assert(lte_link_internal_is_valid_qos(NETWORK_MQTT_QOS0));
    assert(lte_link_internal_is_valid_qos(NETWORK_MQTT_QOS1));
    assert(lte_link_internal_is_valid_qos(NETWORK_MQTT_QOS2));
    assert(!lte_link_internal_is_valid_qos((network_mqtt_qos_t)-1));
    assert(!lte_link_internal_is_valid_qos((network_mqtt_qos_t)3));
}

static void test_subscription_validation(void)
{
    lte_link_sub_entry_t table[1] = {0};
    int found_index = -2;
    int free_index = -2;

    assert(lte_link_internal_find_subscription(NULL, 1, "a", &found_index,
                                               &free_index) == ESP_ERR_INVALID_ARG);
    assert(lte_link_internal_find_subscription(table, 0, "a", &found_index,
                                               &free_index) == ESP_ERR_INVALID_ARG);
    assert(lte_link_internal_find_subscription(table, 1, NULL, &found_index,
                                               &free_index) == ESP_ERR_INVALID_ARG);
    assert(lte_link_internal_find_subscription(table, 1, "a", NULL,
                                               &free_index) == ESP_ERR_INVALID_ARG);
    assert(lte_link_internal_find_subscription(table, 1, "a", &found_index,
                                               NULL) == ESP_ERR_INVALID_ARG);

    assert(lte_link_internal_store_subscription(NULL, 1, "a",
                                                NETWORK_MQTT_QOS0, 8) == ESP_ERR_INVALID_ARG);
    assert(lte_link_internal_store_subscription(table, 0, "a",
                                                NETWORK_MQTT_QOS0, 8) == ESP_ERR_INVALID_ARG);
    assert(lte_link_internal_store_subscription(table, 1, NULL,
                                                NETWORK_MQTT_QOS0, 8) == ESP_ERR_INVALID_ARG);
    assert(lte_link_internal_store_subscription(table, 1, "",
                                                NETWORK_MQTT_QOS0, 8) == ESP_ERR_INVALID_ARG);
    assert(lte_link_internal_store_subscription(table, 1, "a",
                                                (network_mqtt_qos_t)3, 8) == ESP_ERR_INVALID_ARG);
    assert(lte_link_internal_store_subscription(table, 1, "a",
                                                NETWORK_MQTT_QOS0, 0) == ESP_ERR_INVALID_ARG);
    assert(lte_link_internal_store_subscription(table, 1, "abcd",
                                                NETWORK_MQTT_QOS0, 4) == ESP_ERR_INVALID_SIZE);
    assert(lte_link_internal_remove_subscription(table, 1, "") == ESP_ERR_INVALID_ARG);
}

static void test_subscription_table(void)
{
    lte_link_sub_entry_t table[2] = {0};
    int found_index = -2;
    int free_index = -2;

    assert(lte_link_internal_store_subscription(table, 2, "a",
                                                NETWORK_MQTT_QOS0, 8) == ESP_OK);
    assert(table[0].in_use);
    assert(strcmp(table[0].topic, "a") == 0);
    assert(table[0].qos == NETWORK_MQTT_QOS0);

    assert(lte_link_internal_store_subscription(table, 2, "a",
                                                NETWORK_MQTT_QOS2, 8) == ESP_OK);
    assert(table[0].qos == NETWORK_MQTT_QOS2);

    assert(lte_link_internal_store_subscription(table, 2, "b",
                                                NETWORK_MQTT_QOS1, 8) == ESP_OK);
    assert(table[1].in_use);
    assert(strcmp(table[1].topic, "b") == 0);
    assert(table[1].qos == NETWORK_MQTT_QOS1);

    assert(lte_link_internal_find_subscription(table, 2, "b", &found_index,
                                               &free_index) == ESP_OK);
    assert(found_index == 1);
    assert(free_index == -1);

    assert(lte_link_internal_store_subscription(table, 2, "c",
                                                NETWORK_MQTT_QOS0, 8) == ESP_ERR_NO_MEM);
    assert(lte_link_internal_remove_subscription(table, 2, "a") == ESP_OK);
    assert(!table[0].in_use);
    assert(table[0].topic == NULL);
    assert(lte_link_internal_remove_subscription(table, 2, "a") == ESP_ERR_NOT_FOUND);

    lte_link_internal_clear_subscriptions(table, 2);
    assert(!table[0].in_use);
    assert(table[0].topic == NULL);
    assert(!table[1].in_use);
    assert(table[1].topic == NULL);
}

int main(void)
{
    test_status_mapping();
    test_qos_validation();
    test_subscription_validation();
    test_subscription_table();

    printf("lte internal tests passed\n");

    return 0;
}
