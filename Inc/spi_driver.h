/* ==========================================================================
 *  spi.h — SPI driver interface (STM32H735, LL library)
 *
 *  Supports SPI2 in master mode, Mode 0 (CPOL=0, CPHA=0), 32-bit transfers.
 *  Designed for use with the LTC2338-18 18-bit ADC.
 *
 *  Pin assignments:
 *      MISO  : PC2_C  (Pin 36)   AF5
 *      MOSI  : PC3_C  (Pin 37)   AF5  — unused by LTC2338-18 in normal mode
 *      SCK   : PA9    (Pin 128)  AF5
 *      CNV   : PE12   (Pin 74)   GPIO output — conversion trigger
 *      BUSY  : PE15   (Pin 77)   GPIO input  — conversion complete flag
 *
 *  SPI_Config, SPI_Handle, and the spi2_cfg / spi2_handle instances are
 *  defined in bsp.h / bsp.c.  This file exposes only the driver API.
 * ========================================================================== */

#ifndef SPI_H
#define SPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "bsp.h"

/* --------------------------------------------------------------------------
 *  Return codes
 * -------------------------------------------------------------------------- */
typedef enum {
    SPI_OK          = 0,
    SPI_ERR_TIMEOUT,        /* Peripheral did not respond within timeout */
    SPI_ERR_BUSY,           /* Bus or ADC already busy                   */
    SPI_ERR_OVERRUN,        /* RX overrun detected                       */
    SPI_ERR_NOT_INIT,       /* Handle used before initialisation         */
} SPI_Status;

/* --------------------------------------------------------------------------
 *  Public API
 * -------------------------------------------------------------------------- */

/**
 * @brief  Initialise SPI2 peripheral and all associated GPIO pins.
 *         Must be called once before any other SPI function.
 *
 * @param  handle   Pointer to an SPI_Handle bound to a valid SPI_Config.
 * @return SPI_OK on success, SPI_ERR_NOT_INIT if handle/cfg is NULL.
 */
SPI_Status SPI_Init(SPI_Handle *handle);

/**
 * @brief  Trigger an ADC conversion on the LTC2338-18 and read back the
 *         18-bit result via a single 32-bit SPI transfer.
 *
 *         Sequence:
 *           1. Assert CNV HIGH for >= t_CNVH (30 ns min).
 *           2. De-assert CNV LOW — conversion starts on falling edge.
 *           3. Wait for BUSY to go LOW (conversion complete, ~1 us typ).
 *           4. Clock out 32 bits on SCK; ADC shifts out 18-bit result MSB-first.
 *           5. Right-shift raw word by 14 to extract the 18-bit value.
 *
 * @param  handle       Pointer to an initialised SPI_Handle.
 * @param  result_out   Pointer to store the 18-bit ADC result (0-262143).
 * @return SPI_OK, SPI_ERR_TIMEOUT, SPI_ERR_BUSY, or SPI_ERR_OVERRUN.
 */
SPI_Status SPI_LTC2338_Read(SPI_Handle *handle, uint32_t *result_out);

/**
 * @brief  Return the last sticky error flags and clear them on the handle.
 *
 * @param  handle   Pointer to an initialised SPI_Handle.
 * @return Accumulated LL error flag word since last call.
 */
uint32_t SPI_GetAndClearError(SPI_Handle *handle);

/**
 * @brief  De-initialise the SPI peripheral and release GPIO pins to
 *         analog/reset state.  Safe to call if SPI_Init() was never called.
 *
 * @param  handle   Pointer to an SPI_Handle.
 */
void SPI_DeInit(SPI_Handle *handle);

#ifdef __cplusplus
}
#endif

#endif /* SPI_H */
