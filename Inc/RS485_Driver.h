/**
 * @file  RS485_Driver.h
 * @brief Half-duplex RS485 driver for gantry communication via USART7.
 *
 * Hardware:
 *   PF7 (USART7_TX) -> MAX485 DI (pin 6)
 *   PF6 (USART7_RX) <- MAX485 RO (pin 3)
 *   PF8 (GPIO)       -> MAX485 RE+DE (pins 4+5, tied together)
 *
 * Protocol: ASCII, null-terminated.
 *   TX: "@01COMMAND\0"
 *   RX: "VALUE\0" or "OK\0"
 *
 * Fully polled — no DMA, no interrupts.  9600 baud default.
 */

#ifndef RS485_DRIVER_H
#define RS485_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bsp.h"

/* ========================== Public API ====================================== */

/**
 * @brief  Initialise USART7, GPIO pins, and DE/RE direction pin.
 * @return INIT_OK on success.
 */
InitResult RS485_Init(RS485_Handle *handle);

/**
 * @brief  Send a null-terminated ASCII command and wait for the response.
 *
 * Automatically handles DE/RE toggling for half-duplex.
 * The command string must include the full frame (e.g. "@01VER").
 * A null terminator is appended automatically.
 *
 * @param  handle      RS485 handle
 * @param  cmd         Null-terminated command string (without trailing \0 byte)
 * @param  response    Buffer to receive the response (null-terminated on success)
 * @param  max_len     Size of response buffer
 * @param  timeout_ms  Maximum time to wait for a complete response
 * @return Number of bytes received (excluding null), or 0 on timeout/error.
 */
uint16_t RS485_SendCommand(RS485_Handle *handle,
                            const char *cmd,
                            char *response, uint16_t max_len,
                            uint32_t timeout_ms);

/**
 * @brief  Set DE/RE pin HIGH (transmit mode).
 */
void RS485_SetTx(RS485_Handle *handle);

/**
 * @brief  Set DE/RE pin LOW (receive mode).
 */
void RS485_SetRx(RS485_Handle *handle);

#ifdef __cplusplus
}
#endif

#endif /* RS485_DRIVER_H */
