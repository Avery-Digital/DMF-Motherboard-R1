/*******************************************************************************
 * @file    Src/Act_Uart_Driver.c
 * @author  Cam
 * @brief   Actuator Board UART Driver — Polled TX + DMA Circular RX + RS485 DE
 *
 *          Two instances: UART5 (ACT1) and USART6 (ACT2).
 *          TX is polled with RS485 direction control via inverted DE pin.
 *          RX uses DMA circular mode with HT/TC/IDLE interrupts.
 *
 *          DE pin logic is INVERTED (NOT gate on PCB between MCU and LTC2864):
 *            GPIO LOW  → NOT → DE=HIGH, RE=HIGH → Transmit
 *            GPIO HIGH → NOT → DE=LOW,  RE=LOW  → Receive (idle)
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#include "Act_Uart_Driver.h"
#include <string.h>

/* Static TX buffer for Protocol_BuildPacket — shared across both ACT UARTs.
 * Safe because Act_Uart_SendPacket is only called from the main loop. */
static uint8_t act_tx_frame_buf[PKT_TX_BUF_SIZE];

/* ==========================================================================
 *  RS485 DE CONTROL (inverted)
 * ========================================================================== */
static inline void Act_DE_SetTx(Act_Uart_Handle *handle)
{
    /* LOW = transmit (inverted by NOT gate) */
    LL_GPIO_ResetOutputPin(handle->de_pin.port, handle->de_pin.pin);
}

static inline void Act_DE_SetRx(Act_Uart_Handle *handle)
{
    /* HIGH = receive (inverted by NOT gate) */
    LL_GPIO_SetOutputPin(handle->de_pin.port, handle->de_pin.pin);
}

/* ==========================================================================
 *  DMA Stream Init
 * ========================================================================== */
static void Act_DMA_Stream_Init(const DMA_ChannelConfig *dma)
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
 *  Act_Uart_Init
 * ========================================================================== */
InitResult Act_Uart_Init(Act_Uart_Handle *handle, ProtocolParser *parser)
{
    const USART_Config      *cfg    = handle->cfg;
    const DMA_ChannelConfig *dma_rx = handle->dma_rx;

    handle->parser = (void *)parser;

    /* ---- GPIO ---- */
    Pin_Init(&cfg->tx_pin);
    Pin_Init(&cfg->rx_pin);

    /* ---- DE pin — default HIGH (receive / idle) ---- */
    Pin_Init(&handle->de_pin);
    Act_DE_SetRx(handle);

    /* ---- USART peripheral clock ---- */
    if (handle->is_apb2) {
        LL_APB2_GRP1_EnableClock(cfg->bus_clk_enable);
    } else {
        LL_APB1_GRP1_EnableClock(cfg->bus_clk_enable);
    }

    /* ---- Kernel clock source ---- */
    LL_RCC_SetUSARTClockSource(cfg->kernel_clk_source);

    /* ---- USART configuration ---- */
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
    Act_DMA_Stream_Init(dma_rx);

    LL_DMA_SetPeriphAddress(dma_rx->dma, dma_rx->stream,
                            LL_USART_DMA_GetRegAddr(cfg->peripheral,
                                                     LL_USART_DMA_REG_DATA_RECEIVE));

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
 *  Act_Uart_StartRx
 * ========================================================================== */
void Act_Uart_StartRx(Act_Uart_Handle *handle)
{
    const DMA_ChannelConfig *rx = handle->dma_rx;

    LL_DMA_EnableIT_HT(rx->dma, rx->stream);
    LL_DMA_EnableIT_TC(rx->dma, rx->stream);
    LL_DMA_EnableIT_TE(rx->dma, rx->stream);

    LL_DMA_EnableStream(rx->dma, rx->stream);
}

/* ==========================================================================
 *  Act_Uart_RxProcessISR
 * ========================================================================== */
void Act_Uart_RxProcessISR(Act_Uart_Handle *handle)
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
 *  Act_Uart_SendBytes — Polled TX with RS485 DE toggling
 * ========================================================================== */
void Act_Uart_SendBytes(Act_Uart_Handle *handle,
                        const uint8_t *data, uint16_t len)
{
    USART_TypeDef *uart = handle->cfg->peripheral;

    /* Switch to transmit mode */
    Act_DE_SetTx(handle);

    for (uint16_t i = 0; i < len; i++) {
        while (!LL_USART_IsActiveFlag_TXE_TXFNF(uart));
        LL_USART_TransmitData8(uart, data[i]);
    }

    /* Wait for last byte to fully shift out before switching back */
    while (!LL_USART_IsActiveFlag_TC(uart));
    LL_USART_ClearFlag_TC(uart);

    /* Switch back to receive mode */
    Act_DE_SetRx(handle);
}

/* ==========================================================================
 *  Act_Uart_SendPacket — Build framed packet and send via polled TX
 * ========================================================================== */
void Act_Uart_SendPacket(Act_Uart_Handle *handle,
                         uint8_t msg1, uint8_t msg2,
                         uint8_t cmd1, uint8_t cmd2,
                         const uint8_t *payload, uint16_t length)
{
    uint16_t frame_len = Protocol_BuildPacket(
        act_tx_frame_buf,
        msg1, msg2, cmd1, cmd2,
        payload, length
    );

    Act_Uart_SendBytes(handle, act_tx_frame_buf, frame_len);
}
