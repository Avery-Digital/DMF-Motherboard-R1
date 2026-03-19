/*******************************************************************************
 * @file    Inc/DC_Uart_Driver.h
 * @author  Cam
 * @brief   Daughtercard UART Driver — Polled TX + DMA Circular RX
 *
 *          Lightweight UART driver for the 4 daughtercard interfaces.
 *          TX is polled (no DMA), RX uses DMA circular mode with
 *          HT/TC/IDLE interrupts feeding a protocol parser.
 *
 *          Each daughtercard has its own handle, DMA stream, and parser.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef DC_UART_DRIVER_H
#define DC_UART_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bsp.h"
#include "packet_protocol.h"

/* ========================== Public API ==================================== */

/**
 * @brief  Initialise a daughtercard UART — GPIO, peripheral, DMA RX.
 *         Does NOT start reception (call DC_Uart_StartRx after).
 *
 * @param  handle   DC UART handle (config, DMA, buffers)
 * @param  parser   Protocol parser instance for this UART
 * @return INIT_OK on success
 */
InitResult DC_Uart_Init(DC_Uart_Handle *handle, ProtocolParser *parser);

/**
 * @brief  Enable DMA circular RX and IDLE/HT/TC interrupts.
 */
void DC_Uart_StartRx(DC_Uart_Handle *handle);

/**
 * @brief  Process new RX bytes — called from DMA HT/TC and USART IDLE ISRs.
 *         Compares DMA NDTR to last read position, feeds delta to parser.
 */
void DC_Uart_RxProcessISR(DC_Uart_Handle *handle);

/**
 * @brief  Polled transmit — blocks until all bytes are sent.
 *         Safe to call from main loop context only.
 */
void DC_Uart_SendBytes(DC_Uart_Handle *handle,
                        const uint8_t *data, uint16_t len);

/**
 * @brief  Build a framed packet and send it via polled TX.
 *         Uses Protocol_BuildPacket() to frame the data, then
 *         sends the result byte-by-byte.
 *
 *         Must be called from main loop context (blocks).
 */
void DC_Uart_SendPacket(DC_Uart_Handle *handle,
                         uint8_t msg1, uint8_t msg2,
                         uint8_t cmd1, uint8_t cmd2,
                         const uint8_t *payload, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* DC_UART_DRIVER_H */
