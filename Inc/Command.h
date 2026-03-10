/*******************************************************************************
 * @file    Inc/command.h
 * @author  Cam
 * @brief   Command Definitions and Dispatch
 *
 *          Central registry of all supported commands.  Each command is
 *          identified by a 16-bit code formed from cmd1 (high byte) and
 *          cmd2 (low byte).  The dispatcher is called from the packet
 *          reception callback and routes to the appropriate handler.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef COMMAND_H
#define COMMAND_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "packet_protocol.h"
#include "usart_driver.h"

/* ========================== Command Code Macros =========================== */

/**
 * @brief  Combine cmd1 and cmd2 into a single 16-bit command code.
 *         cmd1 is the high byte, cmd2 is the low byte.
 *
 *         Example: CMD_CODE(0xBE, 0xEF) → 0xBEEF
 */
#define CMD_CODE(c1, c2)    ((uint16_t)((c1) << 8) | (c2))

/* ========================= Command Definitions ============================ */

#define CMD_PING            CMD_CODE(0xDE, 0xAD)    /**< Ping / echo test   */


/* Add new commands here as you build out the protocol:
 *
 * #define CMD_SET_VOLTAGE     CMD_CODE(0x10, 0x01)
 * #define CMD_READ_ADC        CMD_CODE(0x10, 0x02)
 * #define CMD_RESET           CMD_CODE(0xFF, 0x00)
 */

/* =========================== Public API =================================== */

/**
 * @brief  Dispatch a received packet to the appropriate command handler.
 *
 *         Called from the protocol parser callback (OnPacketReceived in
 *         main.c).  Decodes the 16-bit command from header->cmd1/cmd2
 *         and routes to the matching handler function.
 *
 * @param  handle   USART handle for sending responses
 * @param  header   Decoded packet header
 * @param  payload  Payload data (header->length bytes)
 */
void Command_Dispatch(USART_Handle *handle,
                      const PacketHeader *header,
                      const uint8_t *payload);

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_H */
