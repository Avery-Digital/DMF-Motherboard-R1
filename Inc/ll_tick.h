/*******************************************************************************
 * @file    Inc/ll_tick.h
 * @author  Cam
 * @brief   SysTick millisecond counter — LL_IncTick / LL_GetTick
 *
 *          These functions are not provided by this version of the
 *          STM32H7 LL utils package.  We provide them here.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef LL_TICK_H
#define LL_TICK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief  Increment the tick counter.  Call from SysTick_Handler().
 */
void LL_IncTick(void);

/**
 * @brief  Return the current tick count in milliseconds.
 */
uint32_t LL_GetTick(void);

#ifdef __cplusplus
}
#endif

#endif /* LL_TICK_H */
