# P0 Code Review Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复 `metering_service` 的两个 P0 状态机卡死问题，以及 `relay` 的状态变化事件 TOCTOU 问题，并通过 host tests 与 ESP-IDF 构建验证结果。

**Architecture:** 先补齐 host-side ESP-IDF/FreeRTOS shim 头文件，让 `metering_service.c` 和 `relay.c` 能直接被 host tests `#include` 进同一翻译单元，从而测试它们的静态 handler/内部结构。然后按 TDD 分两批修复：先锁定 `metering_service` 的 post-failure / fault-recovery 行为，再锁定 `relay` 的事件发布时间点，最后把新测试接入 `test/host/run_host_tests.sh` 并跑完整验证。

**Tech Stack:** ESP-IDF C / FreeRTOS mutex / `esp_event` / host C tests via `cc` and `test/host/run_host_tests.sh` / `idf.py build`.

**关联设计文档:** `docs/superpowers/specs/2026-07-12-p0-code-review-fixes-design.md`

**提交策略:** 仓库规则要求未经用户明确授权不得提交 git commit。本计划不包含 commit 步骤；执行时只修改文件并做验证。

---

## File Structure

- Create `test/support/esp_event.h`: 为 host tests 提供 `esp_event` 类型、宏和函数声明。
- Create `test/support/esp_check.h`: 为 `ESP_RETURN_ON_FALSE` 提供最小 shim。
- Create `test/support/esp_log.h`: 为日志宏和 `esp_err_to_name()` 提供最小 shim。
- Create `test/support/esp_timer.h`: 为 `esp_timer_get_time()` 提供最小声明。
- Create `test/support/freertos/FreeRTOS.h`: 为 `BaseType_t`、`TickType_t`、`pdTRUE`、`portMAX_DELAY` 等提供最小 shim。
- Create `test/support/freertos/semphr.h`: 为 `SemaphoreHandle_t` 和 mutex API 提供最小声明。
- Create `test/support/driver/gpio.h`: 为 `relay.c` / `bl0942.h` 依赖的 GPIO 类型与函数提供最小 shim。
- Create `test/support/driver/uart.h`: 为 `bl0942.h` 的 `uart_port_t` 提供最小 shim。
- Create `test/host/test_metering_service_event_flow.c`: 直接 `#include` `metering_service_internal.c` + `metering_service.c`，测试 snapshot post 失败回滚与 fault 重置 baseline。
- Modify `main/metering/metering_service.c`: 让 `metering_post_snapshot()` 返回 `esp_err_t`，并在 measurement/fault handler 中补上恢复逻辑。
- Create `test/host/test_relay_event_order.c`: 直接 `#include` `relay.c`，测试事件发布仍在 mutex 临界区内。
- Modify `main/relay/relay.c`: 把状态变化事件发布移动到临界区内。
- Modify `test/host/run_host_tests.sh`: 接入两个新 host tests，纳入完整回归。

---

### Task 1: Add Host Shim Headers For ESP-IDF APIs

**Files:**
- Create: `test/support/esp_event.h`
- Create: `test/support/esp_check.h`
- Create: `test/support/esp_log.h`
- Create: `test/support/esp_timer.h`
- Create: `test/support/freertos/FreeRTOS.h`
- Create: `test/support/freertos/semphr.h`
- Create: `test/support/driver/gpio.h`
- Create: `test/support/driver/uart.h`

- [ ] **Step 1: Create the core ESP-IDF shim headers**

Create `test/support/esp_event.h`:

```c
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef const char *esp_event_base_t;
typedef void (*esp_event_handler_t)(void *arg, esp_event_base_t base,
                                    int32_t id, void *event_data);
typedef void *esp_event_handler_instance_t;

#define ESP_EVENT_DECLARE_BASE(id) extern esp_event_base_t id
#define ESP_EVENT_DEFINE_BASE(id) esp_event_base_t id = #id

esp_err_t esp_event_post(esp_event_base_t event_base, int32_t event_id,
                         const void *event_data, size_t event_data_size,
                         uint32_t ticks_to_wait);

esp_err_t esp_event_handler_instance_register(
    esp_event_base_t event_base, int32_t event_id,
    esp_event_handler_t event_handler, void *event_handler_arg,
    esp_event_handler_instance_t *instance);

esp_err_t esp_event_handler_instance_unregister(
    esp_event_base_t event_base, int32_t event_id,
    esp_event_handler_instance_t instance);
```

Create `test/support/esp_check.h`:

```c
#pragma once

#define ESP_RETURN_ON_FALSE(cond, err_code, tag, fmt, ...) \
    do {                                                    \
        (void)(tag);                                        \
        if (!(cond)) {                                      \
            return (err_code);                              \
        }                                                   \
    } while (0)
```

Create `test/support/esp_log.h`:

```c
#pragma once

#include "esp_err.h"

const char *esp_err_to_name(esp_err_t err);

#define ESP_LOGE(tag, fmt, ...) ((void)(tag))
#define ESP_LOGW(tag, fmt, ...) ((void)(tag))
#define ESP_LOGI(tag, fmt, ...) ((void)(tag))
```

Create `test/support/esp_timer.h`:

```c
#pragma once

#include <stdint.h>

int64_t esp_timer_get_time(void);
```

- [ ] **Step 2: Create the FreeRTOS and driver shim headers**

Create `test/support/freertos/FreeRTOS.h`:

```c
#pragma once

#include <stdint.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;

#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY ((TickType_t)0xFFFFFFFFU)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
```

Create `test/support/freertos/semphr.h`:

```c
#pragma once

#include "freertos/FreeRTOS.h"

typedef struct host_test_mutex *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t ticks_to_wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex);
void vSemaphoreDelete(SemaphoreHandle_t mutex);
```

Create `test/support/driver/gpio.h`:

```c
#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef int gpio_num_t;

typedef struct {
    uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
    int pull_down_en;
    int intr_type;
} gpio_config_t;

#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_NUM_NC (-1)

#define GPIO_IS_VALID_OUTPUT_GPIO(gpio) ((gpio) >= 0)

esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);
```

Create `test/support/driver/uart.h`:

```c
#pragma once

typedef int uart_port_t;
```

---

### Task 2: Fix `metering_service` Post-Failure And Fault Recovery

**Files:**
- Create: `test/host/test_metering_service_event_flow.c`
- Modify: `main/metering/metering_service.c`

- [ ] **Step 1: Write the failing host test for event-flow recovery**

Create `test/host/test_metering_service_event_flow.c`:

```c
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/semphr.h"

typedef struct host_test_mutex {
    bool locked;
    bool deleted;
} host_test_mutex_t;

static esp_err_t s_next_event_post_ret = ESP_OK;
static int s_event_post_calls = 0;
static unsigned char s_last_event_data[128];
static size_t s_last_event_size = 0;
static int64_t s_fake_time_us = 0;

esp_event_base_t BL0942_EVENT_BASE = "BL0942_EVENT_BASE";

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return (SemaphoreHandle_t)calloc(1, sizeof(host_test_mutex_t));
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
    return ESP_OK;
}

esp_err_t esp_event_handler_instance_register(
    esp_event_base_t event_base, int32_t event_id,
    esp_event_handler_t event_handler, void *event_handler_arg,
    esp_event_handler_instance_t *instance)
{
    (void)event_base;
    (void)event_id;
    (void)event_handler;
    (void)event_handler_arg;

    if (instance != NULL) {
        *instance = (void *)event_handler;
    }

    return ESP_OK;
}

esp_err_t esp_event_handler_instance_unregister(
    esp_event_base_t event_base, int32_t event_id,
    esp_event_handler_instance_t instance)
{
    (void)event_base;
    (void)event_id;
    (void)instance;
    return ESP_OK;
}

esp_err_t esp_event_post(esp_event_base_t event_base, int32_t event_id,
                         const void *event_data, size_t event_data_size,
                         uint32_t ticks_to_wait)
{
    (void)event_base;
    (void)event_id;
    (void)ticks_to_wait;

    s_event_post_calls++;
    s_last_event_size = 0;
    memset(s_last_event_data, 0, sizeof(s_last_event_data));

    if (event_data != NULL && event_data_size <= sizeof(s_last_event_data)) {
        memcpy(s_last_event_data, event_data, event_data_size);
        s_last_event_size = event_data_size;
    }

    return s_next_event_post_ret;
}

int64_t esp_timer_get_time(void)
{
    return s_fake_time_us;
}

const char *esp_err_to_name(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return "ESP_OK";
    case ESP_FAIL:
        return "ESP_FAIL";
    case ESP_ERR_NO_MEM:
        return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG:
        return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE:
        return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_SIZE:
        return "ESP_ERR_INVALID_SIZE";
    case ESP_ERR_NOT_FOUND:
        return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_NOT_SUPPORTED:
        return "ESP_ERR_NOT_SUPPORTED";
    case ESP_ERR_TIMEOUT:
        return "ESP_ERR_TIMEOUT";
    case ESP_ERR_INVALID_RESPONSE:
        return "ESP_ERR_INVALID_RESPONSE";
    default:
        return "ESP_ERR_UNKNOWN";
    }
}

#include "metering_service_internal.c"
#include "metering_service.c"

static void assert_float_near(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.001f);
}

static void reset_spies(void)
{
    s_next_event_post_ret = ESP_OK;
    s_event_post_calls = 0;
    s_last_event_size = 0;
    s_fake_time_us = 0;
    memset(s_last_event_data, 0, sizeof(s_last_event_data));
}

static void init_service_fixture(metering_service_t *service)
{
    memset(service, 0, sizeof(*service));
    service->mutex = xSemaphoreCreateMutex();
    assert(service->mutex != NULL);
    service->initialized = true;
    service->started = true;
    metering_energy_delta_state_init(&service->energy_delta_state);
}

static void destroy_service_fixture(metering_service_t *service)
{
    vSemaphoreDelete(service->mutex);
    service->mutex = NULL;
}

static bl0942_measurement_t make_valid_measurement(uint32_t cf_cnt_raw,
                                                   uint64_t capture_time_us)
{
    const bl0942_measurement_t measurement = {
        .i_rms_raw = 753639U,
        .v_rms_raw = 3494335U,
        .i_fast_rms_raw = 0U,
        .watt_raw = 411438,
        .cf_cnt_raw = cf_cnt_raw,
        .freq_raw = 20000U,
        .status_raw = 0U,
        .capture_time_us = capture_time_us,
        .valid = true,
    };

    return measurement;
}

static void test_post_failure_discards_pending_token(void)
{
    metering_service_t service;
    metering_energy_delta_result_t retry = {0};
    const bl0942_measurement_t measurement = make_valid_measurement(100U, 1111U);

    reset_spies();
    init_service_fixture(&service);
    s_next_event_post_ret = ESP_FAIL;

    metering_on_bl0942_measurement(&service, BL0942_EVENT_BASE,
                                   BL0942_EVENT_MEASUREMENT,
                                   (void *)&measurement);

    assert(s_event_post_calls == 1);
    assert(service.energy_delta_state.have_pending == false);
    assert(metering_energy_delta_prepare(&service.energy_delta_state, 101U,
                                         &retry) == ESP_OK);
    assert_float_near(retry.energy_delta_mwh, 62.297f);

    destroy_service_fixture(&service);
}

static void test_fault_without_hard_reset_clears_pending_baseline(void)
{
    metering_service_t service;
    metering_energy_delta_result_t baseline = {0};
    metering_energy_delta_result_t pending = {0};
    metering_energy_delta_result_t after_fault = {0};
    const bl0942_fault_info_t fault = {
        .consecutive_failures = 1U,
        .fault_cycles = 1U,
        .hard_reset_attempted = false,
        .last_error = ESP_FAIL,
    };

    reset_spies();
    init_service_fixture(&service);
    service.has_latest = true;
    service.latest = (metering_snapshot_t){
        .voltage = 220.0f,
        .current = 1.0f,
        .power = 220.0f,
        .energy_delta = 0.5f,
        .frequency = 50.0f,
        .timestamp_us = 999U,
        .energy_delta_token = 9U,
        .valid = true,
    };

    assert(metering_energy_delta_prepare(&service.energy_delta_state, 100U,
                                         &baseline) == ESP_OK);
    assert(metering_energy_delta_confirm(&service.energy_delta_state,
                                         baseline.token) == ESP_OK);
    assert(metering_energy_delta_prepare(&service.energy_delta_state, 101U,
                                         &pending) == ESP_OK);

    metering_on_bl0942_fault(&service, BL0942_EVENT_BASE,
                             BL0942_EVENT_FAULT, (void *)&fault);

    assert(s_event_post_calls == 1);
    assert(service.latest.valid == false);
    assert(service.latest.energy_delta == 0.0f);
    assert(service.latest.energy_delta_token == 0U);
    assert(metering_energy_delta_confirm(&service.energy_delta_state,
                                         pending.token) == ESP_ERR_INVALID_STATE);
    assert(metering_energy_delta_prepare(&service.energy_delta_state, 200U,
                                         &after_fault) == ESP_OK);
    assert(after_fault.baseline_established == true);
    assert_float_near(after_fault.energy_delta_mwh, 0.0f);

    destroy_service_fixture(&service);
}

int main(void)
{
    test_post_failure_discards_pending_token();
    test_fault_without_hard_reset_clears_pending_baseline();

    printf("metering event flow tests passed\n");
    return 0;
}
```

- [ ] **Step 2: Run the metering host test and verify it fails before the code change**

Run:

```bash
mkdir -p test/host/build && cc -std=c11 -Wall -Wextra -Werror \
  -Itest/support \
  -Imain/metering \
  -Imain/bl0942 \
  test/host/test_metering_service_event_flow.c \
  -o test/host/build/test_metering_service_event_flow && \
  test/host/build/test_metering_service_event_flow
```

Expected:
- 编译成功。
- 运行时失败，至少有一个断言报错。
- 当前代码下最先出现的失败应是 `service.energy_delta_state.have_pending == false`，证明 post 失败后 pending 没有被回滚。

- [ ] **Step 3: Implement the minimal `metering_service` fix**

Modify the static prototype near the top of `main/metering/metering_service.c` to return `esp_err_t`:

```c
/**
 * @brief 发布电参量快照事件
 * @details Post metering snapshot event
 * @param[in] snapshot 快照数据
 * @return
 *         - ESP_OK: 发布成功； Posted successfully
 *         - 其他: 发布失败； Post failed
 */
static esp_err_t metering_post_snapshot(const metering_snapshot_t *snapshot);
```

Replace `metering_on_bl0942_measurement()` with this version:

```c
static void metering_on_bl0942_measurement(void *arg, esp_event_base_t base,
                                           int32_t id, void *event_data)
{
    metering_service_t *me = (metering_service_t *)arg;
    const bl0942_measurement_t *measurement =
        (const bl0942_measurement_t *)event_data;
    metering_snapshot_t sample = {0};
    metering_snapshot_t snapshot = {0};
    metering_energy_delta_result_t energy_delta = {0};
    bool emit = false;

    (void)base;
    (void)id;

    if (me == NULL || measurement == NULL || !measurement->valid) {
        return;
    }
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (!me->started || me->stopping) {
        (void)xSemaphoreGive(me->mutex);
        return;
    }

    metering_convert_with_config(me, measurement, &sample);
    if (!sample.valid) {
        (void)xSemaphoreGive(me->mutex);
        return;
    }

    if (metering_energy_delta_prepare(&me->energy_delta_state,
                                      measurement->cf_cnt_raw,
                                      &energy_delta) != ESP_OK) {
        (void)xSemaphoreGive(me->mutex);
        return;
    }
    sample.energy_delta = energy_delta.energy_delta_mwh;
    sample.energy_delta_token = energy_delta.token;

    me->latest = sample;
    me->has_latest = true;
    snapshot = sample;
    emit = true;
    (void)xSemaphoreGive(me->mutex);

    if (emit) {
        s_snapshot_log_count++;
        ESP_LOGI(TAG,
                 "metering snapshot #%lu: V=%.2fV I=%.3fA P=%.2fW E=%.3fmWh F=%.2fHz valid=%d",
                 (unsigned long)s_snapshot_log_count,
                 (double)snapshot.voltage,
                 (double)snapshot.current,
                 (double)snapshot.power,
                 (double)snapshot.energy_delta,
                 (double)snapshot.frequency,
                 snapshot.valid ? 1 : 0);

        const esp_err_t post_ret = metering_post_snapshot(&snapshot);
        if (post_ret != ESP_OK) {
            if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
                const bool can_discard =
                    (energy_delta.token != 0U) && me->started &&
                    !me->stopping && me->energy_delta_state.have_pending &&
                    (me->energy_delta_state.pending_token == energy_delta.token);

                if (can_discard) {
                    const esp_err_t discard_ret = metering_energy_delta_discard(
                        &me->energy_delta_state, energy_delta.token);
                    if (discard_ret != ESP_OK) {
                        ESP_LOGW(TAG,
                                 "discard pending energy delta after post failure failed: %s",
                                 esp_err_to_name(discard_ret));
                    }
                }
                (void)xSemaphoreGive(me->mutex);
            } else {
                ESP_LOGW(TAG, "retake mutex after post failure failed");
            }
        }
    }
}
```

Replace `metering_on_bl0942_fault()` with this version:

```c
static void metering_on_bl0942_fault(void *arg, esp_event_base_t base,
                                     int32_t id, void *event_data)
{
    metering_service_t *me = (metering_service_t *)arg;
    metering_snapshot_t snapshot = {0};

    (void)base;
    (void)id;
    (void)event_data;

    if (me == NULL) {
        return;
    }
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (!me->started || me->stopping) {
        (void)xSemaphoreGive(me->mutex);
        return;
    }

    if (me->has_latest) {
        snapshot = me->latest;
    }
    metering_energy_delta_reset_baseline(&me->energy_delta_state);
    snapshot.timestamp_us = (snapshot.timestamp_us != 0ULL) ?
                            snapshot.timestamp_us :
                            (uint64_t)esp_timer_get_time();
    snapshot.energy_delta = 0.0f;
    snapshot.energy_delta_token = 0U;
    snapshot.valid = false;
    me->latest = snapshot;
    me->has_latest = true;
    (void)xSemaphoreGive(me->mutex);

    (void)metering_post_snapshot(&snapshot);
}
```

Replace `metering_post_snapshot()` with this version:

```c
static esp_err_t metering_post_snapshot(const metering_snapshot_t *snapshot)
{
    esp_err_t ret = ESP_OK;

    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = esp_event_post(METERING_EVENT_BASE,
                         METERING_EVENT_SNAPSHOT,
                         snapshot, sizeof(*snapshot),
                         pdMS_TO_TICKS(METERING_EVENT_POST_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "post snapshot event failed: %s", esp_err_to_name(ret));
    }

    return ret;
}
```

- [ ] **Step 4: Re-run the metering host test and verify it passes**

Run:

```bash
mkdir -p test/host/build && cc -std=c11 -Wall -Wextra -Werror \
  -Itest/support \
  -Imain/metering \
  -Imain/bl0942 \
  test/host/test_metering_service_event_flow.c \
  -o test/host/build/test_metering_service_event_flow && \
  test/host/build/test_metering_service_event_flow
```

Expected:
- 编译成功。
- 输出 `metering event flow tests passed`。

---

### Task 3: Fix `relay` Event Ordering Under The Mutex

**Files:**
- Create: `test/host/test_relay_event_order.c`
- Modify: `main/relay/relay.c`

- [ ] **Step 1: Write the failing host test for relay event order**

Create `test/host/test_relay_event_order.c`:

```c
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
static unsigned char s_last_event_data[64];

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
    s_next_gpio_set_ret = ESP_OK;
    s_next_event_post_ret = ESP_OK;
    s_event_post_calls = 0;
    s_event_post_observed_mutex_locked = false;
    memset(s_last_event_data, 0, sizeof(s_last_event_data));
}

static relay_state_changed_event_t read_last_event(void)
{
    relay_state_changed_event_t event = {0};

    memcpy(&event, s_last_event_data, sizeof(event));
    return event;
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
    assert(relay_set(relay, RELAY_SOURCE_CLOUD, true) == ESP_OK);

    event = read_last_event();
    assert(s_event_post_calls == 1);
    assert(s_event_post_observed_mutex_locked == true);
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
    assert(relay_toggle(relay, RELAY_SOURCE_LOCAL_BUTTON) == ESP_OK);

    event = read_last_event();
    assert(s_event_post_calls == 1);
    assert(s_event_post_observed_mutex_locked == true);
    assert(event.on == true);
    assert(event.source == RELAY_SOURCE_LOCAL_BUTTON);
    assert(relay->on == true);

    assert(relay_destroy(relay) == ESP_OK);
}

int main(void)
{
    test_relay_set_posts_event_before_unlock();
    test_relay_toggle_posts_event_before_unlock();

    printf("relay event order tests passed\n");
    return 0;
}
```

- [ ] **Step 2: Run the relay host test and verify it fails before the code change**

Run:

```bash
mkdir -p test/host/build && cc -std=c11 -Wall -Wextra -Werror \
  -Itest/support \
  -Imain/relay \
  test/host/test_relay_event_order.c \
  -o test/host/build/test_relay_event_order && \
  test/host/build/test_relay_event_order
```

Expected:
- 编译成功。
- 运行时失败。
- 当前代码下最先出现的失败应是 `s_event_post_observed_mutex_locked == true`，因为事件是在 `xSemaphoreGive()` 之后才发布的。

- [ ] **Step 3: Implement the minimal `relay` fix**

Replace `relay_set()` in `main/relay/relay.c` with this version:

```c
esp_err_t relay_set(relay_t *me, relay_source_t source, bool on)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "relay is null");
    ESP_RETURN_ON_FALSE(source >= RELAY_SOURCE_INTERNAL &&
                            source < RELAY_SOURCE_MAX,
                        ESP_ERR_INVALID_ARG, TAG, "invalid source");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "relay is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    const bool previous_on = me->on;
    esp_err_t ret = gpio_set_level(me->ctrl_gpio,
                                   relay_logical_to_level(me, on));
    if (ret == ESP_OK) {
        me->on = on;
        if (previous_on != on) {
            relay_post_state_changed(on, source);
        }
    }

    (void)xSemaphoreGive(me->mutex);

    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}
```

Replace `relay_toggle()` with this version:

```c
esp_err_t relay_toggle(relay_t *me, relay_source_t source)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "relay is null");
    ESP_RETURN_ON_FALSE(source >= RELAY_SOURCE_INTERNAL &&
                            source < RELAY_SOURCE_MAX,
                        ESP_ERR_INVALID_ARG, TAG, "invalid source");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "relay is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    const bool new_on = !me->on;
    esp_err_t ret = gpio_set_level(me->ctrl_gpio,
                                   relay_logical_to_level(me, new_on));
    if (ret == ESP_OK) {
        me->on = new_on;
        relay_post_state_changed(new_on, source);
    }

    (void)xSemaphoreGive(me->mutex);

    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}
```

- [ ] **Step 4: Re-run the relay host test and verify it passes**

Run:

```bash
mkdir -p test/host/build && cc -std=c11 -Wall -Wextra -Werror \
  -Itest/support \
  -Imain/relay \
  test/host/test_relay_event_order.c \
  -o test/host/build/test_relay_event_order && \
  test/host/build/test_relay_event_order
```

Expected:
- 编译成功。
- 输出 `relay event order tests passed`。

---

### Task 4: Wire The New Tests Into The Host Runner And Run Full Verification

**Files:**
- Modify: `test/host/run_host_tests.sh`

- [ ] **Step 1: Add both new host tests to `run_host_tests.sh`**

Insert these two compile/run blocks in `test/host/run_host_tests.sh` after the existing `test_metering_service_internal` block and before the `test_app_controller_internal` block:

```bash
"${CC_BIN}" -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/test/support" \
    -I"${ROOT_DIR}/main/metering" \
    -I"${ROOT_DIR}/main/bl0942" \
    "${ROOT_DIR}/test/host/test_metering_service_event_flow.c" \
    -o "${BUILD_DIR}/test_metering_service_event_flow"

"${BUILD_DIR}/test_metering_service_event_flow"

"${CC_BIN}" -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/test/support" \
    -I"${ROOT_DIR}/main/relay" \
    "${ROOT_DIR}/test/host/test_relay_event_order.c" \
    -o "${BUILD_DIR}/test_relay_event_order"

"${BUILD_DIR}/test_relay_event_order"
```

- [ ] **Step 2: Run the complete host test suite**

Run:

```bash
./test/host/run_host_tests.sh
```

Expected:
- 现有 host tests 继续通过。
- 新增输出包含：
  - `metering event flow tests passed`
  - `relay event order tests passed`
- 结尾输出 `host tests passed`。

- [ ] **Step 3: Run the ESP-IDF build from the repository root**

Run:

```bash
source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build
```

Expected:
- ESP-IDF 环境初始化成功。
- `idf.py build` 完成且无编译错误。

---

## Self-Review Checklist

- 覆盖 spec 的 3 个目标问题：`MS-01`、`MS-07`、relay TOCTOU。
- 没有扩展到 `app_controller M3` 或其他 P1/P3。
- 测试先写再运行失败，再改生产代码，再回归通过。
- 新增的 host shim 只服务于测试编译，不改变生产代码接口。
- 最终验证同时包含 targeted host tests、`run_host_tests.sh` 全量 host suite 和 `idf.py build`。
