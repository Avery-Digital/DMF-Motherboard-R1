/*******************************************************************************
 * @file    Src/packet_protocol.c
 * @author  Cam
 * @brief   Packet Protocol — Framing, Parsing, and Packet Building
 *
 *          Transport-agnostic implementation.  Operates on raw byte streams
 *          without any knowledge of USART, DMA, or hardware registers.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "packet_protocol.h"
#include <stddef.h>

/* ==========================================================================
 *  PRIVATE HELPERS
 * ========================================================================== */

/**
 * @brief  Append a byte to a TX buffer with byte-stuffing.
 *
 *         If the byte matches SOF, EOF, or ESC, it is escaped as:
 *           [ESC] [byte ^ ESC]
 *         Otherwise the byte is written directly.
 *
 * @param  buf  TX buffer
 * @param  idx  Pointer to current write index (updated in place)
 * @param  b    Byte to append
 */
static void AppendEscaped(uint8_t *buf, uint16_t *idx, uint8_t b)
{
    if (b == FRAME_SOF || b == FRAME_EOF || b == FRAME_ESC) {
        buf[(*idx)++] = FRAME_ESC;
        buf[(*idx)++] = b ^ FRAME_ESC;
    } else {
        buf[(*idx)++] = b;
    }
}

/**
 * @brief  Decode the 6-byte header from the assembly buffer into a
 *         PacketHeader struct.
 */
static void DecodeHeader(const uint8_t *buf, PacketHeader *hdr)
{
    hdr->msg1   = buf[0];
    hdr->msg2   = buf[1];
    hdr->length = ((uint16_t)buf[2] << 8) | buf[3];
    hdr->cmd1  = buf[4];
    hdr->cmd2  = buf[5];
}

/* ==========================================================================
 *  PARSER INIT / RESET
 * ========================================================================== */

void Protocol_ParserInit(ProtocolParser *parser,
                         PacketRxCallback callback,
                         void *ctx)
{
    parser->state        = PARSE_WAIT_SOF;
    parser->rx_index     = 0;
    parser->expected_len = 0;
    parser->crc_rx       = 0;
    parser->crc_bytes    = 0;
    parser->on_packet    = callback;
    parser->cb_ctx       = ctx;
    parser->packets_ok   = 0;
    parser->packets_err  = 0;
}

void Protocol_ParserReset(ProtocolParser *parser)
{
    parser->state        = PARSE_WAIT_SOF;
    parser->rx_index     = 0;
    parser->expected_len = 0;
    parser->crc_rx       = 0;
    parser->crc_bytes    = 0;
}

/* ==========================================================================
 *  PARSER — FEED BYTES
 *
 *  This is the core state machine.  It processes one byte at a time through
 *  three states:
 *
 *  PARSE_WAIT_SOF:
 *      Discard everything until SOF is seen, then transition to IN_FRAME.
 *
 *  PARSE_IN_FRAME:
 *      - ESC byte → transition to PARSE_ESCAPED
 *      - EOF byte → validate CRC, invoke callback if valid
 *      - SOF byte → restart frame (handles back-to-back SOF)
 *      - Anything else → store in assembly buffer
 *
 *  PARSE_ESCAPED:
 *      XOR the byte with ESC to recover the original value, store it,
 *      then return to IN_FRAME.
 *
 *  Data bytes are routed based on position:
 *      [0..5]         → header (msg1, msg2, len_hi, len_lo, cmd1, cmd2)
 *      [6..6+len-1]   → payload
 *      next 2 bytes   → CRC (not stored in assembly buffer)
 *
 *  The assembly buffer (rx_buf) holds header + payload only.  CRC bytes
 *  are accumulated separately in crc_rx.  On EOF, we compute CRC over
 *  rx_buf[0..header_size+payload_length-1] and compare to crc_rx.
 * ========================================================================== */

void Protocol_FeedBytes(ProtocolParser *parser,
                        const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        bool store = false;

        switch (parser->state) {

        case PARSE_WAIT_SOF:
            if (b == FRAME_SOF) {
                parser->rx_index     = 0;
                parser->expected_len = 0;
                parser->crc_rx       = 0;
                parser->crc_bytes    = 0;
                parser->state        = PARSE_IN_FRAME;
            }
            continue;

        case PARSE_IN_FRAME:
            if (b == FRAME_ESC) {
                parser->state = PARSE_ESCAPED;
                continue;
            }

            if (b == FRAME_SOF) {
                /* Unexpected SOF — treat as new frame start.
                 * Previous partial frame is silently dropped. */
                parser->rx_index     = 0;
                parser->expected_len = 0;
                parser->crc_rx       = 0;
                parser->crc_bytes    = 0;
                parser->packets_err++;
                continue;
            }

            if (b == FRAME_EOF) {
                /* End of frame — validate */
                uint16_t total_data = PKT_HEADER_SIZE + parser->expected_len;

                if (parser->rx_index >= total_data &&
                    parser->crc_bytes == PKT_CRC_SIZE)
                {
                    /* Compute CRC over header + payload */
                    uint16_t crc_calc = CRC16_Calc(parser->rx_buf, total_data);

                    if (crc_calc == parser->crc_rx) {
                        /* Valid packet */
                        DecodeHeader(parser->rx_buf, &parser->header);
                        parser->packets_ok++;

                        if (parser->on_packet != NULL) {
                            parser->on_packet(
                                &parser->header,
                                &parser->rx_buf[PKT_HEADER_SIZE],
                                parser->cb_ctx
                            );
                        }
                    } else {
                        parser->packets_err++;
                    }
                } else {
                    /* Incomplete frame */
                    parser->packets_err++;
                }

                parser->state = PARSE_WAIT_SOF;
                continue;
            }

            /* Normal data byte */
            store = true;
            break;

        case PARSE_ESCAPED:
            b ^= FRAME_ESC;        /* Unescape */
            parser->state = PARSE_IN_FRAME;
            store = true;
            break;
        }

        if (!store) {
            continue;
        }

        /* ---- Route the unstuffed byte by position ---- */

        /* Bytes [0..5]: header (stored in assembly buffer) */
        /* Bytes [6..6+len-1]: payload (stored in assembly buffer) */
        /* Bytes [6+len..6+len+1]: CRC (accumulated separately) */

        uint16_t data_end = PKT_HEADER_SIZE + parser->expected_len;

        if (parser->rx_index < PKT_HEADER_SIZE) {
            /* Header byte */
            if (parser->rx_index < PKT_RX_BUF_SIZE) {
                parser->rx_buf[parser->rx_index] = b;
            }

            /* After byte 3 (len_lo), decode the expected payload length */
            if (parser->rx_index == 3) {
                parser->expected_len =
                    ((uint16_t)parser->rx_buf[2] << 8) | b;

                /* Sanity check */
                if (parser->expected_len > PKT_MAX_PAYLOAD) {
                    parser->state = PARSE_WAIT_SOF;
                    parser->packets_err++;
                    continue;
                }
            }
        }
        else if (parser->rx_index < data_end) {
            /* Payload byte */
            if (parser->rx_index < PKT_RX_BUF_SIZE) {
                parser->rx_buf[parser->rx_index] = b;
            }
        }
        else if (parser->crc_bytes < PKT_CRC_SIZE) {
            /* CRC byte (big-endian) */
            parser->crc_rx = (parser->crc_rx << 8) | b;
            parser->crc_bytes++;
        }
        else {
            /* Extra bytes after CRC but before EOF — frame error */
            parser->state = PARSE_WAIT_SOF;
            parser->packets_err++;
            continue;
        }

        parser->rx_index++;
    }
}

/* ==========================================================================
 *  BUILD PACKET
 *
 *  Constructs a complete framed packet ready for transmission:
 *    1. Compute CRC over header + payload
 *    2. Write SOF
 *    3. Byte-stuff header, payload, and CRC into tx_buf
 *    4. Write EOF
 *
 *  Returns the total frame size in bytes.
 * ========================================================================== */

uint16_t Protocol_BuildPacket(uint8_t *tx_buf,
                              uint8_t msg1, uint8_t msg2,
                              uint8_t cmd1, uint8_t cmd2,
                              const uint8_t *payload, uint16_t length)
{
    uint16_t idx = 0;

    /* Clamp payload length */
    if (length > PKT_MAX_PAYLOAD) {
        length = PKT_MAX_PAYLOAD;
    }

    /* ---- Build CRC input: header + payload ---- */
    /* Compute CRC incrementally to avoid needing a temp buffer */
    uint16_t crc = CRC16_INIT;
    crc = CRC16_Update(crc, msg1);
    crc = CRC16_Update(crc, msg2);
    crc = CRC16_Update(crc, (length >> 8) & 0xFF);
    crc = CRC16_Update(crc, length & 0xFF);
    crc = CRC16_Update(crc, cmd1);
    crc = CRC16_Update(crc, cmd2);

    for (uint16_t i = 0; i < length; i++) {
        crc = CRC16_Update(crc, payload[i]);
    }

    /* ---- Build framed packet ---- */
    tx_buf[idx++] = FRAME_SOF;

    /* Header */
    AppendEscaped(tx_buf, &idx, msg1);
    AppendEscaped(tx_buf, &idx, msg2);
    AppendEscaped(tx_buf, &idx, (length >> 8) & 0xFF);
    AppendEscaped(tx_buf, &idx, length & 0xFF);
    AppendEscaped(tx_buf, &idx, cmd1);
    AppendEscaped(tx_buf, &idx, cmd2);

    /* Payload */
    for (uint16_t i = 0; i < length; i++) {
        AppendEscaped(tx_buf, &idx, payload[i]);
    }

    /* CRC (big-endian) */
    AppendEscaped(tx_buf, &idx, (crc >> 8) & 0xFF);
    AppendEscaped(tx_buf, &idx, crc & 0xFF);

    tx_buf[idx++] = FRAME_EOF;

    return idx;
}
