/*******************************************************************************
 * @file    Inc/TEC_PWM.h
 * @author  Cam
 * @brief   TEC PWM Control — TIM1/TIM8 PWM for DRV8702 H-Bridge
 *
 *          Each DRV8702 has two half-bridges (GH1/GL1 + GH2/GL2) forming
 *          a full H-bridge for bidirectional TEC current control.
 *
 *          IN1 controls half-bridge 1, IN2 controls half-bridge 2.
 *          With MODE=1 (no current regulation):
 *            Heat:  PWM on IN1, IN2 = LOW
 *            Cool:  IN1 = LOW, PWM on IN2
 *            Off:   IN1 = LOW, IN2 = LOW
 *
 *          Timer / Pin Mapping:
 *            TEC 1: IN1=PE9  (TIM1_CH1 AF1), IN2=PE11 (TIM1_CH2 AF1)
 *            TEC 2: IN1=PE13 (TIM1_CH3 AF1), IN2=PE14 (TIM1_CH4 AF1)
 *            TEC 3: IN1=PJ8  (TIM8_CH1 AF3), IN2=PJ10 (TIM8_CH2 AF3)
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef TEC_PWM_H
#define TEC_PWM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bsp.h"

/* TEC instance IDs (1-based to match DRV8702 instance numbering) */
#define TEC_INSTANCE_1      1U
#define TEC_INSTANCE_2      2U
#define TEC_INSTANCE_3      3U
#define TEC_COUNT           3U

/* Default PWM frequency in Hz */
#define TEC_PWM_DEFAULT_FREQ_HZ     20000U

/* TEC direction */
typedef enum {
    TEC_DIR_OFF     = 0,    /**< Both IN1 and IN2 LOW — no current       */
    TEC_DIR_HEAT    = 1,    /**< PWM on IN1, IN2 LOW — current direction A */
    TEC_DIR_COOL    = 2,    /**< IN1 LOW, PWM on IN2 — current direction B */
} TEC_Direction;

typedef enum {
    TEC_PWM_OK          = 0,
    TEC_PWM_ERR_PARAM   = 1,
    TEC_PWM_ERR_INIT    = 2,
} TEC_PWM_Status;

/**
 * @brief  Initialise TIM1 and TIM8 for PWM output on all TEC IN1/IN2 pins.
 *         Reconfigures pins from GPIO to AF mode.
 *         All channels start at 0% duty (TECs off).
 *
 * @param  freq_hz  PWM frequency in Hz (e.g. 20000 for 20 kHz)
 */
TEC_PWM_Status TEC_PWM_Init(uint32_t freq_hz);

/**
 * @brief  Set TEC direction and PWM duty cycle.
 * @param  instance  TEC_INSTANCE_1, _2, or _3
 * @param  dir       TEC_DIR_OFF, TEC_DIR_HEAT, or TEC_DIR_COOL
 * @param  duty_pct  Duty cycle 0–100 (applied to the active IN pin)
 * @return TEC_PWM_OK or TEC_PWM_ERR_PARAM
 */
TEC_PWM_Status TEC_PWM_Set(uint8_t instance, TEC_Direction dir, uint8_t duty_pct);

/**
 * @brief  Get current TEC state.
 * @param  instance  TEC_INSTANCE_1, _2, or _3
 * @param  dir       Output: current direction
 * @param  duty_pct  Output: current duty cycle 0–100
 * @return TEC_PWM_OK or TEC_PWM_ERR_PARAM
 */
TEC_PWM_Status TEC_PWM_Get(uint8_t instance, TEC_Direction *dir, uint8_t *duty_pct);

/**
 * @brief  Stop a TEC — sets both IN1 and IN2 to 0% duty.
 */
TEC_PWM_Status TEC_PWM_Stop(uint8_t instance);

/**
 * @brief  Stop all TECs.
 */
void TEC_PWM_StopAll(void);

#ifdef __cplusplus
}
#endif

#endif /* TEC_PWM_H */
