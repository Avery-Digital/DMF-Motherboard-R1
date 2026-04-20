# Thermistor Conversion Module

## Hardware

**Thermistor:** SC50G104WH (Amphenol Thermometrics, Material Type G, NTC, R25 = 100 KΩ)

**Datasheet:** AAS-913-318C-Temperature-resistance-curves-071816-web.pdf, page 13 (Material Type G)

### Measurement Circuit

```
100 µA ──┬── R_th (thermistor)
         │
         ├── R1 = 10 KΩ
         │
         ├── R2 = 1 KΩ ──── ADC (ADS7066 inst3)
         │
        GND
```

The 100 µA constant-current source drives the thermistor in parallel with the R1+R2 divider (11 KΩ total). The ADS7066 reads the voltage across R2.

- **ADC:** ADS7066 instance 3, channels 0–5 (6 thermistors)
- **Vref:** 4.096 V (internal reference)
- **Resolution:** 16-bit (LSB = 62.5 µV)

### Conversion Chain

1. **ADC code → voltage:** `V_adc = adc_code × (4.096 / 65536)`
2. **Voltage → node voltage:** `V_node = V_adc × 11` (undo R2/(R1+R2) divider)
3. **Node voltage → parallel resistance:** `R_par = V_node / 100 µA`
4. **Parallel resistance → thermistor resistance:** `R_th = (R_par × 11000) / (11000 - R_par)`
5. **Resistance → temperature:** Steinhart-Hart equation (see below)

### Steinhart-Hart Coefficients

```
1/T = a + b·ln(Rt/R25) + c·ln(Rt/R25)² + d·ln(Rt/R25)³
```

Where T is in Kelvin and R25 = 100 KΩ.

Four piecewise ranges from the Material Type G datasheet:

| Temperature Range | Rt/R25 Range | a | b | c | d |
|-------------------|-------------|---|---|---|---|
| -50 to 0 °C | 85.730 – 3.5223 | 3.3537950E-03 | 2.4096581E-04 | 2.2453225E-06 | 1.1817106E-07 |
| 0 to 50 °C | 3.5223 – 0.33620 | 3.3540142E-03 | 2.4060636E-04 | 2.4402986E-06 | 8.0075806E-08 |
| 50 to 100 °C | 0.33620 – 0.05619 | 3.3541651E-03 | 2.4087966E-04 | 2.5742490E-06 | 8.8745970E-08 |
| 100 to 150 °C | 0.05619 – 0.01381 | 3.3357228E-03 | 2.2502940E-04 | -1.9459544E-06 | -3.4181652E-07 |

The correct range is selected by the Rt/R25 ratio (calculated from the resistance, before knowing the temperature).

## Firmware API

**Files:** `Inc/Thermistor.h`, `Src/Thermistor.c`

```c
/* Convert raw 16-bit ADS7066 code to temperature in °C.
 * Returns NaN if out of range or thermistor absent. */
float Thermistor_AdcToTempC(uint16_t adc_code);

/* Convert raw 16-bit ADS7066 code to thermistor resistance in Ω.
 * Returns NaN if thermistor absent (R_parallel >= 11 KΩ). */
float Thermistor_AdcToResistance(uint16_t adc_code);
```

Uses `float` (single-precision) and `logf()` from `<math.h>`. The Cortex-M7 FPU handles single-precision natively. Requires `-lm` linker flag (add `m` to Libraries in STM32CubeIDE linker settings if not already present).

## Command Integration

`CMD_THERM1`–`CMD_THERM6` (`0x0C20`–`0x0C25`) call `Thermistor_AdcToTempC()`, scale the result by 100, and return it as a 2-byte big-endian signed int16 (0.01 °C resolution) in the response payload. `INT16_MIN` (`0x8000`) is reserved as the read-error sentinel.

## Typical Values

| Condition | ADC Code | Voltage | R_th | Temperature |
|-----------|----------|---------|------|-------------|
| No thermistor | ~1600 | ~100 mV | ∞ (NaN) | NaN |
| Room temp (~22 °C) | ~1463 | ~91 mV | ~117 KΩ | ~22 °C |
