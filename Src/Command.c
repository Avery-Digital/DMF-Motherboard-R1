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
#include "ADS7066.h"
#include "DC_Uart_Driver.h"
#include "bsp.h"
#include <string.h>
#include "Thermistor.h"
#include "RS485_Driver.h"
#include "TEC_PWM.h"
#include "DRV8702.h"

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

static void Command_HandleThermistor(USART_Handle *handle,
                                      const PacketHeader *header,
                                      const uint8_t *payload,
                                      uint8_t channel);

static void Command_HandleCurrentSense(USART_Handle *handle,
                                        const PacketHeader *header,
                                        const uint8_t *payload,
                                        ADS7066_Handle *adc_handle,
                                        uint8_t channel);

static void Command_HandleGetBoardType(USART_Handle *handle,
                                        const PacketHeader *header,
                                        const uint8_t *payload);

static void Command_HandleDcForward(const PacketHeader *header,
                                     const uint8_t *payload);

static void Command_HandleDcSetList(const PacketHeader *header,
                                     const uint8_t *payload);

static void Command_HandleDcGetList(const PacketHeader *header,
                                     const uint8_t *payload);

static void Command_HandleGantry(USART_Handle *handle,
                                  const PacketHeader *header,
                                  const uint8_t *payload);

static void Command_HandleActForward(const PacketHeader *header,
                                      const uint8_t *payload);

static void Command_HandleMeasureADC(USART_Handle *handle,
                                      const PacketHeader *header,
                                      const uint8_t *payload);

static void Command_HandleSweepADC(USART_Handle *handle,
                                    const PacketHeader *header,
                                    const uint8_t *payload);

static void Command_HandleTecSet(const PacketHeader *header,
                                  const uint8_t *payload);

static void Command_HandleTecGet(const PacketHeader *header,
                                  const uint8_t *payload);

static void Command_HandleTecStop(const PacketHeader *header,
                                   const uint8_t *payload);

static void Command_HandleTecStopAll(const PacketHeader *header);

static void Command_HandleTecReset(const PacketHeader *header,
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

    case CMD_BURST_ADC:
        Command_HandleBurstADC(handle, header, payload);
        break;

    case CMD_MEASURE_ADC:
        Command_HandleMeasureADC(handle, header, payload);
        break;

    case CMD_SWEEP_ADC:
        Command_HandleSweepADC(handle, header, payload);
        break;

    case CMD_PWM_SYNC:
    {
        /* Pulse GPIO sync lines — resets PWM timers on all driver boards */
        PWM_SyncPulse();

        uint8_t response[2] = { STATUS_CAT_OK, STATUS_CODE_OK };
        tx_request.msg1    = header->msg1;
        tx_request.msg2    = header->msg2;
        tx_request.cmd1    = header->cmd1;
        tx_request.cmd2    = header->cmd2;
        memcpy(tx_request.payload, response, sizeof(response));
        tx_request.length  = sizeof(response);
        tx_request.pending = true;
        break;
    }

    /* ---- TEC Control Commands (0x0C50–0x0C53) ---- */
    case CMD_TEC_SET:
        Command_HandleTecSet(header, payload);
        break;
    case CMD_TEC_GET:
        Command_HandleTecGet(header, payload);
        break;
    case CMD_TEC_STOP:
        Command_HandleTecStop(header, payload);
        break;
    case CMD_TEC_STOP_ALL:
        Command_HandleTecStopAll(header);
        break;
    case CMD_TEC_RESET:
        Command_HandleTecReset(header, payload);
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

    /* ---- Thermistor Commands (ADS7066 instance 3, ch 0–5) ---- */
    case CMD_THERM1:
        Command_HandleThermistor(handle, header, payload, 0);
        break;
    case CMD_THERM2:
        Command_HandleThermistor(handle, header, payload, 1);
        break;
    case CMD_THERM3:
        Command_HandleThermistor(handle, header, payload, 2);
        break;
    case CMD_THERM4:
        Command_HandleThermistor(handle, header, payload, 3);
        break;
    case CMD_THERM5:
        Command_HandleThermistor(handle, header, payload, 4);
        break;
    case CMD_THERM6:
        Command_HandleThermistor(handle, header, payload, 5);
        break;

    /* ---- Load Switch Current Sense (0x0C40–0x0C49) ---- */
    case CMD_CURR_VALVE1:
        Command_HandleCurrentSense(handle, header, payload, &ads7066_2_handle, 6);
        break;
    case CMD_CURR_VALVE2:
        Command_HandleCurrentSense(handle, header, payload, &ads7066_2_handle, 7);
        break;
    case CMD_CURR_MICROPLATE:
        Command_HandleCurrentSense(handle, header, payload, &ads7066_1_handle, 2);
        break;
    case CMD_CURR_FAN:
        Command_HandleCurrentSense(handle, header, payload, &ads7066_1_handle, 3);
        break;
    case CMD_CURR_TEC1:
        Command_HandleCurrentSense(handle, header, payload, &ads7066_2_handle, 0);
        break;
    case CMD_CURR_TEC2:
        Command_HandleCurrentSense(handle, header, payload, &ads7066_2_handle, 1);
        break;
    case CMD_CURR_TEC3:
        Command_HandleCurrentSense(handle, header, payload, &ads7066_1_handle, 0);
        break;
    case CMD_CURR_ASSEMBLY:
        Command_HandleCurrentSense(handle, header, payload, &ads7066_3_handle, 6);
        break;
    case CMD_CURR_DAUGHTER1:
        Command_HandleCurrentSense(handle, header, payload, &ads7066_1_handle, 4);
        break;
    case CMD_CURR_DAUGHTER2:
        Command_HandleCurrentSense(handle, header, payload, &ads7066_1_handle, 5);
        break;

    /* ---- Gantry RS485 Passthrough (0x0C30) ---- */
    case CMD_GANTRY_CMD:
        Command_HandleGantry(handle, header, payload);
        break;

    /* ---- Driverboard Debug Command (0xBEEF) ---- */
    case CMD_DC_DEBUG:
        Command_HandleDcForward(header, payload);
        break;

    /* ---- Driverboard Bulk Switch Commands (synchronous) ---- */
    case CMD_DC_SET_LIST_SW:
        Command_HandleDcSetList(header, payload);
        break;
    case CMD_DC_GET_LIST_SW:
        Command_HandleDcGetList(header, payload);
        break;

    /* ---- Board Identity (0x0B99) — motherboard intercepts, does NOT forward ---- */
    case CMD_GET_BOARD_TYPE:
        Command_HandleGetBoardType(handle, header, payload);
        break;

    default:
        /* Check if command falls in driverboard range (0x0A00–0x0BFF) */
        if (cmd >= CMD_DC_RANGE_START && cmd <= CMD_DC_RANGE_END) {
            Command_HandleDcForward(header, payload);
        }
        /* Check if command falls in actuator board range (0x0F00–0x10FF) */
        else if (cmd >= CMD_ACT_RANGE_START && cmd <= CMD_ACT_RANGE_END) {
            Command_HandleActForward(header, payload);
        }
        /* Unknown command — ignore */
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

    /* Response: [status1][status2]["MB_R1 v1.0.0"] */
    uint8_t response[2 + 12];
    response[0] = STATUS_CAT_OK;
    response[1] = STATUS_CODE_OK;
    response[2]  = 'M';  response[3]  = 'B';  response[4]  = '_';
    response[5]  = 'R';  response[6]  = '1';  response[7]  = ' ';
    response[8]  = 'v';
    response[9]  = '0' + FW_VERSION_MAJOR;
    response[10] = '.';
    response[11] = '0' + FW_VERSION_MINOR;
    response[12] = '.';
    response[13] = '0' + FW_VERSION_PATCH;

    tx_request.msg1    = header->msg1;
    tx_request.msg2    = header->msg2;
    tx_request.cmd1    = header->cmd1;
    tx_request.cmd2    = header->cmd2;
    memcpy(tx_request.payload, response, sizeof(response));
    tx_request.length  = sizeof(response);
    tx_request.pending = true;
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
    uint8_t  response[6];  /* [status1][status2][4-byte ADC LE] */

    response[0] = STATUS_CAT_OK;
    response[1] = STATUS_CODE_OK;

    SPI_Status status = SPI_LTC2338_Read(&spi2_handle, &adc_result);

    if (status == SPI_OK) {
        response[2] = (uint8_t)( adc_result        & 0xFFU);
        response[3] = (uint8_t)((adc_result >>  8U) & 0xFFU);
        response[4] = (uint8_t)((adc_result >> 16U) & 0x03U);
        response[5] = 0x00U;
    } else {
        response[0] = STATUS_CAT_ADC;
        response[1] = STATUS_ADC_SPI_FAIL;
        response[2] = 0xFFU;
        response[3] = 0xFFU;
        response[4] = 0xFFU;
        response[5] = 0xFFU;
    }

    tx_request.msg1    = header->msg1;
    tx_request.msg2    = header->msg2;
    tx_request.cmd1    = header->cmd1;
    tx_request.cmd2    = header->cmd2;
    memcpy(tx_request.payload, response, sizeof(response));
    tx_request.length  = sizeof(response);
    tx_request.pending = true;
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

    for (uint32_t i = 0U; i < ADC_BURST_COUNT; i++) {
        uint32_t   sample = 0U;
        SPI_Status status = SPI_LTC2338_Read(&spi2_handle, &sample);

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
    tx_request.payload[0] = STATUS_CAT_OK;
    tx_request.payload[1] = STATUS_CODE_OK;
    memcpy(&tx_request.payload[2], burst_payload, ADC_BURST_PAYLOAD_SIZE);
    tx_request.length  = 2U + ADC_BURST_PAYLOAD_SIZE;
    tx_request.pending = true;
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
 *    Byte 0 — new state: 0x01 = ON, 0x00 = OFF11
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

    /* Response: [status1][status2][state] */
    uint8_t response[3];
    response[0] = STATUS_CAT_OK;
    response[1] = STATUS_CODE_OK;
    response[2] = LoadSwitch_IsOn(id) ? 0x01U : 0x00U;

    tx_request.msg1    = header->msg1;
    tx_request.msg2    = header->msg2;
    tx_request.cmd1    = header->cmd1;
    tx_request.cmd2    = header->cmd2;
    memcpy(tx_request.payload, response, sizeof(response));
    tx_request.length  = sizeof(response);
    tx_request.pending = true;
}

/* ==========================================================================
 *  THERMISTOR COMMANDS (0x0C20–0x0C25) — ISR context
 *
 *  Reads ADS7066 instance 3 on the specified channel (0–5).
 *  Each channel is connected to a thermistor circuit.
 *
 *  Response payload (4 bytes):
 *    IEEE 754 float, little-endian — temperature in degrees C.
 *    Returns NaN on read error or if the thermistor is absent.
 * ========================================================================== */
static void Command_HandleThermistor(USART_Handle *handle,
                                      const PacketHeader *header,
                                      const uint8_t *payload,
                                      uint8_t channel)
{
    (void)handle;
    (void)payload;

    uint16_t adc_result = 0U;
    float    temp_c;
    uint8_t  response[6];  /* [status1][status2][4-byte float LE] */

    response[0] = STATUS_CAT_OK;
    response[1] = STATUS_CODE_OK;

    ADS7066_Status status = ADS7066_ReadChannel(&ads7066_3_handle,
                                                 channel, &adc_result);

    if (status == ADS7066_OK) {
        temp_c = Thermistor_AdcToTempC(adc_result);
    } else {
        response[0] = STATUS_CAT_ADC;
        response[1] = STATUS_ADS_READ_FAIL;
        uint32_t nan_bits = 0x7FC00000U;
        memcpy(&temp_c, &nan_bits, sizeof(temp_c));
    }

    memcpy(&response[2], &temp_c, sizeof(float));

    tx_request.msg1    = header->msg1;
    tx_request.msg2    = header->msg2;
    tx_request.cmd1    = header->cmd1;
    tx_request.cmd2    = header->cmd2;
    memcpy(tx_request.payload, response, sizeof(response));
    tx_request.length  = sizeof(response);
    tx_request.pending = true;
}

/* ==========================================================================
 *  LOAD SWITCH CURRENT SENSE (0x0C40–0x0C49) — ISR context
 *
 *  Reads VN5T016AH CSENSE voltage via 1 kΩ to GND → ADS7066.
 *  Returns V_SENSE in millivolts as a float.
 *
 *  V_SENSE is proportional to I_OUT:
 *    I_SENSE = I_OUT / kILIS
 *    V_SENSE = I_SENSE × 1 kΩ
 *
 *  kILIS varies with load current (non-linear), so we return the raw
 *  sense voltage and let the host apply calibration if needed.
 *
 *  Response: [status1][status2][v_sense_mV float LE (4B)]
 * ========================================================================== */
static void Command_HandleCurrentSense(USART_Handle *handle,
                                        const PacketHeader *header,
                                        const uint8_t *payload,
                                        ADS7066_Handle *adc_handle,
                                        uint8_t channel)
{
    (void)handle;
    (void)payload;

    uint16_t adc_result = 0U;
    float    v_sense_mV;
    uint8_t  response[6];  /* [status1][status2][4-byte float LE] */

    response[0] = STATUS_CAT_OK;
    response[1] = STATUS_CODE_OK;

    ADS7066_Status status = ADS7066_ReadChannel(adc_handle, channel, &adc_result);

    if (status == ADS7066_OK) {
        /* V_SENSE in mV = (ADC / 65536) × 2500 mV */
        v_sense_mV = ((float)adc_result / CSENSE_ADC_CODES) * (CSENSE_VREF * 1000.0f);
    } else {
        response[0] = STATUS_CAT_ADC;
        response[1] = STATUS_ADS_READ_FAIL;
        uint32_t nan_bits = 0x7FC00000U;
        memcpy(&v_sense_mV, &nan_bits, sizeof(v_sense_mV));
    }

    memcpy(&response[2], &v_sense_mV, sizeof(float));

    tx_request.msg1    = header->msg1;
    tx_request.msg2    = header->msg2;
    tx_request.cmd1    = header->cmd1;
    tx_request.cmd2    = header->cmd2;
    memcpy(tx_request.payload, response, sizeof(response));
    tx_request.length  = sizeof(response);
    tx_request.pending = true;
}

/* ==========================================================================
 *  CMD_TEC_SET (0x0C50) — ISR context
 *
 *  Payload: [tec_id (1-3)] [direction (0=OFF,1=HEAT,2=COOL)] [duty_pct (0-100)]
 *  Response: [status1][status2][tec_id][direction][duty_pct]
 * ========================================================================== */
static void Command_HandleTecSet(const PacketHeader *header,
                                  const uint8_t *payload)
{
    uint8_t r[5] = { STATUS_CAT_OK, STATUS_CODE_OK, 0, 0, 0 };

    if (header->length < 3U || payload == NULL) {
        r[0] = STATUS_CAT_GENERAL;
        r[1] = STATUS_PAYLOAD_SHORT;
        goto reply;
    }

    uint8_t tec_id   = payload[0];
    uint8_t dir      = payload[1];
    uint8_t duty_pct = payload[2];

    r[2] = tec_id;

    if (tec_id < 1U || tec_id > TEC_COUNT || dir > 2U) {
        r[0] = STATUS_CAT_GENERAL;
        r[1] = STATUS_PAYLOAD_SHORT;
        goto reply;
    }

    TEC_PWM_Set(tec_id, (TEC_Direction)dir, duty_pct);

    /* Read back actual state */
    TEC_Direction actual_dir;
    uint8_t actual_duty;
    TEC_PWM_Get(tec_id, &actual_dir, &actual_duty);
    r[3] = (uint8_t)actual_dir;
    r[4] = actual_duty;

reply:
    tx_request.msg1    = header->msg1;
    tx_request.msg2    = header->msg2;
    tx_request.cmd1    = header->cmd1;
    tx_request.cmd2    = header->cmd2;
    memcpy(tx_request.payload, r, sizeof(r));
    tx_request.length  = sizeof(r);
    tx_request.pending = true;
}

/* ==========================================================================
 *  CMD_TEC_GET (0x0C51) — ISR context
 *
 *  Payload: [tec_id (1-3)]
 *  Response: [status1][status2][tec_id][direction][duty_pct][fault]
 * ========================================================================== */
static void Command_HandleTecGet(const PacketHeader *header,
                                  const uint8_t *payload)
{
    uint8_t r[6] = { STATUS_CAT_OK, STATUS_CODE_OK, 0, 0, 0, 0 };

    if (header->length < 1U || payload == NULL) {
        r[0] = STATUS_CAT_GENERAL;
        r[1] = STATUS_PAYLOAD_SHORT;
        goto reply;
    }

    uint8_t tec_id = payload[0];
    r[2] = tec_id;

    if (tec_id < 1U || tec_id > TEC_COUNT) {
        r[0] = STATUS_CAT_GENERAL;
        r[1] = STATUS_PAYLOAD_SHORT;
        goto reply;
    }

    TEC_Direction dir;
    uint8_t duty;
    TEC_PWM_Get(tec_id, &dir, &duty);
    r[3] = (uint8_t)dir;
    r[4] = duty;

    /* Check fault status from DRV8702 */
    DRV8702_Handle *drv = NULL;
    switch (tec_id) {
    case 1: drv = &drv8702_1_handle; break;
    case 2: drv = &drv8702_2_handle; break;
    case 3: drv = &drv8702_3_handle; break;
    }
    r[5] = (drv != NULL && DRV8702_IsFaulted(drv)) ? 0x01U : 0x00U;

reply:
    tx_request.msg1    = header->msg1;
    tx_request.msg2    = header->msg2;
    tx_request.cmd1    = header->cmd1;
    tx_request.cmd2    = header->cmd2;
    memcpy(tx_request.payload, r, sizeof(r));
    tx_request.length  = sizeof(r);
    tx_request.pending = true;
}

/* ==========================================================================
 *  CMD_TEC_STOP (0x0C52) — ISR context
 * ========================================================================== */
static void Command_HandleTecStop(const PacketHeader *header,
                                   const uint8_t *payload)
{
    uint8_t r[2] = { STATUS_CAT_OK, STATUS_CODE_OK };

    if (header->length >= 1U && payload != NULL) {
        TEC_PWM_Stop(payload[0]);
    }

    tx_request.msg1    = header->msg1;
    tx_request.msg2    = header->msg2;
    tx_request.cmd1    = header->cmd1;
    tx_request.cmd2    = header->cmd2;
    memcpy(tx_request.payload, r, sizeof(r));
    tx_request.length  = sizeof(r);
    tx_request.pending = true;
}

/* ==========================================================================
 *  CMD_TEC_STOP_ALL (0x0C53) — ISR context
 * ========================================================================== */
static void Command_HandleTecStopAll(const PacketHeader *header)
{
    TEC_PWM_StopAll();

    uint8_t r[2] = { STATUS_CAT_OK, STATUS_CODE_OK };
    tx_request.msg1    = header->msg1;
    tx_request.msg2    = header->msg2;
    tx_request.cmd1    = header->cmd1;
    tx_request.cmd2    = header->cmd2;
    memcpy(tx_request.payload, r, sizeof(r));
    tx_request.length  = sizeof(r);
    tx_request.pending = true;
}

/* ==========================================================================
 *  CMD_TEC_RESET (0x0C54) — ISR context
 *
 *  Full power-cycle reset of a DRV8702 instance:
 *    1. Stop PWM (EN → 0%)
 *    2. Sleep (nSLEEP LOW — full internal reset)
 *    3. Brief settle delay
 *    4. Wake (nSLEEP HIGH — re-latches MODE=0 PH/EN)
 *    5. Clear fault latches via SPI
 *    6. Bridge ready at 0% duty, no fault
 *
 *  Payload: [tec_id (1-3)]
 *  Response: [s1][s2][tec_id][fault_after_reset]
 * ========================================================================== */
static void Command_HandleTecReset(const PacketHeader *header,
                                    const uint8_t *payload)
{
    uint8_t r[4] = { STATUS_CAT_OK, STATUS_CODE_OK, 0, 0 };

    if (header->length < 1U || payload == NULL) {
        r[0] = STATUS_CAT_GENERAL;
        r[1] = STATUS_PAYLOAD_SHORT;
        goto reply;
    }

    uint8_t tec_id = payload[0];
    r[2] = tec_id;

    if (tec_id < 1U || tec_id > TEC_COUNT) {
        r[0] = STATUS_CAT_GENERAL;
        r[1] = STATUS_PAYLOAD_SHORT;
        goto reply;
    }

    DRV8702_Handle *drv = NULL;
    switch (tec_id) {
    case 1: drv = &drv8702_1_handle; break;
    case 2: drv = &drv8702_2_handle; break;
    case 3: drv = &drv8702_3_handle; break;
    }

    if (drv != NULL) {
        /* 1. Stop PWM output */
        TEC_PWM_Stop(tec_id);

        /* 2. Sleep — full internal reset */
        DRV8702_Sleep(drv);

        /* 3. Brief delay (~1 ms worth of NOPs at 480 MHz) */
        for (volatile uint32_t d = 0; d < 500000U; d++) { __NOP(); }

        /* 4. Wake — re-latches MODE=0 (PH/EN) */
        DRV8702_Wake(drv);

        /* 5. Clear any residual fault latches */
        DRV8702_ClearFaults(drv);

        /* 6. Read fault status after reset */
        r[3] = DRV8702_IsFaulted(drv) ? 0x01U : 0x00U;
    }

reply:
    tx_request.msg1    = header->msg1;
    tx_request.msg2    = header->msg2;
    tx_request.cmd1    = header->cmd1;
    tx_request.cmd2    = header->cmd2;
    memcpy(tx_request.payload, r, sizeof(r));
    tx_request.length  = sizeof(r);
    tx_request.pending = true;
}

/* ==========================================================================
 *  CMD_GET_BOARD_TYPE (0x0C99) — ISR context
 *
 *  Returns a fixed identifier so the host can distinguish the motherboard
 *  from a driverboard.  Mirrors the driverboard's GET_BOARD_TYPE (0x0B99)
 *  but with a different code and identifier.
 *
 *  Response payload (5 bytes):
 *    Byte 0 — 0x00 (status_1)
 *    Byte 1 — 0x00 (status_2: success)
 *    Byte 2 — 0xFF (boardID placeholder)
 *    Byte 3 — 0x4D ('M')
 *    Byte 4 — 0x42 ('B')
 * ========================================================================== */
static void Command_HandleGetBoardType(USART_Handle *handle,
                                        const PacketHeader *header,
                                        const uint8_t *payload)
{
    (void)handle;
    (void)payload;

    uint8_t response[5] = {
        STATUS_CAT_OK,      /* status_1: category */
        STATUS_CODE_OK,     /* status_2: code     */
        0xFFU,              /* boardID (0xFF = motherboard) */
        MB_BOARD_ID_1,      /* 'M' (0x4D) */
        MB_BOARD_ID_2,      /* 'B' (0x42) */
    };

    tx_request.msg1    = header->msg1;
    tx_request.msg2    = header->msg2;
    tx_request.cmd1    = header->cmd1;
    tx_request.cmd2    = header->cmd2;
    memcpy(tx_request.payload, response, sizeof(response));
    tx_request.length  = sizeof(response);
    tx_request.pending = true;  /* Must be last — acts as the commit */
}

/* ==========================================================================
 *  DAUGHTERCARD ASYNC FORWARD — ISR context
 *
 *  Extracts boardID from payload[0] and defers the entire packet to the
 *  main loop for forwarding to the correct DC UART.
 *
 *  BoardID mapping (0-based):
 *    0 → dc1_handle (USART1)    2 → dc3_handle (USART3)
 *    1 → dc2_handle (USART2)    3 → dc4_handle (UART4)
 * ========================================================================== */
static void Command_HandleDcForward(const PacketHeader *header,
                                     const uint8_t *payload)
{
    if (header->length == 0U || payload == NULL) return;

    uint8_t board_id = payload[0];
    if (board_id >= DC_MAX_BOARDS) return;

    dc_forward_request.msg1     = header->msg1;
    dc_forward_request.msg2     = header->msg2;
    dc_forward_request.cmd1     = header->cmd1;
    dc_forward_request.cmd2     = header->cmd2;
    dc_forward_request.board_id = board_id;
    memcpy(dc_forward_request.payload, payload, header->length);
    dc_forward_request.length   = header->length;
    dc_forward_request.pending  = true;  /* Must be last */
}

/* ==========================================================================
 *  SET_LIST_OF_SW (0x0B51) — ISR context, deferred to main loop
 *
 *  Payload = groups of 5 bytes: [boardID][bank][SW_hi][SW_lo][state]
 *  The main loop processes each group sequentially, sending a
 *  SetSingleSwitch (0x0A10) command to the target daughtercard.
 * ========================================================================== */
static void Command_HandleDcSetList(const PacketHeader *header,
                                     const uint8_t *payload)
{
    if (header->length == 0U || payload == NULL) return;

    dc_list_request.msg1   = header->msg1;
    dc_list_request.msg2   = header->msg2;
    dc_list_request.cmd1   = header->cmd1;
    dc_list_request.cmd2   = header->cmd2;
    dc_list_request.mode   = DC_LIST_MODE_SET;
    memcpy(dc_list_request.payload, payload, header->length);
    dc_list_request.length = header->length;
    dc_list_request.pending = true;  /* Must be last */
}

/* ==========================================================================
 *  GET_LIST_OF_SW (0x0B52) — ISR context, deferred to main loop
 *
 *  Payload = groups of 4 bytes: [boardID][bank][SW_hi][SW_lo]
 *  The main loop processes each group sequentially, sending a
 *  GetSingleSwitch (0x0A11) command to the target daughtercard.
 * ========================================================================== */
static void Command_HandleDcGetList(const PacketHeader *header,
                                     const uint8_t *payload)
{
    if (header->length == 0U || payload == NULL) return;

    dc_list_request.msg1   = header->msg1;
    dc_list_request.msg2   = header->msg2;
    dc_list_request.cmd1   = header->cmd1;
    dc_list_request.cmd2   = header->cmd2;
    dc_list_request.mode   = DC_LIST_MODE_GET;
    memcpy(dc_list_request.payload, payload, header->length);
    dc_list_request.length = header->length;
    dc_list_request.pending = true;  /* Must be last */
}

/* ==========================================================================
 *  GANTRY RS485 PASSTHROUGH (0x0C30) — ISR context, deferred to main loop
 *
 *  GUI sends an ASCII command string as the payload (no null terminator).
 *  The ISR copies it into gantry_request, the main loop forwards via RS485
 *  and returns the gantry's response as the packet payload.
 * ========================================================================== */
static void Command_HandleGantry(USART_Handle *handle,
                                  const PacketHeader *header,
                                  const uint8_t *payload)
{
    (void)handle;

    if (header->length == 0U || payload == NULL) return;

    uint16_t len = header->length;
    if (len >= GANTRY_RESPONSE_MAX) len = GANTRY_RESPONSE_MAX - 1U;

    gantry_request.msg1 = header->msg1;
    gantry_request.msg2 = header->msg2;
    gantry_request.cmd1 = header->cmd1;
    gantry_request.cmd2 = header->cmd2;
    memcpy(gantry_request.cmd_str, payload, len);
    gantry_request.cmd_str[len] = '\0';     /* Null-terminate for RS485 */
    gantry_request.cmd_len = len;
    gantry_request.pending = true;          /* Must be last */
}

/* ==========================================================================
 *  Command_ExecuteGantry — main loop context only
 *
 *  Sends the buffered ASCII command via RS485 and waits for a response.
 *  Returns the gantry's ASCII response as the packet payload.
 * ========================================================================== */
void Command_ExecuteGantry(void)
{
    char response[GANTRY_RESPONSE_MAX];

    uint16_t len = RS485_SendCommand(&rs485_handle,
                                      gantry_request.cmd_str,
                                      response,
                                      sizeof(response),
                                      GANTRY_TIMEOUT_MS);

    tx_request.msg1   = gantry_request.msg1;
    tx_request.msg2   = gantry_request.msg2;
    tx_request.cmd1   = gantry_request.cmd1;
    tx_request.cmd2   = gantry_request.cmd2;

    if (len == 0U) {
        tx_request.payload[0] = STATUS_CAT_GANTRY;
        tx_request.payload[1] = STATUS_GANTRY_TIMEOUT;
        memcpy(&tx_request.payload[2], "TIMEOUT", 7);
        tx_request.length = 9U;
    } else {
        tx_request.payload[0] = STATUS_CAT_OK;
        tx_request.payload[1] = STATUS_CODE_OK;
        memcpy(&tx_request.payload[2], response, len);
        tx_request.length = 2U + len;
    }
    tx_request.pending = true;
}

/* ==========================================================================
 *  CMD_MEASURE_ADC (0x0C03) — ISR context, deferred to main loop
 *
 *  Atomic switch-controlled ADC measurement.  The ISR copies the delay
 *  and switch groups into measure_adc_request; the main loop executes
 *  the full save → GND → set → timer → ADC → restore → Vpp sequence.
 *
 *  Request payload:
 *    Bytes [0..1] — delay in ms (uint16 LE, 1–100)
 *    Bytes [2..N] — SET_LIST_OF_SW 5-byte groups:
 *                   [boardID][bank][SW_hi][SW_lo][state]
 *
 *  Minimum payload: 7 bytes (2-byte delay + one 5-byte group)
 * ========================================================================== */
static void Command_HandleMeasureADC(USART_Handle *handle,
                                      const PacketHeader *header,
                                      const uint8_t *payload)
{
    (void)handle;

    /* Need at least 1-byte mask + 2-byte delay + one 5-byte switch group */
    if (header->length < (MEASURE_ADC_FULL_HDR_SIZE + DC_SET_GROUP_SIZE)
        || payload == NULL) {
        tx_request.msg1    = header->msg1;
        tx_request.msg2    = header->msg2;
        tx_request.cmd1    = header->cmd1;
        tx_request.cmd2    = header->cmd2;
        tx_request.payload[0] = STATUS_CAT_GENERAL;
        tx_request.payload[1] = STATUS_PAYLOAD_SHORT;
        tx_request.length  = 2U;
        tx_request.pending = true;
        return;
    }

    /* Parse board mask and delay */
    uint8_t  board_mask = payload[0];
    uint16_t delay_ms   = (uint16_t)payload[1] | ((uint16_t)payload[2] << 8);
    if (delay_ms < MEASURE_ADC_DELAY_MIN_MS) delay_ms = MEASURE_ADC_DELAY_MIN_MS;
    if (delay_ms > MEASURE_ADC_DELAY_MAX_MS) delay_ms = MEASURE_ADC_DELAY_MAX_MS;

    /* Switch payload starts after full header (mask + delay) */
    uint16_t sw_len = header->length - MEASURE_ADC_FULL_HDR_SIZE;

    measure_adc_request.msg1       = header->msg1;
    measure_adc_request.msg2       = header->msg2;
    measure_adc_request.cmd1       = header->cmd1;
    measure_adc_request.cmd2       = header->cmd2;
    measure_adc_request.delay_ms   = delay_ms;
    measure_adc_request.board_mask = board_mask;
    memcpy(measure_adc_request.sw_payload,
           &payload[MEASURE_ADC_FULL_HDR_SIZE], sw_len);
    measure_adc_request.sw_length  = sw_len;
    measure_adc_request.pending    = true;  /* Must be last — acts as commit */
}

/* ==========================================================================
 *  CMD_SWEEP_ADC (0x0C05) — ISR context, deferred to main loop
 *
 *  Same payload format as CMD_MEASURE_ADC.  The main loop measures each
 *  switch individually and returns an array of Vpp values.
 * ========================================================================== */
static void Command_HandleSweepADC(USART_Handle *handle,
                                    const PacketHeader *header,
                                    const uint8_t *payload)
{
    (void)handle;

    if (header->length < (MEASURE_ADC_FULL_HDR_SIZE + DC_SET_GROUP_SIZE)
        || payload == NULL) {
        tx_request.msg1    = header->msg1;
        tx_request.msg2    = header->msg2;
        tx_request.cmd1    = header->cmd1;
        tx_request.cmd2    = header->cmd2;
        tx_request.payload[0] = STATUS_CAT_GENERAL;
        tx_request.payload[1] = STATUS_PAYLOAD_SHORT;
        tx_request.length  = 2U;
        tx_request.pending = true;
        return;
    }

    uint8_t  board_mask = payload[0];
    uint16_t delay_ms   = (uint16_t)payload[1] | ((uint16_t)payload[2] << 8);
    if (delay_ms < MEASURE_ADC_DELAY_MIN_MS) delay_ms = MEASURE_ADC_DELAY_MIN_MS;
    if (delay_ms > MEASURE_ADC_DELAY_MAX_MS) delay_ms = MEASURE_ADC_DELAY_MAX_MS;

    uint16_t sw_len = header->length - MEASURE_ADC_FULL_HDR_SIZE;

    sweep_adc_request.msg1       = header->msg1;
    sweep_adc_request.msg2       = header->msg2;
    sweep_adc_request.cmd1       = header->cmd1;
    sweep_adc_request.cmd2       = header->cmd2;
    sweep_adc_request.delay_ms   = delay_ms;
    sweep_adc_request.board_mask = board_mask;
    memcpy(sweep_adc_request.sw_payload,
           &payload[MEASURE_ADC_FULL_HDR_SIZE], sw_len);
    sweep_adc_request.sw_length  = sw_len;
    sweep_adc_request.pending    = true;
}

/* ==========================================================================
 *  ACTUATOR BOARD FORWARD (0x0F00–0x10FF) — ISR context, deferred
 *
 *  Extracts boardID from payload[0]:
 *    boardID 1 → ACT1 (UART5)
 *    boardID 2 → ACT2 (USART6)
 *
 *  The entire packet (including boardID) is forwarded to the actuator board.
 *  The response comes back via the actuator board's parser callback.
 * ========================================================================== */
static void Command_HandleActForward(const PacketHeader *header,
                                      const uint8_t *payload)
{
    if (header->length == 0U || payload == NULL) return;

    uint8_t board_id = payload[0];
    if (board_id >= ACT_MAX_BOARDS) return;

    act_forward_request.msg1     = header->msg1;
    act_forward_request.msg2     = header->msg2;
    act_forward_request.cmd1     = header->cmd1;
    act_forward_request.cmd2     = header->cmd2;
    act_forward_request.board_id = board_id;
    memcpy(act_forward_request.payload, payload, header->length);
    act_forward_request.length   = header->length;
    act_forward_request.pending  = true;  /* Must be last */
}
