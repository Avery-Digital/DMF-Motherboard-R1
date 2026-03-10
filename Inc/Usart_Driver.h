/*******************************************************************************
 * @file    Inc/usart_driver.h
 * @author  Cam
 * @brief   USART Driver — Init, DMA TX/RX, Protocol Integration
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

/* Public API ----------------------------------------------------------------*/

/**
 * @brief  Initialise USART peripheral, GPIO pins, and both DMA streams.
 * @param  handle  Pointer to a USART_Handle (mutable runtime state)
 * @return INIT_OK on success, or a bitmask of InitResult error flags
 */
InitResult USART_Driver_Init(USART_Handle *handle);

/**
 * @brief  Start circular DMA reception.  Call once after USART_Driver_Init().
 * @param  handle  Pointer to a USART_Handle
 */
void USART_Driver_StartRx(USART_Handle *handle);

/**
 * @brief  Poll for new DMA data and feed it into the protocol parser.
 *
 *         Call this from the main loop.  It compares the current DMA
 *         write position (via NDTR) to the last read position, extracts
 *         any new bytes (handling ring buffer wrap), and feeds them to
 *         Protocol_FeedBytes().
 *
 * @param  handle  Pointer to a USART_Handle
 * @param  parser  Pointer to the protocol parser instance
 */
void USART_Driver_PollRx(USART_Handle *handle, ProtocolParser *parser);

/**
 * @brief  Build a protocol packet and transmit it via DMA.
 *
 *         Combines Protocol_BuildPacket() with the DMA transmit.
 *         Handles framing, CRC, byte-stuffing, and DMA fire in one call.
 *
 * @param  handle   USART handle
 * @param  msg1     Message type byte 1
 * @param  msg2     Message type byte 2
 * @param  cmd1    Command byte 1
 * @param  cmd2    Command byte 2
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

/**
 * @brief  DMA TX complete callback — call from DMA TX IRQ handler.
 *         Clears the tx_busy flag so the next transmission can proceed.
 * @param  handle  Pointer to a USART_Handle
 */
void USART_Driver_TxCompleteCallback(USART_Handle *handle);

#ifdef __cplusplus
}
#endif

#endif /* USART_DRIVER_H */
