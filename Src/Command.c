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
 * ========================================================================== */
static void Command_HandlePing(USART_Handle *handle,
                               const PacketHeader *header,
                               const uint8_t *payload)
{
    (void)payload;  /* Not using the received payload for now */

    /* ---- Edit this array to test different responses ---- */
    static const uint8_t test_payload[] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04
    };

    USART_Driver_SendPacket(handle,
                            header->msg1, header->msg2,
                            header->cmd1, header->cmd2,
                            test_payload, sizeof(test_payload));
}
