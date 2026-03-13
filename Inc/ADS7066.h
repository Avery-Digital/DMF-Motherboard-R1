/*******************************************************************************
 * @file    Inc/ADS7066.h
 * @author  Cam
 * @brief   ADS7066IRTER — 8-Channel 16-bit 250-kSPS SAR ADC, Public API
 *
 *          Three instances share SPI2, each with a unique nCS chip-select.
 *          The ADS7066 defaults to SPI Mode 0 (CPOL=0, CPHA=0) which matches
 *          the SPI2 default.  Only the data width is changed to 24-bit for
 *          register access.
 *
 *          Conversions are triggered by the CS rising edge.  After tCONV
 *          (~3.2 µs), the result is clocked out on the next CS-low frame.
 *
 *          Reference: ADS7066 datasheet (SBAS928C)
 *
 * SPI SHARING NOTICE:
 *          SPI2 is shared with the LTC2338-18 ADC (32-bit Mode 0), DRV8702
 *          TEC drivers (16-bit Mode 0), and DAC80508 DAC (24-bit Mode 1).
 *          ADS7066 uses 24-bit Mode 0 — only the data width changes.
 *          Must not be called concurrently with other SPI2 device access.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef ADS7066_H
#define ADS7066_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bsp.h"

/* ==========================================================================
 *  SPI OPCODES
 * ========================================================================== */
#define ADS7066_OP_NOOP             0x00U   /* No operation                     */
#define ADS7066_OP_READ             0x10U   /* Single register read             */
#define ADS7066_OP_WRITE            0x08U   /* Single register write            */
#define ADS7066_OP_SET_BIT          0x18U   /* Set specified bits               */
#define ADS7066_OP_CLR_BIT          0x20U   /* Clear specified bits             */

/* ==========================================================================
 *  REGISTER ADDRESSES
 * ========================================================================== */
#define ADS7066_REG_SYSTEM_STATUS   0x00U   /* Status + BOR + CRC error flags   */
#define ADS7066_REG_GENERAL_CFG     0x01U   /* REF_EN, CRC_EN, RANGE, CAL, RST */
#define ADS7066_REG_DATA_CFG        0x02U   /* FIX_PAT, APPEND_STATUS, SPI mode */
#define ADS7066_REG_OSR_CFG         0x03U   /* Oversampling ratio               */
#define ADS7066_REG_OPMODE_CFG      0x04U   /* Oscillator, clock divider        */
#define ADS7066_REG_PIN_CFG         0x05U   /* AIN vs GPIO per channel          */
#define ADS7066_REG_GPIO_CFG        0x07U   /* GPIO direction (input/output)    */
#define ADS7066_REG_GPO_DRIVE_CFG   0x09U   /* GPIO output type (OD/PP)         */
#define ADS7066_REG_GPO_VALUE       0x0BU   /* GPIO output levels               */
#define ADS7066_REG_GPI_VALUE       0x0DU   /* GPIO input readback              */
#define ADS7066_REG_SEQUENCE_CFG    0x10U   /* SEQ_MODE, SEQ_START              */
#define ADS7066_REG_CHANNEL_SEL     0x11U   /* MANUAL_CHID[3:0]                 */
#define ADS7066_REG_AUTO_SEQ_CHSEL  0x12U   /* Auto-sequence channel enable     */

/* ==========================================================================
 *  SPI FRAME HELPERS  (24-bit, MSB first, Mode 0)
 *
 *  Frame: [opcode(8)] [address(8)] [data(8)]
 * ========================================================================== */
#define ADS7066_SPI_FRAME(op, addr, data) \
    (((uint32_t)(op) << 16) | ((uint32_t)(addr) << 8) | (uint32_t)(data))

/* ==========================================================================
 *  GENERAL_CFG (0x01) BIT DEFINITIONS
 * ========================================================================== */
#define ADS7066_CFG_REF_EN          (1U << 7)   /* Enable internal 2.5V ref    */
#define ADS7066_CFG_CRC_EN          (1U << 6)   /* Enable CRC on SPI           */
#define ADS7066_CFG_RANGE           (1U << 3)   /* 0 = 1x VREF, 1 = 2x VREF   */
#define ADS7066_CFG_CH_RST          (1U << 2)   /* Force all channels to AIN   */
#define ADS7066_CFG_CAL             (1U << 1)   /* Trigger offset calibration  */
#define ADS7066_CFG_RST             (1U << 0)   /* Software reset              */

/* ==========================================================================
 *  DATA_CFG (0x02) BIT DEFINITIONS
 * ========================================================================== */
#define ADS7066_DATA_FIX_PAT        (1U << 7)   /* Output fixed 0xA5A5         */
#define ADS7066_DATA_APPEND_NONE    (0U << 4)   /* No channel ID / status      */
#define ADS7066_DATA_APPEND_CHID    (1U << 4)   /* Append 4-bit channel ID     */
#define ADS7066_DATA_APPEND_STATUS  (2U << 4)   /* Append 4-bit status flags   */

/* ==========================================================================
 *  SEQUENCE_CFG (0x10) BIT DEFINITIONS
 * ========================================================================== */
#define ADS7066_SEQ_START           (1U << 4)   /* Start auto-sequence          */
#define ADS7066_SEQ_MODE_MANUAL     0x00U       /* Manual channel selection     */
#define ADS7066_SEQ_MODE_AUTO       0x01U       /* Auto-sequence mode           */
#define ADS7066_SEQ_MODE_OTF        0x02U       /* On-the-fly mode              */

/* ==========================================================================
 *  SYSTEM_STATUS (0x00) BIT DEFINITIONS
 * ========================================================================== */
#define ADS7066_STAT_BOR            (1U << 0)   /* Brown-out / power-cycle flag */
#define ADS7066_STAT_CRCERR_IN      (1U << 1)   /* CRC error on incoming data  */
#define ADS7066_STAT_CRCERR_FUSE    (1U << 2)   /* Power-up config CRC error   */

/* ==========================================================================
 *  CONSTANTS
 * ========================================================================== */
#define ADS7066_SPI_TIMEOUT_MS      5U
#define ADS7066_NUM_CHANNELS        8U
#define ADS7066_MAX_CHANNEL         7U

/* ==========================================================================
 *  ENUMERATIONS
 * ========================================================================== */

typedef enum {
    ADS7066_OK           = 0,
    ADS7066_ERR_NOT_INIT = 1,   /* Handle used before ADS7066_Init()          */
    ADS7066_ERR_TIMEOUT  = 2,   /* SPI transfer timed out                     */
    ADS7066_ERR_SPI      = 3,   /* SPI overrun or bus error                   */
    ADS7066_ERR_PARAM    = 4,   /* Invalid parameter (e.g. channel > 7)       */
} ADS7066_Status;

/* ==========================================================================
 *  PUBLIC API
 * ========================================================================== */

/**
 * @brief  Initialise ADS7066 instance — configure nCS pin, clear BOR flag.
 *         SPI2 must be initialised before calling.
 */
ADS7066_Status ADS7066_Init(ADS7066_Handle *handle);

/**
 * @brief  Write an 8-bit value to an ADS7066 register.
 *         Temporarily reconfigures SPI2 to 24-bit, restores 32-bit on exit.
 */
ADS7066_Status ADS7066_WriteReg(ADS7066_Handle *handle,
                                 uint8_t addr, uint8_t data);

/**
 * @brief  Read an 8-bit value from an ADS7066 register.
 *         Requires two SPI frames (read command + NOOP to clock out data).
 */
ADS7066_Status ADS7066_ReadReg(ADS7066_Handle *handle,
                                uint8_t addr, uint8_t *data_out);

/**
 * @brief  Select an analog input channel for manual mode conversion.
 * @param  channel  Channel number (0–7)
 */
ADS7066_Status ADS7066_SelectChannel(ADS7066_Handle *handle, uint8_t channel);

/**
 * @brief  Trigger a conversion and read the 16-bit result.
 *
 *         The ADS7066 starts a conversion on the CS rising edge.  This
 *         function sends a NOOP frame (which triggers conversion on CS rise),
 *         waits for tCONV (~3.2 µs), then reads the 16-bit result.
 *
 * @param  handle      ADS7066 handle
 * @param  result_out  Pointer to store the 16-bit ADC code
 */
ADS7066_Status ADS7066_ReadConversion(ADS7066_Handle *handle,
                                       uint16_t *result_out);

/**
 * @brief  Select a channel, trigger conversion, and read the result.
 *         Convenience wrapper combining SelectChannel + ReadConversion
 *         (with the 2-cycle pipeline latency accounted for).
 *
 *         NOTE: Due to the ADS7066 pipeline, the first call after a channel
 *         switch returns the PREVIOUS channel's data.  This function handles
 *         that by performing two conversion cycles when needed.
 *
 * @param  handle      ADS7066 handle
 * @param  channel     Channel number (0–7)
 * @param  result_out  Pointer to store the 16-bit ADC code
 */
ADS7066_Status ADS7066_ReadChannel(ADS7066_Handle *handle,
                                    uint8_t channel, uint16_t *result_out);

/**
 * @brief  Software reset the ADS7066.  All registers return to defaults.
 *         The device needs ~5 ms to complete reset.
 */
ADS7066_Status ADS7066_SoftReset(ADS7066_Handle *handle);

/**
 * @brief  Enable the internal 2.5V reference.
 */
ADS7066_Status ADS7066_EnableInternalRef(ADS7066_Handle *handle);

/**
 * @brief  Trigger ADC offset calibration.  CAL bit auto-clears when done.
 */
ADS7066_Status ADS7066_Calibrate(ADS7066_Handle *handle);

#ifdef __cplusplus
}
#endif

#endif /* ADS7066_H */
