/*******************************************************************************
 * @file    Src/clock_config.c
 * @author  Cam
 * @brief   Clock Configuration — MCU Init and Clock Tree Setup
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "clock_config.h"

/* ==========================================================================
 *  MCU INITIALIZATION
 *
 *  Power config, flash latency, MPU, NVIC priority grouping.
 *  Call this FIRST before ClockTree_Init().
 * ========================================================================== */
void MCU_Init(void)
{
    /* ---- MPU: Background region, deny all unprivileged access ---- */
    LL_MPU_Disable();
    LL_MPU_ConfigRegion(
        LL_MPU_REGION_NUMBER0,
        0x87,
        0x0,
        LL_MPU_REGION_SIZE_4GB      |
        LL_MPU_TEX_LEVEL0           |
        LL_MPU_REGION_NO_ACCESS     |
        LL_MPU_INSTRUCTION_ACCESS_DISABLE |
        LL_MPU_ACCESS_SHAREABLE     |
        LL_MPU_ACCESS_NOT_CACHEABLE |
        LL_MPU_ACCESS_NOT_BUFFERABLE
    );
    LL_MPU_Enable(LL_MPU_CTRL_PRIVILEGED_DEFAULT);

    /* ---- SYSCFG clock (required for compensation cell, EXTI, etc.) ---- */
    LL_APB4_GRP1_EnableClock(LL_APB4_GRP1_PERIPH_SYSCFG);

    /* ---- NVIC: 4 bits preemption, 0 bits sub-priority ---- */
    NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    NVIC_SetPriority(SysTick_IRQn,
                     NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 15, 0));

    /* ---- Flash latency ---- */
    LL_FLASH_SetLatency(sys_clk_config.flash_latency);
    while (LL_FLASH_GetLatency() != sys_clk_config.flash_latency);

    /* ---- Power: SMPS + LDO, VOS0 for 480 MHz ---- */
    LL_PWR_ConfigSupply(LL_PWR_SMPS_2V5_SUPPLIES_LDO);
    LL_PWR_SetRegulVoltageScaling(sys_clk_config.voltage_scale);
    while (!LL_PWR_IsActiveFlag_VOS());
}

/* ==========================================================================
 *  CLOCK TREE INITIALIZATION
 *
 *  Applies the entire clock configuration from a ClockTree_Config struct.
 *  Sequence follows STM32H7 reference manual recommendations:
 *    1. Enable HSE
 *    2. Configure & enable PLL1  →  switch SYSCLK
 *    3. Set bus prescalers
 *    4. Configure & enable PLL2, PLL3
 * ========================================================================== */
void ClockTree_Init(const ClockTree_Config *clk)
{
    /* ---- Enable GPIOH for HSE oscillator pins ---- */
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOH);

    /* ---- HSE ---- */
    LL_RCC_HSE_Enable();
    while (LL_RCC_HSE_IsReady() != 1U);

    /* ---- PLL1 ---- */
    LL_RCC_PLL1_Disable();
    while (LL_RCC_PLL1_IsReady() != 0U);

    if (clk->pll1.enable_p) LL_RCC_PLL1P_Enable();
    if (clk->pll1.enable_q) LL_RCC_PLL1Q_Enable();
    if (clk->pll1.enable_r) LL_RCC_PLL1R_Enable();

    LL_RCC_PLL_SetSource(LL_RCC_PLLSOURCE_HSE);
    LL_RCC_PLL1_SetVCOInputRange(clk->pll1.vco_input_range);
    LL_RCC_PLL1_SetVCOOutputRange(clk->pll1.vco_output_range);
    LL_RCC_PLL1_SetM(clk->pll1.divm);
    LL_RCC_PLL1_SetN(clk->pll1.divn);
    LL_RCC_PLL1_SetP(clk->pll1.divp);
    LL_RCC_PLL1_SetQ(clk->pll1.divq);
    LL_RCC_PLL1_SetR(clk->pll1.divr);

    LL_RCC_PLL1_Enable();
    while (LL_RCC_PLL1_IsReady() != 1U);

    /* ---- Set intermediate AHB prescaler before switching SYSCLK ---- */
    LL_RCC_SetAHBPrescaler(LL_RCC_AHB_DIV_2);

    /* ---- Switch SYSCLK to PLL1P ---- */
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL1);
    while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL1);

    /* ---- Bus prescalers (final values) ---- */
    LL_RCC_SetSysPrescaler(clk->prescalers.d1cpre);
    LL_RCC_SetAHBPrescaler(clk->prescalers.hpre);
    LL_RCC_SetAPB3Prescaler(clk->prescalers.d1ppre);
    LL_RCC_SetAPB1Prescaler(clk->prescalers.d2ppre1);
    LL_RCC_SetAPB2Prescaler(clk->prescalers.d2ppre2);
    LL_RCC_SetAPB4Prescaler(clk->prescalers.d3ppre);

    /* ---- SysTick and core clock variable ---- */
    LL_Init1msTick(clk->sysclk_hz);
    LL_SetSystemCoreClock(clk->sysclk_hz);

    /* ---- PLL2 ---- */
    LL_RCC_PLL2_Disable();
    while (LL_RCC_PLL2_IsReady() != 0U);

    if (clk->pll2.enable_p) LL_RCC_PLL2P_Enable();
    if (clk->pll2.enable_q) LL_RCC_PLL2Q_Enable();
    if (clk->pll2.enable_r) LL_RCC_PLL2R_Enable();

    LL_RCC_PLL2_SetVCOInputRange(clk->pll2.vco_input_range);
    LL_RCC_PLL2_SetVCOOutputRange(clk->pll2.vco_output_range);
    LL_RCC_PLL2_SetM(clk->pll2.divm);
    LL_RCC_PLL2_SetN(clk->pll2.divn);
    LL_RCC_PLL2_SetP(clk->pll2.divp);
    LL_RCC_PLL2_SetQ(clk->pll2.divq);
    LL_RCC_PLL2_SetR(clk->pll2.divr);

    LL_RCC_PLL2_Enable();
    while (LL_RCC_PLL2_IsReady() != 1U);

    /* ---- PLL3 ---- */
    LL_RCC_PLL3_Disable();
    while (LL_RCC_PLL3_IsReady() != 0U);

    if (clk->pll3.enable_p) LL_RCC_PLL3P_Enable();
    if (clk->pll3.enable_q) LL_RCC_PLL3Q_Enable();
    if (clk->pll3.enable_r) LL_RCC_PLL3R_Enable();

    LL_RCC_PLL3_SetVCOInputRange(clk->pll3.vco_input_range);
    LL_RCC_PLL3_SetVCOOutputRange(clk->pll3.vco_output_range);
    LL_RCC_PLL3_SetM(clk->pll3.divm);
    LL_RCC_PLL3_SetN(clk->pll3.divn);
    LL_RCC_PLL3_SetP(clk->pll3.divp);
    LL_RCC_PLL3_SetQ(clk->pll3.divq);
    LL_RCC_PLL3_SetR(clk->pll3.divr);

    LL_RCC_PLL3_Enable();
    while (LL_RCC_PLL3_IsReady() != 1U);
}
