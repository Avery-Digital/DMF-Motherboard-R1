/*******************************************************************************
 * @file    Inc/usart_driver.h
 * @author  Cam
 * @brief   USART Driver — Interrupt-Driven DMA TX/RX with Protocol Integration
 *
 *          Reception is fully interrupt-driven:
 *            - DMA HT/TC interrupts catch bulk data
 *            - USART IDLE interrupt catches end-of-packet gaps
 *          All three feed bytes into the protocol parser from ISR context.
 *
 *          Transmission uses DMA normal mode with a TC interrupt to
 *          clear the busy flag when complete.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef USART_DRIVER_H
#define USART_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "bsp.h"
#include "packet_protocol.h"

/* =========================== Public API =================================== */

/**
 * @brief  Initialise USART peripheral, GPIO pins, and both DMA streams.
 * @param  handle  Pointer to a USART_Handle (mutable runtime state)
 * @param  parser  Pointer to a ProtocolParser (stored in handle for ISR use)
 * @return INIT_OK on success, or a bitmask of InitResult error flags
 */
InitResult USART_Driver_Init(USART_Handle *handle, ProtocolParser *parser);

/**
 * @brief  Start circular DMA reception with interrupts.
 *         Enables DMA HT/TC and USART IDLE interrupts.
 *         Call once after USART_Driver_Init().
 * @param  handle  Pointer to a USART_Handle
 */
void USART_Driver_StartRx(USART_Handle *handle);

/**
 * @brief  Build a protocol packet and transmit it via DMA.
 * @param  handle   USART handle
 * @param  msg1     Message type byte 1
 * @param  msg2     Message type byte 2
 * @param  cmd1     Command byte 1
 * @param  cmd2     Command byte 2
 * @param  payload  Payload data
 * @param  length   Payload length
 * @return INIT_OK on success, INIT_ERR_DMA if TX is busy
 */
InitResult USART_Driver_SendPacket(USART_Handle *handle,
                                   uint8_t msg1, uint8_t msg2,
                                   uint8_t cmd1, uint8_t cmd2,
                                   const uint8_t *payload, uint16_t length);

/**
 * @brief  Transmit raw data via DMA (no protocol framing).
 * @param  handle  Pointer to a USART_Handle
 * @param  data    Source data to transmit
 * @param  len     Number of bytes to transmit
 * @return INIT_OK on success, INIT_ERR_DMA if a previous TX is in progress
 */
InitResult USART_Driver_Transmit(USART_Handle *handle,
                                 const uint8_t *data, uint16_t len);

/* ======================== ISR Callbacks ==================================== */

/**
 * @brief  Process new DMA data and feed to parser.
 *         Called from DMA RX HT, DMA RX TC, and USART IDLE ISRs.
 *         Handles ring buffer wrap-around automatically.
 * @param  handle  Pointer to a USART_Handle
 */
void USART_Driver_RxProcessISR(USART_Handle *handle);

/**
 * @brief  DMA TX complete callback.
 *         Called from DMA TX TC ISR.  Clears tx_busy flag.
 * @param  handle  Pointer to a USART_Handle
 */
void USART_Driver_TxCompleteISR(USART_Handle *handle);

#ifdef __cplusplus
}
#endif

#endif /* USART_DRIVER_H */
