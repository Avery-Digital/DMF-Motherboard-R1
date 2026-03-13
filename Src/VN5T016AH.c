/*******************************************************************************
 * @file    Src/VN5T016AH.c
 * @author  Cam
 * @brief   VN5T016AHTR-E — Single-Channel High-Side Load Switch Driver
 *
 *          10 instances, each with a unique enable (IN) pin.
 *          All enable pins are GPIO push-pull outputs.
 *          HIGH = load ON, LOW = load OFF.
 *
 *          The VN5T016AH-E latches off on overload/overtemperature and
 *          requires a low pulse on FR_Stby to reset.  FR_Stby handling
 *          can be added later if needed.
 *
 *          Reference: VN5T016AH-E datasheet (DS9252)
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#include "VN5T016AH.h"

/* ==========================================================================
 *  PIN TABLE
 *
 *  Indexed by LoadSwitch_ID.  Each entry is a PinConfig for the enable pin.
 * ========================================================================== */
static const PinConfig load_switch_pins[LOAD_COUNT] = {

    /* [0] VALVE1 — PE10, Pin 72 */
    [LOAD_VALVE1] = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOE,
        .port   = GPIOE,
        .pin    = LL_GPIO_PIN_10,
        .mode   = LL_GPIO_MODE_OUTPUT,
        .af     = 0U,
        .speed  = LL_GPIO_SPEED_FREQ_LOW,
        .pull   = LL_GPIO_PULL_NO,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* [1] VALVE2 — PE8, Pin 68 */
    [LOAD_VALVE2] = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOE,
        .port   = GPIOE,
        .pin    = LL_GPIO_PIN_8,
        .mode   = LL_GPIO_MODE_OUTPUT,
        .af     = 0U,
        .speed  = LL_GPIO_SPEED_FREQ_LOW,
        .pull   = LL_GPIO_PULL_NO,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* [2] MICROPLATE — PE7, Pin 67 */
    [LOAD_MICROPLATE] = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOE,
        .port   = GPIOE,
        .pin    = LL_GPIO_PIN_7,
        .mode   = LL_GPIO_MODE_OUTPUT,
        .af     = 0U,
        .speed  = LL_GPIO_SPEED_FREQ_LOW,
        .pull   = LL_GPIO_PULL_NO,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* [3] FAN — PG2, Pin 110 */
    [LOAD_FAN] = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOG,
        .port   = GPIOG,
        .pin    = LL_GPIO_PIN_2,
        .mode   = LL_GPIO_MODE_OUTPUT,
        .af     = 0U,
        .speed  = LL_GPIO_SPEED_FREQ_LOW,
        .pull   = LL_GPIO_PULL_NO,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* [4] TEC1_PWR — PK2, Pin 109 */
    [LOAD_TEC1_PWR] = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOK,
        .port   = GPIOK,
        .pin    = LL_GPIO_PIN_2,
        .mode   = LL_GPIO_MODE_OUTPUT,
        .af     = 0U,
        .speed  = LL_GPIO_SPEED_FREQ_LOW,
        .pull   = LL_GPIO_PULL_NO,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* [5] TEC2_PWR — PK1, Pin 108 */
    [LOAD_TEC2_PWR] = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOK,
        .port   = GPIOK,
        .pin    = LL_GPIO_PIN_1,
        .mode   = LL_GPIO_MODE_OUTPUT,
        .af     = 0U,
        .speed  = LL_GPIO_SPEED_FREQ_LOW,
        .pull   = LL_GPIO_PULL_NO,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* [6] TEC3_PWR — PJ11, Pin 104 */
    [LOAD_TEC3_PWR] = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOJ,
        .port   = GPIOJ,
        .pin    = LL_GPIO_PIN_11,
        .mode   = LL_GPIO_MODE_OUTPUT,
        .af     = 0U,
        .speed  = LL_GPIO_SPEED_FREQ_LOW,
        .pull   = LL_GPIO_PULL_NO,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* [7] ASSEMBLY_STATION — PJ9, Pin 102 */
    [LOAD_ASSEMBLY_STATION] = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOJ,
        .port   = GPIOJ,
        .pin    = LL_GPIO_PIN_9,
        .mode   = LL_GPIO_MODE_OUTPUT,
        .af     = 0U,
        .speed  = LL_GPIO_SPEED_FREQ_LOW,
        .pull   = LL_GPIO_PULL_NO,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* [8] DAUGHTER_1 — PE6, Pin 5 */
    [LOAD_DAUGHTER_1] = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOE,
        .port   = GPIOE,
        .pin    = LL_GPIO_PIN_6,
        .mode   = LL_GPIO_MODE_OUTPUT,
        .af     = 0U,
        .speed  = LL_GPIO_SPEED_FREQ_LOW,
        .pull   = LL_GPIO_PULL_NO,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },

    /* [9] DAUGHTER_2 — PD14, Pin 97 */
    [LOAD_DAUGHTER_2] = {
        .clk    = LL_AHB4_GRP1_PERIPH_GPIOD,
        .port   = GPIOD,
        .pin    = LL_GPIO_PIN_14,
        .mode   = LL_GPIO_MODE_OUTPUT,
        .af     = 0U,
        .speed  = LL_GPIO_SPEED_FREQ_LOW,
        .pull   = LL_GPIO_PULL_NO,
        .output = LL_GPIO_OUTPUT_PUSHPULL,
    },
};

/* Commanded state tracking */
static bool load_state[LOAD_COUNT];
static bool initialised = false;

/* ==========================================================================
 *  LoadSwitch_Init
 * ========================================================================== */
LoadSwitch_Status LoadSwitch_Init(void)
{
    for (uint8_t i = 0U; i < LOAD_COUNT; i++) {
        Pin_Init(&load_switch_pins[i]);

        /* Default all loads OFF */
        LL_GPIO_ResetOutputPin(load_switch_pins[i].port,
                               load_switch_pins[i].pin);
        load_state[i] = false;
    }

    initialised = true;
    return LOADSW_OK;
}

/* ==========================================================================
 *  LoadSwitch_On
 * ========================================================================== */
LoadSwitch_Status LoadSwitch_On(LoadSwitch_ID id)
{
    if (!initialised) return LOADSW_ERR_NOT_INIT;
    if (id >= LOAD_COUNT) return LOADSW_ERR_PARAM;

    LL_GPIO_SetOutputPin(load_switch_pins[id].port,
                         load_switch_pins[id].pin);
    load_state[id] = true;

    return LOADSW_OK;
}

/* ==========================================================================
 *  LoadSwitch_Off
 * ========================================================================== */
LoadSwitch_Status LoadSwitch_Off(LoadSwitch_ID id)
{
    if (!initialised) return LOADSW_ERR_NOT_INIT;
    if (id >= LOAD_COUNT) return LOADSW_ERR_PARAM;

    LL_GPIO_ResetOutputPin(load_switch_pins[id].port,
                           load_switch_pins[id].pin);
    load_state[id] = false;

    return LOADSW_OK;
}

/* ==========================================================================
 *  LoadSwitch_Set
 * ========================================================================== */
LoadSwitch_Status LoadSwitch_Set(LoadSwitch_ID id, bool state)
{
    return state ? LoadSwitch_On(id) : LoadSwitch_Off(id);
}

/* ==========================================================================
 *  LoadSwitch_IsOn
 * ========================================================================== */
bool LoadSwitch_IsOn(LoadSwitch_ID id)
{
    if (id >= LOAD_COUNT) return false;
    return load_state[id];
}

/* ==========================================================================
 *  LoadSwitch_AllOff
 * ========================================================================== */
void LoadSwitch_AllOff(void)
{
    for (uint8_t i = 0U; i < LOAD_COUNT; i++) {
        LL_GPIO_ResetOutputPin(load_switch_pins[i].port,
                               load_switch_pins[i].pin);
        load_state[i] = false;
    }
}
