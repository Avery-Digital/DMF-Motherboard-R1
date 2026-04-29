/*******************************************************************************
 * @file    Src/TEC_PID.c
 * @author  Cam
 * @brief   TEC PID Temperature Controller — Implementation
 *
 *          Polled PID loop called from main(). Reads THERM6 (top plate),
 *          computes PID output, sets TEC heat/cool duty via TEC_PWM.
 *
 *          Loop rate: configurable, default 100 ms (10 Hz).
 *          Gains are integer × 100 for fixed-point math without floats
 *          in the control loop.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#include "TEC_PID.h"
#include "ADS7066.h"
#include "Thermistor.h"
#include "TEC_PWM.h"
#include "DRV8702.h"
#include "ll_tick.h"

/* ---- Extern handles needed ---- */
extern ADS7066_Handle  ads7066_3_handle;
extern DRV8702_Handle  drv8702_1_handle;
extern DRV8702_Handle  drv8702_2_handle;
extern DRV8702_Handle  drv8702_3_handle;

/* ---- THERM6 = ADS7066 instance 3, channel 5 ---- */
#define PID_THERM_CHANNEL   5U

/* ---- Default PID gains (× 100) ---- */
#define DEFAULT_KP          500     /* 5.00 */
#define DEFAULT_KI          50      /* 0.50 */
#define DEFAULT_KD          100     /* 1.00 */
#define DEFAULT_INTEGRAL_MAX 5000   /* Anti-windup limit */
#define DEFAULT_PERIOD_MS   100U    /* 10 Hz update rate */

/* ---- Global instance ---- */
TEC_PID_State tec_pid = {0};

/* ==========================================================================
 *  Helper: get DRV8702 handle for TEC ID
 * ========================================================================== */
static DRV8702_Handle* GetDrvHandle(uint8_t tec_id)
{
    switch (tec_id) {
    case 1: return &drv8702_1_handle;
    case 2: return &drv8702_2_handle;
    case 3: return &drv8702_3_handle;
    default: return NULL;
    }
}

/* ==========================================================================
 *  TEC_PID_Init
 * ========================================================================== */
void TEC_PID_Init(TEC_PID_State *pid)
{
    pid->kp             = DEFAULT_KP;
    pid->ki             = DEFAULT_KI;
    pid->kd             = DEFAULT_KD;
    pid->setpoint_c100  = 2500;     /* 25.00°C default */
    pid->integral_max   = DEFAULT_INTEGRAL_MAX;
    pid->integral       = 0;
    pid->prev_error     = 0;
    pid->measured_c100  = 0;
    pid->output         = 0;
    pid->running        = false;
    pid->tec_id         = 3U;       /* Default to TEC 3 */
    pid->period_ms      = DEFAULT_PERIOD_MS;
    pid->last_tick      = 0U;
}

/* ==========================================================================
 *  TEC_PID_Start
 * ========================================================================== */
TEC_PID_Status TEC_PID_Start(TEC_PID_State *pid, uint8_t tec_id, int16_t setpoint_c100)
{
    if (tec_id < 1U || tec_id > 3U) {
        return TEC_PID_ERR_PARAM;
    }

    pid->tec_id         = tec_id;
    pid->setpoint_c100  = setpoint_c100;
    pid->integral       = 0;
    pid->prev_error     = 0;
    pid->output         = 0;
    pid->last_tick      = LL_GetTick();
    pid->running        = true;

    return TEC_PID_OK;
}

/* ==========================================================================
 *  TEC_PID_Stop
 * ========================================================================== */
void TEC_PID_Stop(TEC_PID_State *pid)
{
    pid->running = false;
    pid->integral = 0;
    pid->output = 0;

    /* Stop TEC output */
    TEC_PWM_Stop(pid->tec_id);
}

/* ==========================================================================
 *  TEC_PID_SetGains
 * ========================================================================== */
void TEC_PID_SetGains(TEC_PID_State *pid, int32_t kp, int32_t ki, int32_t kd)
{
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    /* Reset integral when gains change to avoid kick */
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
 *  TEC_PID_Update — call from main loop
 *
 *  Returns immediately if not running or period hasn't elapsed.
 *
 *  PID calculation (all in °C × 100 fixed-point):
 *    error = setpoint - measured
 *    integral += error
 *    derivative = error - prev_error
 *    output = (Kp × error + Ki × integral + Kd × derivative) / 100
 *    Clamp output to -100..+100
 *
 *  Output mapping:
 *    output > 0  → heat at output% duty
 *    output < 0  → cool at |output|% duty
 *    output == 0 → brake
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

    /* ---- Read temperature ---- */
    uint16_t adc_raw = 0U;
    ADS7066_Status adc_st = ADS7066_ReadChannel(&ads7066_3_handle,
                                                  PID_THERM_CHANNEL,
                                                  &adc_raw);
    if (adc_st != ADS7066_OK) {
        return TEC_PID_ERR_SENSOR;
    }

    float temp_c = Thermistor_AdcToTempC(adc_raw);
    pid->measured_c100 = (int16_t)(temp_c * 100.0f);

    /* ---- PID calculation ---- */
    int16_t error = pid->setpoint_c100 - pid->measured_c100;

    /* Integral with anti-windup */
    pid->integral += (int32_t)error;
    if (pid->integral > pid->integral_max) pid->integral = pid->integral_max;
    if (pid->integral < -pid->integral_max) pid->integral = -pid->integral_max;

    /* Derivative */
    int16_t derivative = error - pid->prev_error;
    pid->prev_error = error;

    /* PID output (gains are × 100, so divide by 100 at the end) */
    int32_t raw_output = (pid->kp * (int32_t)error
                        + pid->ki * pid->integral
                        + pid->kd * (int32_t)derivative) / 100;

    /* Scale: error is in °C×100, so raw_output is large.
     * Divide by 100 again to get duty percentage. */
    raw_output /= 100;

    /* Clamp to -100..+100 */
    if (raw_output > 100)  raw_output = 100;
    if (raw_output < -100) raw_output = -100;

    pid->output = (int8_t)raw_output;

    /* ---- Apply to TEC ---- */
    DRV8702_Handle *drv = GetDrvHandle(pid->tec_id);
    if (drv == NULL) return TEC_PID_ERR_PARAM;

    if (raw_output > 0) {
        DRV8702_TEC_Heat(drv);
        TEC_PWM_SetDuty(pid->tec_id, (uint8_t)raw_output);
    } else if (raw_output < 0) {
        DRV8702_TEC_Cool(drv);
        TEC_PWM_SetDuty(pid->tec_id, (uint8_t)(-raw_output));
    } else {
        TEC_PWM_Stop(pid->tec_id);
    }

    return TEC_PID_OK;
}

/* ==========================================================================
 *  TEC_PID_GetState
 * ========================================================================== */
void TEC_PID_GetState(const TEC_PID_State *pid,
                       int16_t *measured_c100,
                       int16_t *setpoint_c100,
                       int8_t *output,
                       bool *running)
{
    if (measured_c100) *measured_c100 = pid->measured_c100;
    if (setpoint_c100) *setpoint_c100 = pid->setpoint_c100;
    if (output)        *output        = pid->output;
    if (running)       *running       = pid->running;
}
