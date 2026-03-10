/*******************************************************************************
 * @file    Src/usb2517.c
 * @author  Cam
 * @brief   USB2517I USB Hub Controller — I2C Configuration Driver
 *
 *          Writes default configuration and issues USB_ATTACH over I2C.
 *
 *          The USB2517I uses SMBus Write Block protocol.  Each register
 *          write is: [reg_addr] [byte_count] [data...].  However, for
 *          single-byte registers the byte count is 1 and we can use the
 *          standard I2C register write pattern.
 *
 *          Reference: USB2517 datasheet, Section 3.0 "SMBus Configuration"
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "usb2517.h"

/* ==========================================================================
 *  DEFAULT CONFIGURATION
 *
 *  These are the USB2517I factory defaults.  Writing them explicitly
 *  ensures the hub is in a known state regardless of previous partial
 *  configurations or brown-out conditions.
 *
 *  Modify these values if you need to change hub behavior (e.g. disable
 *  specific ports, change power switching, etc.)
 * ========================================================================== */

typedef struct {
    uint8_t reg;
    uint8_t val;
} USB2517_RegPair;

static const USB2517_RegPair usb2517_defaults[] = {
    /* Vendor ID: 0x0424 (Microchip/SMSC) */
    { USB2517_REG_VID_LSB,      0x24 },
    { USB2517_REG_VID_MSB,      0x04 },

    /* Product ID: 0x2517 */
    { USB2517_REG_PID_LSB,      0x17 },
    { USB2517_REG_PID_MSB,      0x25 },

    /* Device ID: 0x0000 */
    { USB2517_REG_DID_LSB,      0x00 },
    { USB2517_REG_DID_MSB,      0x00 },

    /* Hub Configuration 1: 0x9B (default)
     *   Bit 7:   Self-powered
     *   Bit 4:   High-speed capable
     *   Bit 3:   MTT enable
     *   Bit 1:   Individual port power switching
     *   Bit 0:   Individual overcurrent sensing */
    { USB2517_REG_HUB_CFG1,     0x9B },

    /* Hub Configuration 2: 0x20 (default)
     *   Compound device = 0, OC timer = default */
    { USB2517_REG_HUB_CFG2,     0x20 },

    /* Hub Configuration 3: 0x02 (default)
     *   String support disabled, port indicator disabled */
    { USB2517_REG_HUB_CFG3,     0x02 },

    /* Port Swap: 0x00 — no port swapping */
    { USB2517_REG_PORT_SWAP,    0x00 },

    /* Port Disable: 0x00 — all ports enabled */
    { USB2517_REG_PORT_DIS,     0x00 },
};

#define USB2517_NUM_DEFAULTS \
    (sizeof(usb2517_defaults) / sizeof(usb2517_defaults[0]))

/* ==========================================================================
 *  PRIVATE: Write a single register via SMBus Write Block
 *
 *  SMBus Write Block format:
 *    [START] [addr+W] [reg] [byte_count=1] [value] [STOP]
 *
 *  This is slightly different from a standard I2C register write because
 *  of the byte_count field.  We send it as a 2-byte data payload using
 *  the raw I2C write with reg_addr.
 * ========================================================================== */
static InitResult USB2517_WriteReg(I2C_Handle *i2c, uint8_t reg, uint8_t val)
{
    /* SMBus block write: [reg_addr] [count=1] [value] */
    uint8_t data[2] = { 0x01, val };

    return I2C_Driver_WriteReg(i2c, USB2517_I2C_ADDR, reg, data, 2);
}

/* ==========================================================================
 *  USB2517_Init
 *
 *  Writes all default configuration registers, then sends USB_ATTACH.
 *  After USB_ATTACH, the hub connects to the upstream USB host and
 *  downstream devices (FT231, etc.) become visible to the PC.
 * ========================================================================== */
InitResult USB2517_Init(I2C_Handle *i2c)
{
    InitResult result;

    /* Write all configuration registers */
    for (uint32_t i = 0; i < USB2517_NUM_DEFAULTS; i++) {
        result = USB2517_WriteReg(i2c,
                                  usb2517_defaults[i].reg,
                                  usb2517_defaults[i].val);
        if (result != INIT_OK) {
            return result;
        }

        /* Small delay between writes — some hubs need this */
        for (volatile uint32_t d = 0; d < 1000; d++) { __NOP(); }
    }

    /* Send USB_ATTACH command: register 0xFF, value 0x01 */
    result = USB2517_WriteReg(i2c, USB2517_REG_USB_ATTACH, 0x01);

    return result;
}

/* ==========================================================================
 *  USB2517_IsPresent
 *
 *  Quick check: send address + W and see if the hub ACKs.
 * ========================================================================== */
InitResult USB2517_IsPresent(I2C_Handle *i2c)
{
    return I2C_Driver_IsDeviceReady(i2c, USB2517_I2C_ADDR);
}
