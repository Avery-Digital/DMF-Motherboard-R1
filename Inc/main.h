/*******************************************************************************
 * @file    Inc/main.h
 * @author  Cam
 * @brief   Main Header
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef MAIN_H
#define MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "bsp.h"
#include "clock_config.h"
#include "i2c_driver.h"
#include "usart_driver.h"
#include "usb2517.h"

/* Exported functions --------------------------------------------------------*/
void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */
