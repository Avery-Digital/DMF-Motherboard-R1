/*******************************************************************************
 * @file    Src/main.c
 * @author  Cam
 * @brief   Main Entry Point
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 *
 * Initialization sequence:
 *   1. MCU_Init()            — MPU, SYSCFG, NVIC, flash latency, power
 *   2. ClockTree_Init()      — HSE, PLL1/2/3, bus prescalers
 *   3. USB2517_SetStrapPins()— Assert CFG_SEL pins before hub exits reset
 *   4. I2C_Driver_Init()     — I2C1 on PB7/PB8, 400 kHz
 *   5. SPI_Init()            — SPI2 for LTC2338-18 ADC
 *   6. DRV8702_Init() x3    — TEC H-bridge drivers (GPIO + SPI2 shared bus)
 *   7. Protocol_ParserInit() — Frame parser state machine
 *   8. USART_Driver_Init()   — GPIO, USART10, DMA streams (interrupt-driven)
 *   9. USART_Driver_StartRx()— Enable DMA HT/TC + USART IDLE interrupts
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
#include <string.h>
#include "stm32h7xx_ll_rtc.h"
#include "spi_driver.h"
/* Protocol parser instance */
static ProtocolParser usart10_parser;

/* Deferred TX request — set by ISR, consumed by main loop */
TxRequest    tx_request    = {0};

/* Deferred burst ADC request — set by ISR, consumed by main loop */
BurstRequest burst_request = {0};

/* Private function prototypes -----------------------------------------------*/
static void SystemInit_Sequence(void);
static void OnPacketReceived(const PacketHeader *header,
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

    /* Step 3: Assert USB2517 strapping pins ASAP.
     *         CFG_SEL1 and CFG_SEL2 must be low before the hub exits
     *         power-on reset so it enters SMBus configuration mode. */
    USB2517_SetStrapPins();

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

    /* Step 7: USB2517I hub — write config registers and attach to USB host.
     *         Must complete before the FT231 COM port will enumerate. */
//    result = USB2517_Init(&i2c1_handle);
//    if (result != INIT_OK) {
//        Error_Handler();
//    }

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
