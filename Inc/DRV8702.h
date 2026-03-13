/*******************************************************************************
 * @file    Inc/DRV8702.h
 * @author  Cam
 * @brief   DRV8702DQRHBRQ1 H-Bridge Motor Driver — Register Map and Public API
 *
 *          Used to drive a TEC (Thermoelectric Cooler) via H-bridge polarity
 *          switching.  Three identical circuits share SPI2; each has a unique
 *          nSCS chip-select pin.
 *
 *          GPIO control (direction, enable, sleep) works independently of SPI
 *          and is safe to use immediately after DRV8702_Init().
 *
 *          SPI register access provides:
 *            - Fault and status readback (IC_STAT, VGS_STAT)
 *            - Latched fault clearing (CLR_FLT in IC_CTRL)
 *            - Optional configuration of gate drive strength and OCP thresholds
 *
 * !! REGISTER MAP NOTICE !!
 *          All register addresses and bit field definitions below are derived
 *          from TI DRV8702D product documentation.  Verify every address and
 *          bit mask against the DRV8702D datasheet (SLVSDF9 or later) before
 *          using SPI register access in production firmware.
 *
 * SPI SHARING NOTICE:
 *          SPI2 is shared with the LTC2338-18 ADC (32-bit, Mode 0).  The
 *          DRV8702D uses 16-bit SPI frames (Mode 0 assumed — verify against
 *          the DRV8702D SPI timing diagram in the datasheet).
 *          DRV8702_WriteReg() and DRV8702_ReadReg() temporarily reconfigure
 *          SPI2 data width and restore it on exit.  These functions must not
 *          be called concurrently with SPI_LTC2338_Read().
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef DRV8702_H
#define DRV8702_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bsp.h"

/* ==========================================================================
 *  SPI REGISTER ADDRESSES
 *  !! Verify all addresses against DRV8702D datasheet Table X (SLVSDF9) !!
 * ========================================================================== */
#define DRV8702_REG_IC_STAT         0x00U   /* IC Fault Status   (read only)    */
#define DRV8702_REG_VGS_STAT        0x01U   /* VGS Fault Status  (read only)    */
#define DRV8702_REG_IC_CTRL         0x02U   /* IC Control        (read / write) */
#define DRV8702_REG_DRIVE_CTRL      0x03U   /* Drive Control     (read / write) */
#define DRV8702_REG_GATE_HS         0x04U   /* Gate Drive HS     (read / write) */
#define DRV8702_REG_GATE_LS         0x05U   /* Gate Drive LS     (read / write) */
#define DRV8702_REG_OCP_CTRL        0x06U   /* OCP Control       (read / write) */
#define DRV8702_REG_CSA_CTRL        0x07U   /* CSA Control       (read / write) */

/* ==========================================================================
 *  SPI FRAME FORMAT  (16-bit, MSB first, Mode 0)
 *
 *  Bit  [15]:    R/W  — 0 = write,  1 = read
 *  Bits [14:11]: ADDR — 4-bit register address
 *  Bits [10:0]:  DATA — 11-bit register data field
 * ========================================================================== */
#define DRV8702_SPI_WRITE           (0U << 15)
#define DRV8702_SPI_READ            (1U << 15)
#define DRV8702_SPI_ADDR(a)         (((uint16_t)(a) & 0x0FU) << 11)
#define DRV8702_SPI_DATA(d)         ((uint16_t)(d)  & 0x07FFU)
#define DRV8702_SPI_BUILD_WR(a, d)  (DRV8702_SPI_WRITE | DRV8702_SPI_ADDR(a) | DRV8702_SPI_DATA(d))
#define DRV8702_SPI_BUILD_RD(a)     (DRV8702_SPI_READ  | DRV8702_SPI_ADDR(a))

/* ==========================================================================
 *  IC_STAT (0x00) BIT FLAGS
 *  !! Verify bit positions against DRV8702D datasheet !!
 * ========================================================================== */
#define DRV8702_STAT_FAULT          (1U << 10)  /* Any fault is latched         */
#define DRV8702_STAT_OCP            (1U << 9)   /* Overcurrent protection        */
#define DRV8702_STAT_OTSD           (1U << 8)   /* Overtemperature shutdown      */
#define DRV8702_STAT_UVLO           (1U << 7)   /* Undervoltage lockout          */
#define DRV8702_STAT_OTW            (1U << 6)   /* Overtemperature warning       */

/* ==========================================================================
 *  IC_CTRL (0x02) BIT FLAGS
 *  !! Verify bit positions against DRV8702D datasheet !!
 * ========================================================================== */
#define DRV8702_CTRL_CLR_FLT        (1U << 0)   /* Write 1 to clear fault latches */

/* ==========================================================================
 *  TIMEOUT
 * ========================================================================== */
#define DRV8702_SPI_TIMEOUT_MS      5U

/* ==========================================================================
 *  ENUMERATIONS
 * ========================================================================== */

/** Return codes for all DRV8702 API functions. */
typedef enum {
    DRV8702_OK           = 0,
    DRV8702_ERR_NOT_INIT = 1,   /* Handle used before DRV8702_Init()          */
    DRV8702_ERR_FAULT    = 2,   /* nFAULT asserted, operation refused          */
    DRV8702_ERR_TIMEOUT  = 3,   /* SPI transfer timed out                      */
    DRV8702_ERR_SPI      = 4,   /* SPI overrun or other bus error              */
} DRV8702_Status;

/**
 * @brief  Hardware control mode selected by the MODE pin.
 *
 *         PH/EN mode is recommended for TEC control.  IN1 (PH) sets
 *         current direction; IN2 (EN) enables the bridge and can accept
 *         a PWM signal for power level control.
 */
typedef enum {
    DRV8702_MODE_PHEN = 0,  /* MODE pin LOW  — IN1 = PH direction, IN2 = EN */
    DRV8702_MODE_PWM  = 1,  /* MODE pin HIGH — IN1 and IN2 are PWM inputs   */
} DRV8702_ControlMode;

/**
 * @brief  H-bridge current direction.
 *
 *         FORWARD maps to PH HIGH, REVERSE maps to PH LOW.
 *         Which direction heats vs cools the TEC depends on PCB wiring —
 *         verify against your schematic before use in production.
 */
typedef enum {
    DRV8702_DIR_FORWARD = 0,    /* PH HIGH */
    DRV8702_DIR_REVERSE = 1,    /* PH LOW  */
} DRV8702_Direction;

/* ==========================================================================
 *  PUBLIC API
 * ========================================================================== */

/**
 * @brief  Initialise all GPIO pins and set safe default states:
 *           - nSLEEP LOW  (device in sleep)
 *           - EN LOW      (bridge outputs disabled)
 *           - PH LOW      (forward direction pre-selected)
 *           - MODE LOW    (PH/EN control mode)
 *           - nSCS HIGH   (chip select deasserted)
 *
 *         Call DRV8702_Wake() after init to bring the device out of sleep.
 */
DRV8702_Status DRV8702_Init(DRV8702_Handle *handle);

/** Assert nSLEEP HIGH — device active, bridge controllable via EN/PH. */
void DRV8702_Wake(DRV8702_Handle *handle);

/** Assert nSLEEP LOW — device enters low-power sleep, bridge disabled. */
void DRV8702_Sleep(DRV8702_Handle *handle);

/** Set control mode via the MODE pin.  Change before asserting EN. */
void DRV8702_SetMode(DRV8702_Handle *handle, DRV8702_ControlMode mode);

/** Set H-bridge direction via the PH pin (PH/EN mode). */
void DRV8702_SetDirection(DRV8702_Handle *handle, DRV8702_Direction dir);

/** Assert EN HIGH — bridge outputs active, current flows in the PH direction. */
void DRV8702_Enable(DRV8702_Handle *handle);

/** Assert EN LOW — bridge outputs disabled, TEC coasts (zero current). */
void DRV8702_Disable(DRV8702_Handle *handle);

/**
 * @brief  Sample the nFAULT pin.
 * @return true if nFAULT is asserted (pin LOW = fault present).
 */
bool DRV8702_IsFaulted(DRV8702_Handle *handle);

/**
 * @brief  Write a DRV8702 SPI register.
 *
 *         Temporarily reconfigures SPI2 from 32-bit to 16-bit, performs
 *         the transfer, then restores 32-bit for the LTC2338 ADC.
 *         Must not be called concurrently with SPI_LTC2338_Read().
 */
DRV8702_Status DRV8702_WriteReg(DRV8702_Handle *handle,
                                 uint8_t addr, uint16_t data);

/**
 * @brief  Read a DRV8702 SPI register.
 *         Same SPI sharing constraints as DRV8702_WriteReg().
 */
DRV8702_Status DRV8702_ReadReg(DRV8702_Handle *handle,
                                uint8_t addr, uint16_t *data_out);

/** Read IC_STAT and cache the result in handle->last_fault_reg. */
DRV8702_Status DRV8702_ReadFaultStatus(DRV8702_Handle *handle);

/** Write CLR_FLT to IC_CTRL to clear all latched fault flags. */
DRV8702_Status DRV8702_ClearFaults(DRV8702_Handle *handle);

/* --------------------------------------------------------------------------
 *  TEC Convenience Functions
 *
 *  These wrap SetDirection + Enable / Disable for common TEC operations.
 *  Assumes PH/EN mode.  Verify TEC polarity on the PCB before use.
 * -------------------------------------------------------------------------- */

/** Enable bridge, forward polarity (heat — verify on PCB). */
DRV8702_Status DRV8702_TEC_Heat(DRV8702_Handle *handle);

/** Enable bridge, reverse polarity (cool — verify on PCB). */
DRV8702_Status DRV8702_TEC_Cool(DRV8702_Handle *handle);

/** Disable bridge — TEC current drops to zero. */
DRV8702_Status DRV8702_TEC_Stop(DRV8702_Handle *handle);

#ifdef __cplusplus
}
#endif

#endif /* DRV8702_H */
