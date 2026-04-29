/*******************************************************************************
 * @file    Src/DAC80508.c
 * @author  Cam
 * @brief   DAC80508ZRTER 8-Channel 16-bit DAC — Implementation
 *
 *          SPI2 SHARING:
 *            SPI2 is shared with the LTC2338-18 ADC (32-bit, Mode 0) and the
 *            DRV8702 TEC drivers (16-bit, Mode 0).  The DAC80508 uses 24-bit
 *            frames in Mode 1 (CPOL=0, CPHA=1).
 *
 *            DAC80508_SPITransfer() disables SPI2, changes data width to
 *            24-bit and clock phase to Mode 1, performs the transfer, then
 *            restores 32-bit Mode 0.
 *
 *            Precondition: SPI_Init(&spi2_handle) must be called before
 *            any DAC80508 SPI function.  Do not interleave ADC, DRV8702,
 *            and DAC80508 SPI calls — they are not concurrency-safe.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#include "DAC80508.h"
#include "ll_tick.h"

/* ==========================================================================
 *  PRIVATE — SPI 24-BIT TRANSFER (Mode 1)
 *
 *  Sequence:
 *    1. Disable SPI2, change to 24-bit Mode 1 (CPHA=1), re-enable.
 *    2. Assert nCS LOW.
 *    3. Flush stale RX data.
 *    4. Load 24-bit TX word, start master transfer, wait for RXP.
 *    5. Read 24-bit RX word.
 *    6. Deassert nCS HIGH.
 *    7. Disable SPI2, restore 32-bit Mode 0, re-enable.
 * ========================================================================== */
static DAC80508_Status DAC80508_SPITransfer(const DAC80508_Config *cfg,
                                             uint32_t tx_word,
                                             uint32_t *rx_word)
{
    SPI_TypeDef *spi = cfg->spi;
    uint32_t t0;

    /* ---- Guard SPI2 against ISR contention ---- */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    /* ---- Reconfigure SPI for 24-bit Mode 1 ---- */
    LL_SPI_Disable(spi);
    LL_SPI_SetDataWidth(spi, LL_SPI_DATAWIDTH_24BIT);
    LL_SPI_SetClockPhase(spi, LL_SPI_PHASE_2EDGE);    /* CPHA=1 for Mode 1 */
    LL_SPI_SetFIFOThreshold(spi, LL_SPI_FIFO_TH_01DATA);
    LL_SPI_Enable(spi);

    /* ---- Assert nCS (active low) ---- */
    LL_GPIO_ResetOutputPin(cfg->ncs_pin.port, cfg->ncs_pin.pin);

    /* ---- Flush any stale RX data ---- */
    LL_SPI_ClearFlag_OVR(spi);
    while (LL_SPI_IsActiveFlag_RXP(spi)) {
        (void)LL_SPI_ReceiveData32(spi);
    }

    /* ---- Load TX FIFO and start transfer ---- */
    while (!LL_SPI_IsActiveFlag_TXP(spi));
    LL_SPI_TransmitData32(spi, tx_word);
    LL_SPI_StartMasterTransfer(spi);

    /* ---- Wait for RX frame ---- */
    t0 = LL_GetTick();
    while (!LL_SPI_IsActiveFlag_RXP(spi)) {
        if ((LL_GetTick() - t0) >= DAC80508_SPI_TIMEOUT_MS) {
            LL_GPIO_SetOutputPin(cfg->ncs_pin.port, cfg->ncs_pin.pin);
            /* Restore 32-bit Mode 0 */
            LL_SPI_Disable(spi);
            LL_SPI_SetDataWidth(spi, LL_SPI_DATAWIDTH_32BIT);
            LL_SPI_SetClockPhase(spi, LL_SPI_PHASE_1EDGE);
            LL_SPI_SetFIFOThreshold(spi, LL_SPI_FIFO_TH_01DATA);
            LL_SPI_Enable(spi);
            __set_PRIMASK(primask);
            return DAC80508_ERR_TIMEOUT;
        }
    }

    uint32_t rx = LL_SPI_ReceiveData32(spi);

    if (rx_word != NULL) {
        *rx_word = rx & 0x00FFFFFFU;    /* Mask to 24 bits */
    }

    /* ---- Deassert nCS ---- */
    LL_GPIO_SetOutputPin(cfg->ncs_pin.port, cfg->ncs_pin.pin);

    /* ---- Restore 32-bit Mode 0 for LTC2338 ADC ---- */
    LL_SPI_Disable(spi);
    LL_SPI_SetDataWidth(spi, LL_SPI_DATAWIDTH_32BIT);
    LL_SPI_SetClockPhase(spi, LL_SPI_PHASE_1EDGE);     /* CPHA=0 for Mode 0 */
    LL_SPI_SetFIFOThreshold(spi, LL_SPI_FIFO_TH_01DATA);
    LL_SPI_Enable(spi);

    __set_PRIMASK(primask);
    return DAC80508_OK;
}

/* ==========================================================================
 *  DAC80508_Init
 * ========================================================================== */
DAC80508_Status DAC80508_Init(DAC80508_Handle *handle)
{
    if (handle == NULL || handle->cfg == NULL) {
        return DAC80508_ERR_NOT_INIT;
    }

    const DAC80508_Config *cfg = handle->cfg;

    /* ---- nCS pin initialisation ---- */
    Pin_Init(&cfg->ncs_pin);

    /* ---- nCS HIGH (deasserted) ---- */
    LL_GPIO_SetOutputPin(cfg->ncs_pin.port, cfg->ncs_pin.pin);

    handle->initialised = true;

    return DAC80508_OK;
}

/* ==========================================================================
 *  DAC80508_WriteReg
 * ========================================================================== */
DAC80508_Status DAC80508_WriteReg(DAC80508_Handle *handle,
                                   uint8_t addr, uint16_t data)
{
    if (!handle->initialised) {
        return DAC80508_ERR_NOT_INIT;
    }

    uint32_t frame = DAC80508_SPI_BUILD_WR(addr, data);

    return DAC80508_SPITransfer(handle->cfg, frame, NULL);
}

/* ==========================================================================
 *  DAC80508_ReadReg
 *
 *  The DAC80508 returns read data on the SPI frame FOLLOWING the read
 *  command.  So we need two transactions:
 *    1. Send read command (response is don't-care / previous data)
 *    2. Send NOOP, capture the response which contains the requested data
 * ========================================================================== */
DAC80508_Status DAC80508_ReadReg(DAC80508_Handle *handle,
                                  uint8_t addr, uint16_t *data_out)
{
    if (!handle->initialised) {
        return DAC80508_ERR_NOT_INIT;
    }
    if (data_out == NULL) {
        return DAC80508_ERR_PARAM;
    }

    DAC80508_Status status;

    /* Transaction 1: Send read command */
    uint32_t frame = DAC80508_SPI_BUILD_RD(addr);
    status = DAC80508_SPITransfer(handle->cfg, frame, NULL);
    if (status != DAC80508_OK) {
        return status;
    }

    /* Transaction 2: Clock out the response with a NOOP */
    uint32_t rx = 0U;
    status = DAC80508_SPITransfer(handle->cfg, 0x000000U, &rx);
    if (status != DAC80508_OK) {
        return status;
    }

    *data_out = DAC80508_SPI_RX_DATA(rx);

    return DAC80508_OK;
}

/* ==========================================================================
 *  DAC80508_SetChannel
 * ========================================================================== */
DAC80508_Status DAC80508_SetChannel(DAC80508_Handle *handle,
                                     uint8_t channel, uint16_t value)
{
    if (channel > 7U) {
        return DAC80508_ERR_PARAM;
    }

    return DAC80508_WriteReg(handle,
                              DAC80508_REG_DAC0 + channel,
                              value);
}

/* ==========================================================================
 *  DAC80508_SetAll
 * ========================================================================== */
DAC80508_Status DAC80508_SetAll(DAC80508_Handle *handle, uint16_t value)
{
    return DAC80508_WriteReg(handle, DAC80508_REG_BRDCAST, value);
}

/* ==========================================================================
 *  DAC80508_SoftReset
 * ========================================================================== */
DAC80508_Status DAC80508_SoftReset(DAC80508_Handle *handle)
{
    return DAC80508_WriteReg(handle,
                              DAC80508_REG_TRIGGER,
                              DAC80508_TRIG_SOFT_RESET);
}

/* ==========================================================================
 *  DAC80508_ReadDeviceID
 * ========================================================================== */
DAC80508_Status DAC80508_ReadDeviceID(DAC80508_Handle *handle,
                                       uint16_t *id_out)
{
    return DAC80508_ReadReg(handle, DAC80508_REG_DEVICE_ID, id_out);
}
