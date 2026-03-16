/*******************************************************************************
 * @file    Src/usb2517.c
 * @author  Cam
 * @brief   USB2517I USB Hub Controller — SMBus Configuration Driver
 *
 *          The USB2517I in SMBus slave mode (CFG_SEL[2:1:0] = 0,0,1) waits
 *          indefinitely for the MCU to write configuration registers and
 *          issue USB_ATTACH before connecting to the upstream USB host.
 *
 *          IMPORTANT: In SMBus mode, all registers POR to 0x00 — NOT to the
 *          internal defaults shown in Table 7-1.  Every register that needs
 *          a non-zero value MUST be explicitly written.
 *
 *          SMBus Write Block protocol:
 *            [START] [slave_addr+W] [reg_addr] [byte_count] [data...] [STOP]
 *          For single-byte registers: byte_count = 1.
 *
 *          Reference: USB2517/USB2517I datasheet (DS00001598C)
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "usb2517.h"

/* ==========================================================================
 *  DEFAULT CONFIGURATION
 *
 *  These match the USB2517I "Internal Default ROM" column from Table 7-1.
 *  In SMBus mode the POR values are all 0x00, so we must write every
 *  register that needs a non-zero value.
 *
 *  Registers with internal default = 0x00 are omitted (writes would be
 *  redundant since SMBus POR is already 0x00).
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

    /* Config Data Byte 1: 0x9B
     *   Bit 7:   Self-powered = 1
     *   Bit 4:   High-speed capable = 1
     *   Bit 3:   MTT enable = 1
     *   Bit 1:   Individual port power switching = 1
     *   Bit 0:   Individual overcurrent sensing = 1 */
    { USB2517_REG_CFG1,         0x9B },

    /* Config Data Byte 2: 0x20
     *   Compound device = 0, OC timer = default */
    { USB2517_REG_CFG2,         0x20 },

    /* Config Data Byte 3: 0x00 — string support disabled, port indicator off
     * (SMBus POR is already 0x00, but write explicitly for clarity) */
    { USB2517_REG_CFG3,         0x00 },

    /* Max Power Self: 0x01 (2 mA units → 2 mA hub controller consumption) */
    { USB2517_REG_MAXPS,        0x01 },

    /* Max Power Bus: 0x32 (2 mA units → 100 mA, required for bus-powered) */
    { USB2517_REG_MAXPB,        0x32 },

    /* Hub Controller Max Current Self: 0x01 (2 mA units → 2 mA) */
    { USB2517_REG_HCMCS,        0x01 },

    /* Hub Controller Max Current Bus: 0x32 (2 mA units → 100 mA) */
    { USB2517_REG_HCMCB,        0x32 },

    /* Power-on Time: 0x32 (2 ms units → 100 ms port power stabilization) */
    { USB2517_REG_PWRT,         0x32 },
};

#define USB2517_NUM_DEFAULTS \
    (sizeof(usb2517_defaults) / sizeof(usb2517_defaults[0]))

/* ==========================================================================
 *  PRIVATE: Write a single register via SMBus Write Block
 *
 *  SMBus Write Block format (Figure 7-1 in datasheet):
 *    [START] [0x2C+W] [reg_addr] [byte_count=1] [value] [STOP]
 *
 *  Implemented using I2C_Driver_WriteReg with a 2-byte payload:
 *    byte 0 = count (always 1 for single-register writes)
 *    byte 1 = register value
 * ========================================================================== */
static InitResult USB2517_WriteReg(I2C_Handle *i2c, uint8_t reg, uint8_t val)
{
    uint8_t data[2] = { 0x01, val };
    return I2C_Driver_WriteReg(i2c, USB2517_I2C_ADDR, reg, data, 2);
}

/* ==========================================================================
 *  USB2517_SetStrapPins
 *
 *  Drives CFG_SEL1 (PG1) and CFG_SEL2 (PG0) LOW to select SMBus
 *  configuration mode.  CFG_SEL0 is the SCL line which idles HIGH
 *  via pull-ups, giving CFG_SEL[2:1:0] = 0,0,1 = SMBus slave mode.
 *
 *  Call this as early as possible in the boot sequence — ideally
 *  before or immediately after the hub exits power-on reset.
 * ========================================================================== */
void USB2517_SetStrapPins(void)
{
    /* Initialize pins as push-pull outputs */
    Pin_Init(&usb2517_cfg_sel1_pin);
    Pin_Init(&usb2517_cfg_sel2_pin);

    /* Drive BOTH low for SMBus mode: CFG_SEL1 = 0, CFG_SEL2 = 0 */
    LL_GPIO_ResetOutputPin(usb2517_cfg_sel1_pin.port, usb2517_cfg_sel1_pin.pin);   /* PG1 LOW */
    LL_GPIO_ResetOutputPin(usb2517_cfg_sel2_pin.port, usb2517_cfg_sel2_pin.pin);   /* PG0 LOW */
}

/* ==========================================================================
 *  USB2517_Init
 *
 *  Writes all default configuration registers via SMBus, then sends
 *  USB_ATTACH.  After USB_ATTACH, the hub connects to the upstream
 *  USB host and downstream devices (FT231, etc.) become visible.
 *
 *  Timing (from datasheet Table 7-8, SMBus mode):
 *    t2 = 500 µs hub recovery after RESET_N
 *    t3 = SMBus code load (host-determined, no max for self-powered)
 *    t4 = 100 ms USB attach
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

        /* Small delay between writes — SMBus spec compliance */
        for (volatile uint32_t d = 0; d < 1000; d++) { __NOP(); }
    }

    /* Send USB_ATTACH command: register 0xFF, bit 0 = 1
     * This causes the hub to attach to the upstream USB host.
     * After this write, the SMBus registers become write-protected. */
    result = USB2517_WriteReg(i2c, USB2517_REG_STCD, 0x01);

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
