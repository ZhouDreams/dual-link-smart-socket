/**
 * @file metering_service_internal.h
 * @brief 计量服务内部电量增量辅助接口
 * @details Metering service internal energy delta helper interface
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
#include <stdint.h>

#include "esp_err.h"

/*********************
 *      DEFINES
 *********************/

#define METERING_ENERGY_CF_CNT_U24_MASK   (0x00FFFFFFUL)
#define METERING_ENERGY_PULSE_NWH         (62297938ULL)
#define METERING_ENERGY_NWH_PER_MWH_Q3    (1000ULL)

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 电量增量计算状态
 * @details Energy delta calculation state
 */
typedef struct {
    bool have_confirmed_cf_cnt_raw;    /**< 是否已有确认 CF 计数； Whether confirmed CF count exists */
    uint32_t confirmed_cf_cnt_raw;     /**< 已确认 CF 计数； Confirmed CF count */
    uint32_t confirmed_residual_nwh;   /**< 已确认余数 nWh； Confirmed residual in nWh */
    bool have_pending;                 /**< 是否有待确认结果； Whether a pending result exists */
    uint32_t pending_cf_cnt_raw;       /**< 待确认 CF 计数； Pending CF count */
    uint32_t pending_residual_nwh;     /**< 待确认余数 nWh； Pending residual in nWh */
    uint32_t pending_token;            /**< 待确认令牌； Pending token */
    uint32_t next_token;               /**< 下一个令牌； Next token */
} metering_energy_delta_state_t;

/**
 * @brief 电量增量计算结果
 * @details Energy delta calculation result
 */
typedef struct {
    float energy_delta_mwh;       /**< 电量增量 mWh； Energy delta in mWh */
    uint32_t token;               /**< 确认令牌； Confirmation token */
    bool baseline_established;    /**< 是否建立基线； Whether baseline was established */
} metering_energy_delta_result_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

void metering_energy_delta_state_init(metering_energy_delta_state_t *state);
void metering_energy_delta_reset_baseline(metering_energy_delta_state_t *state);
esp_err_t metering_energy_delta_prepare(metering_energy_delta_state_t *state,
                                        uint32_t cf_cnt_raw,
                                        metering_energy_delta_result_t *out);
esp_err_t metering_energy_delta_confirm(metering_energy_delta_state_t *state,
                                        uint32_t token);
esp_err_t metering_energy_delta_discard(metering_energy_delta_state_t *state,
                                        uint32_t token);
uint32_t metering_energy_u24_delta(uint32_t current, uint32_t previous);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif
