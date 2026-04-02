/**
 * @file  RS485_Driver.c
 * @brief Half-duplex RS485 driver for gantry communication via USART7.
 *
 * Fully polled TX and RX.  DE/RE pin toggled for half-duplex direction.
 * Gantry protocol: ASCII, null-terminated commands and responses.
 */

#include "RS485_Driver.h"
#include "ll_tick.h"

/* ---- Direction control --------------------------------------------------- */

/* NOTE: PF8 goes through an inverter (NOT gate) before reaching the MAX485
 *       RE+DE pins.  PF8 LOW → NOT gate → HIGH → DE=1 (transmit).
 *       PF8 HIGH → NOT gate → LOW → RE=0 (receive).                        */

void RS485_SetTx(RS485_Handle *handle)
{
    LL_GPIO_ResetOutputPin(handle->cfg->de_re_pin.port,
                           handle->cfg->de_re_pin.pin);
}

void RS485_SetRx(RS485_Handle *handle)
{
    LL_GPIO_SetOutputPin(handle->cfg->de_re_pin.port,
                         handle->cfg->de_re_pin.pin);
}

/* ---- Polled byte helpers ------------------------------------------------- */

static inline bool TxByte(USART_TypeDef *usart, uint8_t byte, uint32_t timeout_ms)
{
    uint32_t t0 = LL_GetTick();
    while (!LL_USART_IsActiveFlag_TXE_TXFNF(usart)) {
        if ((LL_GetTick() - t0) >= timeout_ms) return false;
    }
    LL_USART_TransmitData8(usart, byte);
    return true;
}

static inline bool WaitTxComplete(USART_TypeDef *usart, uint32_t timeout_ms)
{
    uint32_t t0 = LL_GetTick();
    while (!LL_USART_IsActiveFlag_TC(usart)) {
        if ((LL_GetTick() - t0) >= timeout_ms) return false;
    }
    LL_USART_ClearFlag_TC(usart);
    return true;
}

/* ==========================================================================
 *  RS485_Init
 * ========================================================================== */
InitResult RS485_Init(RS485_Handle *handle)
{
    const RS485_Config *cfg = handle->cfg;
    USART_TypeDef *usart = cfg->usart.peripheral;

    /* ---- GPIO: TX, RX, DE/RE ---- */
    Pin_Init(&cfg->usart.tx_pin);
    Pin_Init(&cfg->usart.rx_pin);
    Pin_Init(&cfg->de_re_pin);

    /* Start in receive mode */
    RS485_SetRx(handle);

    /* ---- USART7 peripheral clock (APB1) ---- */
    LL_APB1_GRP1_EnableClock(cfg->usart.bus_clk_enable);

    /* ---- Kernel clock source ---- */
    LL_RCC_SetUSARTClockSource(cfg->usart.kernel_clk_source);

    /* ---- USART configuration ---- */
    LL_USART_Disable(usart);

    LL_USART_SetPrescaler(usart, cfg->usart.prescaler);
    LL_USART_SetBaudRate(usart,
                         cfg->usart.kernel_clk_hz,
                         cfg->usart.prescaler,
                         cfg->usart.oversampling,
                         cfg->usart.baudrate);
    LL_USART_SetDataWidth(usart, cfg->usart.data_width);
    LL_USART_SetStopBitsLength(usart, cfg->usart.stop_bits);
    LL_USART_SetParity(usart, cfg->usart.parity);
    LL_USART_SetTransferDirection(usart, cfg->usart.direction);
    LL_USART_SetHWFlowCtrl(usart, cfg->usart.hw_flow_control);
    LL_USART_SetOverSampling(usart, cfg->usart.oversampling);

    LL_USART_DisableFIFO(usart);
    LL_USART_ConfigAsyncMode(usart);

    /* ---- Enable USART ---- */
    LL_USART_Enable(usart);

    while (!LL_USART_IsActiveFlag_TEACK(usart));
    while (!LL_USART_IsActiveFlag_REACK(usart));

    /* Clear any pending RX data */
    if (LL_USART_IsActiveFlag_RXNE_RXFNE(usart)) {
        (void)LL_USART_ReceiveData8(usart);
    }

    handle->initialised = true;
    return INIT_OK;
}

/* ==========================================================================
 *  RS485_SendCommand
 *
 *  1. Switch to TX mode (DE/RE HIGH)
 *  2. Send command bytes + null terminator
 *  3. Wait for transmission complete (TC flag)
 *  4. Switch to RX mode (DE/RE LOW)
 *  5. Receive bytes until null terminator or timeout
 * ========================================================================== */
uint16_t RS485_SendCommand(RS485_Handle *handle,
                            const char *cmd,
                            char *response, uint16_t max_len,
                            uint32_t timeout_ms)
{
    USART_TypeDef *usart = handle->cfg->usart.peripheral;

    if (max_len == 0U) return 0U;

    /* Flush any stale RX data */
    while (LL_USART_IsActiveFlag_RXNE_RXFNE(usart)) {
        (void)LL_USART_ReceiveData8(usart);
    }

    /* ---- TX phase ---- */
    RS485_SetTx(handle);

    /* Send command string */
    for (const char *p = cmd; *p != '\0'; p++) {
        if (!TxByte(usart, (uint8_t)*p, timeout_ms)) {
            RS485_SetRx(handle);
            return 0U;
        }
    }

    /* Send carriage return (gantry protocol terminator) */
    if (!TxByte(usart, 0x0DU, timeout_ms)) {
        RS485_SetRx(handle);
        return 0U;
    }

    /* Wait for last byte to fully shift out before switching direction */
    if (!WaitTxComplete(usart, timeout_ms)) {
        RS485_SetRx(handle);
        return 0U;
    }

    /* ---- RX phase ---- */
    RS485_SetRx(handle);

    uint16_t count = 0U;
    uint32_t t0 = LL_GetTick();

    while (count < (max_len - 1U)) {
        if ((LL_GetTick() - t0) >= timeout_ms) {
            break;  /* Timeout */
        }

        if (LL_USART_IsActiveFlag_RXNE_RXFNE(usart)) {
            uint8_t byte = LL_USART_ReceiveData8(usart);

            if (byte == 0x0DU || byte == 0x0AU) {
                /* CR or LF — response complete */
                if (count > 0U) break;  /* ignore leading CR/LF */
                continue;
            }

            response[count++] = (char)byte;
        }
    }

    response[count] = '\0';
    return count;
}
