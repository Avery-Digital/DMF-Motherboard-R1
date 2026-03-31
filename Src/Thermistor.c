/**
 * @file  Thermistor.c
 * @brief NTC thermistor conversion for SC50G104WH (Material Type G, R25 = 100K).
 */

#include "Thermistor.h"
#include <math.h>

/* ---- Circuit constants -------------------------------------------------- */
#define VREF          4.096f          /* ADS7066 reference voltage            */
#define ADC_COUNTS    65536.0f        /* 16-bit ADC                           */
#define I_SOURCE      100.0e-6f       /* 100 uA constant-current source       */
#define R1            10000.0f        /* Upper divider resistor               */
#define R2            1000.0f         /* Lower divider resistor (ADC tap)     */
#define R_DIVIDER     (R1 + R2)       /* 11 K total                           */
#define R25           100000.0f       /* Thermistor resistance at 25 C        */

/* ---- Steinhart-Hart coefficients (Material Type G, four ranges) --------- *
 *  1/T = a + b*ln(Rt/R25) + c*ln(Rt/R25)^2 + d*ln(Rt/R25)^3              *
 *  T in Kelvin.  Ranges defined by Rt/R25 ratio boundaries.                */

typedef struct {
    float ratio_hi;   /* upper Rt/R25 boundary (inclusive)  */
    float ratio_lo;   /* lower Rt/R25 boundary (inclusive)  */
    float a, b, c, d;
} SH_Range;

static const SH_Range sh_ranges[] = {
    /* -50 to 0 C   (Rt/R25: 85.730 .. 3.5223) */
    { 85.730f,  3.5223f,
      3.3537950e-03f, 2.4096581e-04f, 2.2453225e-06f, 1.1817106e-07f },
    /*  0 to 50 C   (Rt/R25: 3.5223 .. 0.33620) */
    {  3.5223f, 0.33620f,
      3.3540142e-03f, 2.4060636e-04f, 2.4402986e-06f, 8.0075806e-08f },
    /* 50 to 100 C  (Rt/R25: 0.33620 .. 0.05619) */
    {  0.33620f, 0.05619f,
      3.3541651e-03f, 2.4087966e-04f, 2.5742490e-06f, 8.8745970e-08f },
    /* 100 to 150 C (Rt/R25: 0.05619 .. 0.01381) */
    {  0.05619f, 0.01381f,
      3.3357228e-03f, 2.2502940e-04f, -1.9459544e-06f, -3.4181652e-07f },
};

#define SH_RANGE_COUNT  (sizeof(sh_ranges) / sizeof(sh_ranges[0]))

/* ------------------------------------------------------------------------- */

float Thermistor_AdcToResistance(uint16_t adc_code)
{
    float voltage   = (float)adc_code * (VREF / ADC_COUNTS);
    float v_node    = voltage * (R_DIVIDER / R2);       /* undo divider */
    float r_parallel = v_node / I_SOURCE;

    if (r_parallel >= R_DIVIDER)
        return NAN;     /* thermistor absent or open */

    return (r_parallel * R_DIVIDER) / (R_DIVIDER - r_parallel);
}

float Thermistor_AdcToTempC(uint16_t adc_code)
{
    float r_th = Thermistor_AdcToResistance(adc_code);
    if (isnan(r_th))
        return NAN;

    float ratio = r_th / R25;

    /* Select the correct Steinhart-Hart range */
    const SH_Range *range = NULL;
    for (uint32_t i = 0U; i < SH_RANGE_COUNT; i++) {
        if (ratio <= sh_ranges[i].ratio_hi && ratio >= sh_ranges[i].ratio_lo) {
            range = &sh_ranges[i];
            break;
        }
    }

    if (range == NULL)
        return NAN;     /* outside -50 .. 150 C */

    float ln_r = logf(ratio);
    float ln_r2 = ln_r * ln_r;
    float ln_r3 = ln_r2 * ln_r;
    float inv_t = range->a + range->b * ln_r + range->c * ln_r2 + range->d * ln_r3;

    return (1.0f / inv_t) - 273.15f;
}
