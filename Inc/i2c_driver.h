/*******************************************************************************
 * @file    Inc/i2c_driver.h
 * @author  Cam
 * @brief   I2C Driver — Init, Polled Write, Polled Read
 *
 *          Generic I2C master driver using LL.  Operates on I2C_Handle
 *          pointers so the same code works with any I2C peripheral.
 *          All hardware-specific values come from bsp.c.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "bsp.h"

/* Timeout for polled operations (in SysTick ms) */
#define I2C_TIMEOUT_MS      100U

/* =========================== Public API =================================== */

/**
 * @brief  Initialise I2C peripheral and GPIO pins.
 * @param  handle  Pointer to an I2C_Handle
 * @return INIT_OK on success
 */
InitResult I2C_Driver_Init(I2C_Handle *handle);

/**
 * @brief  Write data to an I2C slave device.
 * @param  handle   I2C handle
 * @param  addr     7-bit slave address (not shifted)
 * @param  data     Pointer to data to write
 * @param  len      Number of bytes to write
 * @return INIT_OK on success, INIT_ERR_I2C on failure/timeout
 */
InitResult I2C_Driver_Write(I2C_Handle *handle,
                            uint8_t addr,
                            const uint8_t *data, uint16_t len);

/**
 * @brief  Write to a specific register on an I2C slave.
 *         Sends [reg_addr] [data...] in a single I2C transaction.
 * @param  handle    I2C handle
 * @param  addr      7-bit slave address (not shifted)
 * @param  reg_addr  Register address byte
 * @param  data      Pointer to data to write after register address
 * @param  len       Number of data bytes (not counting reg_addr)
 * @return INIT_OK on success, INIT_ERR_I2C on failure/timeout
 */
InitResult I2C_Driver_WriteReg(I2C_Handle *handle,
                               uint8_t addr, uint8_t reg_addr,
                               const uint8_t *data, uint16_t len);

/**
 * @brief  Read data from an I2C slave device.
 * @param  handle   I2C handle
 * @param  addr     7-bit slave address (not shifted)
 * @param  data     Buffer to receive data
 * @param  len      Number of bytes to read
 * @return INIT_OK on success, INIT_ERR_I2C on failure/timeout
 */
InitResult I2C_Driver_Read(I2C_Handle *handle,
                           uint8_t addr,
                           uint8_t *data, uint16_t len);

/**
 * @brief  Read from a specific register on an I2C slave.
 *         Sends [reg_addr] then restarts and reads [len] bytes.
 * @param  handle    I2C handle
 * @param  addr      7-bit slave address (not shifted)
 * @param  reg_addr  Register address byte
 * @param  data      Buffer to receive data
 * @param  len       Number of bytes to read
 * @return INIT_OK on success, INIT_ERR_I2C on failure/timeout
 */
InitResult I2C_Driver_ReadReg(I2C_Handle *handle,
                              uint8_t addr, uint8_t reg_addr,
                              uint8_t *data, uint16_t len);

/**
 * @brief  Check if a slave device is present on the bus.
 * @param  handle  I2C handle
 * @param  addr    7-bit slave address (not shifted)
 * @return INIT_OK if device ACKs, INIT_ERR_I2C if NACK or timeout
 */
InitResult I2C_Driver_IsDeviceReady(I2C_Handle *handle, uint8_t addr);

#ifdef __cplusplus
}
#endif

#endif /* I2C_DRIVER_H */
