/*******************************************************************************
 * @file    Inc/packet_protocol.h
 * @author  Cam
 * @brief   Packet Protocol — Framing, Parsing, and Packet Building
 *
 *          Packet format (after byte-unstuffing):
 *          [SOF] [MSG1] [MSG2] [LEN_HI] [LEN_LO] [CMD1] [CMD2]
 *                [PAYLOAD ...] [CRC_HI] [CRC_LO] [EOF]
 *
 *          Byte stuffing:  If a data byte equals SOF, EOF, or ESC,
 *          transmit ESC followed by (byte ^ ESC).
 *
 *          CRC-16 CCITT is computed over the 6-byte header + payload
 *          (everything between SOF/EOF, after unstuffing, excluding CRC).
 *
 *          This module is transport-agnostic — it knows nothing about
 *          USART, DMA, or any hardware.  Feed it raw bytes from any source.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef PACKET_PROTOCOL_H
#define PACKET_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "crc16.h"

/* =========================== Frame Constants ============================== */

#define FRAME_SOF           0x02U       /**< Start of frame marker           */
#define FRAME_EOF           0x7EU       /**< End of frame marker             */
#define FRAME_ESC           0x2DU       /**< Escape character                */

#define PKT_HEADER_SIZE     6U          /**< msg1+msg2+len_hi+len_lo+cmd1+cmd2 */
#define PKT_CRC_SIZE        2U          /**< CRC-16 = 2 bytes                */
#define PKT_MAX_PAYLOAD     4096U       /**< Maximum payload length          */

/** Worst-case TX buffer: SOF + every byte escaped (×2) + EOF */
#define PKT_TX_BUF_SIZE     (1U + (PKT_HEADER_SIZE + PKT_MAX_PAYLOAD + PKT_CRC_SIZE) * 2U + 1U)

/** RX assembly buffer: header + max payload + CRC */
#define PKT_RX_BUF_SIZE     (PKT_HEADER_SIZE + PKT_MAX_PAYLOAD + PKT_CRC_SIZE)

/* ============================ Packet Header =============================== */

/**
 * @brief  Decoded packet header.
 *         Populated by the parser when a complete, CRC-valid packet arrives.
 */
typedef struct {
    uint8_t     msg1;           /**< Message type byte 1                     */
    uint8_t     msg2;           /**< Message type byte 2                     */
    uint16_t    length;         /**< Payload length (big-endian decoded)      */
    uint8_t     cmd1;          /**< Command byte 1                          */
    uint8_t     cmd2;          /**< Command byte 2                          */
} PacketHeader;

/* ========================== Parser State Machine ========================== */

/**
 * @brief  Parser internal states.
 */
typedef enum {
    PARSE_WAIT_SOF,             /**< Waiting for SOF to start a new frame    */
    PARSE_IN_FRAME,             /**< Receiving frame data                    */
    PARSE_ESCAPED,              /**< Previous byte was ESC, next is XOR'd   */
} ParseState;

/**
 * @brief  Callback type invoked when a complete, CRC-valid packet is received.
 *
 * @param  header   Pointer to the decoded header fields
 * @param  payload  Pointer to the payload data (header->length bytes)
 * @param  ctx      User-supplied context pointer (passed through from init)
 */
typedef void (*PacketRxCallback)(const PacketHeader *header,
                                 const uint8_t *payload,
                                 void *ctx);

/**
 * @brief  Protocol parser instance.
 *
 *         All state is encapsulated here — no globals.  You can instantiate
 *         multiple parsers for multiple USART channels.
 */
typedef struct {
    /* State machine */
    ParseState      state;
    uint16_t        rx_index;       /**< Write index into rx_buf             */
    uint16_t        expected_len;   /**< Payload length from header          */
    uint16_t        crc_rx;         /**< CRC received in the packet          */
    uint8_t         crc_bytes;      /**< How many CRC bytes collected (0–2)  */

    /* Assembly buffer (unstuffed data: header + payload) */
    uint8_t         rx_buf[PKT_RX_BUF_SIZE];

    /* Decoded header (populated on valid packet) */
    PacketHeader    header;

    /* Application callback */
    PacketRxCallback  on_packet;    /**< Called on valid packet reception     */
    void             *cb_ctx;       /**< Context pointer passed to callback  */

    /* Statistics (optional, useful for debugging) */
    uint32_t        packets_ok;     /**< Valid packets received              */
    uint32_t        packets_err;    /**< CRC failures or framing errors      */
} ProtocolParser;

/* ============================ Public API ================================== */

/**
 * @brief  Initialise a protocol parser instance.
 * @param  parser     Parser to initialise
 * @param  callback   Function called on valid packet reception (may be NULL)
 * @param  ctx        User context pointer passed to callback
 */
void Protocol_ParserInit(ProtocolParser *parser,
                         PacketRxCallback callback,
                         void *ctx);

/**
 * @brief  Reset the parser state (e.g. after an error or timeout).
 * @param  parser     Parser to reset
 */
void Protocol_ParserReset(ProtocolParser *parser);

/**
 * @brief  Feed raw bytes into the parser.
 *
 *         Call this from the DMA poll function or IDLE interrupt handler.
 *         The parser handles byte-unstuffing, header decoding, CRC
 *         verification, and invokes the callback for each valid packet.
 *
 * @param  parser  Parser instance
 * @param  data    Raw byte stream (may contain partial/multiple packets)
 * @param  len     Number of bytes in data
 */
void Protocol_FeedBytes(ProtocolParser *parser,
                        const uint8_t *data, uint32_t len);

/**
 * @brief  Build a complete framed packet into a TX buffer.
 *
 *         Computes CRC, applies byte-stuffing, adds SOF/EOF delimiters.
 *         The caller is responsible for transmitting the resulting buffer.
 *
 * @param  tx_buf    Output buffer (must be at least PKT_TX_BUF_SIZE bytes)
 * @param  msg1      Message type byte 1
 * @param  msg2      Message type byte 2
 * @param  cmd1     Command byte 1
 * @param  cmd2     Command byte 2
 * @param  payload   Payload data
 * @param  length    Payload length in bytes
 * @return Number of bytes written to tx_buf (total frame size)
 */
uint16_t Protocol_BuildPacket(uint8_t *tx_buf,
                              uint8_t msg1, uint8_t msg2,
                              uint8_t cmd1, uint8_t cmd2,
                              const uint8_t *payload, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* PACKET_PROTOCOL_H */
