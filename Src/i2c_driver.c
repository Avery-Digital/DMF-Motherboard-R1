/*******************************************************************************
 * @file    Src/i2c_driver.c
 * @author  Cam
 * @brief   I2C Driver — Init, Polled Write, Polled Read
 *
 *          All operations are polled (blocking with timeout).  For high-
 *          throughput or real-time applications, DMA or interrupt-driven
 *          transfers can be added later without changing the API.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "i2c_driver.h"

/* ==========================================================================
 *  PRIVATE HELPERS
 * ========================================================================== */

/**
 * @brief  Simple millisecond timeout check using SysTick.
 *         Returns true if the timeout has expired.
 */
static bool I2C_Timeout(uint32_t start_tick, uint32_t timeout_ms)
{
    /* LL_GetTick() returns the SysTick-based ms counter set up by
     * LL_Init1msTick() in ClockTree_Init(). */
    return ((LL_GetTick() - start_tick) > timeout_ms);
}

/**
 * @brief  Wait for a flag to be set, with timeout.
 * @return true if flag was set, false if timeout expired.
 */
static bool I2C_WaitFlag(I2C_TypeDef *i2c, uint32_t flag, uint32_t timeout_ms)
{
    uint32_t start = LL_GetTick();
    while (!LL_I2C_IsActiveFlag(i2c, flag)) {
        if (I2C_Timeout(start, timeout_ms)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief  Clear all I2C error flags.
 */
static void I2C_ClearErrors(I2C_TypeDef *i2c)
{
    LL_I2C_ClearFlag_BERR(i2c);
    LL_I2C_ClearFlag_ARLO(i2c);
    LL_I2C_ClearFlag_OVR(i2c);
    LL_I2C_ClearFlag_NACK(i2c);
}

/* ==========================================================================
 *  I2C DRIVER — INIT
 * ========================================================================== */
InitResult I2C_Driver_Init(I2C_Handle *handle)
{
    const I2C_Config *cfg = handle->cfg;

    /* ---- GPIO ---- */
    Pin_Init(&cfg->scl_pin);
    Pin_Init(&cfg->sda_pin);

    /* ---- I2C peripheral clock ---- */
    LL_APB1_GRP1_EnableClock(cfg->bus_clk_enable);

    /* ---- Kernel clock source ---- */
    LL_RCC_SetI2CClockSource(cfg->kernel_clk_source);

    /* ---- Disable I2C before configuration ---- */
    LL_I2C_Disable(cfg->peripheral);

    /* ---- Analog and digital filters ---- */
    LL_I2C_EnableAnalogFilter(cfg->peripheral);
    LL_I2C_SetDigitalFilter(cfg->peripheral, cfg->digital_filter);

    /* ---- Timing register (speed) ---- */
    LL_I2C_SetTiming(cfg->peripheral, cfg->timing);

    /* ---- Addressing mode ---- */
    LL_I2C_SetOwnAddress1(cfg->peripheral, cfg->own_address,
                          cfg->addressing_mode);

    /* ---- Master mode defaults ---- */
    LL_I2C_DisableOwnAddress1(cfg->peripheral);  /* Master, no own addr */
    LL_I2C_SetMasterAddressingMode(cfg->peripheral,
                                    LL_I2C_ADDRSLAVE_7BIT);
    LL_I2C_DisableGeneralCall(cfg->peripheral);
    LL_I2C_EnableClockStretching(cfg->peripheral);

    /* ---- Enable I2C ---- */
    LL_I2C_Enable(cfg->peripheral);

    handle->busy  = false;
    handle->error = 0;

    return INIT_OK;
}

/* ==========================================================================
 *  I2C DRIVER — WRITE (raw)
 *
 *  Sends [data[0]] [data[1]] ... [data[len-1]] to the slave.
 * ========================================================================== */
InitResult I2C_Driver_Write(I2C_Handle *handle,
                            uint8_t addr,
                            const uint8_t *data, uint16_t len)
{
    I2C_TypeDef *i2c = handle->cfg->peripheral;
    uint32_t start = LL_GetTick();

    I2C_ClearErrors(i2c);

    /* Configure transfer: slave addr, direction, byte count, autoend */
    LL_I2C_HandleTransfer(i2c,
                          (uint32_t)(addr << 1),
                          LL_I2C_ADDRSLAVE_7BIT,
                          len,
                          LL_I2C_MODE_AUTOEND,
                          LL_I2C_GENERATE_START_WRITE);

    for (uint16_t i = 0; i < len; i++) {
        /* Wait for TXIS (TX register empty) */
        while (!LL_I2C_IsActiveFlag_TXIS(i2c)) {
            if (LL_I2C_IsActiveFlag_NACK(i2c)) {
                LL_I2C_ClearFlag_NACK(i2c);
                return INIT_ERR_I2C;
            }
            if (I2C_Timeout(start, I2C_TIMEOUT_MS)) {
                return INIT_ERR_I2C;
            }
        }
        LL_I2C_TransmitData8(i2c, data[i]);
    }

    /* Wait for STOP condition (autoend) */
    while (!LL_I2C_IsActiveFlag_STOP(i2c)) {
        if (I2C_Timeout(start, I2C_TIMEOUT_MS)) {
            return INIT_ERR_I2C;
        }
    }
    LL_I2C_ClearFlag_STOP(i2c);

    return INIT_OK;
}

/* ==========================================================================
 *  I2C DRIVER — WRITE REGISTER
 *
 *  Sends [reg_addr] [data[0]] ... [data[len-1]] in one transaction.
 *  This is the most common I2C write pattern for register-based devices.
 * ========================================================================== */
InitResult I2C_Driver_WriteReg(I2C_Handle *handle,
                               uint8_t addr, uint8_t reg_addr,
                               const uint8_t *data, uint16_t len)
{
    I2C_TypeDef *i2c = handle->cfg->peripheral;
    uint32_t start = LL_GetTick();

    I2C_ClearErrors(i2c);

    /* Total bytes = 1 (register addr) + len (data) */
    LL_I2C_HandleTransfer(i2c,
                          (uint32_t)(addr << 1),
                          LL_I2C_ADDRSLAVE_7BIT,
                          1 + len,
                          LL_I2C_MODE_AUTOEND,
                          LL_I2C_GENERATE_START_WRITE);

    /* Send register address first */
    while (!LL_I2C_IsActiveFlag_TXIS(i2c)) {
        if (LL_I2C_IsActiveFlag_NACK(i2c)) {
            LL_I2C_ClearFlag_NACK(i2c);
            return INIT_ERR_I2C;
        }
        if (I2C_Timeout(start, I2C_TIMEOUT_MS)) {
            return INIT_ERR_I2C;
        }
    }
    LL_I2C_TransmitData8(i2c, reg_addr);

    /* Send data bytes */
    for (uint16_t i = 0; i < len; i++) {
        while (!LL_I2C_IsActiveFlag_TXIS(i2c)) {
            if (LL_I2C_IsActiveFlag_NACK(i2c)) {
                LL_I2C_ClearFlag_NACK(i2c);
                return INIT_ERR_I2C;
            }
            if (I2C_Timeout(start, I2C_TIMEOUT_MS)) {
                return INIT_ERR_I2C;
            }
        }
        LL_I2C_TransmitData8(i2c, data[i]);
    }

    /* Wait for STOP */
    while (!LL_I2C_IsActiveFlag_STOP(i2c)) {
        if (I2C_Timeout(start, I2C_TIMEOUT_MS)) {
            return INIT_ERR_I2C;
        }
    }
    LL_I2C_ClearFlag_STOP(i2c);

    return INIT_OK;
}

/* ==========================================================================
 *  I2C DRIVER — READ (raw)
 *
 *  Reads [len] bytes from the slave into [data].
 * ========================================================================== */
InitResult I2C_Driver_Read(I2C_Handle *handle,
                           uint8_t addr,
                           uint8_t *data, uint16_t len)
{
    I2C_TypeDef *i2c = handle->cfg->peripheral;
    uint32_t start = LL_GetTick();

    I2C_ClearErrors(i2c);

    LL_I2C_HandleTransfer(i2c,
                          (uint32_t)(addr << 1),
                          LL_I2C_ADDRSLAVE_7BIT,
                          len,
                          LL_I2C_MODE_AUTOEND,
                          LL_I2C_GENERATE_START_READ);

    for (uint16_t i = 0; i < len; i++) {
        while (!LL_I2C_IsActiveFlag_RXNE(i2c)) {
            if (LL_I2C_IsActiveFlag_NACK(i2c)) {
                LL_I2C_ClearFlag_NACK(i2c);
                return INIT_ERR_I2C;
            }
            if (I2C_Timeout(start, I2C_TIMEOUT_MS)) {
                return INIT_ERR_I2C;
            }
        }
        data[i] = LL_I2C_ReceiveData8(i2c);
    }

    /* Wait for STOP */
    while (!LL_I2C_IsActiveFlag_STOP(i2c)) {
        if (I2C_Timeout(start, I2C_TIMEOUT_MS)) {
            return INIT_ERR_I2C;
        }
    }
    LL_I2C_ClearFlag_STOP(i2c);

    return INIT_OK;
}

/* ==========================================================================
 *  I2C DRIVER — READ REGISTER
 *
 *  Write [reg_addr], then restart and read [len] bytes.
 *  This is the standard I2C register read pattern:
 *    START → addr+W → reg_addr → RESTART → addr+R → data... → STOP
 * ========================================================================== */
InitResult I2C_Driver_ReadReg(I2C_Handle *handle,
                              uint8_t addr, uint8_t reg_addr,
                              uint8_t *data, uint16_t len)
{
    I2C_TypeDef *i2c = handle->cfg->peripheral;
    uint32_t start = LL_GetTick();

    I2C_ClearErrors(i2c);

    /* Phase 1: Write the register address (SOFTEND — no STOP, we restart) */
    LL_I2C_HandleTransfer(i2c,
                          (uint32_t)(addr << 1),
                          LL_I2C_ADDRSLAVE_7BIT,
                          1,
                          LL_I2C_MODE_SOFTEND,
                          LL_I2C_GENERATE_START_WRITE);

    while (!LL_I2C_IsActiveFlag_TXIS(i2c)) {
        if (LL_I2C_IsActiveFlag_NACK(i2c)) {
            LL_I2C_ClearFlag_NACK(i2c);
            return INIT_ERR_I2C;
        }
        if (I2C_Timeout(start, I2C_TIMEOUT_MS)) {
            return INIT_ERR_I2C;
        }
    }
    LL_I2C_TransmitData8(i2c, reg_addr);

    /* Wait for transfer complete (TC, not STOP — SOFTEND mode) */
    while (!LL_I2C_IsActiveFlag_TC(i2c)) {
        if (I2C_Timeout(start, I2C_TIMEOUT_MS)) {
            return INIT_ERR_I2C;
        }
    }

    /* Phase 2: Restart and read */
    LL_I2C_HandleTransfer(i2c,
                          (uint32_t)(addr << 1),
                          LL_I2C_ADDRSLAVE_7BIT,
                          len,
                          LL_I2C_MODE_AUTOEND,
                          LL_I2C_GENERATE_START_READ);

    for (uint16_t i = 0; i < len; i++) {
        while (!LL_I2C_IsActiveFlag_RXNE(i2c)) {
            if (LL_I2C_IsActiveFlag_NACK(i2c)) {
                LL_I2C_ClearFlag_NACK(i2c);
                return INIT_ERR_I2C;
            }
            if (I2C_Timeout(start, I2C_TIMEOUT_MS)) {
                return INIT_ERR_I2C;
            }
        }
        data[i] = LL_I2C_ReceiveData8(i2c);
    }

    /* Wait for STOP */
    while (!LL_I2C_IsActiveFlag_STOP(i2c)) {
        if (I2C_Timeout(start, I2C_TIMEOUT_MS)) {
            return INIT_ERR_I2C;
        }
    }
    LL_I2C_ClearFlag_STOP(i2c);

    return INIT_OK;
}

/* ==========================================================================
 *  I2C DRIVER — IS DEVICE READY
 *
 *  Sends a zero-length write to check for ACK from the slave.
 *  Useful for bus scanning and verifying device presence.
 * ========================================================================== */
InitResult I2C_Driver_IsDeviceReady(I2C_Handle *handle, uint8_t addr)
{
    I2C_TypeDef *i2c = handle->cfg->peripheral;
    uint32_t start = LL_GetTick();

    I2C_ClearErrors(i2c);

    LL_I2C_HandleTransfer(i2c,
                          (uint32_t)(addr << 1),
                          LL_I2C_ADDRSLAVE_7BIT,
                          0,
                          LL_I2C_MODE_AUTOEND,
                          LL_I2C_GENERATE_START_WRITE);

    /* Wait for either STOP (ACK) or NACK */
    while (1) {
        if (LL_I2C_IsActiveFlag_STOP(i2c)) {
            LL_I2C_ClearFlag_STOP(i2c);
            return INIT_OK;
        }
        if (LL_I2C_IsActiveFlag_NACK(i2c)) {
            LL_I2C_ClearFlag_NACK(i2c);
            /* Wait for STOP after NACK */
            while (!LL_I2C_IsActiveFlag_STOP(i2c)) {
                if (I2C_Timeout(start, I2C_TIMEOUT_MS)) break;
            }
            LL_I2C_ClearFlag_STOP(i2c);
            return INIT_ERR_I2C;
        }
        if (I2C_Timeout(start, I2C_TIMEOUT_MS)) {
            return INIT_ERR_I2C;
        }
    }
}
