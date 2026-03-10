/*******************************************************************************
 * @file    Inc/clock_config.h
 * @author  Cam
 * @brief   Clock Configuration — MCU Init and Clock Tree Setup
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef CLOCK_CONFIG_H
#define CLOCK_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "bsp.h"

/* Public API ----------------------------------------------------------------*/

/**
 * @brief  MCU core initialisation — MPU, SYSCFG, NVIC, flash latency,
 *         voltage scaling.  Call FIRST before ClockTree_Init().
 */
void MCU_Init(void);

/**
 * @brief  Apply the full clock tree from a ClockTree_Config struct.
 *         Enables HSE, configures PLL1/2/3, switches SYSCLK, sets prescalers.
 * @param  clk  Pointer to a const ClockTree_Config (typically &sys_clk_config)
 */
void ClockTree_Init(const ClockTree_Config *clk);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_CONFIG_H */
