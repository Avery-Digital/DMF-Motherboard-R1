/*******************************************************************************
 * @file    Src/TEC_PWM.c
 * @author  Cam
 * @brief   TEC PWM Control — TIM1/TIM8 PWM for DRV8702 H-Bridge EN pins
 *
 *          DRV8702-Q1 in PH/EN mode (MODE pin = 0):
 *            PH (IN1): GPIO — sets H-bridge current direction
 *              PH=HIGH → forward (current SH1→SH2)
 *              PH=LOW  → reverse (current SH2→SH1)
 *            EN (IN2): PWM — controls power level
 *              EN=HIGH → bridge active, current flows in PH direction
 *              EN=LOW  → brake (both low-side FETs on, slow decay)
 *
 *          Only EN pins are reconfigured to AF for timer PWM output.
 *          PH pins stay as GPIO output, controlled via DRV8702_SetDirection().
 *
 *          Timer clock: APB2 timer clock = 240 MHz
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#include "TEC_PWM.h"
#include "DRV8702.h"
#include "stm32h7xx_ll_tim.h"

#define TIM_CLK_HZ      240000000UL

static uint32_t       pwm_arr = 0U;
static TEC_Direction  dir_cache[TEC_COUNT]  = {TEC_DIR_OFF, TEC_DIR_OFF, TEC_DIR_OFF};
static uint8_t        duty_cache[TEC_COUNT] = {0, 0, 0};

/* Map TEC instance to DRV8702 handle for PH direction control */
static DRV8702_Handle* GetDrvHandle(uint8_t instance)
{
    switch (instance) {
    case TEC_INSTANCE_1: return &drv8702_1_handle;
    case TEC_INSTANCE_2: return &drv8702_2_handle;
    case TEC_INSTANCE_3: return &drv8702_3_handle;
    default: return NULL;
    }
}

/* ==========================================================================
 *  TEC_PWM_Init — configure TIM1 CH2/CH4 and TIM8 CH2 for EN PWM
 * ========================================================================== */
TEC_PWM_Status TEC_PWM_Init(uint32_t freq_hz)
{
    if (freq_hz == 0U || freq_hz > 1000000U) {
        return TEC_PWM_ERR_PARAM;
    }

    uint32_t arr = (TIM_CLK_HZ / freq_hz) - 1U;
    uint32_t psc = 0U;
    while (arr > 65535U) {
        psc++;
        arr = (TIM_CLK_HZ / ((psc + 1U) * freq_hz)) - 1U;
    }
    pwm_arr = arr;

    /* ---- Reconfigure EN pins from GPIO to AF for PWM ---- */

    /* TEC 1 EN: PE11 → TIM1_CH2, AF1 */
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOE);
    LL_GPIO_SetPinMode(GPIOE, LL_GPIO_PIN_11, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_8_15(GPIOE, LL_GPIO_PIN_11, LL_GPIO_AF_1);
    LL_GPIO_SetPinSpeed(GPIOE, LL_GPIO_PIN_11, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinOutputType(GPIOE, LL_GPIO_PIN_11, LL_GPIO_OUTPUT_PUSHPULL);

    /* TEC 2 EN: PE14 → TIM1_CH4, AF1 */
    LL_GPIO_SetPinMode(GPIOE, LL_GPIO_PIN_14, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_8_15(GPIOE, LL_GPIO_PIN_14, LL_GPIO_AF_1);
    LL_GPIO_SetPinSpeed(GPIOE, LL_GPIO_PIN_14, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinOutputType(GPIOE, LL_GPIO_PIN_14, LL_GPIO_OUTPUT_PUSHPULL);

    /* TEC 3 EN: PJ10 → TIM8_CH2, AF3 */
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOJ);
    LL_GPIO_SetPinMode(GPIOJ, LL_GPIO_PIN_10, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_8_15(GPIOJ, LL_GPIO_PIN_10, LL_GPIO_AF_3);
    LL_GPIO_SetPinSpeed(GPIOJ, LL_GPIO_PIN_10, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinOutputType(GPIOJ, LL_GPIO_PIN_10, LL_GPIO_OUTPUT_PUSHPULL);

    /* PH pins (PE9, PE13, PJ8) stay as GPIO — already init'd by DRV8702_Init */

    /* ---- TIM1: TEC 1 (CH2) + TEC 2 (CH4) ---- */
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM1);

    LL_TIM_SetPrescaler(TIM1, psc);
    LL_TIM_SetAutoReload(TIM1, arr);
    LL_TIM_SetCounterMode(TIM1, LL_TIM_COUNTERMODE_UP);

    /* CH2 — TEC 1 EN */
    LL_TIM_OC_SetMode(TIM1, LL_TIM_CHANNEL_CH2, LL_TIM_OCMODE_PWM1);
    LL_TIM_OC_SetPolarity(TIM1, LL_TIM_CHANNEL_CH2, LL_TIM_OCPOLARITY_HIGH);
    LL_TIM_OC_SetCompareCH2(TIM1, 0U);
    LL_TIM_OC_EnablePreload(TIM1, LL_TIM_CHANNEL_CH2);
    LL_TIM_CC_EnableChannel(TIM1, LL_TIM_CHANNEL_CH2);

    /* CH4 — TEC 2 EN */
    LL_TIM_OC_SetMode(TIM1, LL_TIM_CHANNEL_CH4, LL_TIM_OCMODE_PWM1);
    LL_TIM_OC_SetPolarity(TIM1, LL_TIM_CHANNEL_CH4, LL_TIM_OCPOLARITY_HIGH);
    LL_TIM_OC_SetCompareCH4(TIM1, 0U);
    LL_TIM_OC_EnablePreload(TIM1, LL_TIM_CHANNEL_CH4);
    LL_TIM_CC_EnableChannel(TIM1, LL_TIM_CHANNEL_CH4);

    LL_TIM_EnableAllOutputs(TIM1);   /* MOE required for advanced timer */
    LL_TIM_EnableARRPreload(TIM1);
    LL_TIM_GenerateEvent_UPDATE(TIM1);
    LL_TIM_EnableCounter(TIM1);

    /* ---- TIM8: TEC 3 (CH2) ---- */
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM8);

    LL_TIM_SetPrescaler(TIM8, psc);
    LL_TIM_SetAutoReload(TIM8, arr);
    LL_TIM_SetCounterMode(TIM8, LL_TIM_COUNTERMODE_UP);

    /* CH2 — TEC 3 EN */
    LL_TIM_OC_SetMode(TIM8, LL_TIM_CHANNEL_CH2, LL_TIM_OCMODE_PWM1);
    LL_TIM_OC_SetPolarity(TIM8, LL_TIM_CHANNEL_CH2, LL_TIM_OCPOLARITY_HIGH);
    LL_TIM_OC_SetCompareCH2(TIM8, 0U);
    LL_TIM_OC_EnablePreload(TIM8, LL_TIM_CHANNEL_CH2);
    LL_TIM_CC_EnableChannel(TIM8, LL_TIM_CHANNEL_CH2);

    LL_TIM_EnableAllOutputs(TIM8);
    LL_TIM_EnableARRPreload(TIM8);
    LL_TIM_GenerateEvent_UPDATE(TIM8);
    LL_TIM_EnableCounter(TIM8);

    return TEC_PWM_OK;
}

/* ==========================================================================
 *  TEC_PWM_Set — set direction (PH gpio) and duty cycle (EN pwm)
 * ========================================================================== */
TEC_PWM_Status TEC_PWM_Set(uint8_t instance, TEC_Direction dir, uint8_t duty_pct)
{
    if (instance < 1U || instance > TEC_COUNT) {
        return TEC_PWM_ERR_PARAM;
    }
    if (duty_pct > 100U) {
        duty_pct = 100U;
    }

    DRV8702_Handle *drv = GetDrvHandle(instance);
    if (drv == NULL) return TEC_PWM_ERR_PARAM;

    uint32_t ccr = ((uint32_t)duty_pct * (pwm_arr + 1U)) / 100U;

    switch (dir) {
    case TEC_DIR_HEAT:
        DRV8702_SetDirection(drv, DRV8702_DIR_FORWARD);
        break;
    case TEC_DIR_COOL:
        DRV8702_SetDirection(drv, DRV8702_DIR_REVERSE);
        break;
    case TEC_DIR_OFF:
    default:
        ccr = 0U;
        duty_pct = 0U;
        break;
    }

    /* Set EN PWM duty */
    switch (instance) {
    case TEC_INSTANCE_1:
        LL_TIM_OC_SetCompareCH2(TIM1, ccr);
        break;
    case TEC_INSTANCE_2:
        LL_TIM_OC_SetCompareCH4(TIM1, ccr);
        break;
    case TEC_INSTANCE_3:
        LL_TIM_OC_SetCompareCH2(TIM8, ccr);
        break;
    }

    dir_cache[instance - 1U]  = dir;
    duty_cache[instance - 1U] = duty_pct;

    return TEC_PWM_OK;
}

/* ==========================================================================
 *  TEC_PWM_Get
 * ========================================================================== */
TEC_PWM_Status TEC_PWM_Get(uint8_t instance, TEC_Direction *dir, uint8_t *duty_pct)
{
    if (instance < 1U || instance > TEC_COUNT) {
        return TEC_PWM_ERR_PARAM;
    }
    if (dir != NULL)      *dir      = dir_cache[instance - 1U];
    if (duty_pct != NULL) *duty_pct = duty_cache[instance - 1U];
    return TEC_PWM_OK;
}

/* ==========================================================================
 *  TEC_PWM_Stop / StopAll
 * ========================================================================== */
TEC_PWM_Status TEC_PWM_Stop(uint8_t instance)
{
    return TEC_PWM_Set(instance, TEC_DIR_OFF, 0U);
}

void TEC_PWM_StopAll(void)
{
    TEC_PWM_Stop(TEC_INSTANCE_1);
    TEC_PWM_Stop(TEC_INSTANCE_2);
    TEC_PWM_Stop(TEC_INSTANCE_3);
}
