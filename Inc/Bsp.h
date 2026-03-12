/*******************************************************************************
 * @file    Inc/bsp.h
 * @author  Cam
 * @brief   Board Support Package — Type Definitions and Extern Declarations
 *
 *          This header defines the configuration struct types for all
 *          peripherals and declares the const instances that describe
 *          the specific hardware on this board.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef BSP_H
#define BSP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx_ll_cortex.h"
#include "stm32h7xx_ll_dma.h"
#include "stm32h7xx_ll_gpio.h"
#include "stm32h7xx_ll_i2c.h"
#include "stm32h7xx_ll_pwr.h"
#include "stm32h7xx_ll_rcc.h"
#include "stm32h7xx_ll_system.h"
#include "stm32h7xx_ll_spi.h"
#include "stm32h7xx_ll_usart.h"
#include "stm32h7xx_ll_utils.h"

/* NVIC Priority Group definitions (not provided by CMSIS/LL) ---------------*/
#ifndef NVIC_PRIORITYGROUP_0
#define NVIC_PRIORITYGROUP_0    ((uint32_t)0x00000007)  /* 0-bit preemption, 4-bit sub */
#define NVIC_PRIORITYGROUP_1    ((uint32_t)0x00000006)  /* 1-bit preemption, 3-bit sub */
#define NVIC_PRIORITYGROUP_2    ((uint32_t)0x00000005)  /* 2-bit preemption, 2-bit sub */
#define NVIC_PRIORITYGROUP_3    ((uint32_t)0x00000004)  /* 3-bit preemption, 1-bit sub */
#define NVIC_PRIORITYGROUP_4    ((uint32_t)0x00000003)  /* 4-bit preemption, 0-bit sub */
#endif

/* ========================== Return / Status Types ========================= */

/**
 * @brief Extended init result using a bitmask so the caller knows exactly
 *        which subsystem failed.  Keeps the single-return-value pattern
 *        but avoids the ErrorStatus += anti-pattern.
 */
typedef enum {
    INIT_OK             = 0x0000U,
    INIT_ERR_CLK        = (1U << 0),
    INIT_ERR_GPIO       = (1U << 1),
    INIT_ERR_USART      = (1U << 2),
    INIT_ERR_DMA        = (1U << 3),
    INIT_ERR_I2C        = (1U << 4),
    /* Extend as you add peripherals */
} InitResult;

/* ============================ Pin Configuration =========================== */

/**
 * @brief Single-pin descriptor.  Every pin gets its own struct instance so
 *        pins on different ports are handled uniformly — no CMPLX_Port flag.
 */
typedef struct {
    uint32_t            clk;        /**< AHB4 peripheral clock enable mask   */
    GPIO_TypeDef       *port;       /**< GPIO port base (GPIOA … GPIOK)     */
    uint32_t            pin;        /**< LL_GPIO_PIN_x                       */
    uint32_t            mode;       /**< LL_GPIO_MODE_x                      */
    uint32_t            af;         /**< LL_GPIO_AF_x  (ignored if not AF)   */
    uint32_t            speed;      /**< LL_GPIO_SPEED_FREQ_x                */
    uint32_t            pull;       /**< LL_GPIO_PULL_x                      */
    uint32_t            output;     /**< LL_GPIO_OUTPUT_PUSHPULL/OPENDRAIN   */
} PinConfig;

/* =========================== Clock Configuration ========================== */

/**
 * @brief PLL parameter set.  One struct per PLL (PLL1, PLL2, PLL3).
 *
 * VCO_in  = HSE / divm           (must be 2–4 MHz for PLLINPUTRANGE_2_4)
 * VCO_out = VCO_in × divn
 * P_out   = VCO_out / divp       (PLL1P → SYSCLK)
 * Q_out   = VCO_out / divq
 * R_out   = VCO_out / divr
 */
typedef struct {
    uint32_t            divm;       /**< Input divider  (M)                  */
    uint32_t            divn;       /**< VCO multiplier (N)                  */
    uint32_t            divp;       /**< P output divider                    */
    uint32_t            divq;       /**< Q output divider                    */
    uint32_t            divr;       /**< R output divider                    */
    uint32_t            vco_input_range;   /**< LL_RCC_PLLINPUTRANGE_x       */
    uint32_t            vco_output_range;  /**< LL_RCC_PLLVCORANGE_x         */
    bool                enable_p;   /**< Enable P output tap                 */
    bool                enable_q;   /**< Enable Q output tap                 */
    bool                enable_r;   /**< Enable R output tap                 */
} PLL_Config;

/**
 * @brief Bus prescaler set — everything downstream of SYSCLK.
 */
typedef struct {
    uint32_t            d1cpre;     /**< LL_RCC_SYSCLK_DIV_x  (CPU)         */
    uint32_t            hpre;       /**< LL_RCC_AHB_DIV_x     (AHB)         */
    uint32_t            d1ppre;     /**< LL_RCC_APB3_DIV_x    (APB3 / D1)   */
    uint32_t            d2ppre1;    /**< LL_RCC_APB1_DIV_x    (APB1 / D2)   */
    uint32_t            d2ppre2;    /**< LL_RCC_APB2_DIV_x    (APB2 / D2)   */
    uint32_t            d3ppre;     /**< LL_RCC_APB4_DIV_x    (APB4 / D3)   */
} BusPrescaler_Config;

/**
 * @brief Top-level clock tree configuration.
 *
 * Holds HSE frequency, voltage scaling, flash wait states, all three PLLs,
 * and the bus prescalers.  Passed to ClockTree_Init() which applies
 * everything in the correct sequence.
 */
typedef struct {
    /* Source */
    uint32_t            hse_freq_hz;       /**< External crystal frequency   */

    /* Power / flash */
    uint32_t            voltage_scale;     /**< LL_PWR_REGU_VOLTAGE_SCALEx   */
    uint32_t            flash_latency;     /**< LL_FLASH_LATENCY_x           */

    /* PLLs */
    PLL_Config          pll1;              /**< System PLL → SYSCLK          */
    PLL_Config          pll2;              /**< Peripheral PLL (ADC, USART…) */
    PLL_Config          pll3;              /**< Peripheral PLL (SPI, I2C…)   */

    /* Prescalers */
    BusPrescaler_Config prescalers;

    /* Derived (for convenience / readability) */
    uint32_t            sysclk_hz;         /**< Expected SYSCLK after config */
    uint32_t            ahb_hz;            /**< Expected AHB clock           */
    uint32_t            apb1_hz;           /**< Expected APB1 clock          */
    uint32_t            apb2_hz;           /**< Expected APB2 clock          */
    uint32_t 			pll2q_hz;   /* add */
    uint32_t 			pll3q_hz;   /* add */
    uint32_t 			pll3r_hz;   /* add */
} ClockTree_Config;

/* ========================== USART Configuration =========================== */

/**
 * @brief Complete USART peripheral configuration.
 *
 * Includes both GPIO pin configs and the USART register settings.
 * Immutable — const-qualified in bsp.c.
 */
typedef struct {
    /* Pins */
    PinConfig           tx_pin;
    PinConfig           rx_pin;

    /* Peripheral */
    USART_TypeDef      *peripheral;        /**< USARTx base address          */
    uint32_t            bus_clk_enable;     /**< LL_APBx_GRPx_PERIPH_USARTx  */
    uint32_t            kernel_clk_source;  /**< LL_RCC_USARTxx_CLKSOURCE_x  */
    uint32_t            prescaler;          /**< LL_USART_PRESCALER_DIVx     */
    uint32_t            baudrate;
    uint32_t            data_width;         /**< LL_USART_DATAWIDTH_xB       */
    uint32_t            stop_bits;          /**< LL_USART_STOPBITS_x         */
    uint32_t            parity;             /**< LL_USART_PARITY_x           */
    uint32_t            direction;          /**< LL_USART_DIRECTION_x        */
    uint32_t            hw_flow_control;    /**< LL_USART_HWCONTROL_x        */
    uint32_t            oversampling;       /**< LL_USART_OVERSAMPLING_x     */
    uint32_t			kernel_clk_hz;
    /* Interrupt */
    IRQn_Type           irqn;
    uint32_t            irq_priority;
} USART_Config;

/* ============================ DMA Configuration =========================== */

/**
 * @brief Single DMA stream/channel configuration.
 *
 * On STM32H7 the DMAMUX request ID selects which peripheral is routed
 * to the stream — no fixed stream-to-peripheral mapping.
 */
typedef struct {
    /* Clock */
    uint32_t            dma_clk_enable;    /**< LL_AHB1_GRP1_PERIPH_DMAx    */

    /* Stream */
    DMA_TypeDef        *dma;               /**< DMA1 or DMA2                 */
    uint32_t            stream;            /**< LL_DMA_STREAM_x              */
    uint32_t            request;           /**< LL_DMAMUX1_REQ_x             */
    uint32_t            direction;         /**< LL_DMA_DIRECTION_x           */
    uint32_t            mode;              /**< LL_DMA_MODE_NORMAL/CIRCULAR  */
    uint32_t            priority;          /**< LL_DMA_PRIORITY_x            */

    /* Data widths */
    uint32_t            periph_data_align; /**< LL_DMA_PDATAALIGN_BYTE etc.  */
    uint32_t            mem_data_align;    /**< LL_DMA_MDATAALIGN_BYTE etc.  */

    /* Address increment */
    uint32_t            periph_inc;        /**< LL_DMA_PERIPH_NOINCREMENT    */
    uint32_t            mem_inc;           /**< LL_DMA_MEMORY_INCREMENT      */

    /* FIFO (set false for direct mode) */
    bool                use_fifo;
    uint32_t            fifo_threshold;    /**< LL_DMA_FIFOTHRESHOLD_x       */

    /* Interrupt */
    IRQn_Type           irqn;
    uint32_t            irq_priority;
} DMA_ChannelConfig;

/* ====================== USART Runtime Handle ============================== */

/**
 * @brief Mutable runtime state for a USART peripheral with DMA.
 *
 * The const config pointers give the driver access to pin/register
 * definitions.  The remaining fields hold buffer pointers, transfer
 * state, and CRC working data — all of which change at runtime.
 *
 * This struct lives in RAM; the configs it points to live in flash.
 */
typedef struct {
    /* Immutable configuration (flash-resident) */
    const USART_Config        *cfg;
    const DMA_ChannelConfig   *dma_tx;
    const DMA_ChannelConfig   *dma_rx;

    /* DMA buffers — must be in D2 SRAM, not DTCM.
     * Assign these to buffers placed with __attribute__((section(".dma_buffer")))
     */
    uint8_t                   *tx_buf;
    uint16_t                   tx_buf_size;
    uint8_t                   *rx_buf;
    uint16_t                   rx_buf_size;

    /* Protocol parser — assigned at init, used by ISR to feed bytes */
    void                      *parser;       /**< ProtocolParser* (void* to avoid circular include) */

    /* Transfer state */
    volatile uint16_t          tx_len;       /**< Bytes queued for current TX */
    volatile bool              tx_busy;      /**< TX DMA transfer in progress */
    volatile uint16_t          rx_head;      /**< DMA write position tracker  */

    /* CRC working state (your 16-bit CRC protocol) */
    uint16_t                   crc_accumulator;
} USART_Handle;

/* =========================== SPI Configuration ============================ */

/**
 * @brief Complete SPI peripheral configuration.
 *
 * Includes GPIO pin configs for MISO, MOSI, SCK, and device-specific
 * control/status pins (CNV, BUSY) for the LTC2338-18 ADC.
 * Immutable — const-qualified in bsp.c.
 */
typedef struct {
    /* Bus signal pins */
    PinConfig           miso_pin;
    PinConfig           mosi_pin;
    PinConfig           sck_pin;

    /* LTC2338-18 control / status pins */
    PinConfig           cnv_pin;            /**< Conversion trigger — active HIGH pulse */
    PinConfig           busy_pin;           /**< Conversion complete — LOW when ready   */

    /* Peripheral */
    SPI_TypeDef        *peripheral;         /**< SPIx base address                      */
    uint32_t            bus_clk_enable;     /**< LL_APB1_GRP1_PERIPH_SPIx              */

    /* Protocol */
    uint32_t            clk_polarity;       /**< LL_SPI_POLARITY_LOW/HIGH              */
    uint32_t            clk_phase;          /**< LL_SPI_PHASE_1EDGE/2EDGE              */
    uint32_t            bit_order;          /**< LL_SPI_MSB_FIRST / LSB_FIRST          */
    uint32_t            data_width;         /**< LL_SPI_DATAWIDTH_32BIT                */
    uint32_t            nss_mode;           /**< LL_SPI_NSS_SOFT                       */
    uint32_t            baud_prescaler;     /**< LL_SPI_BAUDRATEPRESCALER_DIVx         */

    /* Timeouts (millisecond tick counts) */
    uint32_t            busy_timeout_ms;    /**< Max wait for BUSY to assert           */
    uint32_t            xfer_timeout_ms;    /**< Max wait for TXC/RXP flags            */
} SPI_Config;

/**
 * @brief Mutable runtime state for an SPI peripheral.
 *
 * The const config pointer lives in flash.  The runtime fields track
 * transfer state for polled operation.
 */
typedef struct {
    const SPI_Config   *cfg;

    /* Transfer state */
    bool                initialised;
    volatile bool       busy;
    volatile uint32_t   error;              /**< Last error flags                       */
} SPI_Handle;

/* =========================== I2C Configuration ============================ */

/**
 * @brief Complete I2C peripheral configuration.
 *
 * Includes GPIO pin configs and I2C register settings.
 * Immutable — const-qualified in bsp.c.
 *
 * The timing register value is the most complex part — use STM32CubeMX's
 * I2C timing calculator or the AN4235 application note formula.
 */
typedef struct {
    /* Pins */
    PinConfig           scl_pin;
    PinConfig           sda_pin;

    /* Peripheral */
    I2C_TypeDef        *peripheral;         /**< I2Cx base address            */
    uint32_t            bus_clk_enable;      /**< LL_APB1_GRPx_PERIPH_I2Cx    */
    uint32_t            kernel_clk_source;   /**< LL_RCC_I2Cxxx_CLKSOURCE_x   */

    /* I2C settings */
    uint32_t            timing;              /**< I2C_TIMINGR value            */
    uint32_t            analog_filter;       /**< LL_I2C_ANALOGFILTER_x        */
    uint32_t            digital_filter;      /**< 0x00–0x0F                    */
    uint32_t            own_address;         /**< Own address (0 for master)   */
    uint32_t            addressing_mode;     /**< LL_I2C_ADDRESSING_MODE_xBIT  */
} I2C_Config;

/**
 * @brief Mutable runtime state for an I2C peripheral.
 *
 * The const config pointer lives in flash.  The runtime fields track
 * transfer state for polled or interrupt-driven operation.
 */
typedef struct {
    const I2C_Config   *cfg;

    /* Transfer state */
    volatile bool       busy;
    volatile uint32_t   error;               /**< Last error flags             */
} I2C_Handle;

/* ======================== Utility Prototypes ============================== */

/**
 * @brief Initialise a single GPIO pin from a PinConfig descriptor.
 */
void Pin_Init(const PinConfig *pin);

/* ======================= Extern Config Instances ========================== */

/* Clock tree */
extern const ClockTree_Config   sys_clk_config;

/* USART10 on PG11 (RX) / PG12 (TX) */
extern const USART_Config       usart10_cfg;
extern const DMA_ChannelConfig  usart10_dma_tx_cfg;
extern const DMA_ChannelConfig  usart10_dma_rx_cfg;

/* USART10 runtime handle */
extern USART_Handle             usart10_handle;

/* SPI2 — LTC2338-18 18-bit ADC
 *   MISO : PC2_C (Pin 36)   MOSI : PC3_C (Pin 37)   SCK : PA9 (Pin 128)
 *   CNV  : PE12  (Pin 74)   BUSY : PE15  (Pin 77)                        */
extern const SPI_Config         spi2_cfg;
extern SPI_Handle               spi2_handle;

/* I2C1 on PB8 (SCL) / PB7 (SDA) */
extern const I2C_Config         i2c1_cfg;
extern I2C_Handle               i2c1_handle;

/* USB2517I strapping pins */
extern const PinConfig          usb2517_cfg_sel1_pin;   /* PG1 — Pin 66 */
extern const PinConfig          usb2517_cfg_sel2_pin;   /* PG0 — Pin 63 */

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */
