/*******************************************************************************
 * @file    Src/TEC_PWM.c
 * @author  Cam
 * @brief   TEC PWM Control — TIM1/TIM8 PWM for DRV8702 H-Bridge
 *
 *          DRV8702 has two half-bridges per chip (IN1 → HB1, IN2 → HB2).
 *          Combined they form a full H-bridge for bidirectional TEC current.
 *
 *          Direction control (MODE=1, no current regulation):
 *            IN1=PWM, IN2=0  → current direction A (heat)
 *            IN1=0,   IN2=PWM → current direction B (cool)
 *            IN1=0,   IN2=0  → off (low-side brake)
 *            IN1=1,   IN2=1  → INVALID (both high sides on = fault!)
 *
 *          Timer clock: APB2 timer clock = 240 MHz
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#include "TEC_PWM.h"
#include "stm32h7xx_ll_tim.h"

#define TIM_CLK_HZ      240000000UL

static uint32_t       pwm_arr = 0U;
static TEC_Direction  dir_cache[TEC_COUNT]  = {TEC_DIR_OFF, TEC_DIR_OFF, TEC_DIR_OFF};
static uint8_t        duty_cache[TEC_COUNT] = {0, 0, 0};

/* ==========================================================================
 *  TEC_PWM_Init
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

    /* ---- Reconfigure all IN1/IN2 pins to AF mode ---- */

    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOE);
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOJ);

    /* TEC 1 IN1: PE9 → TIM1_CH1, AF1 */
    LL_GPIO_SetPinMode(GPIOE, LL_GPIO_PIN_9, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_8_15(GPIOE, LL_GPIO_PIN_9, LL_GPIO_AF_1);
    LL_GPIO_SetPinSpeed(GPIOE, LL_GPIO_PIN_9, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinOutputType(GPIOE, LL_GPIO_PIN_9, LL_GPIO_OUTPUT_PUSHPULL);

    /* TEC 1 IN2: PE11 → TIM1_CH2, AF1 */
    LL_GPIO_SetPinMode(GPIOE, LL_GPIO_PIN_11, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_8_15(GPIOE, LL_GPIO_PIN_11, LL_GPIO_AF_1);
    LL_GPIO_SetPinSpeed(GPIOE, LL_GPIO_PIN_11, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinOutputType(GPIOE, LL_GPIO_PIN_11, LL_GPIO_OUTPUT_PUSHPULL);

    /* TEC 2 IN1: PE13 → TIM1_CH3, AF1 */
    LL_GPIO_SetPinMode(GPIOE, LL_GPIO_PIN_13, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_8_15(GPIOE, LL_GPIO_PIN_13, LL_GPIO_AF_1);
    LL_GPIO_SetPinSpeed(GPIOE, LL_GPIO_PIN_13, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinOutputType(GPIOE, LL_GPIO_PIN_13, LL_GPIO_OUTPUT_PUSHPULL);

    /* TEC 2 IN2: PE14 → TIM1_CH4, AF1 */
    LL_GPIO_SetPinMode(GPIOE, LL_GPIO_PIN_14, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_8_15(GPIOE, LL_GPIO_PIN_14, LL_GPIO_AF_1);
    LL_GPIO_SetPinSpeed(GPIOE, LL_GPIO_PIN_14, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinOutputType(GPIOE, LL_GPIO_PIN_14, LL_GPIO_OUTPUT_PUSHPULL);

    /* TEC 3 IN1: PJ8 → TIM8_CH1, AF3 */
    LL_GPIO_SetPinMode(GPIOJ, LL_GPIO_PIN_8, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_8_15(GPIOJ, LL_GPIO_PIN_8, LL_GPIO_AF_3);
    LL_GPIO_SetPinSpeed(GPIOJ, LL_GPIO_PIN_8, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinOutputType(GPIOJ, LL_GPIO_PIN_8, LL_GPIO_OUTPUT_PUSHPULL);

    /* TEC 3 IN2: PJ10 → TIM8_CH2, AF3 */
    LL_GPIO_SetPinMode(GPIOJ, LL_GPIO_PIN_10, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_8_15(GPIOJ, LL_GPIO_PIN_10, LL_GPIO_AF_3);
    LL_GPIO_SetPinSpeed(GPIOJ, LL_GPIO_PIN_10, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinOutputType(GPIOJ, LL_GPIO_PIN_10, LL_GPIO_OUTPUT_PUSHPULL);

    /* ---- TIM1: TEC 1 (CH1+CH2) + TEC 2 (CH3+CH4) ---- */
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM1);

    LL_TIM_SetPrescaler(TIM1, psc);
    LL_TIM_SetAutoReload(TIM1, arr);
    LL_TIM_SetCounterMode(TIM1, LL_TIM_COUNTERMODE_UP);

    /* CH1 — TEC 1 IN1 */
    LL_TIM_OC_SetMode(TIM1, LL_TIM_CHANNEL_CH1, LL_TIM_OCMODE_PWM1);
    LL_TIM_OC_SetPolarity(TIM1, LL_TIM_CHANNEL_CH1, LL_TIM_OCPOLARITY_HIGH);
    LL_TIM_OC_SetCompareCH1(TIM1, 0U);
    LL_TIM_OC_EnablePreload(TIM1, LL_TIM_CHANNEL_CH1);
    LL_TIM_CC_EnableChannel(TIM1, LL_TIM_CHANNEL_CH1);

    /* CH2 — TEC 1 IN2 */
    LL_TIM_OC_SetMode(TIM1, LL_TIM_CHANNEL_CH2, LL_TIM_OCMODE_PWM1);
    LL_TIM_OC_SetPolarity(TIM1, LL_TIM_CHANNEL_CH2, LL_TIM_OCPOLARITY_HIGH);
    LL_TIM_OC_SetCompareCH2(TIM1, 0U);
    LL_TIM_OC_EnablePreload(TIM1, LL_TIM_CHANNEL_CH2);
    LL_TIM_CC_EnableChannel(TIM1, LL_TIM_CHANNEL_CH2);

    /* CH3 — TEC 2 IN1 */
    LL_TIM_OC_SetMode(TIM1, LL_TIM_CHANNEL_CH3, LL_TIM_OCMODE_PWM1);
    LL_TIM_OC_SetPolarity(TIM1, LL_TIM_CHANNEL_CH3, LL_TIM_OCPOLARITY_HIGH);
    LL_TIM_OC_SetCompareCH3(TIM1, 0U);
    LL_TIM_OC_EnablePreload(TIM1, LL_TIM_CHANNEL_CH3);
    LL_TIM_CC_EnableChannel(TIM1, LL_TIM_CHANNEL_CH3);

    /* CH4 — TEC 2 IN2 */
    LL_TIM_OC_SetMode(TIM1, LL_TIM_CHANNEL_CH4, LL_TIM_OCMODE_PWM1);
    LL_TIM_OC_SetPolarity(TIM1, LL_TIM_CHANNEL_CH4, LL_TIM_OCPOLARITY_HIGH);
    LL_TIM_OC_SetCompareCH4(TIM1, 0U);
    LL_TIM_OC_EnablePreload(TIM1, LL_TIM_CHANNEL_CH4);
    LL_TIM_CC_EnableChannel(TIM1, LL_TIM_CHANNEL_CH4);

    LL_TIM_EnableAllOutputs(TIM1);   /* MOE — required for advanced timer */
    LL_TIM_EnableARRPreload(TIM1);
    LL_TIM_GenerateEvent_UPDATE(TIM1);
    LL_TIM_EnableCounter(TIM1);

    /* ---- TIM8: TEC 3 (CH1+CH2) ---- */
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM8);

    LL_TIM_SetPrescaler(TIM8, psc);
    LL_TIM_SetAutoReload(TIM8, arr);
    LL_TIM_SetCounterMode(TIM8, LL_TIM_COUNTERMODE_UP);

    /* CH1 — TEC 3 IN1 */
    LL_TIM_OC_SetMode(TIM8, LL_TIM_CHANNEL_CH1, LL_TIM_OCMODE_PWM1);
    LL_TIM_OC_SetPolarity(TIM8, LL_TIM_CHANNEL_CH1, LL_TIM_OCPOLARITY_HIGH);
    LL_TIM_OC_SetCompareCH1(TIM8, 0U);
    LL_TIM_OC_EnablePreload(TIM8, LL_TIM_CHANNEL_CH1);
    LL_TIM_CC_EnableChannel(TIM8, LL_TIM_CHANNEL_CH1);

    /* CH2 — TEC 3 IN2 */
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
 *  Helper: set CCR values for a TEC instance
 * ========================================================================== */
static void SetChannelPair(uint8_t instance, uint32_t ccr_in1, uint32_t ccr_in2)
{
    switch (instance) {
    case TEC_INSTANCE_1:
        LL_TIM_OC_SetCompareCH1(TIM1, ccr_in1);   /* IN1 = TIM1_CH1 */
        LL_TIM_OC_SetCompareCH2(TIM1, ccr_in2);   /* IN2 = TIM1_CH2 */
        break;
    case TEC_INSTANCE_2:
        LL_TIM_OC_SetCompareCH3(TIM1, ccr_in1);   /* IN1 = TIM1_CH3 */
        LL_TIM_OC_SetCompareCH4(TIM1, ccr_in2);   /* IN2 = TIM1_CH4 */
        break;
    case TEC_INSTANCE_3:
        LL_TIM_OC_SetCompareCH1(TIM8, ccr_in1);   /* IN1 = TIM8_CH1 */
        LL_TIM_OC_SetCompareCH2(TIM8, ccr_in2);   /* IN2 = TIM8_CH2 */
        break;
    }
}

/* ==========================================================================
 *  TEC_PWM_Set
 * ========================================================================== */
TEC_PWM_Status TEC_PWM_Set(uint8_t instance, TEC_Direction dir, uint8_t duty_pct)
{
    if (instance < 1U || instance > TEC_COUNT) {
        return TEC_PWM_ERR_PARAM;
    }
    if (duty_pct > 100U) {
        duty_pct = 100U;
    }

    uint32_t ccr = ((uint32_t)duty_pct * (pwm_arr + 1U)) / 100U;

    switch (dir) {
    case TEC_DIR_HEAT:
        /* PWM on IN1, IN2 = 0 */
        SetChannelPair(instance, ccr, 0U);
        break;
    case TEC_DIR_COOL:
        /* IN1 = 0, PWM on IN2 */
        SetChannelPair(instance, 0U, ccr);
        break;
    case TEC_DIR_OFF:
    default:
        /* Both LOW — bridge off */
        SetChannelPair(instance, 0U, 0U);
        duty_pct = 0U;
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
