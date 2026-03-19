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
#include "DC_Uart_Driver.h"
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

void HardFault_Handler(void)
{
    volatile uint32_t active_irq = SCB->ICSR & 0x1FFU;  /* Current IRQ number */
    volatile uint32_t hfsr = SCB->HFSR;                  /* HardFault status   */
    volatile uint32_t cfsr = SCB->CFSR;                  /* Configurable fault  */
    (void)active_irq;
    (void)hfsr;
    (void)cfsr;
    while (1);  /* Set breakpoint here */
}

void Unhandled_IRQ_Handler(void)
{
    volatile uint32_t active_irq = SCB->ICSR & 0x1FFU;
    (void)active_irq;
    while (1);  /* Breakpoint here — read active_irq in Variables view */
}

/* ==========================================================================
 *  DMA1 STREAM 0 — USART10 TX COMPLETE
 *
 *  Fires when the DMA has finished sending all bytes from the TX buffer.
 *  Clears the tx_busy flag so the next packet can be queued.
 * ========================================================================== */
/* Was: DMA1_Stream0_IRQHandler */
void DMA_STR0_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_TC0(DMA1)) {
        LL_DMA_ClearFlag_TC0(DMA1);
        USART_Driver_TxCompleteISR(&usart10_handle);
    }
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
/* Was: DMA1_Stream1_IRQHandler */
void DMA_STR1_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_HT1(DMA1)) {
        LL_DMA_ClearFlag_HT1(DMA1);
        USART_Driver_RxProcessISR(&usart10_handle);
    }
    if (LL_DMA_IsActiveFlag_TC1(DMA1)) {
        LL_DMA_ClearFlag_TC1(DMA1);
        USART_Driver_RxProcessISR(&usart10_handle);
    }
    if (LL_DMA_IsActiveFlag_TE1(DMA1)) {
        LL_DMA_ClearFlag_TE1(DMA1);
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

/* ==========================================================================
 *  DMA1 STREAM 2 — DC1 USART1 RX (HT/TC)
 * ========================================================================== */
void DMA_STR2_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_HT2(DMA1)) {
        LL_DMA_ClearFlag_HT2(DMA1);
        DC_Uart_RxProcessISR(&dc1_handle);
    }
    if (LL_DMA_IsActiveFlag_TC2(DMA1)) {
        LL_DMA_ClearFlag_TC2(DMA1);
        DC_Uart_RxProcessISR(&dc1_handle);
    }
    if (LL_DMA_IsActiveFlag_TE2(DMA1)) {
        LL_DMA_ClearFlag_TE2(DMA1);
    }
}

/* ==========================================================================
 *  DMA1 STREAM 3 — DC2 USART2 RX (HT/TC)
 * ========================================================================== */
void DMA_STR3_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_HT3(DMA1)) {
        LL_DMA_ClearFlag_HT3(DMA1);
        DC_Uart_RxProcessISR(&dc2_handle);
    }
    if (LL_DMA_IsActiveFlag_TC3(DMA1)) {
        LL_DMA_ClearFlag_TC3(DMA1);
        DC_Uart_RxProcessISR(&dc2_handle);
    }
    if (LL_DMA_IsActiveFlag_TE3(DMA1)) {
        LL_DMA_ClearFlag_TE3(DMA1);
    }
}

/* ==========================================================================
 *  DMA1 STREAM 4 — DC3 USART3 RX (HT/TC)
 * ========================================================================== */
void DMA_STR4_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_HT4(DMA1)) {
        LL_DMA_ClearFlag_HT4(DMA1);
        DC_Uart_RxProcessISR(&dc3_handle);
    }
    if (LL_DMA_IsActiveFlag_TC4(DMA1)) {
        LL_DMA_ClearFlag_TC4(DMA1);
        DC_Uart_RxProcessISR(&dc3_handle);
    }
    if (LL_DMA_IsActiveFlag_TE4(DMA1)) {
        LL_DMA_ClearFlag_TE4(DMA1);
    }
}

/* ==========================================================================
 *  DMA1 STREAM 5 — DC4 UART4 RX (HT/TC)
 * ========================================================================== */
void DMA_STR5_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_HT5(DMA1)) {
        LL_DMA_ClearFlag_HT5(DMA1);
        DC_Uart_RxProcessISR(&dc4_handle);
    }
    if (LL_DMA_IsActiveFlag_TC5(DMA1)) {
        LL_DMA_ClearFlag_TC5(DMA1);
        DC_Uart_RxProcessISR(&dc4_handle);
    }
    if (LL_DMA_IsActiveFlag_TE5(DMA1)) {
        LL_DMA_ClearFlag_TE5(DMA1);
    }
}

/* ==========================================================================
 *  USART1 — DC1 IDLE LINE
 * ========================================================================== */
void USART1_IRQHandler(void)
{
    if (LL_USART_IsActiveFlag_IDLE(USART1)) {
        LL_USART_ClearFlag_IDLE(USART1);
        DC_Uart_RxProcessISR(&dc1_handle);
    }
    if (LL_USART_IsActiveFlag_ORE(USART1))  LL_USART_ClearFlag_ORE(USART1);
    if (LL_USART_IsActiveFlag_FE(USART1))   LL_USART_ClearFlag_FE(USART1);
    if (LL_USART_IsActiveFlag_NE(USART1))   LL_USART_ClearFlag_NE(USART1);
}

/* ==========================================================================
 *  USART2 — DC2 IDLE LINE
 * ========================================================================== */
void USART2_IRQHandler(void)
{
    if (LL_USART_IsActiveFlag_IDLE(USART2)) {
        LL_USART_ClearFlag_IDLE(USART2);
        DC_Uart_RxProcessISR(&dc2_handle);
    }
    if (LL_USART_IsActiveFlag_ORE(USART2))  LL_USART_ClearFlag_ORE(USART2);
    if (LL_USART_IsActiveFlag_FE(USART2))   LL_USART_ClearFlag_FE(USART2);
    if (LL_USART_IsActiveFlag_NE(USART2))   LL_USART_ClearFlag_NE(USART2);
}

/* ==========================================================================
 *  USART3 — DC3 IDLE LINE
 * ========================================================================== */
void USART3_IRQHandler(void)
{
    if (LL_USART_IsActiveFlag_IDLE(USART3)) {
        LL_USART_ClearFlag_IDLE(USART3);
        DC_Uart_RxProcessISR(&dc3_handle);
    }
    if (LL_USART_IsActiveFlag_ORE(USART3))  LL_USART_ClearFlag_ORE(USART3);
    if (LL_USART_IsActiveFlag_FE(USART3))   LL_USART_ClearFlag_FE(USART3);
    if (LL_USART_IsActiveFlag_NE(USART3))   LL_USART_ClearFlag_NE(USART3);
}

/* ==========================================================================
 *  UART4 — DC4 IDLE LINE
 * ========================================================================== */
void UART4_IRQHandler(void)
{
    if (LL_USART_IsActiveFlag_IDLE(UART4)) {
        LL_USART_ClearFlag_IDLE(UART4);
        DC_Uart_RxProcessISR(&dc4_handle);
    }
    if (LL_USART_IsActiveFlag_ORE(UART4))  LL_USART_ClearFlag_ORE(UART4);
    if (LL_USART_IsActiveFlag_FE(UART4))   LL_USART_ClearFlag_FE(UART4);
    if (LL_USART_IsActiveFlag_NE(UART4))   LL_USART_ClearFlag_NE(UART4);
}
