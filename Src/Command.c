/*******************************************************************************
 * @file    Src/command.c
 * @author  Cam
 * @brief   Command Dispatch and Handlers
 *
 *          Each command handler receives the full packet header and payload,
 *          and has access to the USART handle for sending responses.
 *
 *          To add a new command:
 *            1. Define the command code in command.h
 *            2. Write a static handler function in this file
 *            3. Add a case to Command_Dispatch()
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "command.h"
#include "main.h"
#include "spi_driver.h"
#include "bsp.h"
#include <string.h>

/* Private handler prototypes ------------------------------------------------*/
static void Command_HandlePing(USART_Handle *handle,
                               const PacketHeader *header,
                               const uint8_t *payload);

static void Command_HandleReadADC(USART_Handle *handle,
                                  const PacketHeader *header,
                                  const uint8_t *payload);

/* ==========================================================================
 *  COMMAND DISPATCH
 *
 *  Combines cmd1 (high byte) and cmd2 (low byte) into a 16-bit command
 *  code and routes to the matching handler.  Unknown commands are silently
 *  ignored — add a default response (NACK) if your protocol requires one.
 * ========================================================================== */
void Command_Dispatch(USART_Handle *handle,
                      const PacketHeader *header,
                      const uint8_t *payload)
{
    uint16_t cmd = CMD_CODE(header->cmd1, header->cmd2);

    switch (cmd) {

    case CMD_PING:
        Command_HandlePing(handle, header, payload);
        break;

    case CMD_READ_ADC:
        Command_HandleReadADC(handle, header, payload);
        break;

    /* ---- Add new command cases here ---- */
    /*
    case CMD_SET_VOLTAGE:
        Command_HandleSetVoltage(handle, header, payload);
        break;
    */

    default:
        /* Unknown command — ignore or send NACK */
        break;
    }
}

/* ==========================================================================
 *  CMD_PING (0xDEAD)
 *
 *  Responds with a hardcoded test payload for manual testing.
 *  Edit the test_payload array to send whatever data you need
 *  during bring-up and debugging.
 *
 *  NOTE: Never call USART_Driver_SendPacket() directly from a command
 *  handler — this runs in ISR context.  Always set tx_request and let
 *  the main loop perform the actual DMA transfer.
 * ========================================================================== */
static void Command_HandlePing(USART_Handle *handle,
                               const PacketHeader *header,
                               const uint8_t *payload)
{
    (void)handle;
    (void)payload;

    static const uint8_t test_payload[] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04
    };

    /* Never call SendPacket from ISR — set flag for main loop */
    tx_request.msg1    = header->msg1;
    tx_request.msg2    = header->msg2;
    tx_request.cmd1    = header->cmd1;
    tx_request.cmd2    = header->cmd2;
    memcpy(tx_request.payload, test_payload, sizeof(test_payload));
    tx_request.length  = sizeof(test_payload);
    tx_request.pending = true;  /* Must be last — acts as the commit */
}

/* ==========================================================================
 *  CMD_READ_ADC (0x0C01)
 *
 *  Triggers a single conversion on the LTC2338-18 via SPI2 and returns
 *  the raw 18-bit result in the response payload as a 4-byte little-endian
 *  unsigned integer.
 *
 *  Response payload layout (4 bytes):
 *    Byte 0 — bits [7:0]   (LSB)
 *    Byte 1 — bits [15:8]
 *    Byte 2 — bits [17:16]
 *    Byte 3 — 0x00         (reserved, always zero)
 *
 *  On SPI error, all four payload bytes are set to 0xFF as a sentinel
 *  so the host can distinguish a failed read from a valid zero result.
 *
 *  NOTE: Runs in ISR context — SPI_LTC2338_Read uses polling with timeout
 *  which is acceptable here given the short conversion time (~1 µs) and
 *  the 16 MHz SCK giving a 2 µs transfer window.  Move to a deferred task
 *  if tighter ISR latency budgets are required in future.
 * ========================================================================== */
static void Command_HandleReadADC(USART_Handle *handle,
                                  const PacketHeader *header,
                                  const uint8_t *payload)
{
    (void)handle;
    (void)payload;

    uint32_t adc_result = 0U;
    uint8_t  response[4];

    SPI_Status status = SPI_LTC2338_Read(&spi2_handle, &adc_result);

    if (status == SPI_OK) {
        /* Pack 18-bit result as 4-byte little-endian */
        response[0] = (uint8_t)( adc_result        & 0xFFU);
        response[1] = (uint8_t)((adc_result >>  8U) & 0xFFU);
        response[2] = (uint8_t)((adc_result >> 16U) & 0x03U);  /* Only bits [17:16] valid */
        response[3] = 0x00U;
    } else {
        /* Sentinel — all 0xFF signals a failed read to the host */
        response[0] = 0xFFU;
        response[1] = 0xFFU;
        response[2] = 0xFFU;
        response[3] = 0xFFU;
    }

    /* Never call SendPacket from ISR — set flag for main loop */
    tx_request.msg1    = header->msg1;
    tx_request.msg2    = header->msg2;
    tx_request.cmd1    = header->cmd1;
    tx_request.cmd2    = header->cmd2;
    memcpy(tx_request.payload, response, sizeof(response));
    tx_request.length  = sizeof(response);
    tx_request.pending = true;  /* Must be last — acts as the commit */
}
