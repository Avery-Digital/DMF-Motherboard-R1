/*******************************************************************************
 * @file    Src/MightyZap.c
 * @author  Cam
 * @brief   mightyZAP 12Lf Linear Servo Driver — Implementation
 *
 *          Binary packet protocol over RS-485 (UART8) half-duplex.
 *          Polled TX and RX — no DMA, no interrupts.
 *
 *          Timing requirements from datasheet:
 *            - 5 ms delay after write commands
 *            - 10 ms delay after read commands
 *
 *          DE/RE direction (inverted via NOT gate):
 *            PD15 LOW  → NOT → HIGH → DE=1 (transmit)
 *            PD15 HIGH → NOT → LOW  → RE=0 (receive)
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#include "MightyZap.h"
#include "ll_tick.h"

/* ==========================================================================
 *  DIRECTION CONTROL (inverted — same convention as gantry RS485)
 * ========================================================================== */

static inline void SetTx(const MightyZap_Config *cfg)
{
    LL_GPIO_ResetOutputPin(cfg->de_re_pin.port, cfg->de_re_pin.pin);
}

static inline void SetRx(const MightyZap_Config *cfg)
{
    LL_GPIO_SetOutputPin(cfg->de_re_pin.port, cfg->de_re_pin.pin);
}

/* ==========================================================================
 *  POLLED BYTE HELPERS
 * ========================================================================== */

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

static inline int16_t RxByte(USART_TypeDef *usart, uint32_t timeout_ms)
{
    uint32_t t0 = LL_GetTick();
    while (!LL_USART_IsActiveFlag_RXNE_RXFNE(usart)) {
        if ((LL_GetTick() - t0) >= timeout_ms) return -1;
    }
    return (int16_t)LL_USART_ReceiveData8(usart);
}

/* ==========================================================================
 *  CHECKSUM
 * ========================================================================== */

static uint8_t CalcChecksum(const uint8_t *data, uint16_t len)
{
    uint8_t sum = 0U;
    for (uint16_t i = 0U; i < len; i++) {
        sum += data[i];
    }
    return ~sum;  /* Binary invert of lower byte */
}

/* ==========================================================================
 *  SEND PACKET
 *
 *  Builds and sends: [0xFF][0xFF][0xFF][ID][SIZE][CMD][factors...][CHECKSUM]
 * ========================================================================== */

static MightyZap_Status SendPacket(MightyZap_Handle *h,
                                    uint8_t command,
                                    const uint8_t *factors,
                                    uint8_t num_factors)
{
    const MightyZap_Config *cfg = h->cfg;
    USART_TypeDef *usart = cfg->usart;

    uint8_t size = num_factors + 2U;  /* COMMAND + factors + CHECKSUM */

    /* Build checksum over: ID + SIZE + COMMAND + factors */
    uint8_t csum_buf[MZAP_MAX_PACKET_SIZE];
    uint8_t idx = 0U;
    csum_buf[idx++] = h->servo_id;
    csum_buf[idx++] = size;
    csum_buf[idx++] = command;
    for (uint8_t i = 0U; i < num_factors; i++) {
        csum_buf[idx++] = factors[i];
    }
    uint8_t checksum = CalcChecksum(csum_buf, idx);

    /* Flush stale RX */
    while (LL_USART_IsActiveFlag_RXNE_RXFNE(usart)) {
        (void)LL_USART_ReceiveData8(usart);
    }

    /* Switch to transmit */
    SetTx(cfg);

    /* Send header */
    if (!TxByte(usart, MZAP_HEADER_BYTE, MZAP_TIMEOUT_MS)) goto fail;
    if (!TxByte(usart, MZAP_HEADER_BYTE, MZAP_TIMEOUT_MS)) goto fail;
    if (!TxByte(usart, MZAP_HEADER_BYTE, MZAP_TIMEOUT_MS)) goto fail;

    /* Send ID, SIZE, COMMAND */
    if (!TxByte(usart, h->servo_id, MZAP_TIMEOUT_MS)) goto fail;
    if (!TxByte(usart, size, MZAP_TIMEOUT_MS)) goto fail;
    if (!TxByte(usart, command, MZAP_TIMEOUT_MS)) goto fail;

    /* Send factors */
    for (uint8_t i = 0U; i < num_factors; i++) {
        if (!TxByte(usart, factors[i], MZAP_TIMEOUT_MS)) goto fail;
    }

    /* Send checksum */
    if (!TxByte(usart, checksum, MZAP_TIMEOUT_MS)) goto fail;

    /* Wait for TX to complete */
    if (!WaitTxComplete(usart, MZAP_TIMEOUT_MS)) goto fail;

    /* Turnaround delay */
    for (volatile uint32_t d = 0; d < 1200; d++) { __NOP(); }

    /* Switch to receive */
    SetRx(cfg);

    /* Clear error flags (ORE from half-duplex echo) */
    if (LL_USART_IsActiveFlag_ORE(usart))  LL_USART_ClearFlag_ORE(usart);
    if (LL_USART_IsActiveFlag_FE(usart))   LL_USART_ClearFlag_FE(usart);
    if (LL_USART_IsActiveFlag_NE(usart))   LL_USART_ClearFlag_NE(usart);

    /* Flush echo bytes */
    while (LL_USART_IsActiveFlag_RXNE_RXFNE(usart)) {
        (void)LL_USART_ReceiveData8(usart);
    }

    return MZAP_OK;

fail:
    SetRx(cfg);
    return MZAP_ERR_COMM;
}

/* ==========================================================================
 *  RECEIVE FEEDBACK PACKET
 *
 *  Expects: [0xFF][0xFF][0xFF][ID][SIZE][ERROR][factors...][CHECKSUM]
 *  Returns factors in the provided buffer.
 * ========================================================================== */

static MightyZap_Status ReceivePacket(MightyZap_Handle *h,
                                       uint8_t *factors,
                                       uint8_t *num_factors,
                                       uint8_t max_factors)
{
    USART_TypeDef *usart = h->cfg->usart;
    int16_t byte;

    /* Wait for 3x 0xFF header */
    for (uint8_t hdr = 0U; hdr < 3U; hdr++) {
        byte = RxByte(usart, MZAP_TIMEOUT_MS);
        if (byte < 0 || (uint8_t)byte != MZAP_HEADER_BYTE) {
            return MZAP_ERR_TIMEOUT;
        }
    }

    /* ID */
    byte = RxByte(usart, MZAP_TIMEOUT_MS);
    if (byte < 0) return MZAP_ERR_TIMEOUT;
    uint8_t id = (uint8_t)byte;

    /* SIZE */
    byte = RxByte(usart, MZAP_TIMEOUT_MS);
    if (byte < 0) return MZAP_ERR_TIMEOUT;
    uint8_t size = (uint8_t)byte;

    /* ERROR */
    byte = RxByte(usart, MZAP_TIMEOUT_MS);
    if (byte < 0) return MZAP_ERR_TIMEOUT;
    uint8_t error = (uint8_t)byte;
    h->last_error = error;

    /* Factors: size - 2 (ERROR + CHECKSUM) */
    uint8_t factor_count = 0U;
    if (size > 2U) {
        factor_count = size - 2U;
    }

    uint8_t csum_buf[MZAP_MAX_PACKET_SIZE];
    uint8_t csum_idx = 0U;
    csum_buf[csum_idx++] = id;
    csum_buf[csum_idx++] = size;
    csum_buf[csum_idx++] = error;

    for (uint8_t i = 0U; i < factor_count; i++) {
        byte = RxByte(usart, MZAP_TIMEOUT_MS);
        if (byte < 0) return MZAP_ERR_TIMEOUT;

        if (i < max_factors) {
            factors[i] = (uint8_t)byte;
        }
        csum_buf[csum_idx++] = (uint8_t)byte;
    }

    /* CHECKSUM */
    byte = RxByte(usart, MZAP_TIMEOUT_MS);
    if (byte < 0) return MZAP_ERR_TIMEOUT;
    uint8_t rx_checksum = (uint8_t)byte;

    /* Verify checksum */
    uint8_t calc_checksum = CalcChecksum(csum_buf, csum_idx);
    if (rx_checksum != calc_checksum) {
        return MZAP_ERR_CHECKSUM;
    }

    if (num_factors != NULL) {
        *num_factors = (factor_count < max_factors) ? factor_count : max_factors;
    }

    if (error != 0x00U) {
        return MZAP_ERR_SERVO;
    }

    return MZAP_OK;
}

/* ==========================================================================
 *  INIT
 * ========================================================================== */

MightyZap_Status MightyZap_Init(MightyZap_Handle *h)
{
    const MightyZap_Config *cfg = h->cfg;
    USART_TypeDef *usart = cfg->usart;

    /* GPIO pins */
    Pin_Init(&cfg->tx_pin);
    Pin_Init(&cfg->rx_pin);
    Pin_Init(&cfg->de_re_pin);

    /* Start in receive mode */
    SetRx(cfg);

    /* UART8 clock */
    LL_APB1_GRP1_EnableClock(cfg->bus_clk_enable);

    /* Kernel clock source */
    LL_RCC_SetUSARTClockSource(cfg->kernel_clk_src);

    /* USART configuration */
    LL_USART_Disable(usart);

    LL_USART_SetPrescaler(usart, LL_USART_PRESCALER_DIV1);
    LL_USART_SetBaudRate(usart,
                         cfg->kernel_clk_hz,
                         LL_USART_PRESCALER_DIV1,
                         LL_USART_OVERSAMPLING_16,
                         cfg->baudrate);
    LL_USART_SetDataWidth(usart, LL_USART_DATAWIDTH_8B);
    LL_USART_SetStopBitsLength(usart, LL_USART_STOPBITS_1);
    LL_USART_SetParity(usart, LL_USART_PARITY_NONE);
    LL_USART_SetTransferDirection(usart, LL_USART_DIRECTION_TX_RX);
    LL_USART_SetHWFlowCtrl(usart, LL_USART_HWCONTROL_NONE);
    LL_USART_SetOverSampling(usart, LL_USART_OVERSAMPLING_16);

    LL_USART_DisableFIFO(usart);
    LL_USART_ConfigAsyncMode(usart);

    LL_USART_Enable(usart);

    while (!LL_USART_IsActiveFlag_TEACK(usart));
    while (!LL_USART_IsActiveFlag_REACK(usart));

    /* Flush any pending RX */
    if (LL_USART_IsActiveFlag_RXNE_RXFNE(usart)) {
        (void)LL_USART_ReceiveData8(usart);
    }

    h->initialised = true;
    h->last_error = 0U;

    return MZAP_OK;
}

/* ==========================================================================
 *  PING (Echo command 0xF1)
 * ========================================================================== */

MightyZap_Status MightyZap_Ping(MightyZap_Handle *h)
{
    MightyZap_Status st = SendPacket(h, MZAP_CMD_ECHO, NULL, 0U);
    if (st != MZAP_OK) return st;

    uint8_t factors[4];
    uint8_t count = 0U;
    return ReceivePacket(h, factors, &count, sizeof(factors));
}

/* ==========================================================================
 *  READ BYTE
 * ========================================================================== */

MightyZap_Status MightyZap_ReadByte(MightyZap_Handle *h, uint8_t addr, uint8_t *value)
{
    uint8_t factors_out[2] = { addr, 0x01U };  /* address, length=1 */
    MightyZap_Status st = SendPacket(h, MZAP_CMD_LOAD_DATA, factors_out, 2U);
    if (st != MZAP_OK) return st;

    uint8_t factors_in[4];
    uint8_t count = 0U;
    st = ReceivePacket(h, factors_in, &count, sizeof(factors_in));
    if (st != MZAP_OK) return st;

    if (count >= 1U) {
        *value = factors_in[0];
    }

    return MZAP_OK;
}

/* ==========================================================================
 *  READ WORD (16-bit little-endian)
 * ========================================================================== */

MightyZap_Status MightyZap_ReadWord(MightyZap_Handle *h, uint8_t addr, uint16_t *value)
{
    uint8_t factors_out[2] = { addr, 0x02U };  /* address, length=2 */
    MightyZap_Status st = SendPacket(h, MZAP_CMD_LOAD_DATA, factors_out, 2U);
    if (st != MZAP_OK) return st;

    uint8_t factors_in[4];
    uint8_t count = 0U;
    st = ReceivePacket(h, factors_in, &count, sizeof(factors_in));
    if (st != MZAP_OK) return st;

    if (count >= 2U) {
        *value = (uint16_t)factors_in[0] | ((uint16_t)factors_in[1] << 8);
    }

    return MZAP_OK;
}

/* ==========================================================================
 *  WRITE BYTE (Store Data — permanent)
 * ========================================================================== */

MightyZap_Status MightyZap_WriteByte(MightyZap_Handle *h, uint8_t addr, uint8_t value)
{
    uint8_t factors[2] = { addr, value };
    MightyZap_Status st = SendPacket(h, MZAP_CMD_STORE_DATA, factors, 2U);
    if (st != MZAP_OK) return st;

    /* Delay for write settling */
    LL_mDelay(MZAP_WRITE_DELAY_MS);

    /* Read feedback if feedback mode allows */
    uint8_t fb[4];
    uint8_t count = 0U;
    return ReceivePacket(h, fb, &count, sizeof(fb));
}

/* ==========================================================================
 *  WRITE WORD (Store Data — permanent, 16-bit little-endian)
 * ========================================================================== */

MightyZap_Status MightyZap_WriteWord(MightyZap_Handle *h, uint8_t addr, uint16_t value)
{
    uint8_t factors[3] = { addr, (uint8_t)(value & 0xFFU), (uint8_t)(value >> 8) };
    MightyZap_Status st = SendPacket(h, MZAP_CMD_STORE_DATA, factors, 3U);
    if (st != MZAP_OK) return st;

    LL_mDelay(MZAP_WRITE_DELAY_MS);

    uint8_t fb[4];
    uint8_t count = 0U;
    return ReceivePacket(h, fb, &count, sizeof(fb));
}

/* ==========================================================================
 *  CONVENIENCE: Set Goal Position
 * ========================================================================== */

MightyZap_Status MightyZap_SetPosition(MightyZap_Handle *h, uint16_t position)
{
    if (position > MZAP_POS_MAX) position = MZAP_POS_MAX;
    return MightyZap_WriteWord(h, MZAP_REG_GOAL_POS_L, position);
}

/* ==========================================================================
 *  CONVENIENCE: Get Present Position
 * ========================================================================== */

MightyZap_Status MightyZap_GetPosition(MightyZap_Handle *h, uint16_t *position)
{
    return MightyZap_ReadWord(h, MZAP_REG_PRESENT_POS_L, position);
}

/* ==========================================================================
 *  CONVENIENCE: Set Goal Speed
 * ========================================================================== */

MightyZap_Status MightyZap_SetSpeed(MightyZap_Handle *h, uint16_t speed)
{
    if (speed > MZAP_SPEED_MAX) speed = MZAP_SPEED_MAX;
    return MightyZap_WriteWord(h, MZAP_REG_GOAL_SPEED_L, speed);
}

/* ==========================================================================
 *  CONVENIENCE: Force On/Off
 * ========================================================================== */

MightyZap_Status MightyZap_SetForce(MightyZap_Handle *h, bool on)
{
    return MightyZap_WriteByte(h, MZAP_REG_FORCE_ONOFF, on ? 0x01U : 0x00U);
}

/* ==========================================================================
 *  CONVENIENCE: Get Present Voltage
 * ========================================================================== */

MightyZap_Status MightyZap_GetVoltage(MightyZap_Handle *h, uint8_t *voltage)
{
    return MightyZap_ReadByte(h, MZAP_REG_PRESENT_VOLT, voltage);
}
