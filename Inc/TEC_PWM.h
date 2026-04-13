/*******************************************************************************
 * @file    Inc/TEC_PWM.h
 * @author  Cam
 * @brief   TEC PWM Control — TIM1/TIM8 PWM for DRV8702 H-Bridge EN pins
 *
 *          DRV8702-Q1 full H-bridge gate driver in PH/EN mode (MODE=0):
 *            PH (IN1) = direction:  HIGH = forward, LOW = reverse
 *            EN (IN2) = enable/PWM: HIGH = drive, LOW = brake
 *
 *          PWM is applied to EN for power control. PH is GPIO for direction.
 *
 *          Timer / Pin Mapping (EN pins only — PWM):
 *            TEC 1: EN=PE11 → TIM1_CH2 (AF1)
 *            TEC 2: EN=PE14 → TIM1_CH4 (AF1)
 *            TEC 3: EN=PJ10 → TIM8_CH2 (AF3)
 *
 *          PH pins remain GPIO (set by DRV8702_SetDirection):
 *            TEC 1: PH=PE9
 *            TEC 2: PH=PE13
 *            TEC 3: PH=PJ8
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

#define TEC_INSTANCE_1      1U
#define TEC_INSTANCE_2      2U
#define TEC_INSTANCE_3      3U
#define TEC_COUNT           3U

#define TEC_PWM_DEFAULT_FREQ_HZ     20000U

typedef enum {
    TEC_DIR_OFF     = 0,    /**< EN=0% (brake), PH unchanged              */
    TEC_DIR_HEAT    = 1,    /**< PH=HIGH (forward), PWM on EN             */
    TEC_DIR_COOL    = 2,    /**< PH=LOW  (reverse), PWM on EN             */
} TEC_Direction;

typedef enum {
    TEC_PWM_OK          = 0,
    TEC_PWM_ERR_PARAM   = 1,
    TEC_PWM_ERR_INIT    = 2,
} TEC_PWM_Status;

/**
 * @brief  Initialise TIM1 CH2/CH4 and TIM8 CH2 for PWM on EN pins.
 *         Reconfigures EN pins from GPIO to AF mode.
 *         PH pins remain GPIO (controlled by DRV8702_SetDirection).
 *         All channels start at 0% duty (TECs in brake / off).
 */
TEC_PWM_Status TEC_PWM_Init(uint32_t freq_hz);

/**
 * @brief  Set TEC direction and PWM duty cycle.
 *         Sets PH pin for direction, then adjusts EN PWM duty.
 * @param  instance  TEC_INSTANCE_1, _2, or _3
 * @param  dir       TEC_DIR_OFF, TEC_DIR_HEAT, or TEC_DIR_COOL
 * @param  duty_pct  Duty cycle 0–100 (applied to EN pin)
 */
TEC_PWM_Status TEC_PWM_Set(uint8_t instance, TEC_Direction dir, uint8_t duty_pct);

TEC_PWM_Status TEC_PWM_Get(uint8_t instance, TEC_Direction *dir, uint8_t *duty_pct);
TEC_PWM_Status TEC_PWM_Stop(uint8_t instance);
void TEC_PWM_StopAll(void);

#ifdef __cplusplus
}
#endif

#endif /* TEC_PWM_H */
