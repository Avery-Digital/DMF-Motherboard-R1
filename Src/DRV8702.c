/*******************************************************************************
 * @file    Src/DRV8702.c
 * @author  Cam
 * @brief   DRV8702DQRHBRQ1 H-Bridge Motor Driver — Implementation
 *
 *          Direction and enable are controlled via GPIO (PH/EN mode).
 *          SPI provides fault status readback and optional configuration.
 *
 *          SPI2 SHARING:
 *            SPI2 is shared with the LTC2338-18 ADC (spi_driver.c).
 *            The ADC uses 32-bit frames; the DRV8702 uses 16-bit frames.
 *            DRV8702_SPITransfer() disables SPI2, changes data width to
 *            16-bit, performs the transfer, then restores 32-bit.
 *
 *            Precondition: SPI_Init(&spi2_handle) must be called before
 *            any DRV8702 SPI function.  Do not interleave ADC and DRV8702
 *            SPI calls — they are not concurrency-safe.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#include "DRV8702.h"
#include "ll_tick.h"

/* ==========================================================================
 *  PRIVATE — SPI 16-BIT TRANSFER
 *
 *  Assumptions on entry:
 *    - SPI2 is initialised and idle (no transfer in progress).
 *    - No other code will access SPI2 during this function.
 *
 *  Sequence:
 *    1. Disable SPI2, change data width to 16-bit, re-enable.
 *    2. Assert nSCS.
 *    3. Flush stale RX data.
 *    4. Load TX word, start master transfer, wait for RXP.
 *    5. Deassert nSCS.
 *    6. Disable SPI2, restore 32-bit data width, re-enable.
 * ========================================================================== */
static DRV8702_Status DRV8702_SPITransfer(const DRV8702_Config *cfg,
                                           uint16_t tx_word,
                                           uint16_t *rx_word)
{
    SPI_TypeDef *spi = cfg->spi;
    uint32_t t0;

    /* ---- Reconfigure SPI for 16-bit transfers ---- */
    LL_SPI_Disable(spi);
    LL_SPI_SetDataWidth(spi, LL_SPI_DATAWIDTH_16BIT);
    LL_SPI_Enable(spi);

    /* ---- Assert nSCS (active low) ---- */
    LL_GPIO_ResetOutputPin(cfg->ncs_pin.port, cfg->ncs_pin.pin);

    /* ---- Flush any stale RX data left from the previous 32-bit session ---- */
    LL_SPI_ClearFlag_OVR(spi);
    while (LL_SPI_IsActiveFlag_RXP(spi)) {
        (void)LL_SPI_ReceiveData16(spi);
    }

    /* ---- Load TX FIFO and start transfer ---- */
    while (!LL_SPI_IsActiveFlag_TXP(spi));
    LL_SPI_TransmitData16(spi, tx_word);
    LL_SPI_StartMasterTransfer(spi);

    /* ---- Wait for RX frame ---- */
    t0 = LL_GetTick();
    while (!LL_SPI_IsActiveFlag_RXP(spi)) {
        if ((LL_GetTick() - t0) >= DRV8702_SPI_TIMEOUT_MS) {
            LL_GPIO_SetOutputPin(cfg->ncs_pin.port, cfg->ncs_pin.pin);
            LL_SPI_Disable(spi);
            LL_SPI_SetDataWidth(spi, LL_SPI_DATAWIDTH_32BIT);
            LL_SPI_Enable(spi);
            return DRV8702_ERR_TIMEOUT;
        }
    }

    if (rx_word != NULL) {
        *rx_word = LL_SPI_ReceiveData16(spi);
    } else {
        (void)LL_SPI_ReceiveData16(spi);
    }

    /* ---- Deassert nSCS ---- */
    LL_GPIO_SetOutputPin(cfg->ncs_pin.port, cfg->ncs_pin.pin);

    /* ---- Restore 32-bit data width for LTC2338 ADC ---- */
    LL_SPI_Disable(spi);
    LL_SPI_SetDataWidth(spi, LL_SPI_DATAWIDTH_32BIT);
    LL_SPI_Enable(spi);

    return DRV8702_OK;
}

/* ==========================================================================
 *  DRV8702_Init
 * ========================================================================== */
DRV8702_Status DRV8702_Init(DRV8702_Handle *handle)
{
    if (handle == NULL || handle->cfg == NULL) {
        return DRV8702_ERR_NOT_INIT;
    }

    const DRV8702_Config *cfg = handle->cfg;

    /* ---- GPIO pin initialisation ---- */
    Pin_Init(&cfg->ph_pin);
    Pin_Init(&cfg->en_pin);
    Pin_Init(&cfg->nsleep_pin);
    Pin_Init(&cfg->mode_pin);
    Pin_Init(&cfg->ncs_pin);
    Pin_Init(&cfg->nfault_pin);

    /* ---- Safe default output states ---- */

    /* PH LOW — forward direction pre-selected */
    LL_GPIO_ResetOutputPin(cfg->ph_pin.port, cfg->ph_pin.pin);

    /* EN LOW — bridge outputs disabled until explicitly enabled */
    LL_GPIO_ResetOutputPin(cfg->en_pin.port, cfg->en_pin.pin);

    /* MODE LOW — PH/EN control mode */
    LL_GPIO_ResetOutputPin(cfg->mode_pin.port, cfg->mode_pin.pin);

    /* nSCS HIGH — chip select deasserted */
    LL_GPIO_SetOutputPin(cfg->ncs_pin.port, cfg->ncs_pin.pin);

    /* nSLEEP LOW — device in sleep; call DRV8702_Wake() to activate */
    LL_GPIO_ResetOutputPin(cfg->nsleep_pin.port, cfg->nsleep_pin.pin);

    handle->initialised    = true;
    handle->faulted        = false;
    handle->last_fault_reg = 0U;

    return DRV8702_OK;
}

/* ==========================================================================
 *  SLEEP / WAKE
 * ========================================================================== */
void DRV8702_Wake(DRV8702_Handle *handle)
{
    LL_GPIO_SetOutputPin(handle->cfg->nsleep_pin.port,
                         handle->cfg->nsleep_pin.pin);
}

void DRV8702_Sleep(DRV8702_Handle *handle)
{
    /* Disable bridge outputs before sleeping to avoid transients */
    LL_GPIO_ResetOutputPin(handle->cfg->en_pin.port,
                           handle->cfg->en_pin.pin);
    LL_GPIO_ResetOutputPin(handle->cfg->nsleep_pin.port,
                           handle->cfg->nsleep_pin.pin);
}

/* ==========================================================================
 *  MODE CONTROL
 * ========================================================================== */
void DRV8702_SetMode(DRV8702_Handle *handle, DRV8702_ControlMode mode)
{
    if (mode == DRV8702_MODE_PWM) {
        LL_GPIO_SetOutputPin(handle->cfg->mode_pin.port,
                             handle->cfg->mode_pin.pin);
    } else {
        LL_GPIO_ResetOutputPin(handle->cfg->mode_pin.port,
                               handle->cfg->mode_pin.pin);
    }
}

/* ==========================================================================
 *  DIRECTION AND ENABLE  (PH/EN mode)
 * ========================================================================== */
void DRV8702_SetDirection(DRV8702_Handle *handle, DRV8702_Direction dir)
{
    if (dir == DRV8702_DIR_FORWARD) {
        LL_GPIO_SetOutputPin(handle->cfg->ph_pin.port,
                             handle->cfg->ph_pin.pin);
    } else {
        LL_GPIO_ResetOutputPin(handle->cfg->ph_pin.port,
                               handle->cfg->ph_pin.pin);
    }
}

void DRV8702_Enable(DRV8702_Handle *handle)
{
    LL_GPIO_SetOutputPin(handle->cfg->en_pin.port,
                         handle->cfg->en_pin.pin);
}

void DRV8702_Disable(DRV8702_Handle *handle)
{
    LL_GPIO_ResetOutputPin(handle->cfg->en_pin.port,
                           handle->cfg->en_pin.pin);
}

/* ==========================================================================
 *  FAULT MONITORING
 * ========================================================================== */
bool DRV8702_IsFaulted(DRV8702_Handle *handle)
{
    /* nFAULT is open-drain active-low: pin LOW = fault present */
    bool fault = (LL_GPIO_IsInputPinSet(handle->cfg->nfault_pin.port,
                                        handle->cfg->nfault_pin.pin) == 0U);
    handle->faulted = fault;
    return fault;
}

/* ==========================================================================
 *  SPI REGISTER ACCESS
 * ========================================================================== */
DRV8702_Status DRV8702_WriteReg(DRV8702_Handle *handle,
                                 uint8_t addr, uint16_t data)
{
    if (!handle->initialised) {
        return DRV8702_ERR_NOT_INIT;
    }

    return DRV8702_SPITransfer(handle->cfg,
                               DRV8702_SPI_BUILD_WR(addr, data),
                               NULL);
}

DRV8702_Status DRV8702_ReadReg(DRV8702_Handle *handle,
                                uint8_t addr, uint16_t *data_out)
{
    if (!handle->initialised) {
        return DRV8702_ERR_NOT_INIT;
    }

    return DRV8702_SPITransfer(handle->cfg,
                               DRV8702_SPI_BUILD_RD(addr),
                               data_out);
}

DRV8702_Status DRV8702_ReadFaultStatus(DRV8702_Handle *handle)
{
    uint16_t reg = 0U;
    DRV8702_Status s = DRV8702_ReadReg(handle, DRV8702_REG_IC_STAT, &reg);

    if (s == DRV8702_OK) {
        handle->last_fault_reg = reg;
        handle->faulted        = ((reg & DRV8702_STAT_FAULT) != 0U);
    }

    return s;
}

DRV8702_Status DRV8702_ClearFaults(DRV8702_Handle *handle)
{
    DRV8702_Status s = DRV8702_WriteReg(handle,
                                         DRV8702_REG_IC_CTRL,
                                         DRV8702_CTRL_CLR_FLT);
    if (s == DRV8702_OK) {
        handle->faulted        = false;
        handle->last_fault_reg = 0U;
    }

    return s;
}

/* ==========================================================================
 *  TEC CONVENIENCE FUNCTIONS
 * ========================================================================== */
DRV8702_Status DRV8702_TEC_Heat(DRV8702_Handle *handle)
{
    if (!handle->initialised) {
        return DRV8702_ERR_NOT_INIT;
    }

    if (DRV8702_IsFaulted(handle)) {
        return DRV8702_ERR_FAULT;
    }

    /* Reversed: FORWARD was cooling the top plate, REVERSE heats it */
    DRV8702_SetDirection(handle, DRV8702_DIR_REVERSE);
    DRV8702_Enable(handle);

    return DRV8702_OK;
}

DRV8702_Status DRV8702_TEC_Cool(DRV8702_Handle *handle)
{
    if (!handle->initialised) {
        return DRV8702_ERR_NOT_INIT;
    }

    if (DRV8702_IsFaulted(handle)) {
        return DRV8702_ERR_FAULT;
    }

    /* Reversed: REVERSE was heating the top plate, FORWARD cools it */
    DRV8702_SetDirection(handle, DRV8702_DIR_FORWARD);
    DRV8702_Enable(handle);

    return DRV8702_OK;
}

DRV8702_Status DRV8702_TEC_Stop(DRV8702_Handle *handle)
{
    if (!handle->initialised) {
        return DRV8702_ERR_NOT_INIT;
    }

    DRV8702_Disable(handle);

    return DRV8702_OK;
}
