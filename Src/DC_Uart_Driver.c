/*******************************************************************************
 * @file    Src/DC_Uart_Driver.c
 * @author  Cam
 * @brief   Daughtercard UART Driver — Polled TX + DMA Circular RX
 *
 *          4 instances (USART1, USART2, USART3, UART4), one per daughtercard.
 *          TX is polled — the main loop blocks briefly while sending.
 *          RX uses DMA circular mode with HT/TC/IDLE interrupts.
 *
 *          This driver is intentionally separate from the USART10 driver
 *          because it uses polled TX (no DMA) and supports both APB1 and
 *          APB2 peripherals.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#include "DC_Uart_Driver.h"
#include <string.h>

/* Static TX buffer for Protocol_BuildPacket — shared across all DC UARTs.
 * Safe because DC_Uart_SendPacket is only called from the main loop
 * (single-threaded, no concurrent sends). */
static uint8_t dc_tx_frame_buf[PKT_TX_BUF_SIZE];

/* ==========================================================================
 *  PRIVATE: DMA Stream Init (same pattern as USART10 driver)
 * ========================================================================== */
static void DC_DMA_Stream_Init(const DMA_ChannelConfig *dma)
{
    LL_AHB1_GRP1_EnableClock(dma->dma_clk_enable);

    LL_DMA_DisableStream(dma->dma, dma->stream);
    while (LL_DMA_IsEnabledStream(dma->dma, dma->stream));

    LL_DMA_SetPeriphRequest(dma->dma, dma->stream, dma->request);
    LL_DMA_SetDataTransferDirection(dma->dma, dma->stream, dma->direction);
    LL_DMA_SetStreamPriorityLevel(dma->dma, dma->stream, dma->priority);
    LL_DMA_SetMode(dma->dma, dma->stream, dma->mode);
    LL_DMA_SetPeriphSize(dma->dma, dma->stream, dma->periph_data_align);
    LL_DMA_SetMemorySize(dma->dma, dma->stream, dma->mem_data_align);
    LL_DMA_SetPeriphIncMode(dma->dma, dma->stream, dma->periph_inc);
    LL_DMA_SetMemoryIncMode(dma->dma, dma->stream, dma->mem_inc);

    if (dma->use_fifo) {
        LL_DMA_EnableFifoMode(dma->dma, dma->stream);
        LL_DMA_SetFIFOThreshold(dma->dma, dma->stream, dma->fifo_threshold);
    } else {
        LL_DMA_DisableFifoMode(dma->dma, dma->stream);
    }

    NVIC_SetPriority(dma->irqn,
                     NVIC_EncodePriority(NVIC_GetPriorityGrouping(),
                                         dma->irq_priority, 0));
    NVIC_EnableIRQ(dma->irqn);
}

/* ==========================================================================
 *  DC_Uart_Init
 * ========================================================================== */
InitResult DC_Uart_Init(DC_Uart_Handle *handle, ProtocolParser *parser)
{
    const USART_Config      *cfg    = handle->cfg;
    const DMA_ChannelConfig *dma_rx = handle->dma_rx;

    /* Store parser pointer for ISR access */
    handle->parser = (void *)parser;

    /* ---- GPIO ---- */
    Pin_Init(&cfg->tx_pin);
    Pin_Init(&cfg->rx_pin);

    /* ---- USART peripheral clock ---- */
    if (handle->is_apb2) {
        LL_APB2_GRP1_EnableClock(cfg->bus_clk_enable);
    } else {
        LL_APB1_GRP1_EnableClock(cfg->bus_clk_enable);
    }

    /* ---- Kernel clock source ---- */
    LL_RCC_SetUSARTClockSource(cfg->kernel_clk_source);

    /* ---- USART configuration (peripheral must be disabled) ---- */
    LL_USART_Disable(cfg->peripheral);

    LL_USART_SetPrescaler(cfg->peripheral, cfg->prescaler);
    LL_USART_SetBaudRate(cfg->peripheral,
                         cfg->kernel_clk_hz,
                         cfg->prescaler,
                         cfg->oversampling,
                         cfg->baudrate);
    LL_USART_SetDataWidth(cfg->peripheral, cfg->data_width);
    LL_USART_SetStopBitsLength(cfg->peripheral, cfg->stop_bits);
    LL_USART_SetParity(cfg->peripheral, cfg->parity);
    LL_USART_SetTransferDirection(cfg->peripheral, cfg->direction);
    LL_USART_SetHWFlowCtrl(cfg->peripheral, cfg->hw_flow_control);
    LL_USART_SetOverSampling(cfg->peripheral, cfg->oversampling);

    LL_USART_DisableFIFO(cfg->peripheral);
    LL_USART_ConfigAsyncMode(cfg->peripheral);

    /* ---- DMA RX stream ---- */
    DC_DMA_Stream_Init(dma_rx);

    /* RX DMA: peripheral address = USART RDR */
    LL_DMA_SetPeriphAddress(dma_rx->dma, dma_rx->stream,
                            LL_USART_DMA_GetRegAddr(cfg->peripheral,
                                                     LL_USART_DMA_REG_DATA_RECEIVE));

    /* RX DMA: memory address and buffer length */
    LL_DMA_SetMemoryAddress(dma_rx->dma, dma_rx->stream,
                            (uint32_t)handle->rx_buf);
    LL_DMA_SetDataLength(dma_rx->dma, dma_rx->stream,
                         handle->rx_buf_size);

    /* ---- Enable DMA RX request only (TX is polled) ---- */
    LL_USART_EnableDMAReq_RX(cfg->peripheral);

    /* ---- USART IDLE line interrupt ---- */
    LL_USART_EnableIT_IDLE(cfg->peripheral);

    /* ---- USART NVIC ---- */
    NVIC_SetPriority(cfg->irqn,
                     NVIC_EncodePriority(NVIC_GetPriorityGrouping(),
                                         cfg->irq_priority, 0));
    NVIC_EnableIRQ(cfg->irqn);

    /* ---- Enable USART ---- */
    LL_USART_Enable(cfg->peripheral);

    while (!LL_USART_IsActiveFlag_TEACK(cfg->peripheral));
    while (!LL_USART_IsActiveFlag_REACK(cfg->peripheral));

    handle->rx_head = 0;

    return INIT_OK;
}

/* ==========================================================================
 *  DC_Uart_StartRx — Enable DMA circular RX interrupts
 * ========================================================================== */
void DC_Uart_StartRx(DC_Uart_Handle *handle)
{
    const DMA_ChannelConfig *rx = handle->dma_rx;

    LL_DMA_EnableIT_HT(rx->dma, rx->stream);
    LL_DMA_EnableIT_TC(rx->dma, rx->stream);
    LL_DMA_EnableIT_TE(rx->dma, rx->stream);

    LL_DMA_EnableStream(rx->dma, rx->stream);
}

/* ==========================================================================
 *  DC_Uart_RxProcessISR — Feed new DMA bytes to protocol parser
 * ========================================================================== */
void DC_Uart_RxProcessISR(DC_Uart_Handle *handle)
{
    const DMA_ChannelConfig *rx = handle->dma_rx;
    ProtocolParser *parser = (ProtocolParser *)handle->parser;

    if (parser == NULL) return;

    uint16_t dma_write_pos = handle->rx_buf_size -
        (uint16_t)LL_DMA_GetDataLength(rx->dma, rx->stream);

    uint16_t read_pos = handle->rx_head;

    if (dma_write_pos == read_pos) {
        return;
    }

    if (dma_write_pos > read_pos) {
        Protocol_FeedBytes(parser,
                           &handle->rx_buf[read_pos],
                           dma_write_pos - read_pos);
    } else {
        Protocol_FeedBytes(parser,
                           &handle->rx_buf[read_pos],
                           handle->rx_buf_size - read_pos);

        if (dma_write_pos > 0) {
            Protocol_FeedBytes(parser,
                               &handle->rx_buf[0],
                               dma_write_pos);
        }
    }

    handle->rx_head = dma_write_pos;
}

/* ==========================================================================
 *  DC_Uart_SendBytes — Polled TX, blocks until complete
 * ========================================================================== */
void DC_Uart_SendBytes(DC_Uart_Handle *handle,
                        const uint8_t *data, uint16_t len)
{
    USART_TypeDef *uart = handle->cfg->peripheral;

    for (uint16_t i = 0; i < len; i++) {
        while (!LL_USART_IsActiveFlag_TXE_TXFNF(uart));
        LL_USART_TransmitData8(uart, data[i]);
    }

    /* Wait for transmission complete (last byte shifted out) */
    while (!LL_USART_IsActiveFlag_TC(uart));
}

/* ==========================================================================
 *  DC_Uart_SendPacket — Build framed packet and send via polled TX
 * ========================================================================== */
void DC_Uart_SendPacket(DC_Uart_Handle *handle,
                         uint8_t msg1, uint8_t msg2,
                         uint8_t cmd1, uint8_t cmd2,
                         const uint8_t *payload, uint16_t length)
{
    uint16_t frame_len = Protocol_BuildPacket(
        dc_tx_frame_buf,
        msg1, msg2, cmd1, cmd2,
        payload, length
    );

    DC_Uart_SendBytes(handle, dc_tx_frame_buf, frame_len);
}
