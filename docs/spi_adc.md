# SPI2 Driver and LTC2338-18 ADC

## Overview

SPI2 is configured as a master in Mode 0 (CPOL=0, CPHA=0) with 32-bit frames for reading the LTC2338-18 18-bit SAR ADC. The SPI bus is **shared** with the DRV8702 TEC drivers, DAC80508 DAC, and ADS7066 ADCs, which temporarily reconfigure it to 16-bit frames during register access.

## SPI2 Configuration

| Parameter | Value | Source |
|-----------|-------|--------|
| Peripheral | SPI2 |  |
| Kernel clock | PLL3P = 128 MHz | `LL_RCC_SetSPIClockSource(LL_RCC_SPI123_CLKSOURCE_PLL3P)` |
| Baud prescaler | DIV8 | 128 / 8 = **16 MHz SCK** |
| Mode | 0 (CPOL=0, CPHA=0) | Clock idles LOW, data sampled on rising edge |
| Data width | 32-bit | For LTC2338-18; DRV8702 switches to 16-bit |
| Bit order | MSB first |  |
| NSS | Software (internal SS HIGH) |  |
| FIFO threshold | 1 data frame | RXP asserts when 1 complete word ready |
| Direction | Full duplex (MOSI unused by ADC) |  |

## Pin Assignments

| Signal | Pin | Port | AF | Notes |
|--------|-----|------|----|-------|
| MISO | 36 | PC2 | AF5 | ADC data output (SDO) |
| MOSI | 37 | PC3 | AF5 | Not used by LTC2338-18 in normal mode |
| SCK | 128 | PA9 | AF5 | Serial clock |
| CNV | 74 | PE12 | GPIO out | Conversion trigger (active-HIGH pulse) |
| BUSY | 77 | PE15 | GPIO in | Conversion status (LOW = ready) |

**Note:** PC2 and PC3 are the analog-side "C" variants on the H735IGT6 BGA/LQFP-176 package.

## LTC2338-18 ADC Characteristics

| Parameter | Value |
|-----------|-------|
| Resolution | 18 bits |
| Interface | SPI-compatible serial |
| Conversion time | ~1 µs typical |
| Result range | 0 to 262143 (0x0003FFFF) |
| CNV pulse minimum | 30 ns (t_CNVH) |
| Data format | 18 bits MSB-first, followed by zeros |

## Read Sequence (`SPI_LTC2338_Read`)

```
Step 1: Pulse CNV HIGH
    ┌───┐
CNV ┘   └───────────────────────────────────────
    ← ≥30 ns →

Step 2: Wait for BUSY LOW
         ┌────┐
BUSY ────┘    └─────────────────────────────────
              ← ~1 µs conversion →

Step 3: 32-bit SPI transfer
SCK  ─┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌─── (32 clocks)
MISO ─[D17][D16]...[D1][D0][0][0]...[0]─────── (18 data + 14 zeros)

Step 4: Extract result
    raw_word >> 14 → 18-bit value in [17:0]
    Mask with 0x0003FFFF
```

### Timing at 16 MHz SCK

| Phase | Duration |
|-------|----------|
| CNV pulse | ~20 ns (10 NOPs at 480 MHz) |
| BUSY wait | ~1 µs (polled) |
| 32-bit transfer | 2 µs (32 bits / 16 MHz) |
| **Total per read** | **~3 µs** |

## Return Codes

```c
typedef enum {
    SPI_OK          = 0,    /* Successful read */
    SPI_ERR_TIMEOUT,        /* BUSY or RXP flag timed out */
    SPI_ERR_BUSY,           /* Handle already in use */
    SPI_ERR_OVERRUN,        /* RX FIFO overrun detected */
    SPI_ERR_NOT_INIT,       /* Handle not initialised or NULL */
} SPI_Status;
```

Timeouts are configured at 5 ms (`busy_timeout_ms` and `xfer_timeout_ms` in `SPI_Config`). At 16 MHz SCK and ~1 µs conversion, 5 ms is extremely conservative.

## SPI Bus Sharing

SPI2 is shared between three devices, each requiring different frame sizes and modes:

| Device | Data Width | SPI Mode | Notes |
|--------|-----------|----------|-------|
| LTC2338-18 (ADC) | 32-bit | Mode 0 (CPOL=0, CPHA=0) | Default bus config |
| DRV8702 x3 (TEC) | 16-bit | Mode 0 (CPOL=0, CPHA=0) | Reconfigures before/after access |
| DAC80508 (DAC) | 24-bit | Mode 1 (CPOL=0, CPHA=1) | Reconfigures data width + CPHA before/after access |
| ADS7066 x3 (Slow ADC) | 24-bit (reg) / 16-bit (data) | Mode 0 (CPOL=0, CPHA=0) | 24-bit for register access, 16-bit for data reads; reconfigures data width before/after |

Each device driver reconfigures SPI2 (disable, change settings, re-enable) before its transaction and restores the 32-bit Mode 0 default afterward. The DRV8702 driver handles the switch:

1. `LL_SPI_Disable(SPI2)`
2. `LL_SPI_SetDataWidth(SPI2, LL_SPI_DATAWIDTH_16BIT)`
3. `LL_SPI_Enable(SPI2)`
4. Assert nSCS, do 16-bit transfer, deassert nSCS
5. `LL_SPI_Disable(SPI2)`
6. `LL_SPI_SetDataWidth(SPI2, LL_SPI_DATAWIDTH_32BIT)` — restore
7. `LL_SPI_Enable(SPI2)`

The DAC80508 driver uses the same reconfigure pattern, additionally switching CPHA for Mode 1:

1. `LL_SPI_Disable(SPI2)`
2. `LL_SPI_SetDataWidth(SPI2, LL_SPI_DATAWIDTH_24BIT)`
3. `LL_SPI_SetClockPhase(SPI2, LL_SPI_PHASE_2EDGE)` — Mode 1
4. `LL_SPI_Enable(SPI2)`
5. Assert nCS (PD2), do 24-bit transfer, deassert nCS
6. `LL_SPI_Disable(SPI2)`
7. `LL_SPI_SetDataWidth(SPI2, LL_SPI_DATAWIDTH_32BIT)` — restore
8. `LL_SPI_SetClockPhase(SPI2, LL_SPI_PHASE_1EDGE)` — restore Mode 0
9. `LL_SPI_Enable(SPI2)`

**Concurrency constraint:** `SPI_LTC2338_Read()`, `DRV8702_WriteReg()`/`DRV8702_ReadReg()`, `DAC80508_WriteReg()`/`DAC80508_ReadReg()`, and `ADS7066_WriteReg()`/`ADS7066_ReadReg()`/`ADS7066_ReadChannel()` must not be called concurrently. Currently safe because there is no RTOS and all SPI access is sequential.

**ADC consumers:** Three commands read the LTC2338-18:
- `CMD_READ_ADC` (0x0C01) — single read, ISR context
- `CMD_BURST_ADC` (0x0C02) — 100-sample burst, main loop context
- `CMD_MEASURE_ADC` (0x0C03) — switch-controlled 100-sample burst with TIM6 deterministic timing, main loop context. Includes Vpp peak-to-peak calculation using ±10.24 V bipolar scaling (LSB = 20.48/262144).

## Chip Select Lines

Seven chip-select lines on GPIOD (PD0–PD6) are configured by each device driver's Init() function and driven HIGH (deasserted):

| Pin | Assigned To |
|-----|-------------|
| PD0 | DRV8702 instance 2 nSCS |
| PD1 | DRV8702 instance 1 nSCS |
| PD2 | DAC80508 nCS |
| PD3 | ADS7066 instance 3 nCS |
| PD4 | ADS7066 instance 2 nCS |
| PD5 | ADS7066 instance 1 nCS |
| PD6 | DRV8702 instance 3 nSCS |

The LTC2338-18 does not use a chip select — it is always selected and controlled via the CNV/BUSY handshake.

## Known Signal Characteristics

The LTC2338-18 ADC input carries two superimposed signals:

- **Signal of interest:** ~10 kHz waveform (the droplet sensing signal)
- **Mains-coupled noise:** ~58 Hz sinusoid, approximately ±1 V amplitude (60 Hz mains hum)

Because the 100-sample burst window (~300 µs) is much shorter than one 58 Hz cycle (~17 ms), each burst captures the 10 kHz signal riding on a quasi-DC offset determined by the instantaneous phase of the 58 Hz noise. This causes the apparent DC level to shift between bursts (e.g., +2 V to −2 V one burst, +3 V to −1 V the next).

**Suspected sources (to be confirmed with bench supply test):**
1. **Conducted ripple:** Mains ripple passing through the off-the-shelf wall wart power supply onto the board's power rails.
2. **Radiated EMI pickup:** ADC input traces or wiring acting as antennas, picking up 60 Hz from nearby mains wiring, lights, or equipment via capacitive/inductive coupling.

Testing with a regulated bench supply (e.g., BK Precision) will help differentiate: if noise disappears, the wall wart is the source (conducted). If noise persists, it is radiated pickup from the environment — which can be further confirmed by observing amplitude changes when repositioning the board relative to mains sources.

**Mitigation options:**
- **Mean subtraction (GUI/firmware):** Subtract the burst mean to re-center each capture around 0 V. Simple and effective for visualization.
- **Hardware filtering:** Add a high-pass filter or improve analog ground routing to attenuate the 58 Hz coupling at the source.
- **Digital notch filter:** Apply a 58–60 Hz notch filter in firmware or GUI for quantitative measurements.

## API Reference

### `SPI_Init(SPI_Handle *handle)`

Initialize SPI2 peripheral and GPIO pins. Sets CNV LOW, marks handle as initialised.

### `SPI_LTC2338_Read(SPI_Handle *handle, uint32_t *result_out)`

Trigger conversion and read 18-bit result. Returns `SPI_OK` with result in `*result_out`, or an error code. Safe to call from ISR context (completes in ~3 µs).

### `SPI_GetAndClearError(SPI_Handle *handle)`

Returns and clears the last sticky error flags from the handle.

### `SPI_DeInit(SPI_Handle *handle)`

Disable SPI2 peripheral and release GPIO pins to analog/reset state.
