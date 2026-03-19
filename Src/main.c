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
#include <string.h>
#include "stm32h7xx_ll_rtc.h"
#include "spi_driver.h"
#include "ll_tick.h"

/* Protocol parser instances */
static ProtocolParser usart10_parser;
static ProtocolParser dc1_parser;
static ProtocolParser dc2_parser;
static ProtocolParser dc3_parser;
static ProtocolParser dc4_parser;

/* Deferred TX request — set by ISR, consumed by main loop */
TxRequest    tx_request    = {0};

/* Deferred burst ADC request — set by ISR, consumed by main loop */
BurstRequest burst_request = {0};

/* Daughtercard forward request — async, single command to one board */
DcForwardRequest dc_forward_request = {0};

/* Daughtercard list request — synchronous, per-group sequential */
DcListRequest    dc_list_request    = {0};

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

/* Private function prototypes -----------------------------------------------*/
static void SystemInit_Sequence(void);
static void OnPacketReceived(const PacketHeader *header,
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
 *  iterating through each group in the payload, forwarding individual
 *  switch commands to the target daughtercard, waiting for each response,
 *  and building an aggregate response for the GUI.
 *
 *  SET groups: 5 bytes each [boardID][bank][SW_hi][SW_lo][state]
 *   → forwards as SetSingleSwitch (0x0A10) with payload [boardID][bank][SW_hi][SW_lo][state]
 *
 *  GET groups: 4 bytes each [boardID][bank][SW_hi][SW_lo]
 *   → forwards as GetSingleSwitch (0x0A11) with payload [boardID][bank][SW_hi][SW_lo]
 *
 *  This function BLOCKS the main loop until all groups are processed.
 *  Each round-trip is ~3.4 ms (UART + SPI), timeout is 10 ms per group.
 * ========================================================================== */
void Command_ExecuteDcList(void)
{
    const uint8_t *data       = dc_list_request.payload;
    const uint16_t total_len  = dc_list_request.length;
    const uint8_t  mode       = dc_list_request.mode;

    /* Determine group size and forwarded command code */
    uint8_t group_size;
    uint8_t fwd_cmd1, fwd_cmd2;

    if (mode == DC_LIST_MODE_SET) {
        group_size = DC_SET_GROUP_SIZE;  /* 5 */
        fwd_cmd1   = 0x0AU;
        fwd_cmd2   = 0x10U;             /* SetSingleSwitch */
    } else {
        group_size = DC_GET_GROUP_SIZE;  /* 4 */
        fwd_cmd1   = 0x0AU;
        fwd_cmd2   = 0x11U;             /* GetSingleSwitch */
    }

    /* Calculate number of groups */
    uint16_t num_groups = total_len / group_size;
    if (num_groups == 0U) return;

    /* Aggregate response buffer.
     * For SET: 3 bytes per group [status_1][status_2][boardID]
     * For GET: we collect full responses from each daughtercard.
     * We'll use a simple status-per-group approach for both. */
    static uint8_t agg_payload[PKT_MAX_PAYLOAD];
    uint16_t agg_len    = 0U;
    bool     any_error  = false;

    /* Signal ISR to use mailbox instead of tx_request */
    dc_list_active = true;

    for (uint16_t g = 0U; g < num_groups; g++) {
        const uint8_t *group = &data[g * group_size];
        uint8_t board_id     = group[0];

        /* Validate boardID */
        DC_Uart_Handle *dc = DC_GetHandle(board_id);
        if (dc == NULL) {
            /* Invalid boardID — record error, continue */
            if (agg_len + 3U <= PKT_MAX_PAYLOAD) {
                agg_payload[agg_len++] = 0x00U;         /* status_1 */
                agg_payload[agg_len++] = 0x02U;         /* status_2: bad board */
                agg_payload[agg_len++] = board_id;       /* boardID */
            }
            any_error = true;
            continue;
        }

        /* Clear the response mailbox */
        dc_response.ready = false;

        /* Forward this group's data as a single-switch command.
         * The payload sent to the daughtercard is the group itself
         * (boardID + bank + SW + state/nothing). */
        DC_Uart_SendPacket(dc,
                           dc_list_request.msg1,
                           dc_list_request.msg2,
                           fwd_cmd1, fwd_cmd2,
                           group, group_size);

        /* Wait for response with timeout */
        uint32_t t0 = LL_GetTick();
        while (!dc_response.ready) {
            if ((LL_GetTick() - t0) >= DC_RESPONSE_TIMEOUT) {
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
            /* Timeout — record error for this group */
            if (agg_len + 3U <= PKT_MAX_PAYLOAD) {
                agg_payload[agg_len++] = 0x00U;         /* status_1 */
                agg_payload[agg_len++] = 0x05U;         /* status_2: timeout */
                agg_payload[agg_len++] = board_id;       /* boardID */
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
