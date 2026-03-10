/*******************************************************************************
 * @file    Src/usart_driver.c
 * @author  Cam
 * @brief   USART Driver — Init, DMA TX/RX, Protocol Integration
 *
 *          Reusable driver logic that operates on USART_Handle pointers.
 *          All hardware-specific values come from the const config structs
 *          in bsp.c — this file contains no pin numbers, clock values,
 *          or baud rates.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "usart_driver.h"
#include <string.h>

/* Private prototypes --------------------------------------------------------*/
static void DMA_Stream_Init(const DMA_ChannelConfig *dma);

/* ==========================================================================
 *  DMA STREAM INITIALIZATION (internal helper)
 *
 *  Configures a single DMA stream from a DMA_ChannelConfig struct.
 *  The stream is left disabled — the caller enables it when ready.
 * ========================================================================== */
static void DMA_Stream_Init(const DMA_ChannelConfig *dma)
{
    /* Enable DMA peripheral clock */
    LL_AHB1_GRP1_EnableClock(dma->dma_clk_enable);

    /* Make sure the stream is off before configuring */
    LL_DMA_DisableStream(dma->dma, dma->stream);
    while (LL_DMA_IsEnabledStream(dma->dma, dma->stream));

    /* DMAMUX request routing */
    LL_DMA_SetPeriphRequest(dma->dma, dma->stream, dma->request);

    /* Transfer direction, mode, priority */
    LL_DMA_SetDataTransferDirection(dma->dma, dma->stream, dma->direction);
    LL_DMA_SetStreamPriorityLevel(dma->dma, dma->stream, dma->priority);
    LL_DMA_SetMode(dma->dma, dma->stream, dma->mode);

    /* Data alignment */
    LL_DMA_SetPeriphSize(dma->dma, dma->stream, dma->periph_data_align);
    LL_DMA_SetMemorySize(dma->dma, dma->stream, dma->mem_data_align);

    /* Address increment */
    LL_DMA_SetPeriphIncMode(dma->dma, dma->stream, dma->periph_inc);
    LL_DMA_SetMemoryIncMode(dma->dma, dma->stream, dma->mem_inc);

    /* FIFO */
    if (dma->use_fifo) {
        LL_DMA_EnableFifoMode(dma->dma, dma->stream);
        LL_DMA_SetFIFOThreshold(dma->dma, dma->stream, dma->fifo_threshold);
    } else {
        LL_DMA_DisableFifoMode(dma->dma, dma->stream);
    }

    /* NVIC */
    NVIC_SetPriority(dma->irqn,
                     NVIC_EncodePriority(NVIC_GetPriorityGrouping(),
                                         dma->irq_priority, 0));
    NVIC_EnableIRQ(dma->irqn);
}

/* ==========================================================================
 *  USART DRIVER — INIT
 *
 *  Initialises GPIO, USART peripheral, and both DMA streams from the
 *  handle's const config pointers.  Returns a bitmask of any errors.
 * ========================================================================== */
InitResult USART_Driver_Init(USART_Handle *handle)
{
    const USART_Config       *cfg    = handle->cfg;
    const DMA_ChannelConfig  *dma_tx = handle->dma_tx;
    const DMA_ChannelConfig  *dma_rx = handle->dma_rx;

    /* ---- GPIO ---- */
    Pin_Init(&cfg->tx_pin);
    Pin_Init(&cfg->rx_pin);

    /* ---- USART peripheral clock ---- */
    LL_APB2_GRP1_EnableClock(cfg->bus_clk_enable);

    /* ---- Kernel clock source ---- */
    LL_RCC_SetUSARTClockSource(cfg->kernel_clk_source);

    /* ---- USART configuration (peripheral must be disabled) ---- */
    LL_USART_Disable(cfg->peripheral);

    LL_USART_SetPrescaler(cfg->peripheral, cfg->prescaler);
    LL_USART_SetBaudRate(cfg->peripheral,
                         sys_clk_config.apb2_hz,
                         cfg->prescaler,
                         cfg->oversampling,
                         cfg->baudrate);
    LL_USART_SetDataWidth(cfg->peripheral, cfg->data_width);
    LL_USART_SetStopBitsLength(cfg->peripheral, cfg->stop_bits);
    LL_USART_SetParity(cfg->peripheral, cfg->parity);
    LL_USART_SetTransferDirection(cfg->peripheral, cfg->direction);
    LL_USART_SetHWFlowCtrl(cfg->peripheral, cfg->hw_flow_control);
    LL_USART_SetOverSampling(cfg->peripheral, cfg->oversampling);

    /* Disable FIFO, configure async mode */
    LL_USART_DisableFIFO(cfg->peripheral);
    LL_USART_ConfigAsyncMode(cfg->peripheral);

    /* ---- DMA streams ---- */
    DMA_Stream_Init(dma_tx);
    DMA_Stream_Init(dma_rx);

    /* TX DMA: peripheral address = USART TDR */
    LL_DMA_SetPeriphAddress(dma_tx->dma, dma_tx->stream,
                            LL_USART_DMA_GetRegAddr(cfg->peripheral,
                                                     LL_USART_DMA_REG_DATA_TRANSMIT));

    /* RX DMA: peripheral address = USART RDR */
    LL_DMA_SetPeriphAddress(dma_rx->dma, dma_rx->stream,
                            LL_USART_DMA_GetRegAddr(cfg->peripheral,
                                                     LL_USART_DMA_REG_DATA_RECEIVE));

    /* RX DMA: memory address and buffer length (circular, always running) */
    LL_DMA_SetMemoryAddress(dma_rx->dma, dma_rx->stream,
                            (uint32_t)handle->rx_buf);
    LL_DMA_SetDataLength(dma_rx->dma, dma_rx->stream,
                         handle->rx_buf_size);

    /* ---- Enable DMA requests in USART ---- */
    LL_USART_EnableDMAReq_TX(cfg->peripheral);
    LL_USART_EnableDMAReq_RX(cfg->peripheral);

    /* ---- Enable IDLE line interrupt for RX packet detection ---- */
    LL_USART_EnableIT_IDLE(cfg->peripheral);

    /* ---- USART NVIC ---- */
    NVIC_SetPriority(cfg->irqn,
                     NVIC_EncodePriority(NVIC_GetPriorityGrouping(),
                                         cfg->irq_priority, 0));
    NVIC_EnableIRQ(cfg->irqn);

    /* ---- Enable USART ---- */
    LL_USART_Enable(cfg->peripheral);

    /* Wait for USART ready flags (TEACK and REACK) */
    while (!LL_USART_IsActiveFlag_TEACK(cfg->peripheral));
    while (!LL_USART_IsActiveFlag_REACK(cfg->peripheral));

    /* Initialise RX tracking position */
    handle->rx_head = 0;

    return INIT_OK;
}

/* ==========================================================================
 *  USART DRIVER — START RX (DMA circular)
 *
 *  Enables the RX DMA stream.  Call once after USART_Driver_Init().
 * ========================================================================== */
void USART_Driver_StartRx(USART_Handle *handle)
{
    const DMA_ChannelConfig *rx = handle->dma_rx;

    /* Enable transfer-complete and half-transfer interrupts for the ring */
    LL_DMA_EnableIT_TC(rx->dma, rx->stream);
    LL_DMA_EnableIT_HT(rx->dma, rx->stream);

    /* Start the stream */
    LL_DMA_EnableStream(rx->dma, rx->stream);
}

/* ==========================================================================
 *  USART DRIVER — POLL RX
 *
 *  Compares the DMA NDTR (remaining count) to the last known read
 *  position to determine how many new bytes have arrived.  Handles
 *  ring buffer wrap-around.  Feeds new bytes into the protocol parser.
 *
 *  Call this from the main loop — it's non-blocking.
 * ========================================================================== */
void USART_Driver_PollRx(USART_Handle *handle, ProtocolParser *parser)
{
    const DMA_ChannelConfig *rx = handle->dma_rx;

    /* DMA NDTR counts DOWN from rx_buf_size.  Current write position
     * is (buf_size - NDTR).  When NDTR reaches 0 in circular mode,
     * it reloads to buf_size and wraps. */
    uint16_t dma_write_pos = handle->rx_buf_size -
        (uint16_t)LL_DMA_GetDataLength(rx->dma, rx->stream);

    uint16_t read_pos = handle->rx_head;

    if (dma_write_pos == read_pos) {
        /* No new data */
        return;
    }

    if (dma_write_pos > read_pos) {
        /* Linear — no wrap */
        Protocol_FeedBytes(parser,
                           &handle->rx_buf[read_pos],
                           dma_write_pos - read_pos);
    } else {
        /* Wrapped — process end of buffer, then beginning */
        Protocol_FeedBytes(parser,
                           &handle->rx_buf[read_pos],
                           handle->rx_buf_size - read_pos);

        if (dma_write_pos > 0) {
            Protocol_FeedBytes(parser,
                               &handle->rx_buf[0],
                               dma_write_pos);
        }
    }

    /* Update read position */
    handle->rx_head = dma_write_pos;
}

/* ==========================================================================
 *  USART DRIVER — SEND PACKET (protocol-aware DMA TX)
 *
 *  Builds a framed packet (SOF, byte-stuffing, CRC, EOF) directly into
 *  the DMA TX buffer, then fires the DMA stream.
 * ========================================================================== */
InitResult USART_Driver_SendPacket(USART_Handle *handle,
                                   uint8_t msg1, uint8_t msg2,
                                   uint8_t cmd1, uint8_t cmd2,
                                   const uint8_t *payload, uint16_t length)
{
    const DMA_ChannelConfig *tx = handle->dma_tx;

    /* Guard: previous TX still running */
    if (handle->tx_busy) {
        return INIT_ERR_DMA;
    }

    /* Build framed packet directly into DMA-accessible TX buffer */
    uint16_t frame_len = Protocol_BuildPacket(
        handle->tx_buf,
        msg1, msg2, cmd1, cmd2,
        payload, length
    );

    /* Check that the frame fits in our buffer */
    if (frame_len > handle->tx_buf_size) {
        return INIT_ERR_DMA;
    }

    handle->tx_len  = frame_len;
    handle->tx_busy = true;

    /* Configure DMA stream */
    LL_DMA_DisableStream(tx->dma, tx->stream);
    while (LL_DMA_IsEnabledStream(tx->dma, tx->stream));

    LL_DMA_SetMemoryAddress(tx->dma, tx->stream, (uint32_t)handle->tx_buf);
    LL_DMA_SetDataLength(tx->dma, tx->stream, frame_len);

    /* Clear pending DMA flags */
    LL_DMA_ClearFlag_TC0(tx->dma);
    LL_DMA_ClearFlag_HT0(tx->dma);
    LL_DMA_ClearFlag_TE0(tx->dma);

    /* Enable transfer-complete and error interrupts */
    LL_DMA_EnableIT_TC(tx->dma, tx->stream);
    LL_DMA_EnableIT_TE(tx->dma, tx->stream);

    /* Fire */
    LL_DMA_EnableStream(tx->dma, tx->stream);

    return INIT_OK;
}

/* ==========================================================================
 *  USART DRIVER — RAW TRANSMIT (no protocol framing)
 *
 *  Copies data into the TX DMA buffer and fires.  Use this for debug
 *  strings or raw byte output.
 * ========================================================================== */
InitResult USART_Driver_Transmit(USART_Handle *handle,
                                 const uint8_t *data, uint16_t len)
{
    const DMA_ChannelConfig *tx = handle->dma_tx;

    /* Guard: previous TX still running */
    if (handle->tx_busy) {
        return INIT_ERR_DMA;
    }

    /* Clamp to buffer size */
    if (len > handle->tx_buf_size) {
        len = handle->tx_buf_size;
    }

    /* Copy payload into DMA-accessible buffer */
    memcpy(handle->tx_buf, data, len);
    handle->tx_len  = len;
    handle->tx_busy = true;

    /* Configure stream */
    LL_DMA_DisableStream(tx->dma, tx->stream);
    while (LL_DMA_IsEnabledStream(tx->dma, tx->stream));

    LL_DMA_SetMemoryAddress(tx->dma, tx->stream, (uint32_t)handle->tx_buf);
    LL_DMA_SetDataLength(tx->dma, tx->stream, len);

    /* Clear pending DMA flags */
    LL_DMA_ClearFlag_TC0(tx->dma);
    LL_DMA_ClearFlag_HT0(tx->dma);
    LL_DMA_ClearFlag_TE0(tx->dma);

    /* Enable transfer-complete interrupt */
    LL_DMA_EnableIT_TC(tx->dma, tx->stream);

    /* Fire */
    LL_DMA_EnableStream(tx->dma, tx->stream);

    return INIT_OK;
}

/* ==========================================================================
 *  USART DRIVER — TX COMPLETE CALLBACK
 *
 *  Call this from the DMA TX IRQ handler (DMA1_Stream0_IRQHandler).
 *  Clears the busy flag so the next packet can be sent.
 * ========================================================================== */
void USART_Driver_TxCompleteCallback(USART_Handle *handle)
{
    handle->tx_busy = false;
    handle->tx_len  = 0;
}
