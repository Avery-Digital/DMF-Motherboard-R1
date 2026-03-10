/*******************************************************************************
 * @file    Src/bsp.c
 * @author  Cam
 * @brief   Board Support Package — Hardware Configuration Data
 *
 *          All const configuration structs live here.  This is the ONLY file
 *          that needs to change if the same firmware is ported to a different
 *          PCB with the same MCU.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "bsp.h"
#include <stddef.h>

/* ==========================================================================
 *  DMA BUFFERS — D2 SRAM PLACEMENT
 *
 *  STM32H7 DMA1/DMA2 CANNOT access DTCM (0x20000000).  Buffers must be
 *  in D1 AXI-SRAM or D2 SRAM.  The .dma_buffer section is mapped to
 *  RAM_D2 (0x30000000) in the linker script.
 *
 *  32-byte alignment is required for cache maintenance operations if
 *  D-cache is enabled.  Alternatively, mark this MPU region non-cacheable.
 * ========================================================================== */

#define USART10_TX_BUF_SIZE     8512U   /* Worst-case: (4096+6+2)×2 + SOF + EOF + margin */
#define USART10_RX_BUF_SIZE     4096U   /* ~350 ms of data at 115200 baud               */

__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t usart10_tx_dma_buf[USART10_TX_BUF_SIZE];

__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t usart10_rx_dma_buf[USART10_RX_BUF_SIZE];

/* ==========================================================================
 *  CLOCK TREE CONFIGURATION
 *
 *  HSE          = 12 MHz  (crystal on PH0-OSC_IN)
 *  PLL1 VCO in  = 12 / 3       =   4 MHz   (range 2–4 MHz)
 *  PLL1 VCO out =  4 × 120     = 480 MHz   (wide range)
 *  PLL1P        = 480 / 1      = 480 MHz → SYSCLK
 *  PLL1Q        = 480 / 2      = 240 MHz
 *  PLL1R        = 480 / 2      = 240 MHz
 *
 *  D1CPRE  /1  → 480 MHz  CPU
 *  HPRE    /2  → 240 MHz  AHB
 *  D1PPRE  /2  → 120 MHz  APB3
 *  D2PPRE1 /2  → 120 MHz  APB1
 *  D2PPRE2 /2  → 120 MHz  APB2
 *  D3PPRE  /2  → 120 MHz  APB4
 *
 *  PLL2: 12 / 3 = 4 MHz in, × 64 = 256 MHz VCO
 *        P /2 = 128 MHz   Q /2 = 128 MHz   R /2 = 128 MHz
 *        → PLL2Q feeds USART kernel clock (128 MHz)
 *
 *  PLL3: 12 / 3 = 4 MHz in, × 64 = 256 MHz VCO
 *        P /2 = 128 MHz   Q /2 = 128 MHz   R /2 = 128 MHz
 *        → PLL3Q for SPI, PLL3R for I2C (future use)
 * ========================================================================== */
const ClockTree_Config sys_clk_config = {

    .hse_freq_hz        = 12000000UL,

    .voltage_scale      = LL_PWR_REGU_VOLTAGE_SCALE0,
    .flash_latency      = LL_FLASH_LATENCY_4,       /* 4 WS for 480 MHz VOS0 */

    /* ---- PLL1: System clock ---- */
    .pll1 = {
        .divm               = 3U,
        .divn               = 120U,
        .divp               = 1U,       /* 480 MHz → SYSCLK              */
        .divq               = 2U,       /* 240 MHz                        */
        .divr               = 2U,       /* 240 MHz                        */
        .vco_input_range    = LL_RCC_PLLINPUTRANGE_2_4,
        .vco_output_range   = LL_RCC_PLLVCORANGE_WIDE,
        .enable_p           = true,
        .enable_q           = true,
        .enable_r           = false,
    },

    /* ---- PLL2: Peripheral clocks (USART, ADC, DAC) ---- */
    .pll2 = {
        .divm               = 3U,
        .divn               = 64U,
        .divp               = 2U,       /* 128 MHz (ADC, DAC)             */
        .divq               = 2U,       /* 128 MHz (USART kernel clock)   */
        .divr               = 2U,       /* 128 MHz                        */
        .vco_input_range    = LL_RCC_PLLINPUTRANGE_2_4,
        .vco_output_range   = LL_RCC_PLLVCORANGE_WIDE,
        .enable_p           = true,
        .enable_q           = true,
        .enable_r           = false,
    },

    /* ---- PLL3: Peripheral clocks (SPI, I2C) ---- */
    .pll3 = {
        .divm               = 3U,
        .divn               = 64U,
        .divp               = 2U,       /* 128 MHz                        */
        .divq               = 2U,       /* 128 MHz (SPI kernel clock)     */
        .divr               = 2U,       /* 128 MHz (I2C kernel clock)     */
        .vco_input_range    = LL_RCC_PLLINPUTRANGE_2_4,
        .vco_output_range   = LL_RCC_PLLVCORANGE_WIDE,
        .enable_p           = false,
        .enable_q           = true,
        .enable_r           = true,
    },

    /* ---- Bus prescalers ---- */
    .prescalers = {
        .d1cpre     = LL_RCC_SYSCLK_DIV_1,      /* 480 MHz CPU            */
        .hpre       = LL_RCC_AHB_DIV_2,          /* 240 MHz AHB           */
        .d1ppre     = LL_RCC_APB3_DIV_2,          /* 120 MHz APB3          */
        .d2ppre1    = LL_RCC_APB1_DIV_2,          /* 120 MHz APB1          */
        .d2ppre2    = LL_RCC_APB2_DIV_2,          /* 120 MHz APB2          */
        .d3ppre     = LL_RCC_APB4_DIV_2,          /* 120 MHz APB4          */
    },

    /* ---- Derived (used by LL_Init1msTick, etc.) ---- */
    .sysclk_hz  = 480000000UL,
    .ahb_hz     = 240000000UL,
    .apb1_hz    = 120000000UL,
    .apb2_hz    = 120000000UL,
};

/* ==========================================================================
 *  USART10 CONFIGURATION  —  PG12 (TX, Pin 156)  /  PG11 (RX, Pin 155)
 *
 *  USART10 is on APB2.
 *  Kernel clock source: PLL2Q = 128 MHz.
 *  At 128 MHz, baud = 115200 → BRR ≈ 1111 → 0.01% error.
 *  AF11 for both pins per STM32H735 datasheet Table 10.
 * ========================================================================== */
const USART_Config usart10_cfg = {

    /* TX — PG12, Pin 156 */
    .tx_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOG,
        .port       = GPIOG,
        .pin        = LL_GPIO_PIN_12,
        .mode       = LL_GPIO_MODE_ALTERNATE,
        .af         = LL_GPIO_AF_11,
        .speed      = LL_GPIO_SPEED_FREQ_VERY_HIGH,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* RX — PG11, Pin 155 */
    .rx_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOG,
        .port       = GPIOG,
        .pin        = LL_GPIO_PIN_11,
        .mode       = LL_GPIO_MODE_ALTERNATE,
        .af         = LL_GPIO_AF_11,
        .speed      = LL_GPIO_SPEED_FREQ_VERY_HIGH,
        .pull       = LL_GPIO_PULL_UP,      /* Pull-up on RX to idle high   */
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* USART peripheral */
    .peripheral         = USART10,
    .bus_clk_enable     = LL_APB2_GRP1_PERIPH_USART10,
    .kernel_clk_source  = LL_RCC_USART16_CLKSOURCE_PLL2Q,
    .prescaler          = LL_USART_PRESCALER_DIV1,
    .baudrate           = 115200U,
    .data_width         = LL_USART_DATAWIDTH_8B,
    .stop_bits          = LL_USART_STOPBITS_1,
    .parity             = LL_USART_PARITY_NONE,
    .direction          = LL_USART_DIRECTION_TX_RX,
    .hw_flow_control    = LL_USART_HWCONTROL_NONE,
    .oversampling       = LL_USART_OVERSAMPLING_16,

    /* NVIC */
    .irqn               = USART10_IRQn,
    .irq_priority       = 5U,
};

/* ==========================================================================
 *  DMA CONFIGURATION FOR USART10
 *
 *  TX: DMA1 Stream 0, DMAMUX request = USART10_TX
 *      Normal mode — fire once per packet, then stop.
 *
 *  RX: DMA1 Stream 1, DMAMUX request = USART10_RX
 *      Circular mode — continuously fills the ring buffer.
 *      The idle-line interrupt (USART IDLE flag) detects end-of-packet.
 *
 *  NOTE: Verify LL_DMAMUX1_REQ_USART10_TX and _RX request IDs against
 *  RM0468 Table 121 for your firmware package version.
 * ========================================================================== */
const DMA_ChannelConfig usart10_dma_tx_cfg = {
    .dma_clk_enable     = LL_AHB1_GRP1_PERIPH_DMA1,
    .dma                = DMA1,
    .stream             = LL_DMA_STREAM_0,
    .request            = LL_DMAMUX1_REQ_USART10_TX,
    .direction          = LL_DMA_DIRECTION_MEMORY_TO_PERIPH,
    .mode               = LL_DMA_MODE_NORMAL,
    .priority           = LL_DMA_PRIORITY_MEDIUM,
    .periph_data_align  = LL_DMA_PDATAALIGN_BYTE,
    .mem_data_align     = LL_DMA_MDATAALIGN_BYTE,
    .periph_inc         = LL_DMA_PERIPH_NOINCREMENT,
    .mem_inc            = LL_DMA_MEMORY_INCREMENT,
    .use_fifo           = false,
    .fifo_threshold     = 0U,
    .irqn               = DMA1_Stream0_IRQn,
    .irq_priority       = 6U,
};

const DMA_ChannelConfig usart10_dma_rx_cfg = {
    .dma_clk_enable     = LL_AHB1_GRP1_PERIPH_DMA1,
    .dma                = DMA1,
    .stream             = LL_DMA_STREAM_1,
    .request            = LL_DMAMUX1_REQ_USART10_RX,
    .direction          = LL_DMA_DIRECTION_PERIPH_TO_MEMORY,
    .mode               = LL_DMA_MODE_CIRCULAR,
    .priority           = LL_DMA_PRIORITY_HIGH,
    .periph_data_align  = LL_DMA_PDATAALIGN_BYTE,
    .mem_data_align     = LL_DMA_MDATAALIGN_BYTE,
    .periph_inc         = LL_DMA_PERIPH_NOINCREMENT,
    .mem_inc            = LL_DMA_MEMORY_INCREMENT,
    .use_fifo           = false,
    .fifo_threshold     = 0U,
    .irqn               = DMA1_Stream1_IRQn,
    .irq_priority       = 4U,
};

/* ==========================================================================
 *  USART10 RUNTIME HANDLE
 *
 *  This is the only mutable object — everything it points to is const.
 *  DMA buffers are assigned here at compile time.
 * ========================================================================== */
USART_Handle usart10_handle = {
    .cfg            = &usart10_cfg,
    .dma_tx         = &usart10_dma_tx_cfg,
    .dma_rx         = &usart10_dma_rx_cfg,
    .tx_buf         = usart10_tx_dma_buf,
    .tx_buf_size    = USART10_TX_BUF_SIZE,
    .rx_buf         = usart10_rx_dma_buf,
    .rx_buf_size    = USART10_RX_BUF_SIZE,
    .parser         = NULL,     /* Assigned at runtime in main.c */
    .tx_len         = 0U,
    .tx_busy        = false,
    .rx_head        = 0U,
    .crc_accumulator = 0U,
};

/* ==========================================================================
 *  I2C1 CONFIGURATION  —  PB8 (SCL, Pin 168)  /  PB7 (SDA, Pin 166)
 *
 *  I2C1 is on APB1.
 *  Kernel clock source: PLL3R = 128 MHz.
 *  Timing register value calculated for 400 kHz (Fast Mode) with
 *  128 MHz kernel clock, analog filter enabled, digital filter off.
 *
 *  Use STM32CubeMX I2C timing calculator to regenerate if you change
 *  the kernel clock frequency.
 *
 *  AF4 for both pins per STM32H735 datasheet.
 *  I2C pins MUST be open-drain — this is required by the I2C spec.
 * ========================================================================== */
const I2C_Config i2c1_cfg = {

    /* SCL — PB8, Pin 168 */
    .scl_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOB,
        .port       = GPIOB,
        .pin        = LL_GPIO_PIN_8,
        .mode       = LL_GPIO_MODE_ALTERNATE,
        .af         = LL_GPIO_AF_4,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_UP,
        .output     = LL_GPIO_OUTPUT_OPENDRAIN,
    },

    /* SDA — PB7, Pin 166 */
    .sda_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOB,
        .port       = GPIOB,
        .pin        = LL_GPIO_PIN_7,
        .mode       = LL_GPIO_MODE_ALTERNATE,
        .af         = LL_GPIO_AF_4,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_UP,
        .output     = LL_GPIO_OUTPUT_OPENDRAIN,
    },

    /* I2C peripheral */
    .peripheral         = I2C1,
    .bus_clk_enable     = LL_APB1_GRP1_PERIPH_I2C1,
    .kernel_clk_source  = LL_RCC_I2C123_CLKSOURCE_PLL3R,

    /* Timing: 400 kHz Fast Mode @ 128 MHz kernel clock
     * Generated via STM32CubeMX I2C timing calculator.
     * PRESC=0x3, SCLDEL=0x4, SDADEL=0x1, SCLH=0x0F, SCLL=0x13 */
    .timing             = 0x30410F13,
    .analog_filter      = LL_I2C_ANALOGFILTER_ENABLE,
    .digital_filter     = 0x00,
    .own_address        = 0x00,     /* Master mode — no own address */
    .addressing_mode    = LL_I2C_ADDRESSING_MODE_7BIT,
};

/* I2C1 runtime handle */
I2C_Handle i2c1_handle = {
    .cfg    = &i2c1_cfg,
    .busy   = false,
    .error  = 0U,
};

/* ==========================================================================
 *  GPIO PIN INITIALIZATION
 *
 *  Generic single-pin init from a PinConfig struct.
 *  Used by all peripheral drivers — lives here in BSP because it's a
 *  shared utility tied to the PinConfig type.
 * ========================================================================== */
void Pin_Init(const PinConfig *pin)
{
    /* Enable GPIO port clock */
    LL_AHB4_GRP1_EnableClock(pin->clk);

    /* Mode */
    LL_GPIO_SetPinMode(pin->port, pin->pin, pin->mode);

    /* Alternate function (only meaningful in AF mode, but safe to set) */
    if (pin->pin <= LL_GPIO_PIN_7) {
        LL_GPIO_SetAFPin_0_7(pin->port, pin->pin, pin->af);
    } else {
        LL_GPIO_SetAFPin_8_15(pin->port, pin->pin, pin->af);
    }

    /* Speed, pull, output type */
    LL_GPIO_SetPinSpeed(pin->port, pin->pin, pin->speed);
    LL_GPIO_SetPinPull(pin->port, pin->pin, pin->pull);
    LL_GPIO_SetPinOutputType(pin->port, pin->pin, pin->output);
}
