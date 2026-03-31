/**
 * @file  Thermistor.h
 * @brief NTC thermistor conversion for SC50G104WH (Material Type G, R25 = 100K).
 *
 * Circuit: 100 uA constant-current source driving R_th in parallel with a
 * resistor divider (R1 = 10K, R2 = 1K).  The ADS7066 ADC reads across R2.
 *
 * Conversion chain:
 *   ADC code  ->  voltage  ->  R_th  ->  temperature (Steinhart-Hart)
 *
 * Steinhart-Hart coefficients from Amphenol Thermometrics
 * AAS-913-318C, Material Type G, four piecewise ranges (-50 to 150 C).
 */

#ifndef THERMISTOR_H
#define THERMISTOR_H

#include <stdint.h>

/** Convert a raw 16-bit ADS7066 code to temperature in degrees C.
 *  Returns NaN if the reading is out of range or the thermistor is absent. */
float Thermistor_AdcToTempC(uint16_t adc_code);

/** Convert a raw 16-bit ADS7066 code to thermistor resistance in ohms.
 *  Returns NaN if the thermistor is absent (R_parallel >= 11 K). */
float Thermistor_AdcToResistance(uint16_t adc_code);

#endif /* THERMISTOR_H */
