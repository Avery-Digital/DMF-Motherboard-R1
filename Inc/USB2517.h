/*******************************************************************************
 * @file    Inc/usb2517.h
 * @author  Cam
 * @brief   USB2517I USB Hub Controller — I2C Configuration Driver
 *
 *          The USB2517I requires configuration over SMBus/I2C before it
 *          will attach to the upstream USB host.  This driver writes the
 *          default register configuration and sends the USB_ATTACH command.
 *
 *          Call USB2517_Init() early in your boot sequence, after I2C
 *          is initialised and before you expect USB enumeration.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef USB2517_H
#define USB2517_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "i2c_driver.h"

/* ========================= Device Constants =============================== */

#define USB2517_I2C_ADDR        0x2CU   /**< Default 7-bit SMBus address     */

/* ======================== Register Addresses =============================== */

#define USB2517_REG_VID_LSB     0x00U   /**< Vendor ID LSB (default 0x24)    */
#define USB2517_REG_VID_MSB     0x01U   /**< Vendor ID MSB (default 0x04)    */
#define USB2517_REG_PID_LSB     0x02U   /**< Product ID LSB (default 0x17)   */
#define USB2517_REG_PID_MSB     0x03U   /**< Product ID MSB (default 0x25)   */
#define USB2517_REG_DID_LSB     0x04U   /**< Device ID LSB                   */
#define USB2517_REG_DID_MSB     0x05U   /**< Device ID MSB                   */
#define USB2517_REG_HUB_CFG1    0x06U   /**< Hub Configuration 1             */
#define USB2517_REG_HUB_CFG2    0x07U   /**< Hub Configuration 2             */
#define USB2517_REG_HUB_CFG3    0x08U   /**< Hub Configuration 3             */
#define USB2517_REG_PORT_SWAP   0x30U   /**< Port Swap                       */
#define USB2517_REG_PORT_DIS    0x31U   /**< Port Disable                    */
#define USB2517_REG_USB_ATTACH  0xFFU   /**< USB Attach command register     */

/* ========================== Public API ==================================== */

/**
 * @brief  Assert CFG_SEL strapping pins for SMBus mode.
 *         Call this as early as possible — ideally before USB2517 exits
 *         power-on reset.  Drives CFG_SEL1 and CFG_SEL2 low.
 */
void USB2517_SetStrapPins(void);

/**
 * @brief  Initialise the USB2517I hub via I2C.
 *
 *         Writes default configuration registers and sends the USB_ATTACH
 *         command.  After this call, the hub will enumerate on the upstream
 *         USB host and the FT231 (or other downstream devices) will appear.
 *
 * @param  i2c     I2C handle connected to the hub's SMBus pins
 * @return INIT_OK on success, INIT_ERR_I2C if communication fails
 */
InitResult USB2517_Init(I2C_Handle *i2c);

/**
 * @brief  Check if the USB2517I is present on the I2C bus.
 * @param  i2c     I2C handle
 * @return INIT_OK if device ACKs at address 0x2C
 */
InitResult USB2517_IsPresent(I2C_Handle *i2c);

#ifdef __cplusplus
}
#endif

#endif /* USB2517_H */
