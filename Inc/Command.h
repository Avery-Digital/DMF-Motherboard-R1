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

#define CMD_PING            CMD_CODE(0xDE, 0xAD)    /**< Ping / firmware version */

/* Firmware version */
#define FW_VERSION_MAJOR    1U
#define FW_VERSION_MINOR    3U
#define FW_VERSION_PATCH    0U
#define CMD_READ_ADC        CMD_CODE(0x0C, 0x01)    /**< Read LTC2338-18 ADC     */
#define CMD_BURST_ADC       CMD_CODE(0x0C, 0x02)    /**< Burst 100x ADC reads    */
#define CMD_MEASURE_ADC     CMD_CODE(0x0C, 0x03)    /**< Switch-controlled ADC   */

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

/* ---- Thermistor Commands (0x0C20–0x0C25) ----
 *
 *  Read ADS7066 instance 3, channels 0–5.
 *  Response payload: 2 bytes, 16-bit ADC code, little-endian.
 */
#define CMD_THERM1          CMD_CODE(0x0C, 0x20)    /**< Thermistor 1 (inst3, ch0) */
#define CMD_THERM2          CMD_CODE(0x0C, 0x21)    /**< Thermistor 2 (inst3, ch1) */
#define CMD_THERM3          CMD_CODE(0x0C, 0x22)    /**< Thermistor 3 (inst3, ch2) */
#define CMD_THERM4          CMD_CODE(0x0C, 0x23)    /**< Thermistor 4 (inst3, ch3) */
#define CMD_THERM5          CMD_CODE(0x0C, 0x24)    /**< Thermistor 5 (inst3, ch4) */
#define CMD_THERM6          CMD_CODE(0x0C, 0x25)    /**< Thermistor 6 (inst3, ch5) */

/* ---- Gantry RS485 Command (0x0C30) ----
 *
 *  Payload: ASCII command string (e.g. "@01VER"), no null terminator.
 *  The firmware appends the null and forwards via RS485 to the gantry.
 *  Response payload: ASCII response string from gantry (no null).
 *  If the gantry does not respond, payload is "TIMEOUT".
 *
 *  Deferred to main loop (RS485 is polled, ~50 ms round trip at 9600).
 */
#define CMD_GANTRY_CMD      CMD_CODE(0x0C, 0x30)    /**< Gantry RS485 passthrough */

#define GANTRY_RESPONSE_MAX  128U   /**< Max ASCII response bytes from gantry */
#define GANTRY_TIMEOUT_MS    500U   /**< RS485 response timeout               */

/* ---- Board Identity Command (0x0C99) ----
 *
 *  Returns a fixed board type identifier so the host can distinguish
 *  the motherboard from a driverboard.
 *  Response: [0x00][0x00][0xFF][0xMB][0x01]
 *    0xMB 0x01 = Motherboard Rev 1
 *  Compare: driverboard GET_BOARD_TYPE (0x0B99) returns 0xCA 0xCA
 */
#define CMD_GET_BOARD_TYPE  CMD_CODE(0x0B, 0x99)    /**< Board ID (same code as driverboard) */

/* Motherboard identifier bytes */
#define MB_BOARD_ID_1       0x4DU   /**< 'M' */
#define MB_BOARD_ID_2       0x42U   /**< 'B' */

/* ---- Daughtercard (Driverboard) Command Routing ----
 *
 *  Commands in the 0x0A00–0x0BFF range are driverboard commands.
 *  The motherboard does NOT execute these — it extracts the boardID
 *  from the payload and forwards the packet to the correct DC UART.
 *
 *  0xBEEF is also a driverboard command (debug test).
 *
 *  Two commands require special handling (per-group sequential routing):
 *    0x0B51 SET_LIST_OF_SW — 5-byte groups [boardID][bank][SW_hi][SW_lo][state]
 *    0x0B52 GET_LIST_OF_SW — 4-byte groups [boardID][bank][SW_hi][SW_lo]
 */
#define CMD_DC_RANGE_START  CMD_CODE(0x0A, 0x00)    /**< First driverboard cmd   */
#define CMD_DC_RANGE_END    CMD_CODE(0x0B, 0xFF)    /**< Last driverboard cmd    */
#define CMD_DC_SET_LIST_SW  CMD_CODE(0x0B, 0x51)    /**< Bulk switch set (seq)   */
#define CMD_DC_GET_LIST_SW  CMD_CODE(0x0B, 0x52)    /**< Bulk switch get (seq)   */
#define CMD_DC_DEBUG        CMD_CODE(0xBE, 0xEF)    /**< Driverboard debug test  */

/* ---- Actuator Board Command Routing ----
 *
 *  Commands in the 0x0F00–0x10FF range are actuator board commands.
 *  The motherboard forwards these to ACT1 or ACT2 based on boardID
 *  in payload[0]:  boardID 1 → ACT1 (UART5), boardID 2 → ACT2 (USART6).
 */
#define CMD_ACT_RANGE_START CMD_CODE(0x0F, 0x00)    /**< First actuator cmd      */
#define CMD_ACT_RANGE_END   CMD_CODE(0x10, 0xFF)    /**< Last actuator cmd       */
#define ACT_MAX_BOARDS      2U      /**< Number of actuator board slots          */

/* Driverboard SET_LIST group size */
#define DC_SET_GROUP_SIZE   5U      /**< [boardID][bank][SW_hi][SW_lo][state] */
#define DC_GET_GROUP_SIZE   4U      /**< [boardID][bank][SW_hi][SW_lo]        */

/* Switch state encoding (matches driver board EHVSDriver.h) */
#define SW_STATE_GND        0x00U   /**< Switch connected to ground           */
#define SW_STATE_HVSG       0x01U   /**< Switch connected to HVSG             */
#define SW_STATE_FLOAT      0x04U   /**< Switch floating (disconnected)       */

/* GET_HVSG_SWITCHES command bytes (new optimized save command) */
#define CMD_GET_HVSG_SW_CMD1    0x0BU
#define CMD_GET_HVSG_SW_CMD2    0x54U

/* ========================= Response Status Codes ========================== *
 *
 *  Response format: [status1 = category] [status2 = code] [boardID] [data...]
 *  Both 0x00 = success.  Non-zero = error.
 *
 *  Unified error table across all boards:
 *    Category 0x01 = General         0x05 = Load Switch (MB)
 *    Category 0x02 = Switch Matrix   0x06 = ADC/Sensor (MB)
 *    Category 0x03 = HVSG/PWM (DB)   0x07 = Actuator (ACT)
 *    Category 0x04 = PMU/INA228 (DB) 0x08 = Gantry (MB)
 *                                    0x09 = Routing (MB)
 * ========================================================================== */

/* Success */
#define STATUS_CAT_OK           0x00U
#define STATUS_CODE_OK          0x00U

/* Category 0x01 — General Errors */
#define STATUS_CAT_GENERAL      0x01U
#define STATUS_PAYLOAD_SHORT    0x01U
#define STATUS_UNKNOWN_CMD      0x02U

/* Category 0x05 — Load Switch Errors */
#define STATUS_CAT_LOADSW       0x05U
#define STATUS_LOADSW_INVALID   0x01U
#define STATUS_LOADSW_FAILED    0x02U

/* Category 0x06 — ADC/Sensor Errors */
#define STATUS_CAT_ADC          0x06U
#define STATUS_ADC_SPI_FAIL     0x01U
#define STATUS_ADS_READ_FAIL    0x02U
#define STATUS_THERM_NAN        0x03U
#define STATUS_ADC_DC_TIMEOUT   0x04U   /**< Daughtercard timeout during measure */
#define STATUS_ADC_RESTORE_FAIL 0x05U   /**< Switch restore failed               */

/* Category 0x08 — Gantry RS485 Errors */
#define STATUS_CAT_GANTRY       0x08U
#define STATUS_GANTRY_TIMEOUT   0x01U
#define STATUS_GANTRY_COMMERR   0x02U

/* Category 0x09 — Routing Errors */
#define STATUS_CAT_ROUTING      0x09U
#define STATUS_ROUTE_DC_INVALID 0x01U
#define STATUS_ROUTE_ACT_INVALID 0x02U
#define STATUS_ROUTE_TIMEOUT    0x03U

/* ========================= Burst ADC Constants ============================ */

#define ADC_BURST_COUNT         100U    /**< Number of samples per burst     */
#define ADC_BURST_PAYLOAD_SIZE  400U    /**< 100 samples × 4 bytes each      */

/* ======================= Measure ADC Constants ============================ */

#define MEASURE_ADC_DELAY_MIN_MS   0U     /**< Minimum delay in ms             */
#define MEASURE_ADC_DELAY_MAX_MS   100U   /**< Maximum delay in ms             */
#define MEASURE_ADC_DELAY_HDR_SIZE 2U     /**< Delay header: 2 bytes (uint16 LE) */
#define MEASURE_ADC_FULL_HDR_SIZE  3U     /**< mask (1B) + delay (2B)            */
#define MEASURE_ADC_PHASE_COUNT    6U     /**< Number of timed phases            */
#define MEASURE_ADC_TIMING_SIZE    12U    /**< 6 × uint16 = 12 bytes            */
#define MEASURE_ADC_SW_STATES      600U   /**< Switch states per board (2 banks × 300) */

/* Estimated time for one switch to physically latch on the driver board.
 * Covers UART byte time + driver board processing + EHVSBlockProgram.
 * Calibrate on scope if needed. */
#define SWITCH_ENABLE_TIME_US      3000U  /**< ~3 ms per switch (conservative)  */

/* PWMPhaseSync command code for driver board (0x0A81).
 * Resets TIM2/TIM1/TIM8 counters to zero for deterministic phase.
 * Phase 3 sends this and waits for the response before starting TIM6,
 * so no timing constant is needed in the timer calculation. */

/* LTC2338-18 bipolar mode: ±10.24 V full-scale, 18-bit two's complement.
 * LSB = 20.48 / 262144 = 78.125 µV.  Matches GUI PlotBurstADC scaling. */
#define ADC_FULL_SCALE_V        20.48f    /**< Full bipolar span (±10.24 V)    */
#define ADC_FULL_SCALE_CODES    262144.0f /**< 2^18 total codes                */

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

/**
 * @brief  Execute a switch-controlled ADC measurement — main loop only.
 *
 *         Saves switch states, sets GND, enables specified switches,
 *         waits a deterministic hardware-timed delay, burst-reads the ADC,
 *         restores switch states, and returns Vpp + raw samples.
 *
 *         Uses TIM6 one-pulse mode for µs-precision deterministic timing.
 *         Blocks the main loop for the duration of the entire sequence.
 */
void Command_ExecuteMeasureADC(void);

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_H */
