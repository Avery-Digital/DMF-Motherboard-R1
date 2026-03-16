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
 *
 *          Reference: USB2517/USB2517I datasheet (DS00001598C)
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

#define USB2517_I2C_ADDR        0x2CU   /**< 7-bit SMBus slave address       */

/* ======================== Register Addresses =============================== */
/* Datasheet Table 7-1: Internal Default, EEPROM and SMBus Register Memory Map */

#define USB2517_REG_VID_LSB     0x00U   /**< Vendor ID LSB   (default 0x24)  */
#define USB2517_REG_VID_MSB     0x01U   /**< Vendor ID MSB   (default 0x04)  */
#define USB2517_REG_PID_LSB     0x02U   /**< Product ID LSB  (default 0x17)  */
#define USB2517_REG_PID_MSB     0x03U   /**< Product ID MSB  (default 0x25)  */
#define USB2517_REG_DID_LSB     0x04U   /**< Device ID LSB   (default 0x00)  */
#define USB2517_REG_DID_MSB     0x05U   /**< Device ID MSB   (default 0x00)  */
#define USB2517_REG_CFG1        0x06U   /**< Config Data Byte 1 (def 0x9B)   */
#define USB2517_REG_CFG2        0x07U   /**< Config Data Byte 2 (def 0x20)   */
#define USB2517_REG_CFG3        0x08U   /**< Config Data Byte 3 (def 0x00)   */
#define USB2517_REG_NRD         0x09U   /**< Non-Removable Devices (def 0x00)*/
#define USB2517_REG_PDS         0x0AU   /**< Port Disable Self  (def 0x00)   */
#define USB2517_REG_PDB         0x0BU   /**< Port Disable Bus   (def 0x00)   */
#define USB2517_REG_MAXPS       0x0CU   /**< Max Power Self     (def 0x01)   */
#define USB2517_REG_MAXPB       0x0DU   /**< Max Power Bus      (def 0x32)   */
#define USB2517_REG_HCMCS       0x0EU   /**< Hub Ctrl Max Current Self (0x01)*/
#define USB2517_REG_HCMCB       0x0FU   /**< Hub Ctrl Max Current Bus (0x32) */
#define USB2517_REG_PWRT        0x10U   /**< Power-on Time     (def 0x32)    */
#define USB2517_REG_BOOST_UP    0xF6U   /**< Boost Upstream     (def 0x00)   */
#define USB2517_REG_BOOST75     0xF7U   /**< Boost Ports 7:5    (def 0x00)   */
#define USB2517_REG_BOOST40     0xF8U   /**< Boost Ports 4:0    (def 0x00)   */
#define USB2517_REG_PORT_SWAP   0xFAU   /**< Port Swap          (def 0x00)   */
#define USB2517_REG_PRTR12      0xFBU   /**< Port Remap 1&2     (def 0x00)   */
#define USB2517_REG_PRTR34      0xFCU   /**< Port Remap 3&4     (def 0x00)   */
#define USB2517_REG_PRTR56      0xFDU   /**< Port Remap 5&6     (def 0x00)   */
#define USB2517_REG_PRTR7       0xFEU   /**< Port Remap 7       (def 0x00)   */
#define USB2517_REG_STCD        0xFFU   /**< Status/Command (USB_ATTACH)     */

/* ========================== Public API ==================================== */

/**
 * @brief  Assert CFG_SEL strapping pins for SMBus mode.
 *         Call this as early as possible — ideally before USB2517 exits
 *         power-on reset.  Drives CFG_SEL1 (PG1) and CFG_SEL2 (PG0) LOW.
 *         CFG_SEL0 = SCL idles HIGH via pull-up.
 *         Result: CFG_SEL[2:1:0] = 0,0,1 → SMBus slave mode.
 */
void USB2517_SetStrapPins(void);

/**
 * @brief  Initialise the USB2517I hub via SMBus.
 *
 *         Writes all configuration registers with their internal default
 *         values (required because SMBus POR values are 0x00), then sends
 *         the USB_ATTACH command.  After this call, the hub will enumerate
 *         on the upstream USB host.
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
