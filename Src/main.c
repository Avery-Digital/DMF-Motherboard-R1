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
 *   3. Protocol_ParserInit() — Frame parser state machine
 *   4. USART_Driver_Init()   — GPIO, USART10, DMA streams
 *   5. USART_Driver_StartRx()— Enable circular DMA reception
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

    /* Main loop */
    while (1)
    {
        /* Poll DMA for new bytes → feeds into protocol parser.
         * When a complete, CRC-valid packet arrives, OnPacketReceived()
         * is called automatically by the parser. */
        USART_Driver_PollRx(&usart10_handle, &usart10_parser);
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

    /* Step 3: I2C1 on PB7 (SDA) / PB8 (SCL) */
    result = I2C_Driver_Init(&i2c1_handle);
    if (result != INIT_OK) {
        Error_Handler();
    }

    /* Step 4: USB2517I hub — write config registers and attach to USB host.
     *         Must complete before the FT231 COM port will enumerate. */
    result = USB2517_Init(&i2c1_handle);
    if (result != INIT_OK) {
        Error_Handler();
    }

    /* Step 5: Protocol parser — register the packet callback */
    Protocol_ParserInit(&usart10_parser, OnPacketReceived, NULL);

    /* Step 6: USART10 + DMA on PG11 (RX) / PG12 (TX) */
    result = USART_Driver_Init(&usart10_handle);
    if (result != INIT_OK) {
        Error_Handler();
    }

    /* Step 7: Start circular DMA reception */
    USART_Driver_StartRx(&usart10_handle);
}

/* ==========================================================================
 *  PACKET RECEIVED CALLBACK
 *
 *  Called by the protocol parser when a complete, CRC-validated packet
 *  arrives.  This is where you dispatch based on msg1/msg2 to your
 *  application handlers.
 *
 *  NOTE: This is called from the main loop context (inside PollRx),
 *  not from an interrupt, so it's safe to do substantial processing here.
 * ========================================================================== */
static void OnPacketReceived(const PacketHeader *header,
                             const uint8_t *payload,
                             void *ctx)
{
    (void)ctx;  /* Unused for now */

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
