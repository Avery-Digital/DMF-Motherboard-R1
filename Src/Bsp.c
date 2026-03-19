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
	.pll2q_hz   = 128000000UL,
	.pll3q_hz   = 128000000UL,
	.pll3r_hz   = 128000000UL,
};

/* ==========================================================================
 *  USART10 CONFIGURATION  —  PG12 (TX, Pin 156)  /  PG11 (RX, Pin 155)
 *
 *  USART10 is on APB2.
 *  Kernel clock source: PLL2Q = 128 MHz.
 *  At 128 MHz, baud = 115200 → BRR ≈ 1111 → 0.01% error.
 *  AF4 for both pins per STM32H735 datasheet Table 10.
 * ========================================================================== */
const USART_Config usart10_cfg = {

    /* TX — PG12, Pin 156 */
    .tx_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOG,
        .port       = GPIOG,
        .pin        = LL_GPIO_PIN_12,
        .mode       = LL_GPIO_MODE_ALTERNATE,
        .af         = LL_GPIO_AF_4,
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
        .af         = LL_GPIO_AF_4,
        .speed      = LL_GPIO_SPEED_FREQ_VERY_HIGH,
        .pull       = LL_GPIO_PULL_UP,      /* Pull-up on RX to idle high   */
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* USART peripheral */
    .peripheral         = USART10,
    .bus_clk_enable     = LL_APB2_GRP1_PERIPH_USART10,
    .kernel_clk_source  = LL_RCC_USART16910_CLKSOURCE_PLL2Q,
    .prescaler          = LL_USART_PRESCALER_DIV1,
    .baudrate           = 115200U,
    .data_width         = LL_USART_DATAWIDTH_8B,
    .stop_bits          = LL_USART_STOPBITS_1,
    .parity             = LL_USART_PARITY_NONE,
    .direction          = LL_USART_DIRECTION_TX_RX,
    .hw_flow_control    = LL_USART_HWCONTROL_NONE,
    .oversampling       = LL_USART_OVERSAMPLING_16,

	.kernel_clk_hz      = 128000000UL,     /* PLL2Q */



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
 *  SPI2 CONFIGURATION  —  LTC2338-18 18-bit ADC
 *
 *  MISO : PC2_C, Pin 36    AF5
 *  MOSI : PC3_C, Pin 37    AF5  — unused by LTC2338-18 in normal mode
 *  SCK  : PA9,   Pin 128   AF5
 *  CNV  : PE12,  Pin 74    GPIO output — conversion trigger (active HIGH pulse)
 *  BUSY : PE15,  Pin 77    GPIO input  — open-drain, LOW when conversion complete
 *
 *  SPI2 is on APB1.  Kernel clock source: PLL3Q = 128 MHz.
 *  Mode 0 (CPOL=0, CPHA=0): clock idles LOW, data sampled on rising edge.
 *  Baud: PLL3Q / DIV128 = 128 MHz / 128 = 1.0 MHz SCK.
 *
 *  NOTE: PC2_C / PC3_C are the analog-side "C" variants of those pins on
 *  the H735IGT6.  Confirm AF5 is reachable on those balls in your package.
 * ========================================================================== */
const SPI_Config spi2_cfg = {

    /* MISO — PC2_C, Pin 36 */
    .miso_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOC,
        .port       = GPIOC,
        .pin        = LL_GPIO_PIN_2,
        .mode       = LL_GPIO_MODE_ALTERNATE,
        .af         = LL_GPIO_AF_5,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* MOSI — PC3_C, Pin 37 (not driven by LTC2338-18 in normal mode) */
    .mosi_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOC,
        .port       = GPIOC,
        .pin        = LL_GPIO_PIN_3,
        .mode       = LL_GPIO_MODE_ALTERNATE,
        .af         = LL_GPIO_AF_5,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* SCK — PA9, Pin 128 */
    .sck_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOA,
        .port       = GPIOA,
        .pin        = LL_GPIO_PIN_9,
        .mode       = LL_GPIO_MODE_ALTERNATE,
        .af         = LL_GPIO_AF_5,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* CNV — PE12, Pin 74 */
    .cnv_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOE,
        .port       = GPIOE,
        .pin        = LL_GPIO_PIN_12,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,  /* Fast edge needed for t_CNVH */
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* BUSY — PE15, Pin 77 */
    .busy_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOE,
        .port       = GPIOE,
        .pin        = LL_GPIO_PIN_15,
        .mode       = LL_GPIO_MODE_INPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_LOW,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,  /* N/A for input, set to default            */
    },

    /* SPI2 peripheral */
    .peripheral         = SPI2,
    .bus_clk_enable     = LL_APB1_GRP1_PERIPH_SPI2,

    /* Mode 0: CPOL=0 (clock idles LOW), CPHA=0 (sample on rising edge) */
    .clk_polarity       = LL_SPI_POLARITY_LOW,
    .clk_phase          = LL_SPI_PHASE_1EDGE,
    .bit_order          = LL_SPI_MSB_FIRST,
    .data_width         = LL_SPI_DATAWIDTH_32BIT,
    .nss_mode           = LL_SPI_NSS_SOFT,

	/* 128 MHz / 8 = 16 MHz SCK */
	.baud_prescaler     = LL_SPI_BAUDRATEPRESCALER_DIV8,

    /* Timeouts — LTC2338-18 converts in ~1 µs; 5 ms is very conservative */
    .busy_timeout_ms    = 5U,
    .xfer_timeout_ms    = 5U,
};

/* SPI2 runtime handle */
SPI_Handle spi2_handle = {
    .cfg         = &spi2_cfg,
    .initialised = false,
    .busy        = false,
    .error       = 0U,
};

/* ==========================================================================
 *  DAUGHTERCARD UART INTERFACES
 *
 *  4 UARTs for communicating with stacked daughtercard (driver board) PCBs.
 *  Polled TX + DMA circular RX.  All at 115200 baud, 8N1.
 *
 *  DC1: USART1, PB14 TX (AF4), PB15 RX (AF4), DMA1 Stream 2 RX
 *  DC2: USART2, PA2  TX (AF7), PA3  RX (AF7), DMA1 Stream 3 RX
 *  DC3: USART3, PB10 TX (AF7), PB11 RX (AF7), DMA1 Stream 4 RX
 *  DC4: UART4,  PC10 TX (AF8), PC11 RX (AF8), DMA1 Stream 5 RX
 * ========================================================================== */

#define DC_UART_RX_BUF_SIZE  256U

__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t dc1_rx_dma_buf[DC_UART_RX_BUF_SIZE];

__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t dc2_rx_dma_buf[DC_UART_RX_BUF_SIZE];

__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t dc3_rx_dma_buf[DC_UART_RX_BUF_SIZE];

__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t dc4_rx_dma_buf[DC_UART_RX_BUF_SIZE];

/* ---- DC1: USART1 on PB14/PB15 (AF4) ---- */

const USART_Config dc1_uart_cfg = {
    .tx_pin = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOB,
        .port   = GPIOB,
        .pin    = LL_GPIO_PIN_14,
        .mode   = LL_GPIO_MODE_ALTERNATE,
        .af     = LL_GPIO_AF_4,
        .speed  = LL_GPIO_SPEED_FREQ_VERY_HIGH,
        .pull   = LL_GPIO_PULL_NO,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },
    .rx_pin = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOB,
        .port   = GPIOB,
        .pin    = LL_GPIO_PIN_15,
        .mode   = LL_GPIO_MODE_ALTERNATE,
        .af     = LL_GPIO_AF_4,
        .speed  = LL_GPIO_SPEED_FREQ_VERY_HIGH,
        .pull   = LL_GPIO_PULL_UP,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },
    .peripheral        = USART1,
    .bus_clk_enable    = LL_APB2_GRP1_PERIPH_USART1,
    .kernel_clk_source = LL_RCC_USART16910_CLKSOURCE_PLL2Q,
    .prescaler         = LL_USART_PRESCALER_DIV1,
    .baudrate          = 115200U,
    .data_width        = LL_USART_DATAWIDTH_8B,
    .stop_bits         = LL_USART_STOPBITS_1,
    .parity            = LL_USART_PARITY_NONE,
    .direction         = LL_USART_DIRECTION_TX_RX,
    .hw_flow_control   = LL_USART_HWCONTROL_NONE,
    .oversampling      = LL_USART_OVERSAMPLING_16,
    .kernel_clk_hz     = 128000000UL,
    .irqn              = USART1_IRQn,
    .irq_priority      = 5U,
};

const DMA_ChannelConfig dc1_dma_rx_cfg = {
    .dma_clk_enable    = LL_AHB1_GRP1_PERIPH_DMA1,
    .dma               = DMA1,
    .stream            = LL_DMA_STREAM_2,
    .request           = LL_DMAMUX1_REQ_USART1_RX,
    .direction         = LL_DMA_DIRECTION_PERIPH_TO_MEMORY,
    .mode              = LL_DMA_MODE_CIRCULAR,
    .priority          = LL_DMA_PRIORITY_HIGH,
    .periph_data_align = LL_DMA_PDATAALIGN_BYTE,
    .mem_data_align    = LL_DMA_MDATAALIGN_BYTE,
    .periph_inc        = LL_DMA_PERIPH_NOINCREMENT,
    .mem_inc           = LL_DMA_MEMORY_INCREMENT,
    .use_fifo          = false,
    .fifo_threshold    = 0U,
    .irqn              = DMA1_Stream2_IRQn,
    .irq_priority      = 4U,
};

DC_Uart_Handle dc1_handle = {
    .cfg         = &dc1_uart_cfg,
    .dma_rx      = &dc1_dma_rx_cfg,
    .rx_buf      = dc1_rx_dma_buf,
    .rx_buf_size = DC_UART_RX_BUF_SIZE,
    .parser      = NULL,
    .rx_head     = 0U,
    .is_apb2     = true,
    .dc_index    = 1U,
};

/* ---- DC2: USART2 on PA2/PA3 (AF7) ---- */

const USART_Config dc2_uart_cfg = {
    .tx_pin = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOA,
        .port   = GPIOA,
        .pin    = LL_GPIO_PIN_2,
        .mode   = LL_GPIO_MODE_ALTERNATE,
        .af     = LL_GPIO_AF_7,
        .speed  = LL_GPIO_SPEED_FREQ_VERY_HIGH,
        .pull   = LL_GPIO_PULL_NO,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },
    .rx_pin = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOA,
        .port   = GPIOA,
        .pin    = LL_GPIO_PIN_3,
        .mode   = LL_GPIO_MODE_ALTERNATE,
        .af     = LL_GPIO_AF_7,
        .speed  = LL_GPIO_SPEED_FREQ_VERY_HIGH,
        .pull   = LL_GPIO_PULL_UP,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },
    .peripheral        = USART2,
    .bus_clk_enable    = LL_APB1_GRP1_PERIPH_USART2,
    .kernel_clk_source = LL_RCC_USART234578_CLKSOURCE_PLL2Q,
    .prescaler         = LL_USART_PRESCALER_DIV1,
    .baudrate          = 115200U,
    .data_width        = LL_USART_DATAWIDTH_8B,
    .stop_bits         = LL_USART_STOPBITS_1,
    .parity            = LL_USART_PARITY_NONE,
    .direction         = LL_USART_DIRECTION_TX_RX,
    .hw_flow_control   = LL_USART_HWCONTROL_NONE,
    .oversampling      = LL_USART_OVERSAMPLING_16,
    .kernel_clk_hz     = 128000000UL,
    .irqn              = USART2_IRQn,
    .irq_priority      = 5U,
};

const DMA_ChannelConfig dc2_dma_rx_cfg = {
    .dma_clk_enable    = LL_AHB1_GRP1_PERIPH_DMA1,
    .dma               = DMA1,
    .stream            = LL_DMA_STREAM_3,
    .request           = LL_DMAMUX1_REQ_USART2_RX,
    .direction         = LL_DMA_DIRECTION_PERIPH_TO_MEMORY,
    .mode              = LL_DMA_MODE_CIRCULAR,
    .priority          = LL_DMA_PRIORITY_HIGH,
    .periph_data_align = LL_DMA_PDATAALIGN_BYTE,
    .mem_data_align    = LL_DMA_MDATAALIGN_BYTE,
    .periph_inc        = LL_DMA_PERIPH_NOINCREMENT,
    .mem_inc           = LL_DMA_MEMORY_INCREMENT,
    .use_fifo          = false,
    .fifo_threshold    = 0U,
    .irqn              = DMA1_Stream3_IRQn,
    .irq_priority      = 4U,
};

DC_Uart_Handle dc2_handle = {
    .cfg         = &dc2_uart_cfg,
    .dma_rx      = &dc2_dma_rx_cfg,
    .rx_buf      = dc2_rx_dma_buf,
    .rx_buf_size = DC_UART_RX_BUF_SIZE,
    .parser      = NULL,
    .rx_head     = 0U,
    .is_apb2     = false,
    .dc_index    = 2U,
};

/* ---- DC3: USART3 on PB10/PB11 (AF7) ---- */

const USART_Config dc3_uart_cfg = {
    .tx_pin = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOB,
        .port   = GPIOB,
        .pin    = LL_GPIO_PIN_10,
        .mode   = LL_GPIO_MODE_ALTERNATE,
        .af     = LL_GPIO_AF_7,
        .speed  = LL_GPIO_SPEED_FREQ_VERY_HIGH,
        .pull   = LL_GPIO_PULL_NO,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },
    .rx_pin = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOB,
        .port   = GPIOB,
        .pin    = LL_GPIO_PIN_11,
        .mode   = LL_GPIO_MODE_ALTERNATE,
        .af     = LL_GPIO_AF_7,
        .speed  = LL_GPIO_SPEED_FREQ_VERY_HIGH,
        .pull   = LL_GPIO_PULL_UP,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },
    .peripheral        = USART3,
    .bus_clk_enable    = LL_APB1_GRP1_PERIPH_USART3,
    .kernel_clk_source = LL_RCC_USART234578_CLKSOURCE_PLL2Q,
    .prescaler         = LL_USART_PRESCALER_DIV1,
    .baudrate          = 115200U,
    .data_width        = LL_USART_DATAWIDTH_8B,
    .stop_bits         = LL_USART_STOPBITS_1,
    .parity            = LL_USART_PARITY_NONE,
    .direction         = LL_USART_DIRECTION_TX_RX,
    .hw_flow_control   = LL_USART_HWCONTROL_NONE,
    .oversampling      = LL_USART_OVERSAMPLING_16,
    .kernel_clk_hz     = 128000000UL,
    .irqn              = USART3_IRQn,
    .irq_priority      = 5U,
};

const DMA_ChannelConfig dc3_dma_rx_cfg = {
    .dma_clk_enable    = LL_AHB1_GRP1_PERIPH_DMA1,
    .dma               = DMA1,
    .stream            = LL_DMA_STREAM_4,
    .request           = LL_DMAMUX1_REQ_USART3_RX,
    .direction         = LL_DMA_DIRECTION_PERIPH_TO_MEMORY,
    .mode              = LL_DMA_MODE_CIRCULAR,
    .priority          = LL_DMA_PRIORITY_HIGH,
    .periph_data_align = LL_DMA_PDATAALIGN_BYTE,
    .mem_data_align    = LL_DMA_MDATAALIGN_BYTE,
    .periph_inc        = LL_DMA_PERIPH_NOINCREMENT,
    .mem_inc           = LL_DMA_MEMORY_INCREMENT,
    .use_fifo          = false,
    .fifo_threshold    = 0U,
    .irqn              = DMA1_Stream4_IRQn,
    .irq_priority      = 4U,
};

DC_Uart_Handle dc3_handle = {
    .cfg         = &dc3_uart_cfg,
    .dma_rx      = &dc3_dma_rx_cfg,
    .rx_buf      = dc3_rx_dma_buf,
    .rx_buf_size = DC_UART_RX_BUF_SIZE,
    .parser      = NULL,
    .rx_head     = 0U,
    .is_apb2     = false,
    .dc_index    = 3U,
};

/* ---- DC4: UART4 on PC10/PC11 (AF8) ---- */

const USART_Config dc4_uart_cfg = {
    .tx_pin = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOC,
        .port   = GPIOC,
        .pin    = LL_GPIO_PIN_10,
        .mode   = LL_GPIO_MODE_ALTERNATE,
        .af     = LL_GPIO_AF_8,
        .speed  = LL_GPIO_SPEED_FREQ_VERY_HIGH,
        .pull   = LL_GPIO_PULL_NO,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },
    .rx_pin = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOC,
        .port   = GPIOC,
        .pin    = LL_GPIO_PIN_11,
        .mode   = LL_GPIO_MODE_ALTERNATE,
        .af     = LL_GPIO_AF_8,
        .speed  = LL_GPIO_SPEED_FREQ_VERY_HIGH,
        .pull   = LL_GPIO_PULL_UP,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },
    .peripheral        = UART4,
    .bus_clk_enable    = LL_APB1_GRP1_PERIPH_UART4,
    .kernel_clk_source = LL_RCC_USART234578_CLKSOURCE_PLL2Q,
    .prescaler         = LL_USART_PRESCALER_DIV1,
    .baudrate          = 115200U,
    .data_width        = LL_USART_DATAWIDTH_8B,
    .stop_bits         = LL_USART_STOPBITS_1,
    .parity            = LL_USART_PARITY_NONE,
    .direction         = LL_USART_DIRECTION_TX_RX,
    .hw_flow_control   = LL_USART_HWCONTROL_NONE,
    .oversampling      = LL_USART_OVERSAMPLING_16,
    .kernel_clk_hz     = 128000000UL,
    .irqn              = UART4_IRQn,
    .irq_priority      = 5U,
};

const DMA_ChannelConfig dc4_dma_rx_cfg = {
    .dma_clk_enable    = LL_AHB1_GRP1_PERIPH_DMA1,
    .dma               = DMA1,
    .stream            = LL_DMA_STREAM_5,
    .request           = LL_DMAMUX1_REQ_UART4_RX,
    .direction         = LL_DMA_DIRECTION_PERIPH_TO_MEMORY,
    .mode              = LL_DMA_MODE_CIRCULAR,
    .priority          = LL_DMA_PRIORITY_HIGH,
    .periph_data_align = LL_DMA_PDATAALIGN_BYTE,
    .mem_data_align    = LL_DMA_MDATAALIGN_BYTE,
    .periph_inc        = LL_DMA_PERIPH_NOINCREMENT,
    .mem_inc           = LL_DMA_MEMORY_INCREMENT,
    .use_fifo          = false,
    .fifo_threshold    = 0U,
    .irqn              = DMA1_Stream5_IRQn,
    .irq_priority      = 4U,
};

DC_Uart_Handle dc4_handle = {
    .cfg         = &dc4_uart_cfg,
    .dma_rx      = &dc4_dma_rx_cfg,
    .rx_buf      = dc4_rx_dma_buf,
    .rx_buf_size = DC_UART_RX_BUF_SIZE,
    .parser      = NULL,
    .rx_head     = 0U,
    .is_apb2     = false,
    .dc_index    = 4U,
};

/* ==========================================================================
 *  USB2517I STRAPPING PINS
 *
 *  CFG_SEL1 (PG1, Pin 66) and CFG_SEL2 (PG0, Pin 63) must be driven
 *  LOW before the hub exits reset to select SMBus configuration mode.
 *
 *  CFG_SEL[2:1:0] = 0,0,1 → SMBus slave mode (Table 5-2, DS00001598C)
 *    CFG_SEL0 = SCL line (Pin 41, idles high via pull-up) = 1
 *    CFG_SEL1 = PG1 (Pin 66) driven LOW = 0
 *    CFG_SEL2 = PG0 (Pin 63) driven LOW = 0
 * ========================================================================== */
const PinConfig usb2517_reset_n_pin = {
    .clk        = LL_AHB4_GRP1_PERIPH_GPIOC,
    .port       = GPIOC,
    .pin        = LL_GPIO_PIN_13,
    .mode       = LL_GPIO_MODE_OUTPUT,
    .af         = 0U,
    .speed      = LL_GPIO_SPEED_FREQ_LOW,
    .pull       = LL_GPIO_PULL_NO,
    .output     = LL_GPIO_OUTPUT_PUSHPULL,
};

const PinConfig usb2517_cfg_sel1_pin = {
    .clk        = LL_AHB4_GRP1_PERIPH_GPIOG,
    .port       = GPIOG,
    .pin        = LL_GPIO_PIN_1,
    .mode       = LL_GPIO_MODE_OUTPUT,
    .af         = 0U,
    .speed      = LL_GPIO_SPEED_FREQ_LOW,
    .pull       = LL_GPIO_PULL_NO,
    .output     = LL_GPIO_OUTPUT_PUSHPULL,
};

const PinConfig usb2517_cfg_sel2_pin = {
    .clk        = LL_AHB4_GRP1_PERIPH_GPIOG,
    .port       = GPIOG,
    .pin        = LL_GPIO_PIN_0,
    .mode       = LL_GPIO_MODE_OUTPUT,
    .af         = 0U,
    .speed      = LL_GPIO_SPEED_FREQ_LOW,
    .pull       = LL_GPIO_PULL_NO,
    .output     = LL_GPIO_OUTPUT_PUSHPULL,
};

/* ==========================================================================
 *  DRV8702DQRHBRQ1 — TEC H-BRIDGE DRIVER, INSTANCE 1
 *
 *  All three DRV8702 circuits share SPI2 (Mode 0, PLL3Q = 128 MHz kernel).
 *  The driver uses 16-bit SPI frames and temporarily reconfigures SPI2 data
 *  width during each register access, restoring 32-bit for the LTC2338 ADC.
 *
 *  IN1/PH  : PE9,  Pin 69  — Direction control          (output, no AF)
 *  IN2/EN  : PE11, Pin 73  — Enable / PWM input         (output, no AF)
 *  nSLEEP  : PG5,  Pin 115 — Active-low sleep           (output, no AF)
 *  MODE    : PG6,  Pin 116 — PH/EN vs PWM mode select   (output, no AF)
 *  nSCS    : PD1,  Pin 144 — SPI chip select, active-low (output, no AF)
 *  nFAULT  : PG7,  Pin 117 — Active-low fault indicator (input, pull-up)
 *
 *  NOTE: PE9 and PE11 are also TIM1_CH1 / TIM1_CH2 (AF1).  If PWM control
 *  is added in future, reconfigure those pins to AF mode at that time.
 *
 *  NOTE: PD1 is one of the seven chip-select lines already driven HIGH in
 *  SystemInit_Sequence (main.c).  DRV8702_Init() re-initialises it, which
 *  is harmless and ensures the correct state even if init order changes.
 * ========================================================================== */
DRV8702_Config drv8702_1_cfg = {

    /* IN1/PH — PE9, Pin 69 */
    .ph_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOE,
        .port       = GPIOE,
        .pin        = LL_GPIO_PIN_9,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* IN2/EN — PE11, Pin 73 */
    .en_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOE,
        .port       = GPIOE,
        .pin        = LL_GPIO_PIN_11,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* nSLEEP — PG5, Pin 115 */
    .nsleep_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOG,
        .port       = GPIOG,
        .pin        = LL_GPIO_PIN_5,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_LOW,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* MODE — PG6, Pin 116 */
    .mode_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOG,
        .port       = GPIOG,
        .pin        = LL_GPIO_PIN_6,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_LOW,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* nSCS — PD1, Pin 144 */
    .ncs_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOD,
        .port       = GPIOD,
        .pin        = LL_GPIO_PIN_1,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* nFAULT — PG7, Pin 117 (open-drain output from DRV8702, pull-up on STM32) */
    .nfault_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOG,
        .port       = GPIOG,
        .pin        = LL_GPIO_PIN_7,
        .mode       = LL_GPIO_MODE_INPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_LOW,
        .pull       = LL_GPIO_PULL_UP,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,  /* N/A for input */
    },

    .spi      = SPI2,
    .instance = 1U,
};

DRV8702_Handle drv8702_1_handle = {
    .cfg             = &drv8702_1_cfg,
    .initialised     = false,
    .faulted         = false,
    .last_fault_reg  = 0U,
};

/* ==========================================================================
 *  DRV8702DQRHBRQ1 — TEC H-BRIDGE DRIVER, INSTANCE 2
 *
 *
 * ========================================================================== */
DRV8702_Config drv8702_2_cfg = {

    /* IN1/PH */
    .ph_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOE,
        .port       = GPIOE,
        .pin        = LL_GPIO_PIN_13,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* IN2/EN */
    .en_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOE,
        .port       = GPIOE,
        .pin        = LL_GPIO_PIN_14,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* nSLEEP  */
    .nsleep_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOF,
        .port       = GPIOF,
        .pin        = LL_GPIO_PIN_0,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_LOW,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* MODE */
    .mode_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOF,
        .port       = GPIOF,
        .pin        = LL_GPIO_PIN_1,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_LOW,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* nSCS  */
    .ncs_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOD,
        .port       = GPIOD,
        .pin        = LL_GPIO_PIN_0,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* nFAULT  */
    .nfault_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOF,
        .port       = GPIOF,
        .pin        = LL_GPIO_PIN_2,
        .mode       = LL_GPIO_MODE_INPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_LOW,
        .pull       = LL_GPIO_PULL_UP,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    .spi      = SPI2,
    .instance = 2U,
};

DRV8702_Handle drv8702_2_handle = {
    .cfg             = &drv8702_2_cfg,
    .initialised     = false,
    .faulted         = false,
    .last_fault_reg  = 0U,
};

/* ==========================================================================
 *  DRV8702DQRHBRQ1 — TEC H-BRIDGE DRIVER, INSTANCE 3
 *
 *  TODO: Fill in the correct pin assignments below.
 *  Replace every 0U pin value and NULL port pointer with the real values.
 * ========================================================================== */
DRV8702_Config drv8702_3_cfg = {

    /* IN1/PH */
    .ph_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOJ,
        .port       = GPIOJ,
        .pin        = LL_GPIO_PIN_8,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* IN2/EN  */
    .en_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOJ,
        .port       = GPIOJ,
        .pin        = LL_GPIO_PIN_10,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* nSLEEP */
    .nsleep_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOF,
        .port       = GPIOF,
        .pin        = LL_GPIO_PIN_12,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_LOW,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* MODE */
    .mode_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOF,
        .port       = GPIOF,
        .pin        = LL_GPIO_PIN_13,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_LOW,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* nSCS */
    .ncs_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOD,
        .port       = GPIOD,
        .pin        = LL_GPIO_PIN_6,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* nFAULT  */
    .nfault_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOF,
        .port       = GPIOF,
        .pin        = LL_GPIO_PIN_14,
        .mode       = LL_GPIO_MODE_INPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_LOW,
        .pull       = LL_GPIO_PULL_UP,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    .spi      = SPI2,
    .instance = 3U,
};

DRV8702_Handle drv8702_3_handle = {
    .cfg             = &drv8702_3_cfg,
    .initialised     = false,
    .faulted         = false,
    .last_fault_reg  = 0U,
};

/* ==========================================================================
 *  DAC80508ZRTER — 8-CHANNEL 16-BIT DAC
 *
 *  Shares SPI2 with LTC2338-18 and DRV8702.  Uses 24-bit SPI frames in
 *  Mode 1 (CPOL=0, CPHA=1).  The driver temporarily reconfigures SPI2
 *  for each transfer and restores 32-bit Mode 0 on exit.
 *
 *  nCS : PD2, Pin 145 — SPI chip select, active-low
 *
 *  NOTE: PD2 is one of the seven chip-select lines already driven HIGH in
 *  SystemInit_Sequence (main.c).  DAC80508_Init() re-initialises it, which
 *  is harmless and ensures the correct state even if init order changes.
 * ========================================================================== */
DAC80508_Config dac80508_cfg = {

    /* nCS — PD2, Pin 145 */
    .ncs_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOD,
        .port       = GPIOD,
        .pin        = LL_GPIO_PIN_2,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },

    .spi = SPI2,
};

DAC80508_Handle dac80508_handle = {
    .cfg         = &dac80508_cfg,
    .initialised = false,
};

/* ==========================================================================
 *  ADS7066IRTER — 8-CHANNEL 16-BIT SAR ADC (3 INSTANCES)
 *
 *  All three share SPI2 (Mode 0).  Uses 24-bit frames for register access
 *  and 16-bit frames for ADC data reads.  Only the data width is changed;
 *  CPOL/CPHA remain at Mode 0.
 *
 *  Instance 1: nCS on PD5, Pin 148
 *  Instance 2: nCS on PD4, Pin 147
 *  Instance 3: nCS on PD3, Pin 146
 *
 *  NOTE: PD3–PD5 are among the seven chip-select lines already driven HIGH
 *  in SystemInit_Sequence (main.c).
 * ========================================================================== */
ADS7066_Config ads7066_1_cfg = {
    .ncs_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOD,
        .port       = GPIOD,
        .pin        = LL_GPIO_PIN_5,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },
    .spi      = SPI2,
    .instance = 1U,
};

ADS7066_Handle ads7066_1_handle = {
    .cfg             = &ads7066_1_cfg,
    .initialised     = false,
    .current_channel = 0U,
};

ADS7066_Config ads7066_2_cfg = {
    .ncs_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOD,
        .port       = GPIOD,
        .pin        = LL_GPIO_PIN_4,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },
    .spi      = SPI2,
    .instance = 2U,
};

ADS7066_Handle ads7066_2_handle = {
    .cfg             = &ads7066_2_cfg,
    .initialised     = false,
    .current_channel = 0U,
};

ADS7066_Config ads7066_3_cfg = {
    .ncs_pin = {
        .clk        = LL_AHB4_GRP1_PERIPH_GPIOD,
        .port       = GPIOD,
        .pin        = LL_GPIO_PIN_3,
        .mode       = LL_GPIO_MODE_OUTPUT,
        .af         = 0U,
        .speed      = LL_GPIO_SPEED_FREQ_HIGH,
        .pull       = LL_GPIO_PULL_NO,
        .output     = LL_GPIO_OUTPUT_PUSHPULL,
    },
    .spi      = SPI2,
    .instance = 3U,
};

ADS7066_Handle ads7066_3_handle = {
    .cfg             = &ads7066_3_cfg,
    .initialised     = false,
    .current_channel = 0U,
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
