/**
 * @file main.c
 * @brief 智能插座组合根
 * @details Smart socket composition root
 * @author OpenCode
 * @date 2026-05-28
 */

/*********************
 *      INCLUDES
 *********************/

#include "app_controller.h"
#include "bl0942.h"
#include "board_pinmap.h"
#include "button.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#if CONFIG_SMART_SOCKET_LTE_ENABLED
#include "lte_link.h"
#endif
#include "lvgl_dashboard.h"
#include "metering_service.h"
#include "network_manager.h"
#include "relay.h"
#include "safety_guard.h"
#include "tft_panel.h"
#include "thingsboard_client.h"
#include "wifi_link.h"

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
 * @brief 永久空闲等待
 * @details Idle forever
 */
static void smart_socket_idle_forever(void);

/**********************
 *  STATIC VARIABLES
 **********************/

static const char *TAG = "main";

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void app_main(void)
{
    ESP_LOGI(TAG, "Smart_Socket starting...");

    esp_err_t ret = nvs_flash_init();
    if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) ||
        (ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    const board_pinmap_t *pm = board_pinmap_get();
    if (pm == NULL) {
        ESP_LOGE(TAG, "board pinmap unavailable");
        smart_socket_idle_forever();
    }

    relay_t *relay = relay_create(&(relay_config_t) {
        .ctrl_gpio = pm->relay_ctrl_gpio,
        .active_level = (relay_active_level_t)pm->relay_active_level,
    });
    button_t *button = button_create(&(button_config_t) {
        .input_gpio = pm->button_gpio,
        .active_level = (button_active_level_t)pm->button_active_level,
    });
    bl0942_t *bl0942 = bl0942_create(&(bl0942_config_t) {
        .uart_num = CONFIG_SMART_SOCKET_BL0942_UART_NUM,
        .en_gpio = pm->bl0942_en_gpio,
        .tx_gpio = pm->bl0942_tx_gpio,
        .rx_gpio = pm->bl0942_rx_gpio,
        .baud_rate = 9600,
        .device_address = 0,
        .rx_buf_size = 256,
        .read_timeout_ms = 500,
        .sample_period_ms = 1000,
        .fault_threshold = 10,
        .hard_reset_max_attempts = 3,
    });
    metering_service_t *metering = metering_service_create(&(metering_config_t) {
    });
    safety_guard_t *safety = safety_guard_create(&(safety_guard_config_t) {
        .overcurrent_threshold_a = 10.0f,
        .overpower_threshold_w = 2200.0f,
        .persistence_samples = 3,
    });
    tft_panel_t *tft = tft_panel_create(&(tft_panel_config_t) {
        .sclk_gpio = pm->tft_sclk_gpio,
        .mosi_gpio = pm->tft_mosi_gpio,
        .dc_gpio = pm->tft_dc_gpio,
        .cs_gpio = pm->tft_cs_gpio,
        .rst_gpio = pm->tft_rst_gpio,
        .bl_gpio = pm->tft_bl_gpio,
        .panel_width = CONFIG_SMART_SOCKET_PANEL_WIDTH,
        .panel_height = CONFIG_SMART_SOCKET_PANEL_HEIGHT,
    });
    if ((relay == NULL) || (button == NULL) || (bl0942 == NULL) ||
        (metering == NULL) || (safety == NULL) || (tft == NULL)) {
        ESP_LOGE(TAG, "create base module failed");
        smart_socket_idle_forever();
    }

    network_link_t *wifi = wifi_link_create(&(wifi_link_config_t) {
        .ssid = CONFIG_SMART_SOCKET_WIFI_SSID,
        .password = CONFIG_SMART_SOCKET_WIFI_PASSWORD,
        .mqtt_broker_host = CONFIG_SMART_SOCKET_TB_HOST,
        .mqtt_broker_port = CONFIG_SMART_SOCKET_TB_PORT,
        .mqtt_client_id = CONFIG_SMART_SOCKET_TB_CLIENT_ID,
        .mqtt_username = CONFIG_SMART_SOCKET_TB_TOKEN,
        .mqtt_password = NULL,
        .mqtt_keepalive_s = CONFIG_SMART_SOCKET_WIFI_MQTT_KEEPALIVE_S,
        .mqtt_clean_session = CONFIG_SMART_SOCKET_WIFI_MQTT_CLEAN_SESSION,
        .mqtt_use_tls =
#ifdef CONFIG_SMART_SOCKET_WIFI_MQTT_USE_TLS
            true
#else
            false
#endif
        ,
        .max_subscriptions = CONFIG_SMART_SOCKET_WIFI_MAX_SUBSCRIPTIONS,
        .max_topic_len = CONFIG_SMART_SOCKET_WIFI_MAX_TOPIC_LEN,
    });
    network_link_t *lte = NULL;
#if CONFIG_SMART_SOCKET_LTE_ENABLED
    lte = lte_link_create(&(lte_link_config_t) {
        .uart_num = CONFIG_SMART_SOCKET_LTE_UART_NUM,
        .tx_gpio = pm->lte_tx_gpio,
        .rx_gpio = pm->lte_rx_gpio,
        .baud_rate = 0,
        .en_gpio = pm->lte_en_gpio,
        .apn = CONFIG_SMART_SOCKET_LTE_APN,
        .auto_connect = false,
        .mqtt_enabled = true,
        .mqtt_broker_host = CONFIG_SMART_SOCKET_TB_HOST,
        .mqtt_broker_port = CONFIG_SMART_SOCKET_TB_PORT,
        .mqtt_client_id = CONFIG_SMART_SOCKET_TB_CLIENT_ID,
        .mqtt_username = CONFIG_SMART_SOCKET_TB_TOKEN,
        .mqtt_password = NULL,
        .mqtt_keepalive_s = CONFIG_SMART_SOCKET_LTE_MQTT_KEEPALIVE_S,
        .mqtt_clean_session = CONFIG_SMART_SOCKET_LTE_MQTT_CLEAN_SESSION,
        .init_ready_timeout_ms = CONFIG_SMART_SOCKET_LTE_INIT_READY_TIMEOUT_MS,
        .net_activate_timeout_ms = CONFIG_SMART_SOCKET_LTE_NET_ACTIVATE_TIMEOUT_MS,
        .max_subscriptions = CONFIG_SMART_SOCKET_LTE_MAX_SUBSCRIPTIONS,
        .max_topic_len = CONFIG_SMART_SOCKET_LTE_MAX_TOPIC_LEN,
    });
#endif
    network_manager_t *net_mgr = network_manager_create(&(network_manager_config_t) {
        .primary = wifi,
        .backup = lte,
        .preferred_primary = NETWORK_LINK_TYPE_WIFI,
        .failover_recheck_ms = CONFIG_SMART_SOCKET_FAILOVER_RECHECK_MS,
        .failback_delay_ms = CONFIG_SMART_SOCKET_FAILBACK_DELAY_MS,
        .max_subscriptions = CONFIG_SMART_SOCKET_NET_MGR_MAX_SUBSCRIPTIONS,
    });
    thingsboard_client_t *tb = thingsboard_client_create(&(tb_client_config_t) {
        .net_mgr = net_mgr,
        .device_token = CONFIG_SMART_SOCKET_TB_TOKEN,
        .enable_rpc = CONFIG_SMART_SOCKET_TB_ENABLE_RPC,
        .enable_attributes = CONFIG_SMART_SOCKET_TB_ENABLE_ATTRIBUTES,
        .json_buf_size = CONFIG_SMART_SOCKET_TB_JSON_BUF_SIZE,
    });
    if ((wifi == NULL) || (net_mgr == NULL) || (tb == NULL)
#if CONFIG_SMART_SOCKET_LTE_ENABLED
        || (lte == NULL)
#endif
    ) {
        ESP_LOGE(TAG, "create network/cloud module failed");
        smart_socket_idle_forever();
    }

    lvgl_dashboard_t *dashboard = lvgl_dashboard_create(&(lvgl_dashboard_config_t) {
        .panel = tft,
        .network_manager = net_mgr,
        .lvgl_task_stack = 6144,
        .lvgl_task_priority = 4,
        .lvgl_tick_period_ms = 10,
        .update_period_ms = 50,
    });
    app_controller_t *app = app_controller_create(&(app_controller_config_t) {
        .event_loop = NULL,
        .pinmap = pm,
        .relay = relay,
        .button = button,
        .bl0942 = bl0942,
        .tft_panel = tft,
        .metering = metering,
        .safety = safety,
        .tb = tb,
        .net_mgr = net_mgr,
        .dashboard = dashboard,
    });
    if ((dashboard == NULL) || (app == NULL)) {
        ESP_LOGE(TAG, "create app/display module failed");
        smart_socket_idle_forever();
    }

    ret = app_controller_start(app);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "start app controller failed: %s", esp_err_to_name(ret));
        smart_socket_idle_forever();
    }
    ESP_LOGI(TAG, "Smart_Socket started");
    smart_socket_idle_forever();
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void smart_socket_idle_forever(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
