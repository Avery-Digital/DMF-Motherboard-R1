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
#define DEFAULT_KP          500     /* 5.00 */
#define DEFAULT_KI          10      /* 0.10 */
#define DEFAULT_KD          200     /* 2.00 */
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
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->integral = 0;  /* Reset integral to avoid kick */
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

    /* ---- Read temperature ---- */
    uint16_t adc_raw = 0U;
    ADS7066_Status adc_st = ADS7066_ReadChannel(&ads7066_3_handle,
                                                  PID_THERM_CHANNEL,
                                                  &adc_raw);
    if (adc_st != ADS7066_OK) {
        /* Sensor read failed — emergency stop */
        TEC_PWM_Stop(pid->tec_id);
        pid->running = false;
        pid->faulted = true;
        pid->output  = 0;
        StageTelemetry(pid, 0);
        return TEC_PID_ERR_SENSOR;
    }

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

    /* Integral with anti-windup */
    pid->integral += (int32_t)error;
    if (pid->integral > pid->integral_max)  pid->integral = pid->integral_max;
    if (pid->integral < -pid->integral_max) pid->integral = -pid->integral_max;

    /* Derivative */
    int16_t derivative = error - pid->prev_error;
    pid->prev_error = error;

    /* PID output
     * Gains are × 100, error is in °C × 100
     * raw = (Kp × error + Ki × integral + Kd × derivative) / 100
     * Then scale to duty: divide by 100 again */
    int32_t raw_output = (pid->kp * (int32_t)error
                        + pid->ki * pid->integral
                        + pid->kd * (int32_t)derivative) / 100;

    raw_output /= 100;

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
