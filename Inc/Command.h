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

#define CMD_PING            CMD_CODE(0xDE, 0xAD)    /**< Ping / echo test        */
#define CMD_READ_ADC        CMD_CODE(0x0C, 0x01)    /**< Read LTC2338-18 ADC     */
#define CMD_BURST_ADC       CMD_CODE(0x0C, 0x02)    /**< Burst 100x ADC reads    */

/* ---- Load Switch Commands (0x0C10–0x0C19) ----
 *
 *  Payload byte 0: 0x01 = ON, 0x00 = OFF
 *  Response byte 0: new state (0x01 = ON, 0x00 = OFF)
 */
#define CMD_LOAD_VALVE1     CMD_CODE(0x0C, 0x10)    /**< Valve 1 power           */
#define CMD_LOAD_VALVE2     CMD_CODE(0x0C, 0x11)    /**< Valve 2 power           */
#define CMD_LOAD_MICROPLATE CMD_CODE(0x0C, 0x12)    /**< Microplate power        */
#define CMD_LOAD_FAN        CMD_CODE(0x0C, 0x13)    /**< Fan power               */
#define CMD_LOAD_TEC1       CMD_CODE(0x0C, 0x14)    /**< TEC 1 power supply      */
#define CMD_LOAD_TEC2       CMD_CODE(0x0C, 0x15)    /**< TEC 2 power supply      */
#define CMD_LOAD_TEC3       CMD_CODE(0x0C, 0x16)    /**< TEC 3 power supply      */
#define CMD_LOAD_ASSEMBLY   CMD_CODE(0x0C, 0x17)    /**< Assembly station power   */
#define CMD_LOAD_DAUGHTER1  CMD_CODE(0x0C, 0x18)    /**< Daughter board 1 power   */
#define CMD_LOAD_DAUGHTER2  CMD_CODE(0x0C, 0x19)    /**< Daughter board 2 power   */

/* First and last load switch command codes — used for range check in dispatch */
#define CMD_LOAD_FIRST      CMD_LOAD_VALVE1
#define CMD_LOAD_LAST       CMD_LOAD_DAUGHTER2

/* ========================= Burst ADC Constants ============================ */

#define ADC_BURST_COUNT         100U    /**< Number of samples per burst     */
#define ADC_BURST_PAYLOAD_SIZE  400U    /**< 100 samples × 4 bytes each      */

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

/**
 * @brief  Execute a burst ADC read — call from the main loop only.
 *
 *         Performs ADC_BURST_COUNT SPI reads, packs the results into a
 *         400-byte payload, and queues it for UART transmission via
 *         tx_request.  Failed reads are stored as 0xFFFFFFFF sentinel.
 *
 *         Must NOT be called from ISR context (SPI polling + ~300 µs).
 */
void Command_ExecuteBurstADC(void);

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_H */
