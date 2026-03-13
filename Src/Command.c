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
#include "VN5T016AH.h"
#include "bsp.h"
#include <string.h>

/* Private handler prototypes ------------------------------------------------*/
static void Command_HandlePing(USART_Handle *handle,
                               const PacketHeader *header,
                               const uint8_t *payload);

static void Command_HandleReadADC(USART_Handle *handle,
                                  const PacketHeader *header,
                                  const uint8_t *payload);

static void Command_HandleBurstADC(USART_Handle *handle,
                                   const PacketHeader *header,
                                   const uint8_t *payload);

static void Command_HandleLoadSwitch(USART_Handle *handle,
                                     const PacketHeader *header,
                                     const uint8_t *payload,
                                     LoadSwitch_ID id);

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

    case CMD_BURST_ADC:
        Command_HandleBurstADC(handle, header, payload);
        break;

    /* ---- Load Switch Commands (0x0C10–0x0C19) ---- */
    case CMD_LOAD_VALVE1:
        Command_HandleLoadSwitch(handle, header, payload, LOAD_VALVE1);
        break;
    case CMD_LOAD_VALVE2:
        Command_HandleLoadSwitch(handle, header, payload, LOAD_VALVE2);
        break;
    case CMD_LOAD_MICROPLATE:
        Command_HandleLoadSwitch(handle, header, payload, LOAD_MICROPLATE);
        break;
    case CMD_LOAD_FAN:
        Command_HandleLoadSwitch(handle, header, payload, LOAD_FAN);
        break;
    case CMD_LOAD_TEC1:
        Command_HandleLoadSwitch(handle, header, payload, LOAD_TEC1_PWR);
        break;
    case CMD_LOAD_TEC2:
        Command_HandleLoadSwitch(handle, header, payload, LOAD_TEC2_PWR);
        break;
    case CMD_LOAD_TEC3:
        Command_HandleLoadSwitch(handle, header, payload, LOAD_TEC3_PWR);
        break;
    case CMD_LOAD_ASSEMBLY:
        Command_HandleLoadSwitch(handle, header, payload, LOAD_ASSEMBLY_STATION);
        break;
    case CMD_LOAD_DAUGHTER1:
        Command_HandleLoadSwitch(handle, header, payload, LOAD_DAUGHTER_1);
        break;
    case CMD_LOAD_DAUGHTER2:
        Command_HandleLoadSwitch(handle, header, payload, LOAD_DAUGHTER_2);
        break;

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

/* ==========================================================================
 *  CMD_BURST_ADC ISR HANDLER (0x0C02) — ISR context
 *
 *  The 100-sample burst (~300 µs of SPI polling) cannot execute inside
 *  an interrupt.  This handler saves the request context and sets the
 *  burst_request.pending flag; the main loop calls Command_ExecuteBurstADC()
 *  to do the actual work.
 * ========================================================================== */
static void Command_HandleBurstADC(USART_Handle *handle,
                                   const PacketHeader *header,
                                   const uint8_t *payload)
{
    (void)handle;
    (void)payload;

    /* Save echo-back header fields so the response mirrors the request */
    burst_request.msg1    = header->msg1;
    burst_request.msg2    = header->msg2;
    burst_request.cmd1    = header->cmd1;
    burst_request.cmd2    = header->cmd2;
    burst_request.pending = true;   /* Must be last — acts as the commit */
}

/* ==========================================================================
 *  CMD_BURST_ADC EXECUTION — main loop context only
 *
 *  Reads ADC_BURST_COUNT (100) samples from the LTC2338-18 via SPI2.
 *  Each sample is packed as a 4-byte little-endian uint32_t.
 *  A failed read is stored as 0xFFFFFFFF so the host can detect partial
 *  failures without discarding the rest of the burst.
 *
 *  Payload layout (400 bytes):
 *    Bytes [4n+0] — sample n bits [7:0]   (LSB)
 *    Bytes [4n+1] — sample n bits [15:8]
 *    Bytes [4n+2] — sample n bits [17:16]
 *    Bytes [4n+3] — 0x00  (reserved) or 0xFF on error
 *
 *  Called by the main loop when burst_request.pending is set.
 * ========================================================================== */
void Command_ExecuteBurstADC(void)
{
    static uint8_t  burst_payload[ADC_BURST_PAYLOAD_SIZE];
    static uint32_t burst_raw[ADC_BURST_COUNT];  /* Debug: raw samples visible in debugger */

    for (uint32_t i = 0U; i < ADC_BURST_COUNT; i++) {
        uint32_t   sample = 0U;
        SPI_Status status = SPI_LTC2338_Read(&spi2_handle, &sample);
        burst_raw[i] = sample;

        uint32_t offset = i * 4U;

        if (status == SPI_OK) {
            burst_payload[offset + 0U] = (uint8_t)( sample        & 0xFFU);
            burst_payload[offset + 1U] = (uint8_t)((sample >>  8U) & 0xFFU);
            burst_payload[offset + 2U] = (uint8_t)((sample >> 16U) & 0x03U);
            burst_payload[offset + 3U] = 0x00U;
        } else {
            /* Sentinel — 0xFFFFFFFF flags a failed read to the host */
            burst_payload[offset + 0U] = 0xFFU;
            burst_payload[offset + 1U] = 0xFFU;
            burst_payload[offset + 2U] = 0xFFU;
            burst_payload[offset + 3U] = 0xFFU;
        }
    }

    tx_request.msg1    = burst_request.msg1;
    tx_request.msg2    = burst_request.msg2;
    tx_request.cmd1    = burst_request.cmd1;
    tx_request.cmd2    = burst_request.cmd2;
    memcpy(tx_request.payload, burst_payload, ADC_BURST_PAYLOAD_SIZE);
    tx_request.length  = ADC_BURST_PAYLOAD_SIZE;
    tx_request.pending = true;  /* Must be last — acts as the commit */
}

/* ==========================================================================
 *  LOAD SWITCH COMMANDS (0x0C10–0x0C19)
 *
 *  Shared handler for all 10 load switch instances.  The LoadSwitch_ID is
 *  passed in from the dispatch switch.
 *
 *  Request payload (1 byte):
 *    Byte 0 — 0x01 = turn ON, 0x00 = turn OFF
 *             If payload length is 0, the command is treated as a status
 *             query (no state change, just report current state).
 *
 *  Response payload (1 byte):
 *    Byte 0 — new state: 0x01 = ON, 0x00 = OFF
 * ========================================================================== */
static void Command_HandleLoadSwitch(USART_Handle *handle,
                                     const PacketHeader *header,
                                     const uint8_t *payload,
                                     LoadSwitch_ID id)
{
    (void)handle;

    /* If payload present, set the requested state */
    if (header->length > 0U && payload != NULL) {
        if (payload[0] != 0x00U) {
            LoadSwitch_On(id);
        } else {
            LoadSwitch_Off(id);
        }
    }

    /* Respond with current state */
    uint8_t response = LoadSwitch_IsOn(id) ? 0x01U : 0x00U;

    tx_request.msg1    = header->msg1;
    tx_request.msg2    = header->msg2;
    tx_request.cmd1    = header->cmd1;
    tx_request.cmd2    = header->cmd2;
    tx_request.payload[0] = response;
    tx_request.length  = 1U;
    tx_request.pending = true;  /* Must be last — acts as the commit */
}
