/*******************************************************************************
 * @file    Inc/TEC_PID.h
 * @author  Cam
 * @brief   TEC PID Temperature Controller
 *
 *          Closed-loop temperature control for PCR thermal cycling (4–95°C).
 *          Polled from main loop at 10 Hz. Reads THERM6 (top plate),
 *          computes PID, drives TEC via TEC_PWM_Set().
 *
 *          Bidirectional: positive output = heat, negative = cool.
 *          Auto-streams telemetry via PID_Telemetry struct (main loop
 *          sends when TX bus is free — never blocks command responses).
 *
 *          Safety: stops TEC immediately on sensor fault (NaN or out of
 *          range -10 to 120°C).
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
#include <math.h>

/* ======================== Safety Limits ==================================== */

#define PID_TEMP_MIN_C100    (-1000)    /* -10.00°C — below this = fault      */
#define PID_TEMP_MAX_C100    (12000)    /*  120.00°C — above this = fault     */

/* ======================== Status ========================================== */

typedef enum {
    TEC_PID_OK          = 0x00U,
    TEC_PID_ERR_PARAM   = 0x01U,
    TEC_PID_ERR_SENSOR  = 0x02U,
} TEC_PID_Status;

/* ======================== Telemetry (staged for main loop TX) ============= */

typedef struct {
    volatile bool   pending;        /**< Set by PID update, cleared by main loop */
    int16_t         measured_c100;  /**< Current temperature × 100               */
    int16_t         setpoint_c100;  /**< Target temperature × 100                */
    int16_t         error_c100;     /**< Error = setpoint - measured              */
    int8_t          output;         /**< PID output -100 to +100                 */
    uint8_t         tec_id;         /**< TEC instance                            */
    bool            faulted;        /**< Sensor fault occurred                   */
    bool            running;        /**< PID is active                           */
} PID_Telemetry;

/* ======================== PID State ======================================= */

typedef struct {
    /* Gains (scaled: actual = value / 100.0) */
    int32_t     kp;             /**< Proportional gain × 100             */
    int32_t     ki;             /**< Integral gain × 100                 */
    int32_t     kd;             /**< Derivative gain × 100               */

    /* Setpoint and limits */
    int16_t     setpoint_c100;  /**< Target temp in °C × 100             */
    int32_t     integral_max;   /**< Anti-windup clamp for integral term  */

    /* Internal state */
    int32_t     integral;       /**< Accumulated integral                */
    int16_t     prev_error;     /**< Previous error for derivative       */
    int16_t     measured_c100;  /**< Last measured temp in °C × 100      */
    int8_t      output;         /**< Last PID output (-100 to +100)      */

    /* Control */
    bool        running;        /**< PID loop is active                  */
    bool        faulted;        /**< Sensor fault — requires restart     */
    uint8_t     tec_id;         /**< Which TEC to control (1-3)          */
    uint32_t    period_ms;      /**< Control loop period in ms           */
    uint32_t    last_tick;      /**< Tick of last PID execution          */
} TEC_PID_State;

/* ======================== Extern Instances ================================= */

extern TEC_PID_State  tec_pid;
extern PID_Telemetry  pid_telemetry;

/* ======================== Public API ======================================== */

void           TEC_PID_Init(TEC_PID_State *pid);
TEC_PID_Status TEC_PID_Start(TEC_PID_State *pid, uint8_t tec_id, int16_t setpoint_c100);
void           TEC_PID_Stop(TEC_PID_State *pid);
void           TEC_PID_SetGains(TEC_PID_State *pid, int32_t kp, int32_t ki, int32_t kd);
void           TEC_PID_SetTarget(TEC_PID_State *pid, int16_t setpoint_c100);
TEC_PID_Status TEC_PID_Update(TEC_PID_State *pid);

#ifdef __cplusplus
}
#endif

#endif /* TEC_PID_H */
