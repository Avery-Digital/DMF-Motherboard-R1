# DMF Motherboard R1 Firmware

Bare-metal embedded firmware for the **DMF Motherboard Rev 1** built around the **STM32H735IGT6** microcontroller. The board interfaces with a host PC over USB (via USB2517I hub + FT231XQ USB-UART bridge) using a custom framed serial protocol with CRC-16 integrity checking.

## Target Hardware

| Parameter | Value |
|-----------|-------|
| MCU | STM32H735IGT6 (Cortex-M7, 480 MHz) |
| Package | LQFP-176 |
| HSE Crystal | 12 MHz (PH0-OSC_IN) |
| USB Hub | USB2517I-JZX (I2C-configured, 7-port hub) |
| USB-UART Bridge | FT231XQ-T |
| Debug Interface | SWD (SWDIO, SWCLK, SWO) |

## Toolchain

- **IDE:** STM32CubeIDE 2.0.0
- **Compiler:** arm-none-eabi-gcc 13.3
- **HAL/LL Package:** STM32Cube_FW_H7 V1.13.0
- **Driver Layer:** LL (Low-Layer) exclusively — no HAL

## Building

1. Open STM32CubeIDE
2. **File → Import → General → Existing Projects into Workspace**
3. Browse to this repository root
4. Build with **Project → Build All** (Ctrl+B)

### Required Preprocessor Symbols

```
DEBUG
STM32
STM32H7
STM32H735xx
STM32H735IGTx
STM32H7SINGLE
```

### Required Include Paths

```
Inc
Drivers/STM32H7xx_HAL_Driver/Inc
Drivers/CMSIS/Device/ST/STM32H7xx/Include
Drivers/CMSIS/Include
```

## Flashing

Connect a J-Link or ST-LINK via SWD (SWDIO + SWCLK). In STM32CubeIDE:

1. **Run → Debug Configurations → Debugger tab**
2. Set Interface to **SWD**
3. Click **Debug**

Alternatively, use J-Link Commander:

```
J-Link> connect
Device> STM32H735IG
TIF> SWD
Speed> 4000
J-Link> loadbin firmware.bin, 0x08000000
J-Link> r
J-Link> g
```

## Project Structure

```
├── Core/
│   ├── Inc/
│   │   ├── main.h                  Main header
│   │   ├── bsp.h                   Board Support Package — all type defs
│   │   ├── clock_config.h          Clock init prototypes
│   │   ├── i2c_driver.h            Generic I2C master driver
│   │   ├── usart_driver.h          USART + DMA driver
│   │   ├── crc16.h                 CRC-16 CCITT
│   │   ├── packet_protocol.h       Frame parser and packet builder
│   │   ├── command.h               Command definitions and dispatch
│   │   └── usb2517.h               USB hub device driver
│   └── Src/
│       ├── main.c                  Entry point and init sequence
│       ├── bsp.c                   Const config structs and pin init
│       ├── clock_config.c          MCU init, PLL config, prescalers
│       ├── i2c_driver.c            Polled I2C master operations
│       ├── usart_driver.c          USART10 + DMA TX/RX
│       ├── crc16.c                 CRC-16 lookup table
│       ├── packet_protocol.c       Frame state machine
│       ├── command.c               Command handlers
│       └── usb2517.c               USB2517I config and attach
├── docs/
│   ├── architecture.md             Software architecture and data flow
│   ├── packet_protocol.md          Serial protocol specification
│   ├── clock_config.md             Clock tree details and PLL math
│   ├── pin_assignments.md          MCU pin mapping
│   └── i2c_devices.md              I2C bus device table
├── STM32H735IGTX_FLASH.ld         Linker script
└── README.md                       This file
```

## Architecture Overview

The firmware uses a layered architecture with strict separation of concerns:

| Layer | Files | Purpose |
|-------|-------|---------|
| **BSP** | `bsp.h/c` | Hardware configuration data — pins, clocks, addresses. Only file that changes when porting to a different PCB. |
| **Drivers** | `i2c_driver`, `usart_driver`, `clock_config` | Reusable peripheral drivers. Operate on handle pointers, contain no board-specific constants. |
| **Protocol** | `crc16`, `packet_protocol` | Transport-agnostic framing, CRC, and parsing. No hardware dependencies. |
| **Devices** | `usb2517` | I2C device drivers for specific ICs. Use the generic I2C driver. |
| **Application** | `command`, `main` | Command dispatch and system orchestration. |

See [docs/architecture.md](docs/architecture.md) for the full data flow diagram.

## Boot Sequence

1. **MCU_Init()** — MPU, NVIC priority grouping, flash latency, SMPS + LDO VOS0
2. **ClockTree_Init()** — HSE 12 MHz → PLL1 480 MHz SYSCLK, PLL2/PLL3 128 MHz peripherals
3. **I2C_Driver_Init()** — I2C1 on PB7 (SDA) / PB8 (SCL), 400 kHz Fast Mode
4. **USB2517_Init()** — Write hub config registers, send USB_ATTACH command
5. **Protocol_ParserInit()** — Register packet callback
6. **USART_Driver_Init()** — USART10 on PG11 (RX) / PG12 (TX), 115200 baud, DMA
7. **USART_Driver_StartRx()** — Enable circular DMA reception

## Communication Protocol

The board communicates using a custom framed protocol over USART at 115200 baud:

```
[SOF] [MSG1] [MSG2] [LEN_HI] [LEN_LO] [CMD1] [CMD2] [PAYLOAD...] [CRC_HI] [CRC_LO] [EOF]
```

- **SOF:** `0x02`, **EOF:** `0x7E`, **ESC:** `0x2D`
- **CRC:** CRC-16 CCITT (poly 0x1021, init 0xFFFF) over header + payload
- **Byte stuffing:** SOF, EOF, and ESC bytes in data are escaped as `[ESC] [byte ^ ESC]`
- **Max payload:** 4096 bytes

See [docs/packet_protocol.md](docs/packet_protocol.md) for the full specification.

## Adding a New Command

1. Define the command code in `command.h`:
   ```c
   #define CMD_READ_ADC    CMD_CODE(0x10, 0x02)
   ```
2. Write a static handler in `command.c`:
   ```c
   static void Command_HandleReadADC(USART_Handle *handle,
                                     const PacketHeader *header,
                                     const uint8_t *payload);
   ```
3. Add a case to `Command_Dispatch()`:
   ```c
   case CMD_READ_ADC:
       Command_HandleReadADC(handle, header, payload);
       break;
   ```

## Adding a New I2C Device

1. Create `device_name.h` and `device_name.c` in the Devices section
2. Define the register map and I2C address in the header
3. Write init/read/write functions that take an `I2C_Handle *`
4. Add the init call to `SystemInit_Sequence()` in `main.c`

See [docs/i2c_devices.md](docs/i2c_devices.md) for the bus address table.

## License

Copyright (c) 2026. All rights reserved.
