/*******************************************************************************
 * @file    Src/ADS7066.c
 * @author  Cam
 * @brief   ADS7066IRTER 8-Channel 16-bit SAR ADC — Implementation
 *
 *          SPI2 SHARING:
 *            SPI2 is shared with the LTC2338-18 ADC (32-bit Mode 0), DRV8702
 *            TEC drivers (16-bit Mode 0), and DAC80508 DAC (24-bit Mode 1).
 *
 *            The ADS7066 uses 24-bit SPI Mode 0 — matching the SPI2 default
 *            clock polarity/phase.  Only the data width is reconfigured.
 *
 *            For ADC data reads, a 16-bit frame is used to clock out the
 *            conversion result.
 *
 *            Precondition: SPI_Init(&spi2_handle) must be called before any
 *            ADS7066 function.  Do not interleave with other SPI2 device
 *            access — not concurrency-safe.
 *
 *          CONVERSION PIPELINE:
 *            The ADS7066 starts a conversion on the CS rising edge.  The
 *            result from that conversion is available on SDO during the NEXT
 *            CS-low frame.  In manual mode, after a channel switch there is
 *            a 2-cycle latency before the new channel's data appears.
 *
 *          Reference: ADS7066 datasheet (SBAS928C)
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#include "ADS7066.h"
#include "ll_tick.h"

/* ==========================================================================
 *  PRIVATE — SPI 24-BIT TRANSFER (register access)
 *
 *  The ADS7066 uses 24-bit frames in Mode 0 for register read/write.
 *  SPI2 default is 32-bit Mode 0, so we only change the data width.
 *
 *  Sequence:
 *    1. Disable SPI2, change to 24-bit, re-enable.
 *    2. Assert nCS LOW.
 *    3. Flush stale RX.
 *    4. Transmit 24-bit frame, wait for RXP.
 *    5. Read 24-bit response.
 *    6. Deassert nCS HIGH (this triggers a conversion — by design).
 *    7. Disable SPI2, restore 32-bit, re-enable.
 * ========================================================================== */
static ADS7066_Status ADS7066_SPITransfer24(const ADS7066_Config *cfg,
                                             uint32_t tx_word,
                                             uint32_t *rx_word)
{
    SPI_TypeDef *spi = cfg->spi;
    uint32_t t0;

    /* ---- Guard SPI2 against ISR contention ---- */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    /* ---- Reconfigure SPI for 24-bit (Mode 0 unchanged) ---- */
    LL_SPI_Disable(spi);
    LL_SPI_SetDataWidth(spi, LL_SPI_DATAWIDTH_24BIT);
    LL_SPI_SetFIFOThreshold(spi, LL_SPI_FIFO_TH_01DATA);
    LL_SPI_Enable(spi);

    /* ---- Assert nCS (active low) ---- */
    LL_GPIO_ResetOutputPin(cfg->ncs_pin.port, cfg->ncs_pin.pin);

    /* ---- Flush stale RX ---- */
    LL_SPI_ClearFlag_OVR(spi);
    while (LL_SPI_IsActiveFlag_RXP(spi)) {
        (void)LL_SPI_ReceiveData32(spi);
    }

    /* ---- Transmit ---- */
    while (!LL_SPI_IsActiveFlag_TXP(spi));
    LL_SPI_TransmitData32(spi, tx_word);
    LL_SPI_StartMasterTransfer(spi);

    /* ---- Wait for RXP ---- */
    t0 = LL_GetTick();
    while (!LL_SPI_IsActiveFlag_RXP(spi)) {
        if ((LL_GetTick() - t0) >= ADS7066_SPI_TIMEOUT_MS) {
            LL_GPIO_SetOutputPin(cfg->ncs_pin.port, cfg->ncs_pin.pin);
            LL_SPI_Disable(spi);
            LL_SPI_SetDataWidth(spi, LL_SPI_DATAWIDTH_32BIT);
            LL_SPI_SetFIFOThreshold(spi, LL_SPI_FIFO_TH_01DATA);
            LL_SPI_Enable(spi);
            __set_PRIMASK(primask);
            return ADS7066_ERR_TIMEOUT;
        }
    }

    uint32_t rx = LL_SPI_ReceiveData32(spi);
    if (rx_word != NULL) {
        *rx_word = rx & 0x00FFFFFFU;
    }

    /* ---- Deassert nCS (triggers conversion) ---- */
    LL_GPIO_SetOutputPin(cfg->ncs_pin.port, cfg->ncs_pin.pin);

    /* ---- Restore 32-bit ---- */
    LL_SPI_Disable(spi);
    LL_SPI_SetDataWidth(spi, LL_SPI_DATAWIDTH_32BIT);
    LL_SPI_SetFIFOThreshold(spi, LL_SPI_FIFO_TH_01DATA);
    LL_SPI_Enable(spi);

    __set_PRIMASK(primask);
    return ADS7066_OK;
}

/* ==========================================================================
 *  PRIVATE — SPI 16-BIT TRANSFER (ADC data read)
 *
 *  For reading conversion results, only 16 clocks are needed.
 *  The CS rising edge also triggers the next conversion.
 * ========================================================================== */
static ADS7066_Status ADS7066_SPITransfer16(const ADS7066_Config *cfg,
                                             uint16_t *rx_word)
{
    SPI_TypeDef *spi = cfg->spi;
    uint32_t t0;

    /* ---- Guard SPI2 against ISR contention ---- */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    /* ---- Reconfigure SPI for 16-bit (Mode 0 unchanged) ---- */
    LL_SPI_Disable(spi);
    LL_SPI_SetDataWidth(spi, LL_SPI_DATAWIDTH_16BIT);
    LL_SPI_SetFIFOThreshold(spi, LL_SPI_FIFO_TH_01DATA);
    LL_SPI_Enable(spi);

    /* ---- Assert nCS ---- */
    LL_GPIO_ResetOutputPin(cfg->ncs_pin.port, cfg->ncs_pin.pin);

    /* ---- Flush stale RX ---- */
    LL_SPI_ClearFlag_OVR(spi);
    while (LL_SPI_IsActiveFlag_RXP(spi)) {
        (void)LL_SPI_ReceiveData16(spi);
    }

    /* ---- Transmit dummy to clock out data ---- */
    while (!LL_SPI_IsActiveFlag_TXP(spi));
    LL_SPI_TransmitData16(spi, 0x0000U);
    LL_SPI_StartMasterTransfer(spi);

    /* ---- Wait for RXP ---- */
    t0 = LL_GetTick();
    while (!LL_SPI_IsActiveFlag_RXP(spi)) {
        if ((LL_GetTick() - t0) >= ADS7066_SPI_TIMEOUT_MS) {
            LL_GPIO_SetOutputPin(cfg->ncs_pin.port, cfg->ncs_pin.pin);
            LL_SPI_Disable(spi);
            LL_SPI_SetDataWidth(spi, LL_SPI_DATAWIDTH_32BIT);
            LL_SPI_SetFIFOThreshold(spi, LL_SPI_FIFO_TH_01DATA);
            LL_SPI_Enable(spi);
            __set_PRIMASK(primask);
            return ADS7066_ERR_TIMEOUT;
        }
    }

    *rx_word = LL_SPI_ReceiveData16(spi);

    /* ---- Deassert nCS (triggers next conversion) ---- */
    LL_GPIO_SetOutputPin(cfg->ncs_pin.port, cfg->ncs_pin.pin);

    /* ---- Restore 32-bit ---- */
    LL_SPI_Disable(spi);
    LL_SPI_SetDataWidth(spi, LL_SPI_DATAWIDTH_32BIT);
    LL_SPI_SetFIFOThreshold(spi, LL_SPI_FIFO_TH_01DATA);
    LL_SPI_Enable(spi);

    __set_PRIMASK(primask);

    return ADS7066_OK;
}

/* ==========================================================================
 *  ADS7066_Init
 * ========================================================================== */
ADS7066_Status ADS7066_Init(ADS7066_Handle *handle)
{
    if (handle == NULL || handle->cfg == NULL) {
        return ADS7066_ERR_NOT_INIT;
    }

    const ADS7066_Config *cfg = handle->cfg;

    /* ---- nCS pin initialisation ---- */
    Pin_Init(&cfg->ncs_pin);

    /* ---- nCS HIGH (deasserted) ---- */
    LL_GPIO_SetOutputPin(cfg->ncs_pin.port, cfg->ncs_pin.pin);

    handle->initialised    = true;
    handle->current_channel = 0U;

    /* Clear the BOR flag from power-up */
    ADS7066_Status s = ADS7066_WriteReg(handle,
                                         ADS7066_REG_SYSTEM_STATUS,
                                         ADS7066_STAT_BOR);

    return s;
}

/* ==========================================================================
 *  ADS7066_WriteReg
 * ========================================================================== */
ADS7066_Status ADS7066_WriteReg(ADS7066_Handle *handle,
                                 uint8_t addr, uint8_t data)
{
    if (!handle->initialised) {
        return ADS7066_ERR_NOT_INIT;
    }

    uint32_t frame = ADS7066_SPI_FRAME(ADS7066_OP_WRITE, addr, data);
    return ADS7066_SPITransfer24(handle->cfg, frame, NULL);
}

/* ==========================================================================
 *  ADS7066_ReadReg
 *
 *  Two-frame read: first frame sends read command, second frame clocks
 *  out the 8-bit register data in bits [23:16] of the response.
 * ========================================================================== */
ADS7066_Status ADS7066_ReadReg(ADS7066_Handle *handle,
                                uint8_t addr, uint8_t *data_out)
{
    if (!handle->initialised) {
        return ADS7066_ERR_NOT_INIT;
    }
    if (data_out == NULL) {
        return ADS7066_ERR_PARAM;
    }

    ADS7066_Status status;

    /* Frame 1: Send read command */
    uint32_t frame = ADS7066_SPI_FRAME(ADS7066_OP_READ, addr, 0x00);
    status = ADS7066_SPITransfer24(handle->cfg, frame, NULL);
    if (status != ADS7066_OK) {
        return status;
    }

    /* Frame 2: NOOP to clock out the register data */
    uint32_t rx = 0U;
    status = ADS7066_SPITransfer24(handle->cfg, 0x000000U, &rx);
    if (status != ADS7066_OK) {
        return status;
    }

    /* Register data is in bits [23:16] of the response */
    *data_out = (uint8_t)((rx >> 16) & 0xFFU);

    return ADS7066_OK;
}

/* ==========================================================================
 *  ADS7066_SelectChannel
 * ========================================================================== */
ADS7066_Status ADS7066_SelectChannel(ADS7066_Handle *handle, uint8_t channel)
{
    if (channel > ADS7066_MAX_CHANNEL) {
        return ADS7066_ERR_PARAM;
    }

    ADS7066_Status s = ADS7066_WriteReg(handle,
                                         ADS7066_REG_CHANNEL_SEL,
                                         channel & 0x07U);
    if (s == ADS7066_OK) {
        handle->current_channel = channel;
    }

    return s;
}

/* ==========================================================================
 *  ADS7066_ReadConversion
 *
 *  Reads the 16-bit conversion result from the previous conversion.
 *  The CS rising edge at the end of this call triggers a new conversion.
 *
 *  NOTE: There must be at least tCONV (~3.2 µs) between the CS rising
 *  edge that started the conversion and this read.  At 16 MHz SPI with
 *  24-bit register frames + SPI reconfigure overhead, this is naturally
 *  satisfied.  If calling ReadConversion back-to-back at high speed,
 *  a small delay may be needed.
 * ========================================================================== */
ADS7066_Status ADS7066_ReadConversion(ADS7066_Handle *handle,
                                       uint16_t *result_out)
{
    if (!handle->initialised) {
        return ADS7066_ERR_NOT_INIT;
    }
    if (result_out == NULL) {
        return ADS7066_ERR_PARAM;
    }

    return ADS7066_SPITransfer16(handle->cfg, result_out);
}

/* ==========================================================================
 *  ADS7066_ReadChannel
 *
 *  In manual mode, after writing MANUAL_CHID to select channel Y:
 *    - Cycle N:   register write decoded on CS rise → MUX switches to Y
 *    - Cycle N+1: ADC samples Y, outputs previous channel's data
 *    - Cycle N+2: ADC outputs Y's data
 *
 *  This function handles the pipeline by performing:
 *    1. SelectChannel (if different from current)
 *    2. Dummy read (discard — this is the old channel's data)
 *    3. Real read (this is the new channel's data)
 * ========================================================================== */
ADS7066_Status ADS7066_ReadChannel(ADS7066_Handle *handle,
                                    uint8_t channel, uint16_t *result_out)
{
    if (channel > ADS7066_MAX_CHANNEL) {
        return ADS7066_ERR_PARAM;
    }

    ADS7066_Status status;

    if (handle->current_channel != channel) {
        /* Switch channel */
        status = ADS7066_SelectChannel(handle, channel);
        if (status != ADS7066_OK) {
            return status;
        }

        /* Dummy conversion — result is stale (previous channel) */
        uint16_t dummy;
        status = ADS7066_ReadConversion(handle, &dummy);
        if (status != ADS7066_OK) {
            return status;
        }

        /* Second dummy — the ADC sampled old MUX during first read */
        status = ADS7066_ReadConversion(handle, &dummy);
        if (status != ADS7066_OK) {
            return status;
        }
    }

    /* Read the actual result for the selected channel */
    return ADS7066_ReadConversion(handle, result_out);
}

/* ==========================================================================
 *  ADS7066_SoftReset
 * ========================================================================== */
ADS7066_Status ADS7066_SoftReset(ADS7066_Handle *handle)
{
    ADS7066_Status s = ADS7066_WriteReg(handle,
                                         ADS7066_REG_GENERAL_CFG,
                                         ADS7066_CFG_RST);
    if (s == ADS7066_OK) {
        handle->current_channel = 0U;
    }
    return s;
}

/* ==========================================================================
 *  ADS7066_EnableInternalRef
 * ========================================================================== */
ADS7066_Status ADS7066_EnableInternalRef(ADS7066_Handle *handle)
{
    return ADS7066_WriteReg(handle,
                             ADS7066_REG_GENERAL_CFG,
                             ADS7066_CFG_REF_EN);
}

/* ==========================================================================
 *  ADS7066_Calibrate
 * ========================================================================== */
ADS7066_Status ADS7066_Calibrate(ADS7066_Handle *handle)
{
    return ADS7066_WriteReg(handle,
                             ADS7066_REG_GENERAL_CFG,
                             ADS7066_CFG_CAL);
}
