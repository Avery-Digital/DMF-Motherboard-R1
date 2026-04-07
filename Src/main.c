/*******************************************************************************
 * @file    Src/main.c
 * @author  Cam
 * @brief   Main Entry Point
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 *
 * Initialization sequence:
 *   1.  MCU_Init()            — MPU, SYSCFG, NVIC, flash latency, power
 *   2.  ClockTree_Init()      — HSE, PLL1/2/3, bus prescalers
 *   2a. SysTick 1 ms          — LL_Init1msTick + LL_SYSTICK_EnableIT
 *   3.  USB2517_SetStrapPins()— Assert CFG_SEL pins before hub exits reset
 *   4.  I2C_Driver_Init()     — I2C1 on PB7/PB8, 400 kHz
 *   5.  SPI_Init()            — SPI2 for LTC2338-18 ADC (shared bus)
 *   6.  DRV8702_Init() x3    — TEC H-bridge drivers (GPIO + SPI2)
 *   6a. DAC80508_Init()       — 8-ch 16-bit DAC (SPI2, nCS PD2)
 *   6b. ADS7066_Init() x3    — 8-ch 16-bit ADC (SPI2, nCS PD5/PD4/PD3)
 *   6c. LoadSwitch_Init()     — 10x VN5T016AH high-side load switches (GPIO)
 *   7.  USB2517_Init()        — USB hub SMBus config + USB_ATTACH
 *   8.  Protocol_ParserInit() — Frame parser state machine
 *   8.  USART_Driver_Init()   — GPIO, USART10, DMA streams (interrupt-driven)
 *   9.  USART_Driver_StartRx()— Enable DMA HT/TC + USART IDLE interrupts
 *
 * Runtime:
 *   All UART reception is interrupt-driven.  The main loop is free for
 *   other tasks (ADC polling, LED updates, watchdog refresh, sleep).
 *
 *   IMPORTANT: OnPacketReceived() is called from ISR context (DMA or
 *   USART IDLE interrupt).  Keep command handlers short, or set a flag
 *   and defer heavy processing to the main loop.
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "command.h"
#include "DRV8702.h"
#include "DAC80508.h"
#include "ADS7066.h"
#include "VN5T016AH.h"
#include "DC_Uart_Driver.h"
#include "Act_Uart_Driver.h"
#include "RS485_Driver.h"
#include <string.h>
#include "stm32h7xx_ll_rtc.h"
#include "stm32h7xx_ll_tim.h"
#include "stm32h7xx_ll_bus.h"
#include "spi_driver.h"
#include "ll_tick.h"
#include <limits.h>
#include <math.h>

/* Protocol parser instances */
static ProtocolParser usart10_parser;
static ProtocolParser dc1_parser;
static ProtocolParser dc2_parser;
static ProtocolParser dc3_parser;
static ProtocolParser dc4_parser;
static ProtocolParser act1_parser;
static ProtocolParser act2_parser;

/* Deferred TX request — set by ISR, consumed by main loop */
TxRequest    tx_request    = {0};

/* Deferred burst ADC request — set by ISR, consumed by main loop */
BurstRequest burst_request = {0};

/* Daughtercard forward request — async, single command to one board */
DcForwardRequest dc_forward_request = {0};

/* Daughtercard list request — synchronous, per-group sequential */
DcListRequest    dc_list_request    = {0};

/* Gantry RS485 request — deferred to main loop */
GantryRequest    gantry_request     = {0};

/* Measure ADC request — switch-controlled deterministic ADC measurement */
MeasureAdcRequest measure_adc_request = {0};

/* Actuator board forward request — async, single command */
ActForwardRequest act_forward_request = {0};

/* Response mailbox for synchronous DC operations */
DcResponse       dc_response        = {0};

/* Flag: true while Command_ExecuteDcList() is running.
 * OnDC_PacketReceived uses this to route to dc_response instead of tx_request */
volatile bool    dc_list_active     = false;

/* ========================== DC Handle Lookup =============================== */

/**
 * @brief  Map boardID (0–3) to the corresponding DC_Uart_Handle.
 * @return Pointer to handle, or NULL if boardID is out of range.
 */
static DC_Uart_Handle* DC_GetHandle(uint8_t board_id)
{
    static DC_Uart_Handle* const dc_handles[DC_MAX_BOARDS] = {
        &dc1_handle,    /* boardID 0 → USART1 */
        &dc2_handle,    /* boardID 1 → USART2 */
        &dc3_handle,    /* boardID 2 → USART3 */
        &dc4_handle,    /* boardID 3 → UART4  */
    };

    if (board_id >= DC_MAX_BOARDS) return NULL;
    return dc_handles[board_id];
}

/* ======================== ACT Handle Lookup ================================ */

/**
 * @brief  Map boardID (0–1) to the corresponding Act_Uart_Handle.
 * @return Pointer to handle, or NULL if boardID is out of range.
 */
static Act_Uart_Handle* ACT_GetHandle(uint8_t board_id)
{
    static Act_Uart_Handle* const act_handles[ACT_MAX_BOARDS] = {
        &act1_handle,   /* boardID 0 → UART5  */
        &act2_handle,   /* boardID 1 → USART6 */
    };

    if (board_id >= ACT_MAX_BOARDS) return NULL;
    return act_handles[board_id];
}

/* Private function prototypes -----------------------------------------------*/
static void SystemInit_Sequence(void);
static void OnPacketReceived(const PacketHeader *header,
                             const uint8_t *payload,
                             void *ctx);
static void OnACT_PacketReceived(const PacketHeader *header,
                                 const uint8_t *payload,
                                 void *ctx);
static void OnDC_PacketReceived(const PacketHeader *header,
                                 const uint8_t *payload,
                                 void *ctx);



/* Entry point */
int main(void)
{
    /* Initialize MCU and peripherals */
    SystemInit_Sequence();

    /* Main loop — UART is fully interrupt-driven, no polling needed.
     * Add other non-interrupt tasks here as the project grows. */
    while (1)
        {
            /* Execute pending burst ADC read — too slow for ISR context */
            if (burst_request.pending) {
                burst_request.pending = false;
                Command_ExecuteBurstADC();
            }

            /* Forward a driverboard command to the correct DC UART (async) */
            if (dc_forward_request.pending) {
                dc_forward_request.pending = false;
                DC_Uart_Handle *dc = DC_GetHandle(dc_forward_request.board_id);
                if (dc != NULL) {
                    DC_Uart_SendPacket(dc,
                                       dc_forward_request.msg1,
                                       dc_forward_request.msg2,
                                       dc_forward_request.cmd1,
                                       dc_forward_request.cmd2,
                                       dc_forward_request.payload,
                                       dc_forward_request.length);
                }
            }

            /* Forward an actuator board command (async) */
            if (act_forward_request.pending) {
                act_forward_request.pending = false;
                Act_Uart_Handle *act = ACT_GetHandle(act_forward_request.board_id);
                if (act != NULL) {
                    Act_Uart_SendPacket(act,
                                        act_forward_request.msg1,
                                        act_forward_request.msg2,
                                        act_forward_request.cmd1,
                                        act_forward_request.cmd2,
                                        act_forward_request.payload,
                                        act_forward_request.length);
                }
            }

            /* Forward a gantry command via RS485 (polled, ~50 ms) */
            if (gantry_request.pending) {
                gantry_request.pending = false;
                Command_ExecuteGantry();
            }

            /* Execute switch-controlled ADC measurement — synchronous, blocking */
            if (measure_adc_request.pending) {
                measure_adc_request.pending = false;
                Command_ExecuteMeasureADC();
            }

            /* Execute SET_LIST_OF_SW / GET_LIST_OF_SW — synchronous, blocking */
            if (dc_list_request.pending) {
                dc_list_request.pending = false;
                Command_ExecuteDcList();
            }

            /* Consume pending TX outside of ISR context */
            if (tx_request.pending) {
                tx_request.pending = false;
                USART_Driver_SendPacket(&usart10_handle,
                                        tx_request.msg1,
                                        tx_request.msg2,
                                        tx_request.cmd1,
                                        tx_request.cmd2,
                                        tx_request.payload,
                                        tx_request.length);
            }
        }
}

/* ==========================================================================
 *  SYSTEM INITIALIZATION
 * ========================================================================== */
static void SystemInit_Sequence(void)
{
    InitResult      i2c_result;
    InitResult      usart_result;
    SPI_Status      spi_result;
    DRV8702_Status  drv_result;
    DAC80508_Status dac_result;
    ADS7066_Status      adc_result;
    LoadSwitch_Status   lsw_result;

    /* Step 1: MCU core — MPU, flash latency, voltage scaling */
    MCU_Init();

    /* Step 2: Full clock tree — HSE → PLL1 (480 MHz SYSCLK),
     *         PLL2 (128 MHz USART kernel), PLL3 (128 MHz SPI/I2C) */
    ClockTree_Init(&sys_clk_config);

    /* Step 2a: Configure SysTick for 1 ms interrupts @ 480 MHz CPU */
    LL_Init1msTick(sys_clk_config.sysclk_hz);
    LL_SYSTICK_EnableIT();          /* Enable SysTick interrupt → SysTick_Handler */



    /* Step 4: I2C1 on PB7 (SDA) / PB8 (SCL) */
    i2c_result = I2C_Driver_Init(&i2c1_handle);
    if (i2c_result != INIT_OK) {
        Error_Handler(0x10);
    }

    /* Step 5: SPI2 initialization */
    spi_result = SPI_Init(&spi2_handle);
    if (spi_result != SPI_OK) {
        Error_Handler(0x20);
    }

    /* Step 6: DRV8702 TEC H-bridge drivers — GPIO init, safe defaults.
     *         SPI2 must be initialised first (shared bus for register access).
     *         Each instance starts with nSLEEP LOW (sleep) and EN LOW (off).
     *         DRV8702_Wake() is called immediately to bring all three active. */
    drv_result = DRV8702_Init(&drv8702_1_handle);
    if (drv_result != DRV8702_OK) {
        Error_Handler(0x30);
    }
    DRV8702_Wake(&drv8702_1_handle);

    drv_result = DRV8702_Init(&drv8702_2_handle);
    if (drv_result != DRV8702_OK) {
        Error_Handler(0x31);
    }
    DRV8702_Wake(&drv8702_2_handle);

    drv_result = DRV8702_Init(&drv8702_3_handle);
    if (drv_result != DRV8702_OK) {
        Error_Handler(0x32);
    }
    DRV8702_Wake(&drv8702_3_handle);

    /* Step 6a: DAC80508 — 8-channel 16-bit DAC on SPI2.
     *          nCS on PD2 (already driven HIGH by the bulk chip-select init).
     *          SPI2 must be initialised first (shared bus). */
    dac_result = DAC80508_Init(&dac80508_handle);
    if (dac_result != DAC80508_OK) {
        Error_Handler(0x40);
    }

    /* Step 6b: ADS7066 — 8-channel 16-bit ADC x3 on SPI2.
     *          nCS on PD5 (inst 1), PD4 (inst 2), PD3 (inst 3).
     *          SPI2 must be initialised first (shared bus). */
    adc_result = ADS7066_Init(&ads7066_1_handle);
    if (adc_result != ADS7066_OK) {
        Error_Handler(0x50);
    }

    adc_result = ADS7066_Init(&ads7066_2_handle);
    if (adc_result != ADS7066_OK) {
        Error_Handler(0x51);
    }

    adc_result = ADS7066_Init(&ads7066_3_handle);
    if (adc_result != ADS7066_OK) {
        Error_Handler(0x52);
    }

    /* Step 6c: Load switches — 10x VN5T016AH high-side drivers.
     *          All enable pins default OFF (LOW) after init. */
    lsw_result = LoadSwitch_Init();
    if (lsw_result != LOADSW_OK) {
        Error_Handler(0x60);
    }

    /* Step 7: USB2517I hub — reset and strap for internal default mode.
     *         CFG_SEL[2:1:0] = 1,0,1 → internal defaults, dynamic power, LED=USB.
     *         No SMBus config needed — hub attaches automatically after POR. */
    USB2517_SetStrapPins();
    LL_mDelay(100);  /* Give USB2517 time to exit POR and attach */

    /* Step 7a: RS485 — USART7 + MAX485 for gantry communication.
     *          9600 baud, polled TX/RX, half-duplex direction on PF8. */
    if (RS485_Init(&rs485_handle) != INIT_OK) {
        Error_Handler(0x70);
    }

    /* Step 8: Protocol parser — register the packet callback */
    Protocol_ParserInit(&usart10_parser, OnPacketReceived, NULL);

    /* Step 8a: USART10 + DMA on PG11 (RX) / PG12 (TX)
     *          Parser is passed to the driver so ISRs can feed it directly */
    usart_result = USART_Driver_Init(&usart10_handle, &usart10_parser);
    if (usart_result != INIT_OK) {
        Error_Handler(0x11);
    }

    /* Step 9: Start interrupt-driven DMA reception */
    USART_Driver_StartRx(&usart10_handle);
    /* === BOOT TEST — remove after debugging === */
    tx_request.msg1    = 0x01;
    tx_request.msg2    = 0x02;
    tx_request.cmd1    = 0xDE;
    tx_request.cmd2    = 0xAD;
    static const uint8_t test_payload[] = {0xAA, 0xBB, 0xCC};
    memcpy(tx_request.payload, test_payload, sizeof(test_payload));
    tx_request.length  = sizeof(test_payload);
    tx_request.pending = true;

    /* Step 10: Daughtercard UARTs — polled TX + DMA circular RX */
    Protocol_ParserInit(&dc1_parser, OnDC_PacketReceived, &dc1_handle);
    if (DC_Uart_Init(&dc1_handle, &dc1_parser) != INIT_OK) {
        Error_Handler(0x12);
    }
    DC_Uart_StartRx(&dc1_handle);

    Protocol_ParserInit(&dc2_parser, OnDC_PacketReceived, &dc2_handle);
    if (DC_Uart_Init(&dc2_handle, &dc2_parser) != INIT_OK) {
        Error_Handler(0x13);
    }
    DC_Uart_StartRx(&dc2_handle);

    Protocol_ParserInit(&dc3_parser, OnDC_PacketReceived, &dc3_handle);
    if (DC_Uart_Init(&dc3_handle, &dc3_parser) != INIT_OK) {
        Error_Handler(0x14);
    }
    DC_Uart_StartRx(&dc3_handle);

    Protocol_ParserInit(&dc4_parser, OnDC_PacketReceived, &dc4_handle);
    if (DC_Uart_Init(&dc4_handle, &dc4_parser) != INIT_OK) {
        Error_Handler(0x15);
    }
    DC_Uart_StartRx(&dc4_handle);

    /* Step 11: Actuator board UARTs — polled TX + DMA circular RX + RS485 DE */
    Protocol_ParserInit(&act1_parser, OnACT_PacketReceived, &act1_handle);
    if (Act_Uart_Init(&act1_handle, &act1_parser) != INIT_OK) {
        Error_Handler(0x16);
    }
    Act_Uart_StartRx(&act1_handle);

    Protocol_ParserInit(&act2_parser, OnACT_PacketReceived, &act2_handle);
    if (Act_Uart_Init(&act2_handle, &act2_parser) != INIT_OK) {
        Error_Handler(0x17);
    }
    Act_Uart_StartRx(&act2_handle);
}

/* ==========================================================================
 *  PACKET RECEIVED CALLBACK
 *
 *  Called by the protocol parser when a complete, CRC-validated packet
 *  arrives.
 *
 *  WARNING: This runs in ISR context (from DMA HT/TC or USART IDLE
 *  interrupt).  Keep command handlers fast.  If a command requires
 *  significant processing, set a flag here and handle it in the main loop.
 * ========================================================================== */
static void OnPacketReceived(const PacketHeader *header,
                             const uint8_t *payload,
                             void *ctx)
{
    (void)ctx;

    /* Route to command handler based on cmd1 + cmd2 */
    Command_Dispatch(&usart10_handle, header, payload);
}

/* ==========================================================================
 *  ACTUATOR BOARD PACKET RECEIVED CALLBACK
 *
 *  Called when an actuator board sends a response packet.
 *  Relay the response back to the GUI via USART10.
 *  Runs in ISR context — keep it fast.
 * ========================================================================== */
static void OnACT_PacketReceived(const PacketHeader *header,
                                 const uint8_t *payload,
                                 void *ctx)
{
    (void)ctx;

    /* Relay actuator board response to the GUI via USART10 */
    if (!tx_request.pending) {
        tx_request.msg1 = header->msg1;
        tx_request.msg2 = header->msg2;
        tx_request.cmd1 = header->cmd1;
        tx_request.cmd2 = header->cmd2;
        if (header->length > 0U && header->length <= PKT_MAX_PAYLOAD) {
            memcpy(tx_request.payload, payload, header->length);
        }
        tx_request.length  = header->length;
        tx_request.pending = true;
    }
}

/* ==========================================================================
 *  DAUGHTERCARD PACKET RECEIVED CALLBACK
 *
 *  Called when a daughtercard sends a response packet.  The ctx pointer
 *  is the DC_Uart_Handle* so we know which daughtercard sent it.
 *
 *  For now, relay the response back to the GUI via USART10.
 *  Runs in ISR context — keep it fast.
 * ========================================================================== */
static void OnDC_PacketReceived(const PacketHeader *header,
                                 const uint8_t *payload,
                                 void *ctx)
{
    (void)ctx;  /* DC_Uart_Handle* — available if needed */

    /* During synchronous list operations, deposit into the response
     * mailbox instead of tx_request so the main loop can collect it. */
    if (dc_list_active) {
        if (!dc_response.ready) {
            dc_response.cmd1 = header->cmd1;
            dc_response.cmd2 = header->cmd2;
            if (header->length > 0U && header->length <= PKT_MAX_PAYLOAD) {
                memcpy(dc_response.payload, payload, header->length);
            }
            dc_response.length = header->length;
            dc_response.ready  = true;  /* Must be last */
        }
        return;
    }

    /* Async mode: relay the daughtercard response to the GUI via USART10 */
    if (!tx_request.pending) {
        tx_request.msg1 = header->msg1;
        tx_request.msg2 = header->msg2;
        tx_request.cmd1 = header->cmd1;
        tx_request.cmd2 = header->cmd2;
        if (header->length > 0U && header->length <= PKT_MAX_PAYLOAD) {
            memcpy(tx_request.payload, payload, header->length);
        }
        tx_request.length  = header->length;
        tx_request.pending = true;
    }
}

/* ==========================================================================
 *  DAUGHTERCARD LIST COMMAND EXECUTION — main loop context only
 *
 *  Processes SET_LIST_OF_SW (0x0B51) and GET_LIST_OF_SW (0x0B52) by
 *  batching groups by boardID and forwarding one bulk packet per board.
 *
 *  Algorithm:
 *    1. Scan all groups, bucket by boardID (0–3)
 *    2. For each non-empty bucket, build a single SET_LIST_OF_SW or
 *       GET_LIST_OF_SW packet containing all that board's groups
 *    3. Send to the correct DC UART, wait for response (10ms timeout)
 *    4. Collect responses, send aggregate back to GUI
 *
 *  SET groups: 5 bytes each [boardID][bank][SW_hi][SW_lo][state]
 *  GET groups: 4 bytes each [boardID][bank][SW_hi][SW_lo]
 *
 *  This function BLOCKS the main loop until all boards are processed.
 *  At most 4 round-trips (one per board), each ~3.4 ms + switch time.
 * ========================================================================== */
void Command_ExecuteDcList(void)
{
    const uint8_t *data       = dc_list_request.payload;
    const uint16_t total_len  = dc_list_request.length;
    const uint8_t  mode       = dc_list_request.mode;

    /* Determine group size and command code to forward */
    uint8_t group_size;
    uint8_t fwd_cmd1, fwd_cmd2;

    if (mode == DC_LIST_MODE_SET) {
        group_size = DC_SET_GROUP_SIZE;  /* 5 */
        fwd_cmd1   = 0x0BU;
        fwd_cmd2   = 0x51U;             /* SET_LIST_OF_SW */
    } else {
        group_size = DC_GET_GROUP_SIZE;  /* 4 */
        fwd_cmd1   = 0x0BU;
        fwd_cmd2   = 0x52U;             /* GET_LIST_OF_SW */
    }

    uint16_t num_groups = total_len / group_size;
    if (num_groups == 0U) return;

    /* Per-board batch buffers — collect groups destined for each board */
    static uint8_t batch_buf[DC_MAX_BOARDS][PKT_MAX_PAYLOAD];
    uint16_t batch_len[DC_MAX_BOARDS] = {0};

    /* Sort groups into per-board buckets */
    for (uint16_t g = 0U; g < num_groups; g++) {
        const uint8_t *group = &data[g * group_size];
        uint8_t board_id     = group[0];

        if (board_id < DC_MAX_BOARDS) {
            uint16_t len = batch_len[board_id];
            if (len + group_size <= PKT_MAX_PAYLOAD) {
                memcpy(&batch_buf[board_id][len], group, group_size);
                batch_len[board_id] += group_size;
            }
        }
    }

    /* Aggregate response buffer */
    static uint8_t agg_payload[PKT_MAX_PAYLOAD];
    uint16_t agg_len   = 0U;
    bool     any_error = false;

    /* Signal ISR to use mailbox instead of tx_request */
    dc_list_active = true;

    /* Send one batched packet per board */
    for (uint8_t bid = 0U; bid < DC_MAX_BOARDS; bid++) {
        if (batch_len[bid] == 0U) continue;

        DC_Uart_Handle *dc = DC_GetHandle(bid);
        if (dc == NULL) {
            if (agg_len + 3U <= PKT_MAX_PAYLOAD) {
                agg_payload[agg_len++] = 0x00U;     /* status_1 */
                agg_payload[agg_len++] = 0x02U;     /* status_2: bad board */
                agg_payload[agg_len++] = bid;        /* boardID */
            }
            any_error = true;
            continue;
        }

        /* Clear the response mailbox */
        dc_response.ready = false;

        /* Forward the entire batch as a single list command */
        DC_Uart_SendPacket(dc,
                           dc_list_request.msg1,
                           dc_list_request.msg2,
                           fwd_cmd1, fwd_cmd2,
                           batch_buf[bid], batch_len[bid]);

        /* Wait for response with timeout.
         * Batched list commands may process many switches (~3.4 ms each),
         * so use the longer DC_LIST_TIMEOUT instead of DC_RESPONSE_TIMEOUT. */
        uint32_t t0 = LL_GetTick();
        while (!dc_response.ready) {
            if ((LL_GetTick() - t0) >= DC_LIST_TIMEOUT) {
                break;
            }
        }

        if (dc_response.ready) {
            /* Copy daughtercard response into aggregate buffer */
            if (agg_len + dc_response.length <= PKT_MAX_PAYLOAD) {
                memcpy(&agg_payload[agg_len], dc_response.payload,
                       dc_response.length);
                agg_len += dc_response.length;
            }
            dc_response.ready = false;
        } else {
            /* Timeout — record error for this board */
            if (agg_len + 3U <= PKT_MAX_PAYLOAD) {
                agg_payload[agg_len++] = 0x00U;     /* status_1 */
                agg_payload[agg_len++] = 0x05U;     /* status_2: timeout */
                agg_payload[agg_len++] = bid;        /* boardID */
            }
            any_error = true;
        }
    }

    /* Done with synchronous processing */
    dc_list_active = false;

    /* Send aggregate response to GUI */
    tx_request.msg1   = dc_list_request.msg1;
    tx_request.msg2   = dc_list_request.msg2;
    tx_request.cmd1   = dc_list_request.cmd1;
    tx_request.cmd2   = dc_list_request.cmd2;
    if (agg_len > 0U) {
        memcpy(tx_request.payload, agg_payload, agg_len);
    }
    tx_request.length  = agg_len;
    tx_request.pending = true;

    (void)any_error;  /* Available for future use (e.g., error LED) */
}

/* ==========================================================================
 *  SWITCH-CONTROLLED ADC MEASUREMENT — main loop context only
 *
 *  Atomic sequence:
 *    1. Save current switch states (GET_ALL_SW per referenced board)
 *    2. Set all switches to GND (AllGND per referenced board)
 *    3. Enable specified switches (SET_LIST_OF_SW, batched per board)
 *    4. Wait deterministic delay (TIM6 one-pulse, µs precision)
 *    5. Burst-read ADC (100 samples via SPI2)
 *    6. Restore original switch states (SET_LIST_OF_SW from saved data)
 *    7. Calculate Vpp and return response
 *
 *  Uses the same dc_list_active / dc_response mailbox mechanism as
 *  Command_ExecuteDcList() for synchronous daughtercard communication.
 *
 *  TIM6 configuration:
 *    APB1 timer clock = 240 MHz (APB1 prescaler ÷2 → timer clock ×2)
 *    PSC = 239 → 1 MHz tick = 1 µs resolution
 *    ARR = (delay_ms × 1000) - 1
 *    One-pulse mode: counter stops after overflow, poll UIF flag
 * ========================================================================== */
void Command_ExecuteMeasureADC(void)
{
    const uint8_t *sw_data    = measure_adc_request.sw_payload;
    const uint16_t sw_len     = measure_adc_request.sw_length;
    const uint16_t delay_ms   = measure_adc_request.delay_ms;
    const uint8_t  group_size = DC_SET_GROUP_SIZE;  /* 5 */

    uint16_t num_groups = sw_len / group_size;
    if (num_groups == 0U) {
        tx_request.msg1    = measure_adc_request.msg1;
        tx_request.msg2    = measure_adc_request.msg2;
        tx_request.cmd1    = measure_adc_request.cmd1;
        tx_request.cmd2    = measure_adc_request.cmd2;
        tx_request.payload[0] = STATUS_CAT_GENERAL;
        tx_request.payload[1] = STATUS_PAYLOAD_SHORT;
        tx_request.length  = 2U;
        tx_request.pending = true;
        return;
    }

    /* ---- Static buffers for save/restore ---- */
    static uint8_t save_buf[DC_MAX_BOARDS][MEASURE_ADC_SW_STATES];
    bool     save_valid[DC_MAX_BOARDS] = {false};
    bool     error_occurred = false;

    /* Signal ISR to route DC responses to mailbox */
    dc_list_active = true;

    /* ==================================================================
     *  PHASE 1: Save current switch states on connected boards (GET_ALL_SW)
     *
     *  Try every board — boards that don't respond (timeout) are marked
     *  save_valid[bid] = false and skipped in all subsequent phases.
     *  This allows the measurement to proceed even if no driver boards
     *  are plugged in.
     * ================================================================== */
    for (uint8_t bid = 0U; bid < DC_MAX_BOARDS; bid++) {
        DC_Uart_Handle *dc = DC_GetHandle(bid);
        if (dc == NULL) continue;

        dc_response.ready = false;

        /* GET_ALL_SW (0x0B53), payload = [boardID] */
        uint8_t get_all_payload[1] = { bid };
        DC_Uart_SendPacket(dc,
                           measure_adc_request.msg1,
                           measure_adc_request.msg2,
                           0x0BU, 0x53U,
                           get_all_payload, 1U);

        uint32_t t0 = LL_GetTick();
        while (!dc_response.ready) {
            if ((LL_GetTick() - t0) >= DC_LIST_TIMEOUT) break;
        }

        if (dc_response.ready) {
            /* Response: [status1][status2][boardID][600 bytes switch states]
             * Copy the 600 switch state bytes (skip 3-byte header) */
            if (dc_response.length >= (3U + MEASURE_ADC_SW_STATES)) {
                memcpy(save_buf[bid], &dc_response.payload[3],
                       MEASURE_ADC_SW_STATES);
                save_valid[bid] = true;
            }
            dc_response.ready = false;
        }
        /* Timeout = board not connected, save_valid stays false — skip it */
    }

    /* ==================================================================
     *  PHASE 2: Set connected boards to GND (AllGND / 0x0A02)
     *
     *  Only boards that responded in Phase 1 are grounded.
     *  Boards that didn't respond are skipped (not connected).
     * ================================================================== */
    for (uint8_t bid = 0U; bid < DC_MAX_BOARDS; bid++) {
        if (!save_valid[bid]) continue;

        DC_Uart_Handle *dc = DC_GetHandle(bid);
        if (dc == NULL) continue;

        dc_response.ready = false;

        /* AllGND (0x0A02), payload = [boardID] */
        uint8_t gnd_payload[1] = { bid };
        DC_Uart_SendPacket(dc,
                           measure_adc_request.msg1,
                           measure_adc_request.msg2,
                           0x0AU, 0x02U,
                           gnd_payload, 1U);

        uint32_t t0 = LL_GetTick();
        while (!dc_response.ready) {
            if ((LL_GetTick() - t0) >= DC_LIST_TIMEOUT) break;
        }

        if (dc_response.ready) {
            dc_response.ready = false;
        } else {
            error_occurred = true;
        }
    }

    /* ==================================================================
     *  PHASE 3+4: PWM phase sync + switch enable under deterministic timer
     *
     *  1. Bucket switch groups by boardID, track which boards need sync
     *  2. Start TIM6 (phase sync + switch enable + user delay)
     *  3. Fire PWMPhaseSync (0x0A81) to each board — resets TIM2/TIM1/TIM8
     *  4. Fire SET_LIST_OF_SW per board
     *  5. Spin on TIM6 UIF → ADC burst
     *
     *  Timer value = PWM_PHASE_SYNC_TIME_US
     *              + (num_switches × SWITCH_ENABLE_TIME_US)
     *              + (delay_ms × 1000)
     *
     *  TIM6 configuration (16-bit basic timer):
     *    APB1 timer clock = 240 MHz (D2PPRE1 = ÷2 → timer ×2)
     *    PSC = 2399 → 100 kHz → 10 µs per tick (max ARR 65535 → 655 ms)
     *    ARR = total_us / 10 - 1
     * ================================================================== */
    /* Bucket groups by boardID */
    static uint8_t batch_buf[DC_MAX_BOARDS][PKT_MAX_PAYLOAD];
    uint16_t batch_len[DC_MAX_BOARDS] = {0};
    bool board_needs_sync[DC_MAX_BOARDS] = {false};

    for (uint16_t g = 0U; g < num_groups; g++) {
        const uint8_t *group = &sw_data[g * group_size];
        uint8_t bid = group[0];
        if (bid < DC_MAX_BOARDS) {
            uint16_t len = batch_len[bid];
            if (len + group_size <= PKT_MAX_PAYLOAD) {
                memcpy(&batch_buf[bid][len], group, group_size);
                batch_len[bid] += group_size;
            }
            board_needs_sync[bid] = true;
        }
    }

    /* Compute total timer value: phase sync + switch enable + settling */
    uint32_t total_us = PWM_PHASE_SYNC_TIME_US
                      + ((uint32_t)num_groups * SWITCH_ENABLE_TIME_US)
                      + ((uint32_t)delay_ms * 1000U);
    uint32_t arr_val  = (total_us / 10U) - 1U;
    if (arr_val > 65535U) arr_val = 65535U;  /* Clamp to 16-bit max */

    /* Start TIM6 BEFORE sending any commands */
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM6);

    LL_TIM_SetPrescaler(TIM6, 2399U);       /* 240 MHz / 2400 = 100 kHz = 10 µs */
    LL_TIM_SetAutoReload(TIM6, arr_val);
    LL_TIM_SetOnePulseMode(TIM6, LL_TIM_ONEPULSEMODE_SINGLE);
    LL_TIM_GenerateEvent_UPDATE(TIM6);       /* Force PSC/ARR load */
    LL_TIM_ClearFlag_UPDATE(TIM6);           /* Clear UIF from UG */
    LL_TIM_EnableCounter(TIM6);              /* Start counting */

    /* Fire PWMPhaseSync (0x0A81) to each board — resets PWM counters to zero */
    for (uint8_t bid = 0U; bid < DC_MAX_BOARDS; bid++) {
        if (!board_needs_sync[bid]) continue;
        if (!save_valid[bid]) continue;

        DC_Uart_Handle *dc = DC_GetHandle(bid);
        if (dc == NULL) continue;

        uint8_t sync_payload[1] = { bid };
        DC_Uart_SendPacket(dc,
                           measure_adc_request.msg1,
                           measure_adc_request.msg2,
                           0x0AU, 0x81U,  /* PWMPhaseSync */
                           sync_payload, 1U);
    }

    /* Fire SET_LIST_OF_SW to each board — no response wait */
    for (uint8_t bid = 0U; bid < DC_MAX_BOARDS; bid++) {
        if (batch_len[bid] == 0U) continue;
        if (!save_valid[bid]) continue;

        DC_Uart_Handle *dc = DC_GetHandle(bid);
        if (dc == NULL) continue;

        DC_Uart_SendPacket(dc,
                           measure_adc_request.msg1,
                           measure_adc_request.msg2,
                           0x0BU, 0x51U,  /* SET_LIST_OF_SW */
                           batch_buf[bid], batch_len[bid]);
    }

    /* Spin until timer expires — PWM phase locks + switches enable during this window */
    while (!LL_TIM_IsActiveFlag_UPDATE(TIM6)) {
        /* Deterministic spin — no jitter from ISR latency */
    }

    LL_TIM_ClearFlag_UPDATE(TIM6);
    LL_TIM_DisableCounter(TIM6);
    LL_APB1_GRP1_DisableClock(LL_APB1_GRP1_PERIPH_TIM6);

    /* ==================================================================
     *  PHASE 5: Burst ADC read — 100 samples via SPI2
     * ================================================================== */
    static uint8_t  meas_burst_payload[ADC_BURST_PAYLOAD_SIZE];
    static uint32_t meas_burst_raw[ADC_BURST_COUNT];

    for (uint32_t i = 0U; i < ADC_BURST_COUNT; i++) {
        uint32_t   sample = 0U;
        SPI_Status status = SPI_LTC2338_Read(&spi2_handle, &sample);
        meas_burst_raw[i] = sample;

        uint32_t offset = i * 4U;
        if (status == SPI_OK) {
            meas_burst_payload[offset + 0U] = (uint8_t)( sample        & 0xFFU);
            meas_burst_payload[offset + 1U] = (uint8_t)((sample >>  8U) & 0xFFU);
            meas_burst_payload[offset + 2U] = (uint8_t)((sample >> 16U) & 0x03U);
            meas_burst_payload[offset + 3U] = 0x00U;
        } else {
            meas_burst_payload[offset + 0U] = 0xFFU;
            meas_burst_payload[offset + 1U] = 0xFFU;
            meas_burst_payload[offset + 2U] = 0xFFU;
            meas_burst_payload[offset + 3U] = 0xFFU;
        }
    }

    /* ==================================================================
     *  Drain stale Phase 3+4 responses
     *
     *  Fire-and-forget commands sent: PWMPhaseSync + SET_LIST_OF_SW.
     *  Each produces a response that may be queued in dc_response.
     *  Drain them all before Phase 6 restore begins.
     * ================================================================== */
    {
        /* Count how many fire-and-forget commands were sent */
        uint8_t pending_responses = 0U;
        for (uint8_t bid = 0U; bid < DC_MAX_BOARDS; bid++) {
            if (!save_valid[bid]) continue;
            if (board_needs_sync[bid]) pending_responses += 1U; /* PWMPhaseSync */
            if (batch_len[bid] > 0U)   pending_responses += 1U; /* SET_LIST_OF_SW */
        }

        for (uint8_t r = 0U; r < pending_responses; r++) {
            uint32_t t0 = LL_GetTick();
            while (!dc_response.ready) {
                if ((LL_GetTick() - t0) >= DC_LIST_TIMEOUT) break;
            }
            dc_response.ready = false;
        }
    }

    /* ==================================================================
     *  PHASE 6: Restore ALL 4 boards to their original switch states
     *
     *  For each board: AllGND first (clean slate), then replay non-GND
     *  switches from the saved state via SET_LIST_OF_SW.
     * ================================================================== */
    for (uint8_t bid = 0U; bid < DC_MAX_BOARDS; bid++) {
        if (!save_valid[bid]) continue;

        DC_Uart_Handle *dc = DC_GetHandle(bid);
        if (dc == NULL) continue;

        /* First set AllGND to clear any measurement switches */
        dc_response.ready = false;
        uint8_t gnd_payload[1] = { bid };
        DC_Uart_SendPacket(dc,
                           measure_adc_request.msg1,
                           measure_adc_request.msg2,
                           0x0AU, 0x02U,
                           gnd_payload, 1U);

        uint32_t t0 = LL_GetTick();
        while (!dc_response.ready) {
            if ((LL_GetTick() - t0) >= DC_LIST_TIMEOUT) break;
        }
        dc_response.ready = false;

        /* Build SET_LIST_OF_SW groups for non-GND switches.
         * save_buf layout: [0..299] = bank 0, [300..599] = bank 1.
         * Each byte: 0=Float, 1=HVSG, 2=GND.
         * Group format: [boardID][bank][SW_hi][SW_lo][state] */
        static uint8_t restore_buf[PKT_MAX_PAYLOAD];
        uint16_t restore_len = 0U;

        for (uint16_t bank = 0U; bank < 2U; bank++) {
            for (uint16_t sw = 0U; sw < 300U; sw++) {
                uint8_t state = save_buf[bid][bank * 300U + sw];
                if (state == 0x02U) continue;  /* GND — already set by AllGND */

                if (restore_len + 5U > PKT_MAX_PAYLOAD) break;

                restore_buf[restore_len + 0U] = bid;
                restore_buf[restore_len + 1U] = (uint8_t)bank;
                restore_buf[restore_len + 2U] = (uint8_t)(sw >> 8);
                restore_buf[restore_len + 3U] = (uint8_t)(sw & 0xFFU);
                restore_buf[restore_len + 4U] = state;
                restore_len += 5U;
            }
        }

        if (restore_len > 0U) {
            dc_response.ready = false;

            DC_Uart_SendPacket(dc,
                               measure_adc_request.msg1,
                               measure_adc_request.msg2,
                               0x0BU, 0x51U,  /* SET_LIST_OF_SW */
                               restore_buf, restore_len);

            t0 = LL_GetTick();
            while (!dc_response.ready) {
                if ((LL_GetTick() - t0) >= DC_LIST_TIMEOUT) break;
            }
            dc_response.ready = false;
        }
    }

    /* Done with synchronous DC processing */
    dc_list_active = false;

    /* ==================================================================
     *  PHASE 7: Calculate Vpp and build response
     *
     *  LTC2338-18 bipolar mode: ±10.24 V, 18-bit two's complement.
     *  Bit 17 is sign bit.  Range: -131072 to +131071.
     *  LSB = 20.48 / 262144 = 78.125 µV.
     *  Matches GUI PlotBurstADC: lsbVolts = 20.48 / 262144.0
     * ================================================================== */
    int32_t min_val = INT32_MAX;
    int32_t max_val = INT32_MIN;

    for (uint32_t i = 0U; i < ADC_BURST_COUNT; i++) {
        int32_t s = (int32_t)(meas_burst_raw[i] & 0x3FFFFU);
        /* Sign-extend bit 17 */
        if (s & 0x20000) {
            s |= (int32_t)0xFFFC0000;
        }
        if (s < min_val) min_val = s;
        if (s > max_val) max_val = s;
    }

    float vpp = (float)(max_val - min_val)
              * (ADC_FULL_SCALE_V / ADC_FULL_SCALE_CODES);

    /* Response: [status1][status2][Vpp float LE (4B)][burst data (400B)]
     * Total: 406 bytes */
    tx_request.msg1   = measure_adc_request.msg1;
    tx_request.msg2   = measure_adc_request.msg2;
    tx_request.cmd1   = measure_adc_request.cmd1;
    tx_request.cmd2   = measure_adc_request.cmd2;

    tx_request.payload[0] = error_occurred ? STATUS_CAT_ADC : STATUS_CAT_OK;
    tx_request.payload[1] = error_occurred ? STATUS_ADC_RESTORE_FAIL : STATUS_CODE_OK;
    memcpy(&tx_request.payload[2], &vpp, sizeof(float));
    memcpy(&tx_request.payload[6], meas_burst_payload, ADC_BURST_PAYLOAD_SIZE);
    tx_request.length  = 2U + 4U + ADC_BURST_PAYLOAD_SIZE;  /* 406 bytes */
    tx_request.pending = true;
}

/* ==========================================================================
 *  ERROR HANDLER
 * ========================================================================== */
void Error_Handler(uint32_t fault_code)
{
    __disable_irq();
    LL_PWR_EnableBkUpAccess();
    LL_RTC_BAK_SetRegister(RTC, LL_RTC_BKP_DR0, fault_code);
    while (1);
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    while (1);
}
#endif
