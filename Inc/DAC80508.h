/*******************************************************************************
 * @file    Inc/DAC80508.h
 * @author  Cam
 * @brief   DAC80508ZRTER — 8-Channel 16-bit DAC, Register Map and Public API
 *
 *          SPI interface shared with LTC2338-18 ADC and DRV8702 drivers on
 *          SPI2.  The DAC80508 uses 24-bit SPI frames in Mode 1 (CPOL=0,
 *          CPHA=1).  Transfer functions temporarily reconfigure SPI2 data
 *          width and clock phase, then restore the LTC2338 defaults (32-bit,
 *          Mode 0) on exit.
 *
 *          Must not be called concurrently with SPI_LTC2338_Read() or
 *          DRV8702 SPI register access.
 *
 * SPI MODE NOTICE:
 *          The DAC80508 supports SPI Mode 1 (CPOL=0, CPHA=1) and Mode 2
 *          (CPOL=1, CPHA=0).  This driver uses Mode 1.  Verify against the
 *          DAC80508 datasheet (SLASER2) timing diagrams.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef DAC80508_H
#define DAC80508_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bsp.h"

/* ==========================================================================
 *  SPI REGISTER ADDRESSES
 * ========================================================================== */
#define DAC80508_REG_NOOP           0x00U   /* No operation                     */
#define DAC80508_REG_DEVICE_ID      0x01U   /* Device ID         (read only)    */
#define DAC80508_REG_SYNC           0x02U   /* Synchronous update control       */
#define DAC80508_REG_CONFIG         0x03U   /* Configuration                    */
#define DAC80508_REG_GAIN           0x04U   /* Gain settings                    */
#define DAC80508_REG_TRIGGER        0x05U   /* Software trigger                 */
#define DAC80508_REG_BRDCAST        0x06U   /* Broadcast data to all channels   */
#define DAC80508_REG_STATUS         0x07U   /* Status register   (read only)    */
#define DAC80508_REG_DAC0           0x08U   /* DAC channel 0 data               */
#define DAC80508_REG_DAC1           0x09U   /* DAC channel 1 data               */
#define DAC80508_REG_DAC2           0x0AU   /* DAC channel 2 data               */
#define DAC80508_REG_DAC3           0x0BU   /* DAC channel 3 data               */
#define DAC80508_REG_DAC4           0x0CU   /* DAC channel 4 data               */
#define DAC80508_REG_DAC5           0x0DU   /* DAC channel 5 data               */
#define DAC80508_REG_DAC6           0x0EU   /* DAC channel 6 data               */
#define DAC80508_REG_DAC7           0x0FU   /* DAC channel 7 data               */

/* ==========================================================================
 *  SPI FRAME FORMAT  (24-bit, MSB first, Mode 1)
 *
 *  Bit  [23]:    R/W  — 0 = write, 1 = read
 *  Bits [22:20]: Reserved (000)
 *  Bits [19:16]: ADDR — 4-bit register address
 *  Bits [15:0]:  DATA — 16-bit register data
 * ========================================================================== */
#define DAC80508_SPI_WRITE          (0U << 23)
#define DAC80508_SPI_READ           (1U << 23)
#define DAC80508_SPI_ADDR(a)        (((uint32_t)(a) & 0x0FU) << 16)
#define DAC80508_SPI_DATA(d)        ((uint32_t)(d) & 0xFFFFU)
#define DAC80508_SPI_BUILD_WR(a, d) (DAC80508_SPI_WRITE | DAC80508_SPI_ADDR(a) | DAC80508_SPI_DATA(d))
#define DAC80508_SPI_BUILD_RD(a)    (DAC80508_SPI_READ  | DAC80508_SPI_ADDR(a))

/* Extract 16-bit data from 24-bit SPI read response */
#define DAC80508_SPI_RX_DATA(rx)    ((uint16_t)((rx) & 0xFFFFU))

/* ==========================================================================
 *  CONFIG REGISTER (0x03) BIT DEFINITIONS
 * ========================================================================== */
#define DAC80508_CFG_REF_PWDWN      (1U << 8)   /* 1 = internal ref powered down */
#define DAC80508_CFG_DAC7_PWDWN     (1U << 7)   /* 1 = DAC7 powered down         */
#define DAC80508_CFG_DAC6_PWDWN     (1U << 6)
#define DAC80508_CFG_DAC5_PWDWN     (1U << 5)
#define DAC80508_CFG_DAC4_PWDWN     (1U << 4)
#define DAC80508_CFG_DAC3_PWDWN     (1U << 3)
#define DAC80508_CFG_DAC2_PWDWN     (1U << 2)
#define DAC80508_CFG_DAC1_PWDWN     (1U << 1)
#define DAC80508_CFG_DAC0_PWDWN     (1U << 0)

/* ==========================================================================
 *  GAIN REGISTER (0x04) BIT DEFINITIONS
 *
 *  Bits [15:9]:  REFDIV-EN + BUF-GAINx for each channel
 *  Bit  [8]:     REFDIV-EN — 0 = VDD as ref, 1 = internal ref / 2
 *  Bits [7:0]:   BUF-GAINx — 0 = gain of 1, 1 = gain of 2 (per channel)
 * ========================================================================== */
#define DAC80508_GAIN_REFDIV_EN     (1U << 8)    /* Enable internal ref divider  */
#define DAC80508_GAIN_BUFF7_2X      (1U << 7)    /* DAC7 buffer gain = 2x        */
#define DAC80508_GAIN_BUFF6_2X      (1U << 6)
#define DAC80508_GAIN_BUFF5_2X      (1U << 5)
#define DAC80508_GAIN_BUFF4_2X      (1U << 4)
#define DAC80508_GAIN_BUFF3_2X      (1U << 3)
#define DAC80508_GAIN_BUFF2_2X      (1U << 2)
#define DAC80508_GAIN_BUFF1_2X      (1U << 1)
#define DAC80508_GAIN_BUFF0_2X      (1U << 0)

/* ==========================================================================
 *  SYNC REGISTER (0x02) BIT DEFINITIONS
 *
 *  When a channel's sync bit is set, writes to its DAC register are
 *  buffered until a software LDAC trigger or all sync bits are cleared.
 * ========================================================================== */
#define DAC80508_SYNC_DAC7          (1U << 7)
#define DAC80508_SYNC_DAC6          (1U << 6)
#define DAC80508_SYNC_DAC5          (1U << 5)
#define DAC80508_SYNC_DAC4          (1U << 4)
#define DAC80508_SYNC_DAC3          (1U << 3)
#define DAC80508_SYNC_DAC2          (1U << 2)
#define DAC80508_SYNC_DAC1          (1U << 1)
#define DAC80508_SYNC_DAC0          (1U << 0)

/* ==========================================================================
 *  TRIGGER REGISTER (0x05) BIT DEFINITIONS
 * ========================================================================== */
#define DAC80508_TRIG_LDAC          (1U << 4)    /* Software LDAC trigger        */
#define DAC80508_TRIG_SOFT_RESET    (0x0AU)      /* Write 0x000A to soft reset   */

/* ==========================================================================
 *  TIMEOUT
 * ========================================================================== */
#define DAC80508_SPI_TIMEOUT_MS     5U

/* ==========================================================================
 *  ENUMERATIONS
 * ========================================================================== */

typedef enum {
    DAC80508_OK           = 0,
    DAC80508_ERR_NOT_INIT = 1,   /* Handle used before DAC80508_Init()        */
    DAC80508_ERR_TIMEOUT  = 2,   /* SPI transfer timed out                    */
    DAC80508_ERR_SPI      = 3,   /* SPI overrun or bus error                  */
    DAC80508_ERR_PARAM    = 4,   /* Invalid parameter (e.g. channel > 7)      */
} DAC80508_Status;

/* ==========================================================================
 *  PUBLIC API
 * ========================================================================== */

/**
 * @brief  Initialise the DAC80508 — configure nCS pin and verify SPI
 *         communication by reading the DEVICE_ID register.
 *
 *         SPI2 must be initialised before calling this function.
 *         nCS pin is driven HIGH (deasserted) on init.
 */
DAC80508_Status DAC80508_Init(DAC80508_Handle *handle);

/**
 * @brief  Write a 16-bit value to a DAC80508 register.
 *
 *         Temporarily reconfigures SPI2 to 24-bit Mode 1, performs the
 *         transfer, then restores 32-bit Mode 0 for the LTC2338 ADC.
 */
DAC80508_Status DAC80508_WriteReg(DAC80508_Handle *handle,
                                   uint8_t addr, uint16_t data);

/**
 * @brief  Read a 16-bit value from a DAC80508 register.
 *
 *         Requires two SPI transactions: first sends the read command,
 *         second clocks out the response (DAC80508 returns data on the
 *         NEXT frame after the read command).
 */
DAC80508_Status DAC80508_ReadReg(DAC80508_Handle *handle,
                                  uint8_t addr, uint16_t *data_out);

/**
 * @brief  Set a single DAC channel output value.
 *
 * @param  handle   DAC handle
 * @param  channel  Channel number (0–7)
 * @param  value    16-bit DAC code (0x0000 = 0V, 0xFFFF = full scale)
 */
DAC80508_Status DAC80508_SetChannel(DAC80508_Handle *handle,
                                     uint8_t channel, uint16_t value);

/**
 * @brief  Set all 8 DAC channels to the same value via the broadcast
 *         register.
 */
DAC80508_Status DAC80508_SetAll(DAC80508_Handle *handle, uint16_t value);

/**
 * @brief  Software reset the DAC80508.  All registers return to defaults.
 */
DAC80508_Status DAC80508_SoftReset(DAC80508_Handle *handle);

/**
 * @brief  Read the DEVICE_ID register.
 * @param  handle    DAC handle
 * @param  id_out    Pointer to store the 16-bit device ID
 */
DAC80508_Status DAC80508_ReadDeviceID(DAC80508_Handle *handle,
                                       uint16_t *id_out);

#ifdef __cplusplus
}
#endif

#endif /* DAC80508_H */
