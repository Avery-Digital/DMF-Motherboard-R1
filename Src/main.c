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
#include "endian_be.h"
#include "DRV8702.h"
#include "TEC_PWM.h"
#include "DAC80508.h"
#include "ADS7066.h"
#include "VN5T016AH.h"
#include "DC_Uart_Driver.h"
#include "Act_Uart_Driver.h"
#include "RS485_Driver.h"
#include "MightyZap.h"
#include "TEC_PID.h"
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
ServoRawRequest  servo_raw_request  = {0};

/* Measure ADC request — switch-controlled deterministic ADC measurement */
MeasureAdcRequest measure_adc_request = {0};
MeasureAdcRequest sweep_adc_request   = {0};

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

/* ========================= PWM Phase Sync ================================= */

/**
 * @brief  Pulse both sync GPIOs HIGH then LOW to reset PWM timers on
 *         all connected driver boards.  Rising edge triggers EXTI on
 *         driver board PD3, which resets TIM2/TIM1/TIM8 counters.
 *
 *         Both connectors are pulsed back-to-back — at 480 MHz the
 *         gap between them is ~10 ns, negligible at 10 kHz PWM.
 *
 *         Pulse width: ~100 ns (a few NOPs for reliable edge detection).
 */
void PWM_SyncPulse(void)
{
    /* Assert HIGH on both sync lines simultaneously */
    LL_GPIO_SetOutputPin(pwm_sync_con1_pin.port, pwm_sync_con1_pin.pin);
    LL_GPIO_SetOutputPin(pwm_sync_con2_pin.port, pwm_sync_con2_pin.pin);

    /* Brief pulse — ~100 ns at 480 MHz */
    __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP();

    /* De-assert — return to idle LOW */
    LL_GPIO_ResetOutputPin(pwm_sync_con1_pin.port, pwm_sync_con1_pin.pin);
    LL_GPIO_ResetOutputPin(pwm_sync_con2_pin.port, pwm_sync_con2_pin.pin);
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

            /* Forward raw bytes to mightyZAP servo via UART8 RS485 */
            if (servo_raw_request.pending) {
                servo_raw_request.pending = false;
                Command_ExecuteServoRaw();
            }

            /* Execute switch-controlled ADC measurement — synchronous, blocking */
            if (measure_adc_request.pending) {
                measure_adc_request.pending = false;
                Command_ExecuteMeasureADC();
            }

            /* Execute per-switch ADC sweep — synchronous, blocking */
            if (sweep_adc_request.pending) {
                sweep_adc_request.pending = false;
                Command_ExecuteSweepADC();
            }

            /* Execute SET_LIST_OF_SW / GET_LIST_OF_SW — synchronous, blocking */
            if (dc_list_request.pending) {
                dc_list_request.pending = false;
                Command_ExecuteDcList();
            }

            /* TEC PID update — polled at 10 Hz (100 ms period).
             * Returns immediately if period hasn't elapsed. */
            if (tec_pid.running) {
                TEC_PID_Update(&tec_pid);
            }

            /* Consume pending TX outside of ISR context (HIGHEST PRIORITY) */
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
            /* PID telemetry auto-stream (LOWER PRIORITY — only when bus is free).
             * Skips gracefully if a command response is pending. */
            else if (pid_telemetry.pending) {
                pid_telemetry.pending = false;
                USART_Driver_SendPacket(&usart10_handle,
                                        0x01, 0x02,     /* msg1, msg2 (telemetry) */
                                        0x0C, 0x5B,     /* CMD_TEC_PID_STATUS     */
                                        (const uint8_t[]){
                                            STATUS_CAT_OK, STATUS_CODE_OK,
                                            pid_telemetry.tec_id,
                                            pid_telemetry.running  ? 0x01U : 0x00U,
                                            pid_telemetry.faulted  ? 0x01U : 0x00U,
                                            (uint8_t)(pid_telemetry.measured_c100 >> 8),
                                            (uint8_t)(pid_telemetry.measured_c100 & 0xFF),
                                            (uint8_t)(pid_telemetry.setpoint_c100 >> 8),
                                            (uint8_t)(pid_telemetry.setpoint_c100 & 0xFF),
                                            (uint8_t)pid_telemetry.output,
                                            (uint8_t)(pid_telemetry.error_c100 >> 8),
                                            (uint8_t)(pid_telemetry.error_c100 & 0xFF),
                                        },
                                        12U);
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

    /* Step 5a: TEC PWM — TIM1/TIM8 for DRV8702 H-bridge IN1/IN2 pins.
     *          Reconfigures IN1/IN2 from GPIO to AF mode for PWM.
     *          All TECs start at 0% duty (off). 20 kHz default. */
    TEC_PWM_Init(TEC_PWM_DEFAULT_FREQ_HZ);

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

    /* Step 6c: PWM sync GPIO — idle LOW, ready for rising-edge pulse */
    Pin_Init(&pwm_sync_con1_pin);
    LL_GPIO_ResetOutputPin(pwm_sync_con1_pin.port, pwm_sync_con1_pin.pin);
    Pin_Init(&pwm_sync_con2_pin);
    LL_GPIO_ResetOutputPin(pwm_sync_con2_pin.port, pwm_sync_con2_pin.pin);

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

    /* Step 7b: mightyZAP linear servo — UART8 + RS-485 on PE0/PE1/PD15
     *          57600 baud, polled TX/RX, half-duplex direction on PD15. */
    if (MightyZap_Init(&mzap_handle) != MZAP_OK) {
        Error_Handler(0x71);
    }

    /* Step 7c: TEC PID controller — initialize state (not running yet) */
    TEC_PID_Init(&tec_pid);

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
    /* ---- Elapsed time and phase timing ---- */
    uint32_t t_start = LL_GetTick();
    uint16_t phase_ms[MEASURE_ADC_PHASE_COUNT] = {0};
    uint32_t phase_start;

    const uint8_t *sw_data    = measure_adc_request.sw_payload;
    const uint16_t sw_len     = measure_adc_request.sw_length;
    const uint16_t delay_ms   = measure_adc_request.delay_ms;
    const uint8_t  board_mask = measure_adc_request.board_mask;
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

    /* ---- Static buffers ---- */
    static uint8_t save_buf[DC_MAX_BOARDS][PKT_MAX_PAYLOAD]; /* HVSG switch triplets */
    uint16_t save_len[DC_MAX_BOARDS] = {0};
    static SwitchSaveEntry meas_save[MAX_MEAS_SAVE_ENTRIES]; /* measurement switch original states */
    uint16_t meas_save_count = 0U;
    bool     save_valid[DC_MAX_BOARDS] = {false};
    bool     error_occurred = false;

    /* Signal ISR to route DC responses to mailbox */
    dc_list_active = true;

    /* ==================================================================
     *  PHASE 1: Save HVSG switch list from connected boards
     *
     *  Uses GET_HVSG_SWITCHES (0x0B54) — returns only the switches set
     *  to HVSG as [bank][SW_hi][SW_lo] triplets.  Much smaller than
     *  GET_ALL_SW (603 bytes → typically < 50 bytes).
     *  Skips boards not in board_mask.
     * ================================================================== */
    phase_start = LL_GetTick();
    for (uint8_t bid = 0U; bid < DC_MAX_BOARDS; bid++) {
        if (!(board_mask & (1U << bid))) continue;

        DC_Uart_Handle *dc = DC_GetHandle(bid);
        if (dc == NULL) continue;

        dc_response.ready = false;

        uint8_t get_payload[1] = { bid };
        DC_Uart_SendPacket(dc,
                           measure_adc_request.msg1,
                           measure_adc_request.msg2,
                           CMD_GET_HVSG_SW_CMD1, CMD_GET_HVSG_SW_CMD2,
                           get_payload, 1U);

        uint32_t t0 = LL_GetTick();
        while (!dc_response.ready) {
            if ((LL_GetTick() - t0) >= DC_LIST_TIMEOUT) break;
        }

        if (dc_response.ready) {
            /* Response: [status1][status2][boardID][triplets...]
             * Copy triplets (skip 3-byte header) */
            if (dc_response.length > 3U) {
                uint16_t data_len = dc_response.length - 3U;
                if (data_len > PKT_MAX_PAYLOAD) data_len = PKT_MAX_PAYLOAD;
                memcpy(save_buf[bid], &dc_response.payload[3], data_len);
                save_len[bid] = data_len;
            }
            save_valid[bid] = true;
            dc_response.ready = false;
        }
    }
    /* ---- Phase 1b: Query original state of measurement switches not in HVSG list ----
     *
     *  For each measurement switch, check if it already appears in the
     *  HVSG save list.  If not, its original state is GND or FLOAT and
     *  we need to query + save it so Phase 6 can restore it.
     *  Uses GET_LIST_OF_SW (0x0B52): send [bid][bank][SW_hi][SW_lo] groups,
     *  receive [0xAB][0xCD][5-byte groups...].
     *
     *  NOTE: The driver board response echoes back garbled SW bytes, so we
     *  pair response states with the original query order (1:1 correspondence).
     *  The state byte at position [4] of each 5-byte group IS correct.
     *
     *  If any query times out → abort entire command.
     */
    for (uint8_t bid = 0U; bid < DC_MAX_BOARDS; bid++) {
        if (!(board_mask & (1U << bid))) continue;
        if (!save_valid[bid]) continue;

        /* Build query and record which switches we're asking about */
        static uint8_t query_buf[PKT_MAX_PAYLOAD];
        uint16_t query_len = 0U;
        uint16_t query_start = meas_save_count;  /* remember where this board's entries start */

        for (uint16_t g = 0U; g < num_groups; g++) {
            const uint8_t *group = &sw_data[g * group_size];
            if (group[0] != bid) continue;

            uint8_t  sw_bank = group[1];
            uint8_t  sw_hi   = group[2];
            uint8_t  sw_lo   = group[3];

            /* Check if this switch is already in the HVSG save list */
            bool found = false;
            uint16_t num_hvsg = save_len[bid] / 3U;
            for (uint16_t e = 0U; e < num_hvsg; e++) {
                if (save_buf[bid][e * 3U + 0U] == sw_bank &&
                    save_buf[bid][e * 3U + 1U] == sw_hi &&
                    save_buf[bid][e * 3U + 2U] == sw_lo) {
                    found = true;
                    break;
                }
            }

            if (!found && query_len + 4U <= PKT_MAX_PAYLOAD
                       && meas_save_count < MAX_MEAS_SAVE_ENTRIES) {
                /* Pre-populate entry with switch identity from the query;
                 * original_state will be filled from the response. */
                meas_save[meas_save_count].bid    = bid;
                meas_save[meas_save_count].bank   = sw_bank;
                meas_save[meas_save_count].sw_num = ((uint16_t)sw_hi << 8) | sw_lo;
                meas_save[meas_save_count].original_state = SW_STATE_GND; /* default */
                meas_save_count++;

                query_buf[query_len + 0U] = bid;
                query_buf[query_len + 1U] = sw_bank;
                query_buf[query_len + 2U] = sw_hi;
                query_buf[query_len + 3U] = sw_lo;
                query_len += 4U;
            }
        }

        if (query_len > 0U) {
            DC_Uart_Handle *dc = DC_GetHandle(bid);
            if (dc == NULL) continue;

            dc_response.ready = false;

            DC_Uart_SendPacket(dc,
                               measure_adc_request.msg1,
                               measure_adc_request.msg2,
                               0x0BU, 0x52U,  /* GET_LIST_OF_SW */
                               query_buf, query_len);

            uint32_t t0 = LL_GetTick();
            while (!dc_response.ready) {
                if ((LL_GetTick() - t0) >= DC_LIST_TIMEOUT) break;
            }

            if (!dc_response.ready) {
                /* Timeout — abort entire command */
                dc_list_active = false;
                tx_request.msg1    = measure_adc_request.msg1;
                tx_request.msg2    = measure_adc_request.msg2;
                tx_request.cmd1    = measure_adc_request.cmd1;
                tx_request.cmd2    = measure_adc_request.cmd2;
                tx_request.payload[0] = STATUS_CAT_ADC;
                tx_request.payload[1] = STATUS_ADC_RESTORE_FAIL;
                tx_request.length  = 2U;
                tx_request.pending = true;
                return;
            }

            /* Extract state bytes from response, paired 1:1 with query order.
             * Response: [0xAB][0xCD][5-byte group]... — state is byte [4] of each group.
             * We already stored switch identity from the query; just fill in the state. */
            if (dc_response.length > 2U) {
                uint16_t data_len = dc_response.length - 2U;
                uint16_t num_resp = data_len / 5U;
                uint16_t num_queried = meas_save_count - query_start;
                uint16_t n = (num_resp < num_queried) ? num_resp : num_queried;

                for (uint16_t e = 0U; e < n; e++) {
                    uint16_t off = 2U + e * 5U;
                    meas_save[query_start + e].original_state = dc_response.payload[off + 4U];
                }
            }
            dc_response.ready = false;
        }
    }

    phase_ms[0] = (uint16_t)(LL_GetTick() - phase_start);

    /* ==================================================================
     *  PHASE 2: Selectively GND only the HVSG switches
     *
     *  Instead of AllGND (all 600 switches), build SET_LIST_OF_SW from
     *  the saved HVSG list, setting each to SW_STATE_GND.
     * ================================================================== */
    phase_start = LL_GetTick();
    for (uint8_t bid = 0U; bid < DC_MAX_BOARDS; bid++) {
        if (!(board_mask & (1U << bid))) continue;
        if (!save_valid[bid]) continue;
        if (save_len[bid] == 0U) continue;

        DC_Uart_Handle *dc = DC_GetHandle(bid);
        if (dc == NULL) continue;

        static uint8_t gnd_buf[PKT_MAX_PAYLOAD];
        uint16_t gnd_len = 0U;
        uint16_t num_entries = save_len[bid] / 3U;

        for (uint16_t e = 0U; e < num_entries; e++) {
            if (gnd_len + 5U > PKT_MAX_PAYLOAD) break;
            gnd_buf[gnd_len + 0U] = bid;
            gnd_buf[gnd_len + 1U] = save_buf[bid][e * 3U + 0U];  /* bank */
            gnd_buf[gnd_len + 2U] = save_buf[bid][e * 3U + 1U];  /* SW_hi */
            gnd_buf[gnd_len + 3U] = save_buf[bid][e * 3U + 2U];  /* SW_lo */
            gnd_buf[gnd_len + 4U] = SW_STATE_GND;
            gnd_len += 5U;
        }

        if (gnd_len > 0U) {
            dc_response.ready = false;

            DC_Uart_SendPacket(dc,
                               measure_adc_request.msg1,
                               measure_adc_request.msg2,
                               0x0BU, 0x51U,  /* SET_LIST_OF_SW */
                               gnd_buf, gnd_len);

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
    }
    phase_ms[1] = (uint16_t)(LL_GetTick() - phase_start);

    /* ==================================================================
     *  PHASE 3: PWM phase sync — GPIO hardware pulse
     *
     *  Rising edge on PA12 (connector 1, boards 0+1) and PC5
     *  (connector 2, boards 2+3) triggers EXTI3 on each driver board's
     *  PD3, which resets TIM2/TIM1/TIM8 counters to zero.
     *
     *  Sub-microsecond sync — no UART round-trip needed.
     * ================================================================== */
    phase_start = LL_GetTick();

    /* Build switch batch buffers (needed by Phase 4) */
    static uint8_t batch_buf[DC_MAX_BOARDS][PKT_MAX_PAYLOAD];
    uint16_t batch_len[DC_MAX_BOARDS] = {0};

    for (uint16_t g = 0U; g < num_groups; g++) {
        const uint8_t *group = &sw_data[g * group_size];
        uint8_t bid = group[0];
        if (bid < DC_MAX_BOARDS && (board_mask & (1U << bid))) {
            uint16_t len = batch_len[bid];
            if (len + group_size <= PKT_MAX_PAYLOAD) {
                memcpy(&batch_buf[bid][len], group, group_size);
                batch_len[bid] += group_size;
            }
        }
    }

    /* GPIO sync pulse — resets all driver board PWM timers simultaneously */
    PWM_SyncPulse();

    phase_ms[2] = (uint16_t)(LL_GetTick() - phase_start);

    /* ==================================================================
     *  PHASE 4: Start TIM2 AFTER phase sync, then fire switches
     *
     *  TIM2 is a 32-bit timer with 100 ns resolution:
     *    APB1 timer clock = 240 MHz (D2PPRE1 = ÷2 → timer ×2)
     *    PSC = 23 → 10 MHz → 100 ns per tick
     *    ARR = total_ticks - 1 (32-bit, max ~429 seconds)
     *
     *  Includes burst ADC read immediately after timer expires.
     * ================================================================== */
    phase_start = LL_GetTick();

    uint32_t total_us = ((uint32_t)num_groups * SWITCH_ENABLE_TIME_US)
                      + ((uint32_t)delay_ms * 1000U);
    uint32_t arr_val  = (total_us * 10U) - 1U;  /* 100 ns ticks */

    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);

    LL_TIM_SetPrescaler(TIM2, 23U);         /* 240 MHz / 24 = 10 MHz = 100 ns */
    LL_TIM_SetAutoReload(TIM2, arr_val);
    LL_TIM_SetOnePulseMode(TIM2, LL_TIM_ONEPULSEMODE_SINGLE);
    LL_TIM_GenerateEvent_UPDATE(TIM2);       /* Force PSC/ARR load */
    LL_TIM_ClearFlag_UPDATE(TIM2);           /* Clear UIF from UG */
    LL_TIM_EnableCounter(TIM2);              /* Start counting */

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

    /* Spin until timer expires */
    while (!LL_TIM_IsActiveFlag_UPDATE(TIM2)) {
        /* Deterministic spin — no jitter from ISR latency */
    }

    LL_TIM_ClearFlag_UPDATE(TIM2);
    LL_TIM_DisableCounter(TIM2);
    LL_APB1_GRP1_DisableClock(LL_APB1_GRP1_PERIPH_TIM2);

    /* Burst ADC read — 100 samples via SPI2 */
    static uint8_t  meas_burst_payload[ADC_BURST_PAYLOAD_SIZE];
    static uint32_t meas_burst_raw[ADC_BURST_COUNT];

    for (uint32_t i = 0U; i < ADC_BURST_COUNT; i++) {
        uint32_t   sample = 0U;
        SPI_Status status = SPI_LTC2338_Read(&spi2_handle, &sample);
        meas_burst_raw[i] = sample;

        uint32_t offset = i * 4U;
        if (status == SPI_OK) {
            be32_pack(&meas_burst_payload[offset], sample & 0x3FFFFU);
        } else {
            be32_pack(&meas_burst_payload[offset], 0xFFFFFFFFU);
        }
    }
    phase_ms[3] = (uint16_t)(LL_GetTick() - phase_start);

    /* ==================================================================
     *  PHASE 5: Drain stale Phase 4 responses
     * ================================================================== */
    phase_start = LL_GetTick();
    for (uint8_t bid = 0U; bid < DC_MAX_BOARDS; bid++) {
        if (!(board_mask & (1U << bid))) continue;
        if (batch_len[bid] == 0U) continue;
        if (!save_valid[bid]) continue;

        uint32_t t0 = LL_GetTick();
        while (!dc_response.ready) {
            if ((LL_GetTick() - t0) >= DC_LIST_TIMEOUT) break;
        }
        dc_response.ready = false;
    }
    phase_ms[4] = (uint16_t)(LL_GetTick() - phase_start);

    /* ==================================================================
     *  PHASE 6: Restore all switches to their original state (fire-and-forget)
     *
     *  Two parts:
     *  6a: Restore HVSG switches from Phase 1 save list → SW_STATE_HVSG
     *  6b: Restore non-HVSG measurement switches from Phase 1b → original state
     *
     *  Both are combined into a single SET_LIST_OF_SW per board.
     *  Measurement is already captured so we send without waiting.
     * ================================================================== */
    phase_start = LL_GetTick();
    for (uint8_t bid = 0U; bid < DC_MAX_BOARDS; bid++) {
        if (!(board_mask & (1U << bid))) continue;
        if (!save_valid[bid]) continue;

        DC_Uart_Handle *dc = DC_GetHandle(bid);
        if (dc == NULL) continue;

        static uint8_t restore_buf[PKT_MAX_PAYLOAD];
        uint16_t restore_len = 0U;

        /* 6a: HVSG switches → restore to HVSG */
        uint16_t num_hvsg = save_len[bid] / 3U;
        for (uint16_t e = 0U; e < num_hvsg; e++) {
            if (restore_len + 5U > PKT_MAX_PAYLOAD) break;
            restore_buf[restore_len + 0U] = bid;
            restore_buf[restore_len + 1U] = save_buf[bid][e * 3U + 0U];  /* bank */
            restore_buf[restore_len + 2U] = save_buf[bid][e * 3U + 1U];  /* SW_hi */
            restore_buf[restore_len + 3U] = save_buf[bid][e * 3U + 2U];  /* SW_lo */
            restore_buf[restore_len + 4U] = SW_STATE_HVSG;
            restore_len += 5U;
        }

        /* 6b: Non-HVSG measurement switches → restore to original state */
        for (uint16_t e = 0U; e < meas_save_count; e++) {
            if (meas_save[e].bid != bid) continue;
            if (restore_len + 5U > PKT_MAX_PAYLOAD) break;
            restore_buf[restore_len + 0U] = meas_save[e].bid;
            restore_buf[restore_len + 1U] = meas_save[e].bank;
            restore_buf[restore_len + 2U] = (uint8_t)(meas_save[e].sw_num >> 8);
            restore_buf[restore_len + 3U] = (uint8_t)(meas_save[e].sw_num & 0xFFU);
            restore_buf[restore_len + 4U] = meas_save[e].original_state;
            restore_len += 5U;
        }

        if (restore_len > 0U) {
            DC_Uart_SendPacket(dc,
                               measure_adc_request.msg1,
                               measure_adc_request.msg2,
                               0x0BU, 0x51U,  /* SET_LIST_OF_SW */
                               restore_buf, restore_len);
        }
    }
    phase_ms[5] = (uint16_t)(LL_GetTick() - phase_start);

    /* Done with synchronous DC processing */
    dc_list_active = false;

    /* ==================================================================
     *  PHASE 7: Calculate Vpp, elapsed time, and build response
     *
     *  Response (426 bytes):
     *    [s1][s2][Vpp × 10000 as int32 BE 4B][elapsed_ms uint32 BE 4B]
     *    [phase1..6 uint16 BE × 6 = 12B]
     *    [total_ms uint32 BE 4B]
     *    [100 × 4B ADC samples (uint32 BE) = 400B]
     *  Error: Vpp = 0x80 00 00 00 (INT32_MIN sentinel) if ADC restore failed.
     * ================================================================== */
    uint32_t elapsed_ms = LL_GetTick() - t_start;

    int32_t min_val = INT32_MAX;
    int32_t max_val = INT32_MIN;

    for (uint32_t i = 0U; i < ADC_BURST_COUNT; i++) {
        int32_t s = (int32_t)(meas_burst_raw[i] & 0x3FFFFU);
        if (s & 0x20000) {
            s |= (int32_t)0xFFFC0000;
        }
        if (s < min_val) min_val = s;
        if (s > max_val) max_val = s;
    }

    float vpp = (float)(max_val - min_val)
              * (ADC_FULL_SCALE_V / ADC_FULL_SCALE_CODES);

    tx_request.msg1   = measure_adc_request.msg1;
    tx_request.msg2   = measure_adc_request.msg2;
    tx_request.cmd1   = measure_adc_request.cmd1;
    tx_request.cmd2   = measure_adc_request.cmd2;

    tx_request.payload[0] = error_occurred ? STATUS_CAT_ADC : STATUS_CAT_OK;
    tx_request.payload[1] = error_occurred ? STATUS_ADC_RESTORE_FAIL : STATUS_CODE_OK;

    /* Vpp as int32 BE, scaled ×10000 (0.1 mV resolution, matches HVSG setpoint) */
    if (error_occurred) {
        be32_pack(&tx_request.payload[2], 0x80000000U);  /* INT32_MIN sentinel */
    } else {
        be32_pack(&tx_request.payload[2], (uint32_t)(int32_t)(vpp * 10000.0f));
    }

    be32_pack(&tx_request.payload[6], elapsed_ms);

    /* Per-phase timings: 6 × uint16 BE at bytes 10-21 */
    for (uint8_t p = 0U; p < MEASURE_ADC_PHASE_COUNT; p++) {
        be16_pack(&tx_request.payload[10U + p * 2U], phase_ms[p]);
    }

    /* Total time: start to right before shipping data to host */
    uint32_t total_ms = LL_GetTick() - t_start;
    be32_pack(&tx_request.payload[22], total_ms);

    memcpy(&tx_request.payload[26], meas_burst_payload, ADC_BURST_PAYLOAD_SIZE);
    tx_request.length  = 2U + 4U + 4U + MEASURE_ADC_TIMING_SIZE + 4U + ADC_BURST_PAYLOAD_SIZE;  /* 426 bytes */
    tx_request.pending = true;
}

/* ==========================================================================
 *  CMD_SWEEP_ADC — Per-switch ADC measurement sweep
 *
 *  Same save/restore as CMD_MEASURE_ADC, but measures each switch
 *  individually.  For each switch: PWM sync → timer → enable → ADC burst
 *  → Vpp → GND.  Returns array of Vpp values.
 *
 *  Response: [s1][s2][total_ms uint32 BE][Vpp_0..N-1 × 10000 as int32 BE each]
 * ========================================================================== */
void Command_ExecuteSweepADC(void)
{
    uint32_t t_start = LL_GetTick();

    const uint8_t *sw_data    = sweep_adc_request.sw_payload;
    const uint16_t sw_len     = sweep_adc_request.sw_length;
    const uint16_t delay_ms   = sweep_adc_request.delay_ms;
    const uint8_t  board_mask = sweep_adc_request.board_mask;
    const uint8_t  group_size = DC_SET_GROUP_SIZE;  /* 5 */

    uint16_t num_groups = sw_len / group_size;
    if (num_groups == 0U) {
        tx_request.msg1    = sweep_adc_request.msg1;
        tx_request.msg2    = sweep_adc_request.msg2;
        tx_request.cmd1    = sweep_adc_request.cmd1;
        tx_request.cmd2    = sweep_adc_request.cmd2;
        tx_request.payload[0] = STATUS_CAT_GENERAL;
        tx_request.payload[1] = STATUS_PAYLOAD_SHORT;
        tx_request.length  = 2U;
        tx_request.pending = true;
        return;
    }

    /* ---- Static buffers ---- */
    static uint8_t save_buf[DC_MAX_BOARDS][PKT_MAX_PAYLOAD];
    uint16_t save_len[DC_MAX_BOARDS] = {0};
    static SwitchSaveEntry meas_save[MAX_MEAS_SAVE_ENTRIES];
    uint16_t meas_save_count = 0U;
    bool     save_valid[DC_MAX_BOARDS] = {false};
    bool     error_occurred = false;
    static float vpp_results[PKT_MAX_PAYLOAD / DC_SET_GROUP_SIZE]; /* max switches */

    dc_list_active = true;

    /* ==================================================================
     *  PHASE 1: Save HVSG switches (same as CMD_MEASURE_ADC)
     * ================================================================== */
    for (uint8_t bid = 0U; bid < DC_MAX_BOARDS; bid++) {
        if (!(board_mask & (1U << bid))) continue;

        DC_Uart_Handle *dc = DC_GetHandle(bid);
        if (dc == NULL) continue;

        dc_response.ready = false;

        uint8_t get_payload[1] = { bid };
        DC_Uart_SendPacket(dc,
                           sweep_adc_request.msg1,
                           sweep_adc_request.msg2,
                           CMD_GET_HVSG_SW_CMD1, CMD_GET_HVSG_SW_CMD2,
                           get_payload, 1U);

        uint32_t t0 = LL_GetTick();
        while (!dc_response.ready) {
            if ((LL_GetTick() - t0) >= DC_LIST_TIMEOUT) break;
        }

        if (dc_response.ready) {
            if (dc_response.length > 3U) {
                uint16_t data_len = dc_response.length - 3U;
                if (data_len > PKT_MAX_PAYLOAD) data_len = PKT_MAX_PAYLOAD;
                memcpy(save_buf[bid], &dc_response.payload[3], data_len);
                save_len[bid] = data_len;
            }
            save_valid[bid] = true;
            dc_response.ready = false;
        }
    }

    /* Abort if no boards responded */
    {
        bool any_valid = false;
        for (uint8_t bid = 0U; bid < DC_MAX_BOARDS; bid++) {
            if (save_valid[bid]) { any_valid = true; break; }
        }
        if (!any_valid) {
            dc_list_active = false;
            tx_request.msg1    = sweep_adc_request.msg1;
            tx_request.msg2    = sweep_adc_request.msg2;
            tx_request.cmd1    = sweep_adc_request.cmd1;
            tx_request.cmd2    = sweep_adc_request.cmd2;
            tx_request.payload[0] = STATUS_CAT_ADC;
            tx_request.payload[1] = STATUS_ADC_RESTORE_FAIL;
            tx_request.length  = 2U;
            tx_request.pending = true;
            return;
        }
    }

    /* ==================================================================
     *  PHASE 1b: Query measurement switch original states
     * ================================================================== */
    for (uint8_t bid = 0U; bid < DC_MAX_BOARDS; bid++) {
        if (!(board_mask & (1U << bid))) continue;
        if (!save_valid[bid]) continue;

        static uint8_t query_buf[PKT_MAX_PAYLOAD];
        uint16_t query_len = 0U;
        uint16_t query_start = meas_save_count;

        for (uint16_t g = 0U; g < num_groups; g++) {
            const uint8_t *group = &sw_data[g * group_size];
            if (group[0] != bid) continue;

            uint8_t sw_bank = group[1];
            uint8_t sw_hi   = group[2];
            uint8_t sw_lo   = group[3];

            bool found = false;
            uint16_t num_hvsg = save_len[bid] / 3U;
            for (uint16_t e = 0U; e < num_hvsg; e++) {
                if (save_buf[bid][e * 3U + 0U] == sw_bank &&
                    save_buf[bid][e * 3U + 1U] == sw_hi &&
                    save_buf[bid][e * 3U + 2U] == sw_lo) {
                    found = true;
                    break;
                }
            }

            if (!found && query_len + 4U <= PKT_MAX_PAYLOAD
                       && meas_save_count < MAX_MEAS_SAVE_ENTRIES) {
                meas_save[meas_save_count].bid    = bid;
                meas_save[meas_save_count].bank   = sw_bank;
                meas_save[meas_save_count].sw_num = ((uint16_t)sw_hi << 8) | sw_lo;
                meas_save[meas_save_count].original_state = SW_STATE_GND;
                meas_save_count++;

                query_buf[query_len + 0U] = bid;
                query_buf[query_len + 1U] = sw_bank;
                query_buf[query_len + 2U] = sw_hi;
                query_buf[query_len + 3U] = sw_lo;
                query_len += 4U;
            }
        }

        if (query_len > 0U) {
            DC_Uart_Handle *dc = DC_GetHandle(bid);
            if (dc == NULL) continue;

            dc_response.ready = false;
            DC_Uart_SendPacket(dc,
                               sweep_adc_request.msg1,
                               sweep_adc_request.msg2,
                               0x0BU, 0x52U,
                               query_buf, query_len);

            uint32_t t0 = LL_GetTick();
            while (!dc_response.ready) {
                if ((LL_GetTick() - t0) >= DC_LIST_TIMEOUT) break;
            }

            if (!dc_response.ready) {
                dc_list_active = false;
                tx_request.msg1    = sweep_adc_request.msg1;
                tx_request.msg2    = sweep_adc_request.msg2;
                tx_request.cmd1    = sweep_adc_request.cmd1;
                tx_request.cmd2    = sweep_adc_request.cmd2;
                tx_request.payload[0] = STATUS_CAT_ADC;
                tx_request.payload[1] = STATUS_ADC_RESTORE_FAIL;
                tx_request.length  = 2U;
                tx_request.pending = true;
                return;
            }

            if (dc_response.length > 2U) {
                uint16_t data_len = dc_response.length - 2U;
                uint16_t num_resp = data_len / 5U;
                uint16_t num_queried = meas_save_count - query_start;
                uint16_t n = (num_resp < num_queried) ? num_resp : num_queried;
                for (uint16_t e = 0U; e < n; e++) {
                    uint16_t off = 2U + e * 5U;
                    meas_save[query_start + e].original_state = dc_response.payload[off + 4U];
                }
            }
            dc_response.ready = false;
        }
    }

    /* ==================================================================
     *  PHASE 2: GND all saved HVSG switches
     * ================================================================== */
    for (uint8_t bid = 0U; bid < DC_MAX_BOARDS; bid++) {
        if (!(board_mask & (1U << bid))) continue;
        if (!save_valid[bid]) continue;
        if (save_len[bid] == 0U) continue;

        DC_Uart_Handle *dc = DC_GetHandle(bid);
        if (dc == NULL) continue;

        static uint8_t gnd_buf[PKT_MAX_PAYLOAD];
        uint16_t gnd_len = 0U;
        uint16_t num_entries = save_len[bid] / 3U;

        for (uint16_t e = 0U; e < num_entries; e++) {
            if (gnd_len + 5U > PKT_MAX_PAYLOAD) break;
            gnd_buf[gnd_len + 0U] = bid;
            gnd_buf[gnd_len + 1U] = save_buf[bid][e * 3U + 0U];
            gnd_buf[gnd_len + 2U] = save_buf[bid][e * 3U + 1U];
            gnd_buf[gnd_len + 3U] = save_buf[bid][e * 3U + 2U];
            gnd_buf[gnd_len + 4U] = SW_STATE_GND;
            gnd_len += 5U;
        }

        if (gnd_len > 0U) {
            dc_response.ready = false;
            DC_Uart_SendPacket(dc,
                               sweep_adc_request.msg1,
                               sweep_adc_request.msg2,
                               0x0BU, 0x51U,
                               gnd_buf, gnd_len);

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
    }

    /* ==================================================================
     *  PER-SWITCH MEASUREMENT LOOP
     *
     *  For each switch:
     *    1. PWM sync (GPIO pulse)
     *    2. Start timer (1 switch × SWITCH_ENABLE_TIME_US + delay)
     *    3. Enable this switch (fire-and-forget)
     *    4. Timer expires → burst ADC → Vpp
     *    5. Drain SET_LIST_OF_SW response
     *    6. GND this switch (wait for response)
     * ================================================================== */
    /* volatile so the debugger-inspection writes aren't optimized out and
     * GCC doesn't raise -Wunused-but-set-variable. */
    static volatile uint32_t burst_raw[ADC_BURST_COUNT];
    (void)burst_raw;

    for (uint16_t g = 0U; g < num_groups; g++) {
        const uint8_t *group = &sw_data[g * group_size];
        uint8_t bid = group[0];

        if (!(board_mask & (1U << bid))) { vpp_results[g] = 0.0f; continue; }
        if (!save_valid[bid])            { vpp_results[g] = 0.0f; continue; }

        DC_Uart_Handle *dc = DC_GetHandle(bid);
        if (dc == NULL) { vpp_results[g] = 0.0f; continue; }

        /* 1. PWM sync */
        PWM_SyncPulse();

        /* 2. Start timer */
        uint32_t total_us = SWITCH_ENABLE_TIME_US + ((uint32_t)delay_ms * 1000U);
        uint32_t arr_val  = (total_us * 10U) - 1U;  /* 100 ns ticks */

        LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);
        LL_TIM_SetPrescaler(TIM2, 23U);
        LL_TIM_SetAutoReload(TIM2, arr_val);
        LL_TIM_SetOnePulseMode(TIM2, LL_TIM_ONEPULSEMODE_SINGLE);
        LL_TIM_GenerateEvent_UPDATE(TIM2);
        LL_TIM_ClearFlag_UPDATE(TIM2);
        LL_TIM_EnableCounter(TIM2);

        /* 3. Enable this one switch (fire-and-forget) */
        uint8_t sw_cmd[DC_SET_GROUP_SIZE];
        memcpy(sw_cmd, group, DC_SET_GROUP_SIZE);
        DC_Uart_SendPacket(dc,
                           sweep_adc_request.msg1,
                           sweep_adc_request.msg2,
                           0x0BU, 0x51U,
                           sw_cmd, DC_SET_GROUP_SIZE);

        /* 4. Timer expires → burst ADC → Vpp */
        while (!LL_TIM_IsActiveFlag_UPDATE(TIM2)) { }

        LL_TIM_ClearFlag_UPDATE(TIM2);
        LL_TIM_DisableCounter(TIM2);
        LL_APB1_GRP1_DisableClock(LL_APB1_GRP1_PERIPH_TIM2);

        int32_t min_val = INT32_MAX;
        int32_t max_val = INT32_MIN;

        for (uint32_t i = 0U; i < ADC_BURST_COUNT; i++) {
            uint32_t sample = 0U;
            SPI_LTC2338_Read(&spi2_handle, &sample);
            burst_raw[i] = sample;

            int32_t s = (int32_t)(sample & 0x3FFFFU);
            if (s & 0x20000) s |= (int32_t)0xFFFC0000;
            if (s < min_val) min_val = s;
            if (s > max_val) max_val = s;
        }

        vpp_results[g] = (float)(max_val - min_val)
                       * (ADC_FULL_SCALE_V / ADC_FULL_SCALE_CODES);

        /* 5. Drain SET_LIST_OF_SW response */
        {
            uint32_t t0 = LL_GetTick();
            while (!dc_response.ready) {
                if ((LL_GetTick() - t0) >= DC_LIST_TIMEOUT) break;
            }
            dc_response.ready = false;
        }

        /* 6. GND this switch (wait for response) */
        {
            uint8_t gnd_cmd[DC_SET_GROUP_SIZE];
            memcpy(gnd_cmd, group, DC_SET_GROUP_SIZE);
            gnd_cmd[4] = SW_STATE_GND;

            dc_response.ready = false;
            DC_Uart_SendPacket(dc,
                               sweep_adc_request.msg1,
                               sweep_adc_request.msg2,
                               0x0BU, 0x51U,
                               gnd_cmd, DC_SET_GROUP_SIZE);

            uint32_t t0 = LL_GetTick();
            while (!dc_response.ready) {
                if ((LL_GetTick() - t0) >= DC_LIST_TIMEOUT) break;
            }
            dc_response.ready = false;
        }
    }

    /* ==================================================================
     *  RESTORE: HVSG switches + non-HVSG measurement switches
     * ================================================================== */
    for (uint8_t bid = 0U; bid < DC_MAX_BOARDS; bid++) {
        if (!(board_mask & (1U << bid))) continue;
        if (!save_valid[bid]) continue;

        DC_Uart_Handle *dc = DC_GetHandle(bid);
        if (dc == NULL) continue;

        static uint8_t restore_buf[PKT_MAX_PAYLOAD];
        uint16_t restore_len = 0U;

        uint16_t num_hvsg = save_len[bid] / 3U;
        for (uint16_t e = 0U; e < num_hvsg; e++) {
            if (restore_len + 5U > PKT_MAX_PAYLOAD) break;
            restore_buf[restore_len + 0U] = bid;
            restore_buf[restore_len + 1U] = save_buf[bid][e * 3U + 0U];
            restore_buf[restore_len + 2U] = save_buf[bid][e * 3U + 1U];
            restore_buf[restore_len + 3U] = save_buf[bid][e * 3U + 2U];
            restore_buf[restore_len + 4U] = SW_STATE_HVSG;
            restore_len += 5U;
        }

        for (uint16_t e = 0U; e < meas_save_count; e++) {
            if (meas_save[e].bid != bid) continue;
            if (restore_len + 5U > PKT_MAX_PAYLOAD) break;
            restore_buf[restore_len + 0U] = meas_save[e].bid;
            restore_buf[restore_len + 1U] = meas_save[e].bank;
            restore_buf[restore_len + 2U] = (uint8_t)(meas_save[e].sw_num >> 8);
            restore_buf[restore_len + 3U] = (uint8_t)(meas_save[e].sw_num & 0xFFU);
            restore_buf[restore_len + 4U] = meas_save[e].original_state;
            restore_len += 5U;
        }

        if (restore_len > 0U) {
            DC_Uart_SendPacket(dc,
                               sweep_adc_request.msg1,
                               sweep_adc_request.msg2,
                               0x0BU, 0x51U,
                               restore_buf, restore_len);
        }
    }

    dc_list_active = false;

    /* ==================================================================
     *  BUILD RESPONSE: [s1][s2][total_ms uint32 BE][N × Vpp × 10000 int32 BE]
     * ================================================================== */
    uint32_t total_ms = LL_GetTick() - t_start;

    tx_request.msg1 = sweep_adc_request.msg1;
    tx_request.msg2 = sweep_adc_request.msg2;
    tx_request.cmd1 = sweep_adc_request.cmd1;
    tx_request.cmd2 = sweep_adc_request.cmd2;

    tx_request.payload[0] = error_occurred ? STATUS_CAT_ADC : STATUS_CAT_OK;
    tx_request.payload[1] = error_occurred ? STATUS_ADC_RESTORE_FAIL : STATUS_CODE_OK;
    be32_pack(&tx_request.payload[2], total_ms);

    for (uint16_t g = 0U; g < num_groups; g++) {
        be32_pack(&tx_request.payload[6U + g * 4U],
                  (uint32_t)(int32_t)(vpp_results[g] * 10000.0f));
    }

    tx_request.length  = 2U + 4U + (num_groups * 4U);
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
