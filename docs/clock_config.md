# Clock Configuration

## Clock Sources

| Source | Frequency | Purpose |
|--------|-----------|---------|
| HSE | 12 MHz | External crystal on PH0-OSC_IN (pin 31) |
| HSI | 64 MHz | Internal RC (not used, HSE preferred for accuracy) |
| LSI | 32 kHz | Internal RC for IWDG/RTC (not currently used) |
| LSE | 32.768 kHz | External crystal for RTC (not currently populated) |

## PLL Configuration

### PLL1 — System Clock

```
HSE (12 MHz) → ÷ DIVM1 (3) → 4 MHz VCO input
                                  │
                             × DIVN1 (120)
                                  │
                            480 MHz VCO output
                           ╱          ╲
                     ÷ DIVP1 (1)   ÷ DIVQ1 (2)
                          │              │
                    480 MHz SYSCLK   240 MHz (available)
```

| Parameter | Value | Result |
|-----------|-------|--------|
| HSE | 12 MHz | Input clock |
| DIVM1 | 3 | 12 / 3 = 4 MHz VCO input (2–4 MHz range) |
| DIVN1 | 120 | 4 × 120 = 480 MHz VCO output (wide range) |
| DIVP1 | 1 | 480 / 1 = **480 MHz → SYSCLK** |
| DIVQ1 | 2 | 480 / 2 = 240 MHz |
| DIVR1 | 2 | 480 / 2 = 240 MHz (not enabled) |
| VCO input range | 2–4 MHz | `LL_RCC_PLLINPUTRANGE_2_4` |
| VCO output range | Wide | `LL_RCC_PLLVCORANGE_WIDE` |

### PLL2 — USART, ADC, DAC Peripheral Clocks

```
HSE (12 MHz) → ÷ DIVM2 (3) → 4 MHz VCO input
                                  │
                             × DIVN2 (64)
                                  │
                            256 MHz VCO output
                           ╱     │      ╲
                     ÷ DIVP2(2) ÷DIVQ2(2) ÷DIVR2(2)
                          │        │         │
                     128 MHz   128 MHz   128 MHz
                    (ADC,DAC) (USART)   (not enabled)
```

| Parameter | Value | Result |
|-----------|-------|--------|
| DIVM2 | 3 | 4 MHz VCO input |
| DIVN2 | 64 | 256 MHz VCO output |
| DIVP2 | 2 | **128 MHz → ADC, DAC kernel clock** |
| DIVQ2 | 2 | **128 MHz → USART10 kernel clock** |
| DIVR2 | 2 | 128 MHz (not enabled) |

### PLL3 — SPI, I2C Peripheral Clocks

```
HSE (12 MHz) → ÷ DIVM3 (3) → 4 MHz VCO input
                                  │
                             × DIVN3 (64)
                                  │
                            256 MHz VCO output
                           ╱     │      ╲
                     ÷ DIVP3(2) ÷DIVQ3(2) ÷DIVR3(2)
                          │        │         │
                     128 MHz   128 MHz   128 MHz
                  (not enabled) (SPI)     (I2C)
```

| Parameter | Value | Result |
|-----------|-------|--------|
| DIVM3 | 3 | 4 MHz VCO input |
| DIVN3 | 64 | 256 MHz VCO output |
| DIVP3 | 2 | 128 MHz (not enabled) |
| DIVQ3 | 2 | **128 MHz → SPI kernel clock** |
| DIVR3 | 2 | **128 MHz → I2C1 kernel clock** |

## Bus Prescalers

```
SYSCLK (480 MHz)
    │
    ├── D1CPRE ÷1 → 480 MHz CPU (Cortex-M7)
    │
    ├── HPRE ÷2 → 240 MHz AHB (AHB1, AHB2, AHB3, AHB4)
    │
    ├── D1PPRE ÷2 → 120 MHz APB3 (D1 domain)
    │
    ├── D2PPRE1 ÷2 → 120 MHz APB1 (I2C1, TIMers, etc.)
    │
    ├── D2PPRE2 ÷2 → 120 MHz APB2 (USART10, SPI4, etc.)
    │
    └── D3PPRE ÷2 → 120 MHz APB4 (SYSCFG, LPTIM, etc.)
```

| Bus | Prescaler | Frequency | Peripherals |
|-----|-----------|-----------|-------------|
| CPU | D1CPRE ÷1 | 480 MHz | Cortex-M7 core |
| AHB | HPRE ÷2 | 240 MHz | DMA1, DMA2, GPIO, flash |
| APB1 | D2PPRE1 ÷2 | 120 MHz | I2C1, I2C2, TIM2–TIM7 |
| APB2 | D2PPRE2 ÷2 | 120 MHz | USART10, SPI4, TIM1/TIM8 |
| APB3 | D1PPRE ÷2 | 120 MHz | LTDC, WWDG |
| APB4 | D3PPRE ÷2 | 120 MHz | SYSCFG, LPTIM, LPUART |

## Power Configuration

| Parameter | Setting |
|-----------|---------|
| Supply | SMPS 2.5V + LDO |
| Voltage scaling | VOS0 (highest performance) |
| Flash wait states | 4 WS (required for 480 MHz at VOS0) |

## Peripheral Kernel Clock Assignments

The STM32H7 allows each peripheral to select its clock source independently from the bus clock. This is configured via the RCC kernel clock mux registers.

| Peripheral | Kernel Clock Source | Frequency | Notes |
|------------|-------------------|-----------|-------|
| USART10 | PLL2Q | 128 MHz | 115200 baud, 0.01% error |
| SPI2 | PLL3P | 128 MHz | 16 MHz SCK (DIV8); LTC2338-18 + DRV8702 x3 + DAC80508 + ADS7066 x3 |
| I2C1 | PLL3R | 128 MHz | 400 kHz Fast Mode |
| ADC (future) | PLL2P | 128 MHz | Available but not yet used |

**Note:** `Clock_Config.c` sets the SPI kernel clock source to `LL_RCC_SPI123_CLKSOURCE_PLL3P`. The SPI2 baud prescaler is DIV8, giving 128 / 8 = 16 MHz SCK.

## Baud Rate Accuracy

### USART10 at 115200 baud

```
Kernel clock: 128,000,000 Hz (PLL2Q)
BRR = 128000000 / 115200 = 1111.11
Actual baud = 128000000 / 1111 = 115,211.5
Error = (115211.5 - 115200) / 115200 = 0.01%
```

UART specification allows up to ~2% error. 0.01% is essentially perfect.

### I2C1 at 400 kHz

I2C timing register value `0x30410F13` is calculated for 400 kHz Fast Mode with a 128 MHz kernel clock (PLL3R), analog filter enabled, no digital filter. Regenerate using the STM32CubeMX I2C timing calculator if the kernel clock changes.

## Changing Clock Frequencies

All clock parameters are in the `sys_clk_config` struct in `bsp.c`. To change:

1. Modify the PLL dividers and derived frequency values in `sys_clk_config`
2. Update flash wait states if SYSCLK changes (see reference manual Table 15)
3. Recalculate the I2C timing register if the I2C kernel clock changes
4. Verify USART baud rate error is within spec at the new kernel clock
5. Update the derived `_hz` fields so `LL_Init1msTick()` and `LL_USART_SetBaudRate()` get correct values
