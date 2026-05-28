/**
 * @file metering_service.c
 * @brief 电参量业务聚合实现
 * @details Electrical metering service implementation
 * @author OpenCode
 * @date 2026-05-24
 */

/*********************
 *      INCLUDES
 *********************/

#include "metering_service.h"

#include <stdlib.h>
#include <string.h>

#include "bl0942.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*********************
 *      DEFINES
 *********************/

#define TAG "metering_service"

#define METERING_DEFAULT_WINDOW_SAMPLES      (10)
#define METERING_DEFAULT_PUBLISH_PERIOD_MS   (1000)
#define METERING_EVENT_POST_TIMEOUT_MS       (10)

#define METERING_VREF_MV         (1218)
#define METERING_RL_MILLIOHM     (3)
#define METERING_R1_OHM          (510)
#define METERING_R2_OHM          (1950000)
#define METERING_RSUM_OHM        (METERING_R1_OHM + METERING_R2_OHM)
#define METERING_KI_SCALE        (305978)
#define METERING_KV_SCALE        (73989)
#define METERING_KP_SCALE        (3537)
#define METERING_POWER_NUM       (2679285553LL)
#define METERING_POWER_DEN       (50107500000LL)
#define METERING_FREQ_CLOCK_HZ   (1000000UL)
#define METERING_CF_CNT_U24_MASK (0x00FFFFFFUL)
#define METERING_PULSE_ENERGY_NWH (62297938ULL)
#define METERING_NWH_PER_WH      (1000000000ULL)

ESP_EVENT_DEFINE_BASE(METERING_EVENT_BASE);

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    int32_t voltage_cv;
    int32_t current_ma;
    int32_t power_cw;
    uint16_t frequency_chz;
    uint32_t cf_cnt_raw;
    uint64_t timestamp_us;
    bool valid;
} metering_fixed_sample_t;

struct metering_service {
    metering_config_t config;
    SemaphoreHandle_t mutex;
    metering_snapshot_t latest;
    bool has_latest;
    float window_v_sum;
    float window_i_sum;
    float window_p_sum;
    float window_freq_sum;
    int window_count;
    uint64_t window_start_us;
    uint64_t window_last_us;
    float total_energy;
    uint64_t total_energy_nwh;
    uint32_t last_cf_cnt_raw;
    esp_event_handler_instance_t measurement_handler;
    esp_event_handler_instance_t fault_handler;
    bool have_last_cf_cnt_raw;
    bool started;
    bool starting;
    bool stopping;
    bool initialized;
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**
 * @brief 应用电参量默认配置
 * @details Apply default metering configuration values
 * @param[in] config 输入配置，可为 NULL
 * @param[out] out 输出配置
 */
static void metering_apply_defaults(const metering_config_t *config,
                                    metering_config_t *out);

/**
 * @brief 运行换算自检
 * @details Run conversion self-test checks
 * @return
 *         - ESP_OK: 成功
 *         - ESP_FAIL: 自检失败
 */
static esp_err_t metering_run_conversion_selftest(void);

/**
 * @brief 使用默认公式换算原始采样
 * @details Convert raw BL0942 measurement using default formulas
 * @param[in] in 原始采样
 * @param[out] out 定点换算结果
 */
static void metering_convert_default(const bl0942_measurement_t *in,
                                     metering_fixed_sample_t *out);

/**
 * @brief 使用配置换算原始采样
 * @details Convert raw BL0942 measurement using configured coefficients
 * @param[in] me 电参量服务句柄
 * @param[in] in 原始采样
 * @param[out] out 浮点快照结果
 */
static void metering_convert_with_config(const metering_service_t *me,
                                         const bl0942_measurement_t *in,
                                         metering_snapshot_t *out);

/**
 * @brief 计算 24 位计数器增量
 * @details Compute wrap-safe delta for a 24-bit counter
 * @param[in] current 当前计数
 * @param[in] previous 上一次计数
 * @return 24 位计数器增量
 */
static uint32_t metering_u24_delta(uint32_t current, uint32_t previous);

/**
 * @brief 更新累计电能
 * @details Update accumulated energy from CF counter while locked
 * @param[in,out] me 电参量服务句柄
 * @param[in] cf_cnt_raw 原始 CF 计数
 */
static void metering_update_energy_locked(metering_service_t *me,
                                          uint32_t cf_cnt_raw);

/**
 * @brief 处理 BL0942 测量事件
 * @details Handle BL0942 measurement event
 * @param[in] arg 事件处理上下文
 * @param[in] base 事件基
 * @param[in] id 事件 ID
 * @param[in] event_data 事件载荷
 */
static void metering_on_bl0942_measurement(void *arg, esp_event_base_t base,
                                           int32_t id, void *event_data);

/**
 * @brief 处理 BL0942 故障事件
 * @details Handle BL0942 fault event
 * @param[in] arg 事件处理上下文
 * @param[in] base 事件基
 * @param[in] id 事件 ID
 * @param[in] event_data 事件载荷
 */
static void metering_on_bl0942_fault(void *arg, esp_event_base_t base,
                                     int32_t id, void *event_data);

/**
 * @brief 发布电参量快照事件
 * @details Post metering snapshot event
 * @param[in] snapshot 快照数据
 */
static void metering_post_snapshot(const metering_snapshot_t *snapshot);

/**********************
 *  STATIC VARIABLES
 **********************/

static uint32_t s_snapshot_log_count;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

metering_service_t *metering_service_create(const metering_config_t *config)
{
    metering_config_t resolved = {0};

    if (metering_run_conversion_selftest() != ESP_OK) {
        ESP_LOGE(TAG, "conversion self-test failed");
        return NULL;
    }

    metering_apply_defaults(config, &resolved);

    metering_service_t *me = calloc(1, sizeof(*me));
    if (me == NULL) {
        ESP_LOGE(TAG, "calloc metering service failed");
        return NULL;
    }

    me->config = resolved;
    me->mutex = xSemaphoreCreateMutex();
    if (me->mutex == NULL) {
        ESP_LOGE(TAG, "create mutex failed");
        free(me);
        return NULL;
    }

    me->initialized = true;
    return me;
}

esp_err_t metering_service_destroy(metering_service_t *me)
{
    if (me == NULL) {
        return ESP_OK;
    }

    const esp_err_t stop_ret = metering_service_stop(me);
    if (stop_ret != ESP_OK) {
        return stop_ret;
    }

    if (me->mutex != NULL) {
        vSemaphoreDelete(me->mutex);
    }
    free(me);
    return ESP_OK;
}

esp_err_t metering_service_start(metering_service_t *me)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "service is null");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "service is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    if (me->started) {
        const bool handlers_registered = (me->measurement_handler != NULL &&
                                          me->fault_handler != NULL);
        (void)xSemaphoreGive(me->mutex);
        return handlers_registered ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (me->starting || me->stopping) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (me->measurement_handler != NULL || me->fault_handler != NULL) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    me->starting = true;
    (void)xSemaphoreGive(me->mutex);

    ret = esp_event_handler_instance_register(BL0942_EVENT_BASE,
                                              BL0942_EVENT_MEASUREMENT,
                                              metering_on_bl0942_measurement,
                                              me, &me->measurement_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register measurement handler failed: %s",
                 esp_err_to_name(ret));
        goto clear_starting;
    }

    ret = esp_event_handler_instance_register(BL0942_EVENT_BASE,
                                              BL0942_EVENT_FAULT,
                                              metering_on_bl0942_fault, me,
                                              &me->fault_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register fault handler failed: %s",
                 esp_err_to_name(ret));
        const esp_err_t unregister_ret = esp_event_handler_instance_unregister(
            BL0942_EVENT_BASE, BL0942_EVENT_MEASUREMENT,
            me->measurement_handler);
        if (unregister_ret == ESP_OK) {
            me->measurement_handler = NULL;
        } else {
            ESP_LOGW(TAG, "unregister measurement handler failed: %s",
                     esp_err_to_name(unregister_ret));
        }
        goto clear_starting;
    }

    if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE) {
        ret = ESP_ERR_TIMEOUT;
        if (me->measurement_handler != NULL) {
            const esp_err_t measurement_ret =
                esp_event_handler_instance_unregister(
                    BL0942_EVENT_BASE, BL0942_EVENT_MEASUREMENT,
                    me->measurement_handler);
            if (measurement_ret == ESP_OK) {
                me->measurement_handler = NULL;
            }
        }
        if (me->fault_handler != NULL) {
            const esp_err_t fault_ret = esp_event_handler_instance_unregister(
                BL0942_EVENT_BASE, BL0942_EVENT_FAULT, me->fault_handler);
            if (fault_ret == ESP_OK) {
                me->fault_handler = NULL;
            }
        }
        goto clear_starting;
    }

    me->started = true;
    me->starting = false;
    (void)xSemaphoreGive(me->mutex);
    return ESP_OK;

clear_starting:
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
        me->starting = false;
        (void)xSemaphoreGive(me->mutex);
    }
    return ret;
}

esp_err_t metering_service_stop(metering_service_t *me)
{
    esp_err_t first_error = ESP_OK;
    esp_err_t ret = ESP_OK;
    esp_event_handler_instance_t measurement_handler = NULL;
    esp_event_handler_instance_t fault_handler = NULL;
    bool measurement_unregistered = false;
    bool fault_unregistered = false;

    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "service is null");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "service is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    if (!me->started && !me->starting && me->measurement_handler == NULL &&
        me->fault_handler == NULL) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_OK;
    }
    if (me->starting || me->stopping) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    me->stopping = true;
    measurement_handler = me->measurement_handler;
    fault_handler = me->fault_handler;
    (void)xSemaphoreGive(me->mutex);

    if (measurement_handler != NULL) {
        ret = esp_event_handler_instance_unregister(BL0942_EVENT_BASE,
                                                    BL0942_EVENT_MEASUREMENT,
                                                    measurement_handler);
        if (ret == ESP_OK) {
            measurement_unregistered = true;
        } else {
            first_error = ret;
            ESP_LOGW(TAG, "unregister measurement handler failed: %s",
                     esp_err_to_name(ret));
        }
    }

    if (fault_handler != NULL) {
        ret = esp_event_handler_instance_unregister(BL0942_EVENT_BASE,
                                                    BL0942_EVENT_FAULT,
                                                    fault_handler);
        if (ret == ESP_OK) {
            fault_unregistered = true;
        } else {
            if (first_error == ESP_OK) {
                first_error = ret;
            }
            ESP_LOGW(TAG, "unregister fault handler failed: %s",
                     esp_err_to_name(ret));
        }
    }

    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");
    if (measurement_unregistered &&
        me->measurement_handler == measurement_handler) {
        me->measurement_handler = NULL;
    }
    if (fault_unregistered && me->fault_handler == fault_handler) {
        me->fault_handler = NULL;
    }
    if (first_error == ESP_OK) {
        me->started = false;
    }
    me->stopping = false;
    (void)xSemaphoreGive(me->mutex);

    return first_error;
}

esp_err_t metering_service_get_latest(metering_service_t *me,
                                      metering_snapshot_t *out)
{
    ESP_RETURN_ON_FALSE(me != NULL && out != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "invalid argument");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "service is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    if (!me->has_latest) {
        (void)xSemaphoreGive(me->mutex);
        return ESP_ERR_INVALID_STATE;
    }

    *out = me->latest;
    (void)xSemaphoreGive(me->mutex);
    return ESP_OK;
}

esp_err_t metering_service_reset_energy(metering_service_t *me)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "service is null");
    ESP_RETURN_ON_FALSE(me->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "service is not initialized");
    ESP_RETURN_ON_FALSE(me->mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "mutex is null");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "take mutex failed");

    me->total_energy = 0.0f;
    me->total_energy_nwh = 0ULL;
    me->last_cf_cnt_raw = 0U;
    me->have_last_cf_cnt_raw = false;
    if (me->has_latest) {
        me->latest.total_energy = 0.0f;
    }

    (void)xSemaphoreGive(me->mutex);
    return ESP_OK;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void metering_apply_defaults(const metering_config_t *config,
                                    metering_config_t *out)
{
    if (out == NULL) {
        return;
    }

    if (config != NULL) {
        *out = *config;
    } else {
        memset(out, 0, sizeof(*out));
    }

    if (out->window_samples <= 0) {
        out->window_samples = METERING_DEFAULT_WINDOW_SAMPLES;
    }
    if (out->publish_period_ms <= 0) {
        out->publish_period_ms = METERING_DEFAULT_PUBLISH_PERIOD_MS;
    }
}

static uint32_t metering_u24_delta(uint32_t current, uint32_t previous)
{
    current &= METERING_CF_CNT_U24_MASK;
    previous &= METERING_CF_CNT_U24_MASK;
    return (current - previous) & METERING_CF_CNT_U24_MASK;
}

static void metering_update_energy_locked(metering_service_t *me,
                                           uint32_t cf_cnt_raw)
{
    if (me == NULL) {
        return;
    }

    cf_cnt_raw &= METERING_CF_CNT_U24_MASK;
    if (!me->have_last_cf_cnt_raw) {
        me->last_cf_cnt_raw = cf_cnt_raw;
        me->have_last_cf_cnt_raw = true;
        return;
    }

    const uint32_t delta = metering_u24_delta(cf_cnt_raw,
                                              me->last_cf_cnt_raw);
    me->last_cf_cnt_raw = cf_cnt_raw;

    if (me->config.cf_coeff > 0.0f) {
        me->total_energy += (float)delta * me->config.cf_coeff;
        return;
    }

    me->total_energy_nwh += (uint64_t)delta * METERING_PULSE_ENERGY_NWH;
    me->total_energy = (float)((double)me->total_energy_nwh /
                               (double)METERING_NWH_PER_WH);
}

static void metering_convert_default(const bl0942_measurement_t *in,
                                     metering_fixed_sample_t *out)
{
    int64_t power_cw = 0;

    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    if (in == NULL || !in->valid) {
        return;
    }

    const uint32_t i_rms_raw = in->i_rms_raw & METERING_CF_CNT_U24_MASK;
    const uint32_t v_rms_raw = in->v_rms_raw & METERING_CF_CNT_U24_MASK;
    const uint32_t cf_cnt_raw = in->cf_cnt_raw & METERING_CF_CNT_U24_MASK;

    out->current_ma = (int32_t)(((int64_t)i_rms_raw * METERING_VREF_MV) /
                                ((int64_t)METERING_KI_SCALE * METERING_RL_MILLIOHM));
    out->voltage_cv = (int32_t)(((int64_t)v_rms_raw * METERING_VREF_MV *
                                 METERING_RSUM_OHM) /
                                ((int64_t)METERING_KV_SCALE * METERING_R1_OHM * 10000LL));
    power_cw = ((int64_t)in->watt_raw * METERING_POWER_NUM) /
               METERING_POWER_DEN;
    if (power_cw < 0) {
        power_cw = 0;
    }
    out->power_cw = (int32_t)power_cw;
    if (in->freq_raw != 0U) {
        uint32_t frequency_chz = (uint32_t)(((uint64_t)METERING_FREQ_CLOCK_HZ * 100ULL) /
                                            in->freq_raw);
        if (frequency_chz > UINT16_MAX) {
            frequency_chz = UINT16_MAX;
        }
        out->frequency_chz = (uint16_t)frequency_chz;
    }

    if (out->voltage_cv < 0) {
        out->voltage_cv = 0;
    }
    if (out->current_ma < 0) {
        out->current_ma = 0;
    }
    out->cf_cnt_raw = cf_cnt_raw;
    out->timestamp_us = in->capture_time_us;
    out->valid = true;
}

static void metering_convert_with_config(const metering_service_t *me,
                                         const bl0942_measurement_t *in,
                                         metering_snapshot_t *out)
{
    metering_fixed_sample_t fixed = {0};
    uint32_t i_rms_raw = 0;
    uint32_t v_rms_raw = 0;

    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    if (me == NULL || in == NULL || !in->valid) {
        return;
    }

    metering_convert_default(in, &fixed);
    if (!fixed.valid) {
        return;
    }

    i_rms_raw = in->i_rms_raw & METERING_CF_CNT_U24_MASK;
    v_rms_raw = in->v_rms_raw & METERING_CF_CNT_U24_MASK;

    out->voltage = (me->config.v_rms_coeff > 0.0f) ?
                   ((float)v_rms_raw * me->config.v_rms_coeff) :
                   ((float)fixed.voltage_cv / 100.0f);
    out->current = (me->config.i_rms_coeff > 0.0f) ?
                   ((float)i_rms_raw * me->config.i_rms_coeff) :
                   ((float)fixed.current_ma / 1000.0f);
    out->power = (me->config.watt_coeff > 0.0f) ?
                 ((float)in->watt_raw * me->config.watt_coeff) :
                 ((float)fixed.power_cw / 100.0f);
    out->frequency = (float)fixed.frequency_chz / 100.0f;
    out->timestamp_us = fixed.timestamp_us;
    out->valid = true;
}

static esp_err_t metering_run_conversion_selftest(void)
{
    const bl0942_measurement_t golden = {
        .i_rms_raw = 753639,
        .v_rms_raw = 3494335,
        .i_fast_rms_raw = 0,
        .watt_raw = 411438,
        .cf_cnt_raw = 0,
        .freq_raw = 20000,
        .status_raw = 0,
        .capture_time_us = 0,
        .valid = true,
    };
    const bl0942_measurement_t high_power = {
        .i_rms_raw = 0,
        .v_rms_raw = 0,
        .i_fast_rms_raw = 0,
        .watt_raw = 0x7FFFFF,
        .cf_cnt_raw = 0,
        .freq_raw = 20000,
        .status_raw = 0,
        .capture_time_us = 0,
        .valid = true,
    };
    const bl0942_measurement_t negative_power = {
        .i_rms_raw = 0,
        .v_rms_raw = 0,
        .i_fast_rms_raw = 0,
        .watt_raw = -0x800000,
        .cf_cnt_raw = 0,
        .freq_raw = 20000,
        .status_raw = 0,
        .capture_time_us = 0,
        .valid = true,
    };
    metering_fixed_sample_t out = {0};
    metering_service_t energy_test = {0};

    metering_convert_default(&golden, &out);
    ESP_RETURN_ON_FALSE(out.current_ma >= 994 && out.current_ma <= 1004,
                        ESP_FAIL, TAG, "current self-test failed");
    ESP_RETURN_ON_FALSE(out.voltage_cv >= 21989 && out.voltage_cv <= 22009,
                        ESP_FAIL, TAG, "voltage self-test failed");
    ESP_RETURN_ON_FALSE(out.power_cw >= 21989 && out.power_cw <= 22009,
                        ESP_FAIL, TAG, "power self-test failed");
    ESP_RETURN_ON_FALSE(out.frequency_chz >= 4995U && out.frequency_chz <= 5005U,
                        ESP_FAIL, TAG, "frequency self-test failed");

    metering_convert_default(&high_power, &out);
    ESP_RETURN_ON_FALSE(out.power_cw > 0 && out.power_cw < INT32_MAX,
                        ESP_FAIL, TAG, "high power self-test failed");

    metering_convert_default(&negative_power, &out);
    ESP_RETURN_ON_FALSE(out.power_cw == 0,
                        ESP_FAIL, TAG, "negative power self-test failed");

    metering_update_energy_locked(&energy_test, 10U);
    ESP_RETURN_ON_FALSE(energy_test.have_last_cf_cnt_raw &&
                            energy_test.last_cf_cnt_raw == 10U &&
                            energy_test.total_energy_nwh == 0ULL &&
                            energy_test.total_energy == 0.0f,
                        ESP_FAIL, TAG, "energy baseline self-test failed");

    metering_update_energy_locked(&energy_test, 12U);
    ESP_RETURN_ON_FALSE(energy_test.total_energy_nwh ==
                            (2ULL * METERING_PULSE_ENERGY_NWH),
                        ESP_FAIL, TAG, "energy nWh self-test failed");

    memset(&energy_test, 0, sizeof(energy_test));
    energy_test.config.cf_coeff = 0.5f;
    metering_update_energy_locked(&energy_test, 0x00FFFFFEUL);
    metering_update_energy_locked(&energy_test, 1U);
    ESP_RETURN_ON_FALSE(energy_test.total_energy > 1.49f &&
                            energy_test.total_energy < 1.51f,
                        ESP_FAIL, TAG, "energy coefficient wrap self-test failed");

    return ESP_OK;
}

static void metering_on_bl0942_measurement(void *arg, esp_event_base_t base,
                                           int32_t id, void *event_data)
{
    metering_service_t *me = (metering_service_t *)arg;
    const bl0942_measurement_t *measurement =
        (const bl0942_measurement_t *)event_data;
    metering_snapshot_t sample = {0};
    metering_snapshot_t snapshot = {0};
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

    metering_update_energy_locked(me, measurement->cf_cnt_raw);
    sample.total_energy = me->total_energy;

    if (me->window_count == 0) {
        me->window_start_us = sample.timestamp_us;
    }
    me->window_last_us = sample.timestamp_us;
    me->window_v_sum += sample.voltage;
    me->window_i_sum += sample.current;
    me->window_p_sum += sample.power;
    me->window_freq_sum += sample.frequency;
    me->window_count++;

    const uint64_t elapsed_us = (me->window_last_us >= me->window_start_us) ?
                                (me->window_last_us - me->window_start_us) :
                                0ULL;
    if (me->window_count >= me->config.window_samples ||
        elapsed_us >= ((uint64_t)me->config.publish_period_ms * 1000ULL)) {
        snapshot.voltage = me->window_v_sum / (float)me->window_count;
        snapshot.current = me->window_i_sum / (float)me->window_count;
        snapshot.power = me->window_p_sum / (float)me->window_count;
        snapshot.frequency = me->window_freq_sum / (float)me->window_count;
        snapshot.total_energy = me->total_energy;
        snapshot.timestamp_us = me->window_last_us;
        snapshot.valid = true;

        me->latest = snapshot;
        me->has_latest = true;
        me->window_v_sum = 0.0f;
        me->window_i_sum = 0.0f;
        me->window_p_sum = 0.0f;
        me->window_freq_sum = 0.0f;
        me->window_count = 0;
        me->window_start_us = 0ULL;
        me->window_last_us = 0ULL;
        emit = true;
    }
    (void)xSemaphoreGive(me->mutex);

    if (emit) {
        s_snapshot_log_count++;
        if ((s_snapshot_log_count % 10U) == 0U) {
            ESP_LOGI(TAG,
                     "metering snapshot #%lu: V=%.2fV I=%.3fA P=%.2fW E=%.3fWh F=%.2fHz valid=%d",
                     (unsigned long)s_snapshot_log_count,
                     (double)snapshot.voltage,
                     (double)snapshot.current,
                     (double)snapshot.power,
                     (double)snapshot.total_energy,
                     (double)snapshot.frequency,
                     snapshot.valid ? 1 : 0);
        }
        metering_post_snapshot(&snapshot);
    }
}

static void metering_on_bl0942_fault(void *arg, esp_event_base_t base,
                                      int32_t id, void *event_data)
{
    metering_service_t *me = (metering_service_t *)arg;
    const bl0942_fault_info_t *fault_info =
        (const bl0942_fault_info_t *)event_data;
    metering_snapshot_t snapshot = {0};

    (void)base;
    (void)id;

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
    if (fault_info != NULL && fault_info->hard_reset_attempted) {
        me->have_last_cf_cnt_raw = false;
        me->last_cf_cnt_raw = 0U;
    }
    snapshot.timestamp_us = (snapshot.timestamp_us != 0ULL) ?
                            snapshot.timestamp_us :
                            (uint64_t)esp_timer_get_time();
    snapshot.total_energy = me->total_energy;
    snapshot.valid = false;
    me->latest = snapshot;
    me->has_latest = true;
    (void)xSemaphoreGive(me->mutex);

    metering_post_snapshot(&snapshot);
}

static void metering_post_snapshot(const metering_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    const esp_err_t ret = esp_event_post(METERING_EVENT_BASE,
                                         METERING_EVENT_SNAPSHOT,
                                         snapshot, sizeof(*snapshot),
                                         pdMS_TO_TICKS(
                                             METERING_EVENT_POST_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "post snapshot event failed: %s", esp_err_to_name(ret));
    }
}
