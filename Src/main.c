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
 *   3. I2C_Driver_Init()     — I2C1 on PB7/PB8, 400 kHz
 *   4. USB2517_Init()        — Configure USB hub, send USB_ATTACH
 *   5. Protocol_ParserInit() — Frame parser state machine
 *   6. USART_Driver_Init()   — GPIO, USART10, DMA streams (interrupt-driven)
 *   7. USART_Driver_StartRx()— Enable DMA HT/TC + USART IDLE interrupts
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

/* Protocol parser instance */
static ProtocolParser usart10_parser;

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
        /* TODO: Watchdog refresh */
        /* TODO: Status LED update */
        /* TODO: ADC polling */
        /* TODO: Low-power sleep (WFI) if desired */
    }
}

/* ==========================================================================
 *  SYSTEM INITIALIZATION
 * ========================================================================== */
static void SystemInit_Sequence(void)
{
    InitResult result;

    /* Step 1: MCU core — MPU, flash latency, voltage scaling */
    MCU_Init();

    /* Step 2: Full clock tree — HSE → PLL1 (480 MHz SYSCLK),
     *         PLL2 (128 MHz USART kernel), PLL3 (128 MHz SPI/I2C) */
    ClockTree_Init(&sys_clk_config);

    /* Step 3: Assert USB2517 strapping pins ASAP.
     *         CFG_SEL1 and CFG_SEL2 must be low before the hub exits
     *         power-on reset so it enters SMBus configuration mode. */
    //USB2517_SetStrapPins();

    /* Step 4: I2C1 on PB7 (SDA) / PB8 (SCL) */
    result = I2C_Driver_Init(&i2c1_handle);
    if (result != INIT_OK) {
        Error_Handler();
    }

    /* Step 5: USB2517I hub — write config registers and attach to USB host.
     *         Must complete before the FT231 COM port will enumerate. */
//    result = USB2517_Init(&i2c1_handle);
//    if (result != INIT_OK) {
//        Error_Handler();
//    }

    /* Step 5: Protocol parser — register the packet callback */
    Protocol_ParserInit(&usart10_parser, OnPacketReceived, NULL);

    /* Step 6: USART10 + DMA on PG11 (RX) / PG12 (TX)
     *         Parser is passed to the driver so ISRs can feed it directly */
    result = USART_Driver_Init(&usart10_handle, &usart10_parser);
    if (result != INIT_OK) {
        Error_Handler();
    }

    /* Step 7: Start interrupt-driven DMA reception */
    USART_Driver_StartRx(&usart10_handle);
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
void Error_Handler(void)
{
    __disable_irq();
    while (1) {
        /* Halt */
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    while (1);
}
#endif
