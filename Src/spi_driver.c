/* ==========================================================================
 *  spi.c — SPI2 driver implementation (STM32H735, LL library)
 *
 *  Configuration data (spi2_cfg, spi2_handle) lives in bsp.c / bsp.h.
 *  This file contains only the driver logic — init, read, and deinit.
 *  See spi.h for the public API and pin assignment notes.
 * ========================================================================== */

#include "spi_driver.h"

/* --------------------------------------------------------------------------
 *  Internal helpers
 * -------------------------------------------------------------------------- */

/**
 * @brief  Obtain a millisecond timestamp.
 *         Replace with your RTOS tick / HAL_GetTick() / DWT counter as needed.
 */
static inline uint32_t _tick_ms(void)
{
    extern uint32_t HAL_GetTick(void);
    return HAL_GetTick();
}

/**
 * @brief  Configure and enable a single GPIO pin from a PinConfig descriptor.
 */
static void _gpio_init(const PinConfig *p)
{
    LL_AHB4_GRP1_EnableClock(p->clk);

    LL_GPIO_InitTypeDef gpio = {0};
    gpio.Pin        = p->pin;
    gpio.Mode       = p->mode;
    gpio.Speed      = p->speed;
    gpio.Pull       = p->pull;
    gpio.OutputType = p->output;
    gpio.Alternate  = p->af;
    LL_GPIO_Init(p->port, &gpio);
}

/* --------------------------------------------------------------------------
 *  Public API implementation
 * -------------------------------------------------------------------------- */

SPI_Status SPI_Init(SPI_Handle *handle)
{
    if (handle == NULL || handle->cfg == NULL) {
        return SPI_ERR_NOT_INIT;
    }

    const SPI_Config *cfg = handle->cfg;

    /* --- GPIO ------------------------------------------------------------ */
    _gpio_init(&cfg->miso_pin);
    _gpio_init(&cfg->mosi_pin);
    _gpio_init(&cfg->sck_pin);
    _gpio_init(&cfg->cnv_pin);
    _gpio_init(&cfg->busy_pin);

    /* CNV idles LOW — no conversion in progress */
    LL_GPIO_ResetOutputPin(cfg->cnv_pin.port, cfg->cnv_pin.pin);

    /* --- SPI peripheral -------------------------------------------------- */
    LL_APB1_GRP1_EnableClock(cfg->bus_clk_enable);

    /* Ensure peripheral is disabled before configuring */
    LL_SPI_Disable(cfg->peripheral);

    LL_SPI_InitTypeDef spi_init = {0};
    spi_init.TransferDirection  = LL_SPI_FULL_DUPLEX;
    spi_init.Mode               = LL_SPI_MODE_MASTER;
    spi_init.DataWidth          = cfg->data_width;
    spi_init.ClockPolarity      = cfg->clk_polarity;
    spi_init.ClockPhase         = cfg->clk_phase;
    spi_init.NSS                = cfg->nss_mode;
    spi_init.BaudRate           = cfg->baud_prescaler;
    spi_init.BitOrder           = cfg->bit_order;
    spi_init.CRCCalculation     = LL_SPI_CRCCALCULATION_DISABLE;
    spi_init.CRCPoly            = 7U;

    LL_SPI_Init(cfg->peripheral, &spi_init);

    /* H7 SPI: FIFO threshold = 1 data frame — RXP flag asserts when >= 1 complete 32-bit word is ready */
    LL_SPI_SetFIFOThreshold(cfg->peripheral, LL_SPI_FIFO_TH_01DATA);

    LL_SPI_Enable(cfg->peripheral);

    handle->initialised = true;
    handle->busy        = false;
    handle->error       = 0U;

    return SPI_OK;
}

/* -------------------------------------------------------------------------- */

SPI_Status SPI_LTC2338_Read(SPI_Handle *handle, uint32_t *result_out)
{
    if (!handle->initialised) {
        return SPI_ERR_NOT_INIT;
    }
    if (handle->busy) {
        return SPI_ERR_BUSY;
    }
    if (result_out == NULL) {
        return SPI_ERR_NOT_INIT;
    }

    handle->busy = true;

    const SPI_Config *cfg = handle->cfg;
    uint32_t t0;

    /* ------------------------------------------------------------------
     *  Step 1: Pulse CNV HIGH to start conversion.
     *
     *  t_CNVH minimum is 30 ns per LTC2338-18 datasheet.
     *  At 550 MHz CPU, a single GPIO set + reset gives ~2 ns — insert
     *  NOPs or a short delay loop if your clock is slower than ~300 MHz.
     *  At typical embedded frequencies a few NOPs are sufficient.
     * ------------------------------------------------------------------ */
    LL_GPIO_SetOutputPin(cfg->cnv_pin.port, cfg->cnv_pin.pin);

    /* Minimum t_CNVH delay — adjust NOP count for your CPU frequency */
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();

    LL_GPIO_ResetOutputPin(cfg->cnv_pin.port, cfg->cnv_pin.pin);

    /* ------------------------------------------------------------------
     *  Step 2: Wait for BUSY to go LOW (conversion complete).
     *
     *  BUSY goes HIGH briefly after CNV falls, then LOW when the ADC
     *  has finished converting (~1 µs typical).  Poll with timeout.
     * ------------------------------------------------------------------ */
    t0 = _tick_ms();

    /* Wait for BUSY to assert HIGH first (t_BUSYLH) */
    while (LL_GPIO_IsInputPinSet(cfg->busy_pin.port, cfg->busy_pin.pin) == 0U) {
        if ((_tick_ms() - t0) >= cfg->busy_timeout_ms) {
            handle->busy  = false;
            handle->error = SPI_ERR_TIMEOUT;
            return SPI_ERR_TIMEOUT;
        }
    }

    /* Now wait for BUSY to fall LOW — data is ready on the SDO line */
    while (LL_GPIO_IsInputPinSet(cfg->busy_pin.port, cfg->busy_pin.pin) != 0U) {
        if ((_tick_ms() - t0) >= cfg->busy_timeout_ms) {
            handle->busy  = false;
            handle->error = SPI_ERR_TIMEOUT;
            return SPI_ERR_TIMEOUT;
        }
    }

    /* ------------------------------------------------------------------
     *  Step 3: Perform a single 32-bit SPI read.
     *
     *  The LTC2338-18 shifts out 18 bits MSB-first beginning on the
     *  first SCK rising edge after BUSY falls.  With a 32-bit transfer
     *  the 18 valid bits occupy [31:14]; bits [13:0] are zeros/don't-care.
     *
     *  Raw word layout after transfer:
     *    Bit 31 (MSB) = D17 (ADC MSB)
     *    Bit 30       = D16
     *    ...
     *    Bit 14       = D0  (ADC LSB)
     *    Bits 13–0    = 0 (don't care)
     *
     *  Right-shift by 14 to get the 18-bit result in [17:0].
     * ------------------------------------------------------------------ */

    /* Clear any stale RX data */
    while (LL_SPI_IsActiveFlag_RXWNE(cfg->peripheral)) {
        (void)LL_SPI_ReceiveData32(cfg->peripheral);
    }

    /* Transmit a dummy 32-bit word to clock out the ADC data */
    t0 = _tick_ms();
    while (!LL_SPI_IsActiveFlag_TXP(cfg->peripheral)) {
        if ((_tick_ms() - t0) >= cfg->xfer_timeout_ms) {
            handle->busy  = false;
            handle->error = SPI_ERR_TIMEOUT;
            return SPI_ERR_TIMEOUT;
        }
    }
    LL_SPI_TransmitData32(cfg->peripheral, 0x00000000UL);

    /* Wait for RX word to arrive */
    t0 = _tick_ms();
    while (!LL_SPI_IsActiveFlag_RXWNE(cfg->peripheral)) {
        if ((_tick_ms() - t0) >= cfg->xfer_timeout_ms) {
            handle->busy  = false;
            handle->error = SPI_ERR_TIMEOUT;
            return SPI_ERR_TIMEOUT;
        }
    }

    /* Check for overrun before reading */
    if (LL_SPI_IsActiveFlag_OVR(cfg->peripheral)) {
        LL_SPI_ClearFlag_OVR(cfg->peripheral);
        handle->busy  = false;
        handle->error = SPI_ERR_OVERRUN;
        return SPI_ERR_OVERRUN;
    }

    uint32_t raw = LL_SPI_ReceiveData32(cfg->peripheral);

    /* ------------------------------------------------------------------
     *  Step 4: Extract the 18-bit result.
     *
     *  Mask to 18 bits after shifting to guard against any noise on the
     *  lower don't-care bits.
     * ------------------------------------------------------------------ */
    *result_out = (raw >> 14U) & 0x0003FFFFUL;

    handle->busy = false;
    return SPI_OK;
}

/* -------------------------------------------------------------------------- */

uint32_t SPI_GetAndClearError(SPI_Handle *handle)
{
    uint32_t err    = handle->error;
    handle->error   = 0U;
    return err;
}

/* -------------------------------------------------------------------------- */

void SPI_DeInit(SPI_Handle *handle)
{
    if (handle == NULL || handle->cfg == NULL) {
        return;
    }

    const SPI_Config *cfg = handle->cfg;

    LL_SPI_Disable(cfg->peripheral);
    LL_SPI_DeInit(cfg->peripheral);
    LL_APB1_GRP1_DisableClock(cfg->bus_clk_enable);

    /* Return GPIO pins to reset/analog state */
    LL_GPIO_InitTypeDef gpio_reset = {0};
    gpio_reset.Mode = LL_GPIO_MODE_ANALOG;
    gpio_reset.Pull = LL_GPIO_PULL_NO;

    gpio_reset.Pin = cfg->miso_pin.pin;    LL_GPIO_Init(cfg->miso_pin.port, &gpio_reset);
    gpio_reset.Pin = cfg->mosi_pin.pin;    LL_GPIO_Init(cfg->mosi_pin.port, &gpio_reset);
    gpio_reset.Pin = cfg->sck_pin.pin;     LL_GPIO_Init(cfg->sck_pin.port,  &gpio_reset);
    gpio_reset.Pin = cfg->cnv_pin.pin;     LL_GPIO_Init(cfg->cnv_pin.port,  &gpio_reset);
    gpio_reset.Pin = cfg->busy_pin.pin;    LL_GPIO_Init(cfg->busy_pin.port,  &gpio_reset);

    handle->initialised = false;
    handle->busy        = false;
    handle->error       = 0U;
}
