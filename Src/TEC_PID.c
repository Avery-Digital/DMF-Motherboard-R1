/*******************************************************************************
 * @file    Src/TEC_PID.c
 * @author  Cam
 * @brief   TEC PID Temperature Controller — Implementation
 *
 *          Polled PID loop called from main(). Reads THERM6 (top plate,
 *          ADS7066 instance 3 channel 5), computes PID output, drives TEC
 *          via TEC_PWM_Set().
 *
 *          Loop rate: 100 ms (10 Hz).
 *          Gains are integer × 100 for fixed-point math.
 *
 *          Safety: if thermistor reads NaN or outside -10 to 120°C,
 *          TEC is stopped immediately and faulted flag is set.
 *
 *          Telemetry: PID_Telemetry struct is staged each cycle for
 *          the main loop to send when the TX bus is free.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#include "TEC_PID.h"
#include "TEC_PWM.h"
#include "ADS7066.h"
#include "Thermistor.h"
#include "ll_tick.h"

/* ---- Extern handles ---- */
extern ADS7066_Handle ads7066_3_handle;

/* ---- THERM6 = ADS7066 instance 3, channel 5 ---- */
#define PID_THERM_CHANNEL   5U

/* ---- Default gains (× 100) ---- */
/* Heating gains — aggressive to reach hot setpoints fast */
#define DEFAULT_KP_HEAT     400     /* 4.00 */
#define DEFAULT_KI_HEAT     30      /* 0.30 */
#define DEFAULT_KD_HEAT     200     /* 2.00 */

/* Cooling gains — gentler to avoid overshoot when holding cold temps */
#define DEFAULT_KP_COOL     200     /* 2.00 */
#define DEFAULT_KI_COOL     15      /* 0.15 */
#define DEFAULT_KD_COOL     300     /* 3.00 */
#define DEFAULT_INTEGRAL_MAX 10000  /* Anti-windup limit */
#define DEFAULT_PERIOD_MS   100U    /* 10 Hz update rate */

/* ---- Global instances ---- */
TEC_PID_State  tec_pid        = {0};
PID_Telemetry  pid_telemetry  = {0};

/* ==========================================================================
 *  TEC_PID_Init
 * ========================================================================== */
void TEC_PID_Init(TEC_PID_State *pid)
{
    pid->kp_heat        = DEFAULT_KP_HEAT;
    pid->ki_heat        = DEFAULT_KI_HEAT;
    pid->kd_heat        = DEFAULT_KD_HEAT;
    pid->kp_cool        = DEFAULT_KP_COOL;
    pid->ki_cool        = DEFAULT_KI_COOL;
    pid->kd_cool        = DEFAULT_KD_COOL;
    pid->setpoint_c100  = 2500;     /* 25.00°C default */
    pid->integral_max   = DEFAULT_INTEGRAL_MAX;
    pid->integral       = 0;
    pid->prev_error     = 0;
    pid->measured_c100  = 0;
    pid->output         = 0;
    pid->running        = false;
    pid->faulted        = false;
    pid->tec_id         = 3U;
    pid->period_ms      = DEFAULT_PERIOD_MS;
    pid->last_tick      = 0U;

    pid_telemetry.pending = false;
}

/* ==========================================================================
 *  TEC_PID_Start
 * ========================================================================== */
TEC_PID_Status TEC_PID_Start(TEC_PID_State *pid, uint8_t tec_id, int16_t setpoint_c100)
{
    if (tec_id < 1U || tec_id > TEC_COUNT) {
        return TEC_PID_ERR_PARAM;
    }

    pid->tec_id         = tec_id;
    pid->setpoint_c100  = setpoint_c100;
    pid->integral       = 0;
    pid->prev_error     = 0;
    pid->output         = 0;
    pid->faulted        = false;
    pid->last_tick      = LL_GetTick();
    pid->running        = true;

    return TEC_PID_OK;
}

/* ==========================================================================
 *  TEC_PID_Stop
 * ========================================================================== */
void TEC_PID_Stop(TEC_PID_State *pid)
{
    pid->running  = false;
    pid->integral = 0;
    pid->output   = 0;

    /* Stop TEC output */
    TEC_PWM_Stop(pid->tec_id);
}

/* ==========================================================================
 *  TEC_PID_SetGains
 * ========================================================================== */
void TEC_PID_SetGains(TEC_PID_State *pid, int32_t kp, int32_t ki, int32_t kd)
{
    /* Set both heating and cooling gains to the same values.
     * For asymmetric tuning, call with different values for each direction
     * or modify the defaults in TEC_PID_Init(). */
    pid->kp_heat = kp;
    pid->ki_heat = ki;
    pid->kd_heat = kd;
    pid->kp_cool = kp;
    pid->ki_cool = ki;
    pid->kd_cool = kd;
    pid->integral = 0;
}

/* ==========================================================================
 *  TEC_PID_SetTarget
 * ========================================================================== */
void TEC_PID_SetTarget(TEC_PID_State *pid, int16_t setpoint_c100)
{
    pid->setpoint_c100 = setpoint_c100;
}

/* ==========================================================================
 *  Stage telemetry for main loop to send
 * ========================================================================== */
static void StageTelemetry(TEC_PID_State *pid, int16_t error)
{
    pid_telemetry.measured_c100 = pid->measured_c100;
    pid_telemetry.setpoint_c100 = pid->setpoint_c100;
    pid_telemetry.error_c100    = error;
    pid_telemetry.output        = pid->output;
    pid_telemetry.tec_id        = pid->tec_id;
    pid_telemetry.faulted       = pid->faulted;
    pid_telemetry.running       = pid->running;
    pid_telemetry.pending       = true;  /* Must be last */
}

/* ==========================================================================
 *  TEC_PID_Update — call from main loop
 *
 *  Returns immediately if not running or period hasn't elapsed.
 * ========================================================================== */
TEC_PID_Status TEC_PID_Update(TEC_PID_State *pid)
{
    if (!pid->running) return TEC_PID_OK;

    /* Check if period has elapsed */
    uint32_t now = LL_GetTick();
    if ((now - pid->last_tick) < pid->period_ms) {
        return TEC_PID_OK;
    }
    pid->last_tick = now;

    /* ---- Read temperature (10-sample median-filtered average) ----
     * Take 10 samples with inter-sample delay to avoid SPI contention.
     * Sort samples, discard the 2 highest and 2 lowest (outlier rejection),
     * average the remaining 6 for a robust reading.
     */
    #define PID_ADC_SAMPLES      10U
    #define PID_ADC_TRIM         2U    /* Discard this many from each end */
    #define PID_ADC_KEEP         (PID_ADC_SAMPLES - 2U * PID_ADC_TRIM)

    uint16_t samples[PID_ADC_SAMPLES];
    uint16_t adc_raw = 0U;

    for (uint8_t s = 0U; s < PID_ADC_SAMPLES; s++) {
        ADS7066_Status adc_st = ADS7066_ReadChannel(&ads7066_3_handle,
                                                      PID_THERM_CHANNEL,
                                                      &samples[s]);
        if (adc_st != ADS7066_OK) {
            TEC_PWM_Stop(pid->tec_id);
            pid->running = false;
            pid->faulted = true;
            pid->output  = 0;
            StageTelemetry(pid, 0);
            return TEC_PID_ERR_SENSOR;
        }

        /* Brief delay between samples to avoid SPI bus contention */
        for (volatile uint32_t d = 0; d < 5000; d++) { __NOP(); }
    }

    /* Simple insertion sort (10 elements, fast enough) */
    for (uint8_t i = 1U; i < PID_ADC_SAMPLES; i++) {
        uint16_t key = samples[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && samples[j] > key) {
            samples[j + 1] = samples[j];
            j--;
        }
        samples[j + 1] = key;
    }

    /* Average the middle 6 samples (trim 2 lowest + 2 highest) */
    uint32_t adc_sum = 0U;
    for (uint8_t s = PID_ADC_TRIM; s < PID_ADC_SAMPLES - PID_ADC_TRIM; s++) {
        adc_sum += samples[s];
    }
    adc_raw = (uint16_t)(adc_sum / PID_ADC_KEEP);

    float temp_c = Thermistor_AdcToTempC(adc_raw);

    /* ---- Safety check ---- */
    if (isnan(temp_c)) {
        TEC_PWM_Stop(pid->tec_id);
        pid->running = false;
        pid->faulted = true;
        pid->output  = 0;
        pid->measured_c100 = 0x7FFF;  /* Sentinel for NaN */
        StageTelemetry(pid, 0);
        return TEC_PID_ERR_SENSOR;
    }

    pid->measured_c100 = (int16_t)(temp_c * 100.0f);

    if (pid->measured_c100 < PID_TEMP_MIN_C100 ||
        pid->measured_c100 > PID_TEMP_MAX_C100) {
        TEC_PWM_Stop(pid->tec_id);
        pid->running = false;
        pid->faulted = true;
        pid->output  = 0;
        StageTelemetry(pid, 0);
        return TEC_PID_ERR_SENSOR;
    }

    /* ---- PID calculation ---- */
    int16_t error = pid->setpoint_c100 - pid->measured_c100;

    /* ---- Two-stage control with blended handoff ----
     *
     * Far from target (|error| > threshold): bang-bang at 100% duty
     * Within threshold: blend between bang-bang and PID output
     *   - At threshold edge: mostly bang-bang
     *   - Near setpoint: mostly PID
     * This eliminates the hard transition that causes ringing.
     *
     * Blend formula:
     *   bang_weight = |error| / threshold  (1.0 at edge, 0.0 at setpoint)
     *   pid_weight  = 1.0 - bang_weight
     *   output = bang_weight * 100% + pid_weight * pid_output
     */
    #define PID_BANGBANG_THRESHOLD_C100  300   /* 3.00°C */

    int32_t raw_output;

    int16_t abs_error = (error >= 0) ? error : -error;

    if (abs_error > PID_BANGBANG_THRESHOLD_C100) {
        /* Far from target — full blast */
        raw_output = (error > 0) ? 100 : -100;
        pid->integral = 0;
        pid->prev_error = error;
    } else {
        /* Within threshold — compute PID with direction-dependent gains */

        /* Select gains based on whether we need to heat or cool */
        int32_t kp = (error > 0) ? pid->kp_heat : pid->kp_cool;
        int32_t ki = (error > 0) ? pid->ki_heat : pid->ki_cool;
        int32_t kd = (error > 0) ? pid->kd_heat : pid->kd_cool;

        /* Integral with anti-windup */
        pid->integral += (int32_t)error;
        if (pid->integral > pid->integral_max)  pid->integral = pid->integral_max;
        if (pid->integral < -pid->integral_max) pid->integral = -pid->integral_max;

        /* Derivative */
        int16_t derivative = error - pid->prev_error;
        pid->prev_error = error;

        /* PID output */
        int32_t pid_output = (kp * (int32_t)error
                            + ki * pid->integral
                            + kd * (int32_t)derivative) / 100;
        pid_output /= 100;

        /* Clamp PID to ±100 */
        if (pid_output > 100)  pid_output = 100;
        if (pid_output < -100) pid_output = -100;

        /* Blend: smoothly transition from bang-bang to PID as error shrinks.
         * bang_pct = abs_error / threshold (fixed-point: ×100 / threshold)
         * At threshold edge: bang_pct=100, pid_pct=0
         * At setpoint:       bang_pct=0,   pid_pct=100 */
        int32_t bang_pct = (abs_error * 100) / PID_BANGBANG_THRESHOLD_C100;
        int32_t pid_pct  = 100 - bang_pct;

        int32_t bang_val = (error > 0) ? 100 : -100;

        raw_output = (bang_pct * bang_val + pid_pct * pid_output) / 100;
    }

    /* Clamp to -100..+100 */
    if (raw_output > 100)  raw_output = 100;
    if (raw_output < -100) raw_output = -100;

    pid->output = (int8_t)raw_output;

    /* ---- Apply to TEC ---- */
    if (raw_output > 0) {
        TEC_PWM_Set(pid->tec_id, TEC_DIR_HEAT, (uint8_t)raw_output);
    } else if (raw_output < 0) {
        TEC_PWM_Set(pid->tec_id, TEC_DIR_COOL, (uint8_t)(-raw_output));
    } else {
        TEC_PWM_Set(pid->tec_id, TEC_DIR_OFF, 0U);
    }

    /* ---- Stage telemetry ---- */
    StageTelemetry(pid, error);

    return TEC_PID_OK;
}
