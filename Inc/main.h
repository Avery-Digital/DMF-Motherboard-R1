/*******************************************************************************
 * @file    Inc/main.h
 * @author  Cam
 * @brief   Main Header
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef MAIN_H
#define MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "bsp.h"
#include "clock_config.h"
#include "i2c_driver.h"
#include "spi_driver.h"
#include "usart_driver.h"
#include "usb2517.h"
#include <stdint.h>
#include <stdbool.h>
#include "packet_protocol.h"
#include "command.h"

typedef struct {
    volatile bool   pending;
    uint8_t         msg1, msg2, cmd1, cmd2;
    uint8_t         payload[PKT_MAX_PAYLOAD];
    uint16_t        length;
} TxRequest;

/**
 * @brief  Deferred request for commands that are too heavy to execute in
 *         ISR context (e.g. burst ADC reads).  The ISR handler populates
 *         this struct and sets pending; the main loop does the actual work.
 */
typedef struct {
    volatile bool   pending;
    uint8_t         msg1;
    uint8_t         msg2;
    uint8_t         cmd1;
    uint8_t         cmd2;
} BurstRequest;

/* =================== Daughtercard Forward Request ========================= */

/**
 * @brief  Deferred request to forward a packet to a daughtercard UART.
 *         ISR extracts boardID from payload[0], populates this struct.
 *         Main loop sends via DC_Uart_SendPacket().
 */
typedef struct {
    volatile bool   pending;
    uint8_t         msg1, msg2, cmd1, cmd2;
    uint8_t         payload[PKT_MAX_PAYLOAD];
    uint16_t        length;
    uint8_t         board_id;       /**< 0–3 → dc1..dc4 handle              */
} DcForwardRequest;

/* =================== Actuator Board Forward Request ======================= */

/**
 * @brief  Deferred request to forward a packet to an actuator board UART.
 *         ISR extracts boardID from payload[0]:
 *           boardID 1 → ACT1 (UART5), boardID 2 → ACT2 (USART6).
 *         Main loop sends via Act_Uart_SendPacket().
 */
typedef struct {
    volatile bool   pending;
    uint8_t         msg1, msg2, cmd1, cmd2;
    uint8_t         payload[PKT_MAX_PAYLOAD];
    uint16_t        length;
    uint8_t         board_id;       /**< 1 or 2 → act1 or act2 handle       */
} ActForwardRequest;

extern ActForwardRequest act_forward_request;

/**
 * @brief  Deferred request for SET_LIST_OF_SW / GET_LIST_OF_SW.
 *         These require synchronous per-group processing in the main loop.
 */
typedef struct {
    volatile bool   pending;
    uint8_t         msg1, msg2, cmd1, cmd2;
    uint8_t         payload[PKT_MAX_PAYLOAD];
    uint16_t        length;
    uint8_t         mode;           /**< 1 = SET_LIST (5B groups), 2 = GET_LIST (4B groups) */
} DcListRequest;

/**
 * @brief  Response mailbox for synchronous daughtercard operations.
 *         OnDC_PacketReceived deposits here during list processing.
 */
typedef struct {
    volatile bool   ready;
    uint8_t         payload[PKT_MAX_PAYLOAD];
    uint16_t        length;
    uint8_t         cmd1, cmd2;
} DcResponse;

/* =================== Measure ADC Request ================================= */

/**
 * @brief  Deferred request for CMD_MEASURE_ADC (0x0C03).
 *         ISR copies switch groups and delay, main loop executes the full
 *         save → GND → set → timer → ADC → restore → Vpp sequence.
 */
typedef struct {
    volatile bool   pending;
    uint8_t         msg1, msg2, cmd1, cmd2;
    uint8_t         sw_payload[PKT_MAX_PAYLOAD]; /**< Switch groups (5B each)  */
    uint16_t        sw_length;                    /**< Total switch payload bytes */
    uint16_t        delay_ms;                     /**< Deterministic delay in ms  */
} MeasureAdcRequest;

extern MeasureAdcRequest measure_adc_request;

/* =================== Gantry RS485 Request ================================ */

/**
 * @brief  Deferred request to send an ASCII command to the gantry via RS485.
 *         ISR copies the payload string, main loop calls RS485_SendCommand().
 */
typedef struct {
    volatile bool   pending;
    uint8_t         msg1, msg2, cmd1, cmd2;
    char            cmd_str[GANTRY_RESPONSE_MAX];   /**< Null-terminated ASCII cmd */
    uint16_t        cmd_len;
} GantryRequest;

extern GantryRequest    gantry_request;

void Command_ExecuteGantry(void);

#define DC_LIST_MODE_SET    1U      /**< SET_LIST_OF_SW (5-byte groups)      */
#define DC_LIST_MODE_GET    2U      /**< GET_LIST_OF_SW (4-byte groups)      */
#define DC_RESPONSE_TIMEOUT 10U     /**< Timeout in ms for async responses   */
#define DC_LIST_TIMEOUT     100U    /**< Timeout in ms for batched list cmds */
#define DC_MAX_BOARDS       4U      /**< Number of daughtercard slots        */

extern TxRequest        tx_request;
extern BurstRequest     burst_request;
extern DcForwardRequest dc_forward_request;
extern DcListRequest    dc_list_request;
extern DcResponse       dc_response;
extern volatile bool    dc_list_active;

/* Exported functions --------------------------------------------------------*/
void Error_Handler(uint32_t fault_code);
void Command_ExecuteDcList(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */
