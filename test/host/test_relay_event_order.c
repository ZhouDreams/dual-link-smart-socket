#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/semphr.h"

typedef struct host_test_mutex {
    bool locked;
    bool deleted;
} host_test_mutex_t;

static host_test_mutex_t *s_last_created_mutex = NULL;
static esp_err_t s_next_gpio_set_ret = ESP_OK;
static esp_err_t s_next_event_post_ret = ESP_OK;
static int s_event_post_calls = 0;
static bool s_event_post_observed_mutex_locked = false;
static void *s_tracked_relay = NULL;
static bool s_event_post_observed_relay_on = false;
static bool s_event_post_observed_relay_on_valid = false;
static unsigned char s_last_event_data[64];

static bool host_test_read_tracked_relay_on(void);

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    s_last_created_mutex = (host_test_mutex_t *)calloc(1, sizeof(host_test_mutex_t));
    return (SemaphoreHandle_t)s_last_created_mutex;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t ticks_to_wait)
{
    host_test_mutex_t *host_mutex = (host_test_mutex_t *)mutex;

    (void)ticks_to_wait;
    assert(host_mutex != NULL);
    assert(host_mutex->deleted == false);
    assert(host_mutex->locked == false);
    host_mutex->locked = true;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex)
{
    host_test_mutex_t *host_mutex = (host_test_mutex_t *)mutex;

    assert(host_mutex != NULL);
    assert(host_mutex->deleted == false);
    assert(host_mutex->locked == true);
    host_mutex->locked = false;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t mutex)
{
    host_test_mutex_t *host_mutex = (host_test_mutex_t *)mutex;

    if (host_mutex == NULL) {
        return;
    }

    host_mutex->deleted = true;
    free(host_mutex);
    if (host_mutex == s_last_created_mutex) {
        s_last_created_mutex = NULL;
    }
}

esp_err_t gpio_config(const gpio_config_t *config)
{
    (void)config;
    return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    (void)gpio_num;
    (void)level;
    return s_next_gpio_set_ret;
}

esp_err_t esp_event_post(esp_event_base_t event_base, int32_t event_id,
                         const void *event_data, size_t event_data_size,
                         uint32_t ticks_to_wait)
{
    (void)event_base;
    (void)event_id;
    (void)ticks_to_wait;

    s_event_post_calls++;
    s_event_post_observed_mutex_locked =
        (s_last_created_mutex != NULL) && s_last_created_mutex->locked;
    if (s_tracked_relay != NULL) {
        s_event_post_observed_relay_on = host_test_read_tracked_relay_on();
        s_event_post_observed_relay_on_valid = true;
    }
    memset(s_last_event_data, 0, sizeof(s_last_event_data));
    if (event_data != NULL && event_data_size <= sizeof(s_last_event_data)) {
        memcpy(s_last_event_data, event_data, event_data_size);
    }

    return s_next_event_post_ret;
}

const char *esp_err_to_name(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return "ESP_OK";
    case ESP_FAIL:
        return "ESP_FAIL";
    case ESP_ERR_INVALID_ARG:
        return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE:
        return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_TIMEOUT:
        return "ESP_ERR_TIMEOUT";
    default:
        return "ESP_ERR_UNKNOWN";
    }
}

#include "relay.c"

static void reset_spies(void)
{
    (void)host_test_read_tracked_relay_on;

    s_next_gpio_set_ret = ESP_OK;
    s_next_event_post_ret = ESP_OK;
    s_event_post_calls = 0;
    s_event_post_observed_mutex_locked = false;
    s_tracked_relay = NULL;
    s_event_post_observed_relay_on = false;
    s_event_post_observed_relay_on_valid = false;
    memset(s_last_event_data, 0, sizeof(s_last_event_data));
}

static relay_state_changed_event_t read_last_event(void)
{
    relay_state_changed_event_t event = {0};

    memcpy(&event, s_last_event_data, sizeof(event));
    return event;
}

static bool host_test_read_tracked_relay_on(void)
{
    relay_t *relay = (relay_t *)s_tracked_relay;

    assert(relay != NULL);
    return relay->on;
}

static void test_relay_set_posts_event_before_unlock(void)
{
    const relay_config_t config = {
        .ctrl_gpio = 4,
        .active_level = RELAY_ACTIVE_HIGH,
    };
    relay_t *relay = relay_create(&config);
    relay_state_changed_event_t event = {0};

    assert(relay != NULL);

    reset_spies();
    s_tracked_relay = relay;
    assert(relay_set(relay, RELAY_SOURCE_CLOUD, true) == ESP_OK);

    event = read_last_event();
    assert(s_event_post_calls == 1);
    assert(s_event_post_observed_mutex_locked == true);
    assert(s_event_post_observed_relay_on_valid == true);
    assert(s_event_post_observed_relay_on == event.on);
    assert(event.on == true);
    assert(event.source == RELAY_SOURCE_CLOUD);
    assert(relay->on == true);

    assert(relay_destroy(relay) == ESP_OK);
}

static void test_relay_toggle_posts_event_before_unlock(void)
{
    const relay_config_t config = {
        .ctrl_gpio = 5,
        .active_level = RELAY_ACTIVE_HIGH,
    };
    relay_t *relay = relay_create(&config);
    relay_state_changed_event_t event = {0};

    assert(relay != NULL);

    reset_spies();
    s_tracked_relay = relay;
    assert(relay_toggle(relay, RELAY_SOURCE_LOCAL_BUTTON) == ESP_OK);

    event = read_last_event();
    assert(s_event_post_calls == 1);
    assert(s_event_post_observed_mutex_locked == true);
    assert(s_event_post_observed_relay_on_valid == true);
    assert(s_event_post_observed_relay_on == event.on);
    assert(event.on == true);
    assert(event.source == RELAY_SOURCE_LOCAL_BUTTON);
    assert(relay->on == true);

    assert(relay_destroy(relay) == ESP_OK);
}

static void test_relay_set_retries_failed_event_on_same_state(void)
{
    const relay_config_t config = {
        .ctrl_gpio = 6,
        .active_level = RELAY_ACTIVE_HIGH,
    };
    relay_t *relay = relay_create(&config);
    relay_state_changed_event_t event = {0};

    assert(relay != NULL);
    reset_spies();
    s_tracked_relay = relay;
    s_next_event_post_ret = ESP_ERR_TIMEOUT;

    assert(relay_set(relay, RELAY_SOURCE_SAFETY, true) == ESP_OK);
    assert(s_event_post_calls == 1);
    assert(relay->event_pending == true);

    s_next_event_post_ret = ESP_OK;
    assert(relay_set(relay, RELAY_SOURCE_CLOUD, true) == ESP_OK);
    assert(s_event_post_calls == 2);
    assert(relay->event_pending == false);
    event = read_last_event();
    assert(event.on == true);
    assert(event.source == RELAY_SOURCE_SAFETY);

    assert(relay_destroy(relay) == ESP_OK);
}

static void test_relay_set_replaces_failed_event_with_latest_state(void)
{
    const relay_config_t config = {
        .ctrl_gpio = 7,
        .active_level = RELAY_ACTIVE_HIGH,
    };
    relay_t *relay = relay_create(&config);
    relay_state_changed_event_t event = {0};

    assert(relay != NULL);
    reset_spies();
    s_tracked_relay = relay;
    s_next_event_post_ret = ESP_ERR_TIMEOUT;

    assert(relay_set(relay, RELAY_SOURCE_SAFETY, true) == ESP_OK);
    assert(relay->event_pending == true);

    s_next_event_post_ret = ESP_OK;
    assert(relay_set(relay, RELAY_SOURCE_CLOUD, false) == ESP_OK);
    assert(s_event_post_calls == 2);
    assert(relay->event_pending == false);
    event = read_last_event();
    assert(event.on == false);
    assert(event.source == RELAY_SOURCE_CLOUD);

    assert(relay_destroy(relay) == ESP_OK);
}

static void test_gpio_output_validity_accepts_esp32s3_high_pins(void)
{
    assert(GPIO_IS_VALID_OUTPUT_GPIO(47));
    assert(GPIO_IS_VALID_OUTPUT_GPIO(48));
    assert(!GPIO_IS_VALID_OUTPUT_GPIO(49));
}

int main(void)
{
    test_relay_set_posts_event_before_unlock();
    test_relay_toggle_posts_event_before_unlock();
    test_relay_set_retries_failed_event_on_same_state();
    test_relay_set_replaces_failed_event_with_latest_state();
    test_gpio_output_validity_accepts_esp32s3_high_pins();

    printf("relay event order tests passed\n");
    return 0;
}
