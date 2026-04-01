/*******************************************************************************
 * @file    Inc/Act_Uart_Driver.h
 * @author  Cam
 * @brief   Actuator Board UART Driver — Polled TX + DMA Circular RX + RS485 DE
 *
 *          Two instances (UART5 for ACT1, USART6 for ACT2).
 *          TX is polled with inverted DE toggling (NOT gate on PCB):
 *            GPIO LOW  = transmit
 *            GPIO HIGH = receive (idle)
 *          RX uses DMA circular mode with HT/TC/IDLE interrupts.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef ACT_UART_DRIVER_H
#define ACT_UART_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bsp.h"
#include "packet_protocol.h"

InitResult Act_Uart_Init(Act_Uart_Handle *handle, ProtocolParser *parser);
void Act_Uart_StartRx(Act_Uart_Handle *handle);
void Act_Uart_RxProcessISR(Act_Uart_Handle *handle);

void Act_Uart_SendBytes(Act_Uart_Handle *handle,
                        const uint8_t *data, uint16_t len);

void Act_Uart_SendPacket(Act_Uart_Handle *handle,
                         uint8_t msg1, uint8_t msg2,
                         uint8_t cmd1, uint8_t cmd2,
                         const uint8_t *payload, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* ACT_UART_DRIVER_H */
