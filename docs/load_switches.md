# VN5T016AHTR-E — High-Side Load Switches

## Overview

10 instances of the VN5T016AHTR-E single-channel high-side load switch control power to various subsystems on the DMF Motherboard. Each instance is controlled by a single GPIO enable pin — HIGH turns the load on, LOW turns it off.

All loads default to **OFF** after initialization.

Reference: VN5T016AH-E datasheet (DS9252)

## VN5T016AH-E Characteristics

| Parameter | Value |
|-----------|-------|
| Operating voltage | 8–36V (24V nominal) |
| Typical RON | 16 mΩ |
| Current limit | 60 A typical |
| Input threshold HIGH | 2.1V (3.3V CMOS compatible) |
| Input threshold LOW | 0.9V |
| Turn-on delay | 55 µs typical |
| Turn-off delay | 53 µs typical |
| Standby current | 2 µA typical |

## Features

- Overload and short-to-ground latch-off protection
- Thermal shutdown with latch-off
- Analog current sense output (CS pin — not currently used)
- Fault reset via FR_Stby pin (not currently used)
- Reverse battery protection

## Instance Pin Assignments

| ID | Name | Enable Pin | Pin # | Port | Load Description |
|----|------|-----------|-------|------|------------------|
| 0 | VALVE1 | PE10 | 72 | GPIOE | Valve 1 |
| 1 | VALVE2 | PE8 | 68 | GPIOE | Valve 2 |
| 2 | MICROPLATE | PE7 | 67 | GPIOE | Microplate |
| 3 | FAN | PG2 | 110 | GPIOG | Fan |
| 4 | TEC1_PWR | PK2 | 109 | GPIOK | TEC 1 power supply |
| 5 | TEC2_PWR | PK1 | 108 | GPIOK | TEC 2 power supply |
| 6 | TEC3_PWR | PJ11 | 104 | GPIOJ | TEC 3 power supply |
| 7 | ASSEMBLY_STATION | PJ9 | 102 | GPIOJ | Assembly station |
| 8 | DAUGHTER_1 | PE6 | 5 | GPIOE | Daughter board 1 |
| 9 | DAUGHTER_2 | PD14 | 97 | GPIOD | Daughter board 2 |

All pins configured as push-pull outputs, low speed, no pull.

## TEC Power Relationship

The TEC load switches (TEC1_PWR, TEC2_PWR, TEC3_PWR) control the power supply to each TEC circuit. The DRV8702 H-bridge drivers control the direction and enable of current through the TEC elements. Both must be active for a TEC to operate:

1. `LoadSwitch_On(LOAD_TEC1_PWR)` — powers the TEC1 circuit
2. `DRV8702_TEC_Heat(&drv8702_1_handle)` — drives current through TEC1

## API Reference

### `LoadSwitch_Init(void)`

Initialize all 10 enable pins as outputs, drive all LOW (loads OFF). Error code `0x60` on failure.

### `LoadSwitch_On(LoadSwitch_ID id)`

Turn on a specific load (EN pin HIGH).

```c
LoadSwitch_On(LOAD_FAN);           /* Turn on the fan */
LoadSwitch_On(LOAD_TEC1_PWR);      /* Power the TEC1 circuit */
```

### `LoadSwitch_Off(LoadSwitch_ID id)`

Turn off a specific load (EN pin LOW).

### `LoadSwitch_Set(LoadSwitch_ID id, bool state)`

Set a load to a specific state (true = ON, false = OFF).

### `LoadSwitch_IsOn(LoadSwitch_ID id)`

Returns true if the load is currently commanded ON.

### `LoadSwitch_AllOff(void)`

Emergency/shutdown: turn off all 10 loads immediately.

## Return Codes

```c
typedef enum {
    LOADSW_OK           = 0,   /* Success */
    LOADSW_ERR_NOT_INIT = 1,   /* Not initialized */
    LOADSW_ERR_PARAM    = 2,   /* Invalid load ID */
} LoadSwitch_Status;
```

## Load Identifiers

```c
typedef enum {
    LOAD_VALVE1           = 0,
    LOAD_VALVE2           = 1,
    LOAD_MICROPLATE       = 2,
    LOAD_FAN              = 3,
    LOAD_TEC1_PWR         = 4,
    LOAD_TEC2_PWR         = 5,
    LOAD_TEC3_PWR         = 6,
    LOAD_ASSEMBLY_STATION = 7,
    LOAD_DAUGHTER_1       = 8,
    LOAD_DAUGHTER_2       = 9,
    LOAD_COUNT            = 10,
} LoadSwitch_ID;
```

## Design Notes

- **No BSP separation** for this driver. The pin configs are stored as a static const table inside `VN5T016AH.c` rather than in `Bsp.c`. This keeps all 10 pin definitions together in one place since they're all identical in type (GPIO output) and only differ in port/pin.
- **State tracking** via a static `load_state[]` array. `LoadSwitch_IsOn()` returns the commanded state, not the actual hardware state.
- **FR_Stby and CS pins** are not currently connected/used. If latch-off fault recovery or current sensing is needed, these can be added as optional fields in the pin table.
