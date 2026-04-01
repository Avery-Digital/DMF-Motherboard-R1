# RS485 Gantry Communication

## Overview

The motherboard communicates with a gantry system (TPA Motion PR01 / Nippon Pulse Commander CMD-4CR) via RS485 half-duplex using USART7 and a MAX485 transceiver in uMAX package.

## Hardware

### MCU to MAX485 Connections

| MCU Pin | LQFP Pin | Function | MAX485 uMAX Pin | Signal |
|---------|----------|----------|-----------------|--------|
| PF7 | 27 | USART7_TX (AF7) | 6 | DI (Driver Input) |
| PF6 | 26 | USART7_RX (AF7) | 3 | RO (Receiver Output) |
| PF8 | 28 | GPIO Output | 4+5 via NOT gate | RE + DE (tied together) |

### MAX485 uMAX Pinout

| Pin | Name | Function |
|-----|------|----------|
| 1 | B | Inverting bus line |
| 2 | VCC | +5V supply (see known issues) |
| 3 | RO | Receiver Output → MCU RX |
| 4 | RE | Receiver Enable (active low) |
| 5 | DE | Driver Enable (active high) |
| 6 | DI | Driver Input ← MCU TX |
| 7 | GND | Ground |
| 8 | A | Non-inverting bus line |

### NOT Gate (Inverter)

PF8 passes through an inverter before reaching the MAX485 RE+DE pins:

- **PF8 LOW** → NOT gate → HIGH → DE=1, RE=1 → **Transmit mode**
- **PF8 HIGH** → NOT gate → LOW → DE=0, RE=0 → **Receive mode**

The `RS485_SetTx()` and `RS485_SetRx()` functions account for this inversion.

### Known Hardware Issues

- **MAX485 VCC is 3.3V** on this board revision. The MAX485 requires 4.75–5.25V. At 3.3V, the differential output swing is ~2.25V (above RS485 spec minimum of 1.5V but below MAX485 guaranteed specs). A 5V bodge wire to VCC (pin 2) is needed for reliable operation. For Rev 2, route a 5V supply to this chip.

## USART7 Configuration

| Parameter | Value |
|-----------|-------|
| Peripheral | UART7 (APB1) |
| Kernel clock | PLL2Q = 128 MHz |
| Baud rate | 9600 |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Mode | Polled TX + polled RX (no DMA, no interrupts) |

## Gantry Protocol

The gantry uses ASCII, null-terminated commands over half-duplex RS485.

### Command Format (TX)

```
@[ID][COMMAND]\0
```

- `@` — Start character
- `[ID]` — 2-digit device ID (default: `01`)
- `[COMMAND]` — ASCII command string (uppercase)
- `\0` — Null terminator

### Response Format (RX)

```
[RESPONSE]\0
```

- Write commands return: `OK\0`
- Read commands return the value as ASCII, e.g. `4664\0`

### Example Commands

| Command | Description | Example Response |
|---------|-------------|-----------------|
| `@01VER` | Read firmware version | Version string |
| `@01ID` | Read device ID | `01` |
| `@01SN` | Read serial number | Serial string |
| `@01MSTX` | Motor X status bitmask | `0` (idle) |
| `@01PX` | Read X position | `11340` |
| `@01X5000` | Move X to position 5000 | `OK` |
| `@01EO=7` | Enable all axes | `OK` |

### Important Notes

- **RS485 is disabled when USB is plugged into the gantry controller.** Disconnect USB before testing.
- Default device ID is `01` (configurable via `ID` command, range 0–63).
- Baud rate is changeable via `DB` command (1=9600, 2=19200, 3=38400, 4=57600, 5=115200).

See `PR01_RS485_Command_Reference.md` in the repo root for the full command list.

## Firmware API

**Files:** `Inc/RS485_Driver.h`, `Src/RS485_Driver.c`

```c
/* Initialize USART7, GPIO, and DE/RE pin. */
InitResult RS485_Init(RS485_Handle *handle);

/* Send ASCII command, receive response with timeout.
 * Returns number of response bytes, or 0 on timeout. */
uint16_t RS485_SendCommand(RS485_Handle *handle,
                            const char *cmd,
                            char *response, uint16_t max_len,
                            uint32_t timeout_ms);

/* Direction control (inverted for NOT gate) */
void RS485_SetTx(RS485_Handle *handle);
void RS485_SetRx(RS485_Handle *handle);
```

### BSP Configuration

Defined in `Src/Bsp.c`:

- `rs485_cfg` — USART7 peripheral config + DE/RE pin config
- `rs485_handle` — Runtime handle

Error code `0x70` on init failure.

## Command Integration

`CMD_GANTRY_CMD` (`0x0C30`) provides GUI-to-gantry passthrough:

1. GUI sends ASCII command as binary payload (no null terminator)
2. ISR copies to `gantry_request`, sets `.pending = true`
3. Main loop calls `Command_ExecuteGantry()`
4. Firmware appends null, sends via RS485, waits for response (500 ms timeout)
5. Response (or "TIMEOUT") sent back to GUI as packet payload

## Gantry Terminal Block Wiring

| Gantry Pin | Signal | Connect To |
|------------|--------|------------|
| 1 | Power GND | System ground |
| 2 | +24VDC | Power input |
| 3 | RS485 GND | Motherboard GND |
| 4 | RS485 B- | MAX485 pin 1 (B) |
| 5 | RS485 A+ | MAX485 pin 8 (A) |

Use twisted pair for A+/B-. Connect RS485 GND between devices. 120 Ω termination recommended at each end of the bus.
