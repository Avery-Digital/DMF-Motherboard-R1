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
#include "spi_driver.h"
#include "usart_driver.h"
#include "usb2517.h"
#include <stdint.h>
#include <stdbool.h>
#include "packet_protocol.h"

typedef struct {
    volatile bool   pending;
    uint8_t         msg1, msg2, cmd1, cmd2;
    uint8_t         payload[PKT_MAX_PAYLOAD];
    uint16_t        length;
} TxRequest;

extern TxRequest tx_request;
/* Exported functions --------------------------------------------------------*/
void Error_Handler(uint32_t fault_code);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */
