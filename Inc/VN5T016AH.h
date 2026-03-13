/*******************************************************************************
 * @file    Inc/VN5T016AH.h
 * @author  Cam
 * @brief   VN5T016AHTR-E — Single-Channel High-Side Load Switch Driver
 *
 *          10 instances on the DMF Motherboard, each controlling a different
 *          power rail.  The enable (IN) pin is GPIO output — HIGH turns the
 *          load on, LOW turns it off.
 *
 *          The VN5T016AH-E is a 24V automotive high-side driver with:
 *            - 16 mΩ typical RON
 *            - 60 A current limit
 *            - Overload/overtemperature latch-off (reset via FR_Stby)
 *            - Analog current sense output (CS pin)
 *            - 3.0V CMOS compatible input
 *
 *          This driver handles only the enable pin.  Current sense and
 *          FR_Stby can be added later if needed.
 *
 *          Reference: VN5T016AH-E datasheet (DS9252)
 *******************************************************************************
 * Copyright (c) 2026
 * All rights reserved.
 ******************************************************************************/

#ifndef VN5T016AH_H
#define VN5T016AH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bsp.h"

/* ==========================================================================
 *  ENUMERATIONS
 * ========================================================================== */

typedef enum {
    LOADSW_OK           = 0,
    LOADSW_ERR_NOT_INIT = 1,   /* Handle used before LoadSwitch_Init()       */
    LOADSW_ERR_PARAM    = 2,   /* Invalid parameter                          */
} LoadSwitch_Status;

/* ==========================================================================
 *  LOAD IDENTIFIERS
 *
 *  Human-readable names for each load switch instance.
 * ========================================================================== */
typedef enum {
    LOAD_VALVE1          = 0,
    LOAD_VALVE2          = 1,
    LOAD_MICROPLATE      = 2,
    LOAD_FAN             = 3,
    LOAD_TEC1_PWR        = 4,
    LOAD_TEC2_PWR        = 5,
    LOAD_TEC3_PWR        = 6,
    LOAD_ASSEMBLY_STATION = 7,
    LOAD_DAUGHTER_1      = 8,
    LOAD_DAUGHTER_2      = 9,
    LOAD_COUNT           = 10,
} LoadSwitch_ID;

/* ==========================================================================
 *  PUBLIC API
 * ========================================================================== */

/**
 * @brief  Initialise all 10 load switch enable pins.
 *         All outputs default to OFF (LOW) after init.
 */
LoadSwitch_Status LoadSwitch_Init(void);

/**
 * @brief  Turn on a load switch (EN pin HIGH).
 * @param  id  Load identifier (LOAD_VALVE1 ... LOAD_DAUGHTER_2)
 */
LoadSwitch_Status LoadSwitch_On(LoadSwitch_ID id);

/**
 * @brief  Turn off a load switch (EN pin LOW).
 * @param  id  Load identifier
 */
LoadSwitch_Status LoadSwitch_Off(LoadSwitch_ID id);

/**
 * @brief  Set a load switch to a specific state.
 * @param  id     Load identifier
 * @param  state  true = ON, false = OFF
 */
LoadSwitch_Status LoadSwitch_Set(LoadSwitch_ID id, bool state);

/**
 * @brief  Read the current commanded state of a load switch.
 * @param  id  Load identifier
 * @return true if the load is commanded ON, false if OFF
 */
bool LoadSwitch_IsOn(LoadSwitch_ID id);

/**
 * @brief  Turn off all load switches.
 */
void LoadSwitch_AllOff(void);

#ifdef __cplusplus
}
#endif

#endif /* VN5T016AH_H */
