/*******************************************************************************
 * @file    Src/stm32h7xx_it.c
 * @author  Cam
 * @brief   Interrupt Service Routines
 *
 *          All ISR handlers live in this file.  Each handler does the
 *          minimum work required (clear flags, call driver callback)
 *          and returns.  Heavy processing is deferred to the main loop
 *          or handled by the protocol parser within the callback.
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "bsp.h"
#include "usart_driver.h"
#include "stm32h7xx_ll_utils.h"
#include "ll_tick.h"

/* ==========================================================================
 *  SYSTEM TICK (1 ms)
 *
 *  Increments the LL tick counter used by LL_GetTick() for timeouts.
 * ========================================================================== */
void SysTick_Handler(void)
{
    LL_IncTick();
}

/* ==========================================================================
 *  DMA1 STREAM 0 — USART10 TX COMPLETE
 *
 *  Fires when the DMA has finished sending all bytes from the TX buffer.
 *  Clears the tx_busy flag so the next packet can be queued.
 * ========================================================================== */
void DMA1_Stream0_IRQHandler(void)
{
    /* Transfer Complete */
    if (LL_DMA_IsActiveFlag_TC0(DMA1)) {
        LL_DMA_ClearFlag_TC0(DMA1);
        USART_Driver_TxCompleteISR(&usart10_handle);
    }

    /* Transfer Error */
    if (LL_DMA_IsActiveFlag_TE0(DMA1)) {
        LL_DMA_ClearFlag_TE0(DMA1);
        USART_Driver_TxCompleteISR(&usart10_handle);
    }
}

/* ==========================================================================
 *  DMA1 STREAM 1 — USART10 RX (HALF-TRANSFER and TRANSFER-COMPLETE)
 *
 *  HT fires when the circular buffer is half full.
 *  TC fires when the buffer wraps back to the beginning.
 *  Both trigger processing of any new bytes in the ring.
 * ========================================================================== */
void DMA1_Stream1_IRQHandler(void)
{
    /* Half-Transfer */
    if (LL_DMA_IsActiveFlag_HT1(DMA1)) {
        LL_DMA_ClearFlag_HT1(DMA1);
        USART_Driver_RxProcessISR(&usart10_handle);
    }

    /* Transfer Complete (buffer wrapped) */
    if (LL_DMA_IsActiveFlag_TC1(DMA1)) {
        LL_DMA_ClearFlag_TC1(DMA1);
        USART_Driver_RxProcessISR(&usart10_handle);
    }

    /* Transfer Error */
    if (LL_DMA_IsActiveFlag_TE1(DMA1)) {
        LL_DMA_ClearFlag_TE1(DMA1);
        /* TODO: error counter or recovery */
    }
}

/* ==========================================================================
 *  USART10 — IDLE LINE INTERRUPT
 *
 *  Fires when the UART RX line goes idle after receiving data.  This is
 *  the key interrupt for low-latency reception — it catches the end of
 *  a packet even if the DMA buffer is nowhere near half full.
 *
 *  Also handles overrun errors (ORE) which can occur if the DMA falls
 *  behind, though this shouldn't happen in circular mode.
 * ========================================================================== */
void USART10_IRQHandler(void)
{
    /* IDLE line detected — process any new bytes in the DMA buffer */
    if (LL_USART_IsActiveFlag_IDLE(USART10)) {
        LL_USART_ClearFlag_IDLE(USART10);
        USART_Driver_RxProcessISR(&usart10_handle);
    }

    /* Overrun error — clear flag to prevent repeated interrupts */
    if (LL_USART_IsActiveFlag_ORE(USART10)) {
        LL_USART_ClearFlag_ORE(USART10);
    }

    /* Framing error */
    if (LL_USART_IsActiveFlag_FE(USART10)) {
        LL_USART_ClearFlag_FE(USART10);
    }

    /* Noise error */
    if (LL_USART_IsActiveFlag_NE(USART10)) {
        LL_USART_ClearFlag_NE(USART10);
    }
}
