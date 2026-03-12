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
#include <string.h>

/* Private handler prototypes ------------------------------------------------*/
static void Command_HandlePing(USART_Handle *handle,
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

    /* ---- Add new command cases here ---- */
    /*
    case CMD_SET_VOLTAGE:
        Command_HandleSetVoltage(handle, header, payload);
        break;

    case CMD_READ_ADC:
        Command_HandleReadADC(handle, header, payload);
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
