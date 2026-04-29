/*******************************************************************************
 * @file    Inc/TEC_PID.h
 * @author  Cam
 * @brief   TEC PID Temperature Controller
 *
 *          Closed-loop temperature control for the TEC subsystem.
 *          Runs a periodic PID loop on a hardware timer interrupt,
 *          reads THERM6 (top plate) and adjusts DRV8703 PWM duty.
 *
 *          Bidirectional: positive output = heat, negative = cool.
 *          Setpoint is in degrees C × 100 (integer, 0.01°C resolution).
 *
 *          Control loop:
 *            1. Read THERM6 ADC → convert to °C
 *            2. Calculate error = setpoint - measured
 *            3. PID: output = Kp*e + Ki*integral + Kd*derivative
 *            4. Clamp output to ±100 (duty cycle)
 *            5. If output > 0 → heat at |output|% duty
 *               If output < 0 → cool at |output|% duty
 *               If output ≈ 0 → brake (stop)
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef TEC_PID_H
#define TEC_PID_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ======================== Status ========================================== */

typedef enum {
    TEC_PID_OK          = 0x00U,
    TEC_PID_ERR_PARAM   = 0x01U,
    TEC_PID_ERR_SENSOR  = 0x02U,
} TEC_PID_Status;

/* ======================== PID State ======================================= */

typedef struct {
    /* Gains (scaled: actual = value / 100.0) */
    int32_t     kp;             /**< Proportional gain × 100             */
    int32_t     ki;             /**< Integral gain × 100                 */
    int32_t     kd;             /**< Derivative gain × 100               */

    /* Setpoint and limits */
    int16_t     setpoint_c100;  /**< Target temp in °C × 100             */
    int16_t     integral_max;   /**< Anti-windup clamp for integral term  */

    /* Internal state */
    int32_t     integral;       /**< Accumulated integral                */
    int16_t     prev_error;     /**< Previous error for derivative       */
    int16_t     measured_c100;  /**< Last measured temp in °C × 100      */
    int8_t      output;         /**< Last PID output (-100 to +100)      */

    /* Control */
    bool        running;        /**< PID loop is active                  */
    uint8_t     tec_id;         /**< Which TEC to control (1-3)          */
    uint32_t    period_ms;      /**< Control loop period in ms           */
    uint32_t    last_tick;      /**< Tick of last PID execution          */
} TEC_PID_State;

/* ======================== Extern Instance ================================== */

extern TEC_PID_State tec_pid;

/* ======================== Public API ======================================== */

/**
 * @brief  Initialize PID state with default gains.
 */
void TEC_PID_Init(TEC_PID_State *pid);

/**
 * @brief  Start the PID loop for the specified TEC.
 * @param  tec_id       TEC instance (1-3)
 * @param  setpoint_c100  Target temperature in °C × 100 (e.g., 9500 = 95.00°C)
 */
TEC_PID_Status TEC_PID_Start(TEC_PID_State *pid, uint8_t tec_id, int16_t setpoint_c100);

/**
 * @brief  Stop the PID loop and turn off the TEC.
 */
void TEC_PID_Stop(TEC_PID_State *pid);

/**
 * @brief  Set PID gains.
 * @param  kp, ki, kd  Gains × 100 (e.g., kp=500 means Kp=5.00)
 */
void TEC_PID_SetGains(TEC_PID_State *pid, int32_t kp, int32_t ki, int32_t kd);

/**
 * @brief  Update setpoint while running.
 */
void TEC_PID_SetTarget(TEC_PID_State *pid, int16_t setpoint_c100);

/**
 * @brief  Execute one PID iteration. Call this from the main loop.
 *         Returns immediately if the period hasn't elapsed.
 *         Reads thermistor, computes PID, sets TEC duty/direction.
 */
TEC_PID_Status TEC_PID_Update(TEC_PID_State *pid);

/**
 * @brief  Get current PID state for telemetry.
 */
void TEC_PID_GetState(const TEC_PID_State *pid,
                       int16_t *measured_c100,
                       int16_t *setpoint_c100,
                       int8_t *output,
                       bool *running);

#ifdef __cplusplus
}
#endif

#endif /* TEC_PID_H */
