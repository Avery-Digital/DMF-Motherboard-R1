# DMF Motherboard R1 Firmware

Bare-metal embedded firmware for the **DMF Motherboard Rev 1** built around the **STM32H735IGT6** microcontroller. The board interfaces with a host PC over USB (via USB2517I hub + FT231XQ USB-UART bridge) using a custom framed serial protocol with CRC-16 integrity checking.

The system controls **3 TEC (thermoelectric cooler) H-bridges**, reads an **18-bit ADC** for temperature/voltage sensing, **routes driverboard commands to up to 4 daughtercard boards** over dedicated UARTs, and **routes actuator board commands to up to 2 actuator boards** over RS485 (UART5/USART6 via LTC2864 transceivers), all commanded remotely by a host PC.

## Target Hardware

| Parameter | Value |
|-----------|-------|
| MCU | STM32H735IGT6 (Cortex-M7, 480 MHz) |
| Package | LQFP-176 |
| HSE Crystal | 12 MHz (PH0-OSC_IN) |
| Fast ADC | LTC2338-18 (18-bit, SPI) |
| Slow ADC | ADS7066IRTER x3 (8-ch 16-bit, SPI) |
| DAC | DAC80508ZRTER (8-ch 16-bit, SPI) |
| TEC Drivers | DRV8702DQRHBRQ1 x3 (H-bridge, shared SPI + GPIO) |
| Load Switches | VN5T016AHTR-E x10 (high-side, GPIO enable) |
| USB Hub | USB2517I-JZX (GPIO-strapped, internal defaults, 7-port hub) |
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
├── Inc/
│   ├── main.h                  Main header, TxRequest/BurstRequest types
│   ├── Bsp.h                   Board Support Package — all type definitions
│   ├── Clock_Config.h          Clock init prototypes
│   ├── i2c_driver.h            Generic I2C master driver
│   ├── Usart_Driver.h          USART + DMA driver (interrupt-driven)
│   ├── DC_Uart_Driver.h        Daughtercard UART driver (polled TX + DMA RX)
│   ├── spi_driver.h            SPI2 driver for LTC2338-18 ADC
│   ├── DRV8702.h               TEC H-bridge driver — register map and API
│   ├── VN5T016AH.h             High-side load switch driver (10 instances)
│   ├── DAC80508.h              DAC80508 8-ch 16-bit DAC driver
│   ├── ADS7066.h               ADS7066 8-ch 16-bit slow ADC driver
│   ├── crc16.h                 CRC-16 CCITT
│   ├── Packet_Protocol.h       Frame parser and packet builder
│   ├── Command.h               Command definitions and dispatch
│   ├── USB2517.h               USB hub device driver
│   ├── RS485_Driver.h          Half-duplex RS485 driver (USART7 + MAX485)
│   ├── Act_Uart_Driver.h       Actuator board UART driver (UART5/USART6 + LTC2864 RS485)
│   ├── Thermistor.h            NTC thermistor ADC-to-temperature conversion
│   └── ll_tick.h               SysTick millisecond counter
├── Src/
│   ├── main.c                  Entry point, init sequence, main loop
│   ├── Bsp.c                   Const config structs, DMA buffers, Pin_Init()
│   ├── Clock_Config.c          MCU init, PLL1/2/3, bus prescalers
│   ├── i2c_driver.c            Polled I2C1 master operations
│   ├── Usart_Driver.c          USART10 + DMA TX/RX (interrupt-driven)
│   ├── DC_Uart_Driver.c        Daughtercard UART driver (4 instances)
│   ├── spi_driver.c            SPI2 init + LTC2338-18 polled read
│   ├── DRV8702.c               DRV8702 GPIO control + SPI register access
│   ├── VN5T016AH.c             Load switch enable/disable (GPIO only)
│   ├── DAC80508.c              DAC80508 init, channel set, register access
│   ├── ADS7066.c               ADS7066 init, channel read, register access
│   ├── crc16.c                 CRC-16 lookup table
│   ├── Packet_Protocol.c       Frame state machine and packet builder
│   ├── Command.c               Command dispatch + handlers
│   ├── USB2517.c               USB2517I GPIO strapping and reset control
│   ├── RS485_Driver.c          Polled RS485 TX/RX with DE/RE toggling
│   ├── Act_Uart_Driver.c       Actuator board UART driver (2 instances, polled TX + DMA RX)
│   ├── Thermistor.c            Steinhart-Hart conversion (SC50G104WH)
│   ├── ll_tick.c               LL_IncTick / LL_GetTick implementation
│   └── stm32h7xx_it.c          ISR handlers (SysTick, DMA, USART, faults)
├── docs/
│   ├── architecture.md         Software architecture and data flow
│   ├── packet_protocol.md      Serial protocol specification
│   ├── command_reference.md    Command codes, payloads, and responses
│   ├── spi_adc.md              SPI2 driver and LTC2338-18 ADC details
│   ├── drv8702.md              DRV8702 TEC H-bridge driver details
│   ├── dac80508.md             DAC80508 8-ch DAC driver details
│   ├── ads7066.md              ADS7066 8-ch slow ADC driver details
│   ├── load_switches.md        VN5T016AH load switch instances and API
│   ├── thermistor.md           NTC thermistor circuit and Steinhart-Hart conversion
│   ├── rs485_gantry.md         RS485 gantry driver, MAX485 wiring, protocol
│   ├── clock_config.md         Clock tree details and PLL math
│   ├── pin_assignments.md      MCU pin mapping (all peripherals)
│   └── i2c_devices.md          I2C bus device table
├── STM32H735IGTX_FLASH.ld     Linker script
└── README.md                   This file
```

## Architecture Overview

The firmware uses a layered architecture with strict separation of concerns:

| Layer | Files | Purpose |
|-------|-------|---------|
| **BSP** | `Bsp.h/c` | Hardware configuration data — pins, clocks, addresses. Only file that changes when porting to a different PCB. |
| **Drivers** | `i2c_driver`, `Usart_Driver`, `spi_driver`, `Clock_Config` | Reusable peripheral drivers. Operate on handle pointers, contain no board-specific constants. |
| **Protocol** | `crc16`, `Packet_Protocol` | Transport-agnostic framing, CRC, and parsing. No hardware dependencies. |
| **Devices** | `USB2517`, `DRV8702`, `VN5T016AH`, `DAC80508`, `ADS7066` | IC-specific drivers. USB2517 uses GPIO strapping (no I2C); DRV8702, DAC80508, and ADS7066 use shared SPI2; VN5T016AH uses GPIO only. |
| **Daughtercard** | `DC_Uart_Driver` | Polled TX + DMA circular RX UART driver for 4 daughtercard interfaces (USART1, USART2, USART3, UART4). Routes driverboard commands by boardID. |
| **RS485** | `RS485_Driver` | Polled half-duplex RS485 driver (USART7 + MAX485) for gantry communication. 9600 baud ASCII protocol. |
| **Actuator** | `Act_Uart_Driver` | Polled TX + DMA circular RX RS485 driver for 2 actuator board interfaces (UART5/USART6 via LTC2864). 115200 baud, inverted DE logic. Routes commands in 0x0F00-0x10FF range by boardID (1 or 2). |
| **Conversion** | `Thermistor` | NTC thermistor ADC-to-temperature conversion using Steinhart-Hart equation (SC50G104WH, Material Type G). |
| **Application** | `Command`, `main` | Command dispatch, deferred task execution, and system orchestration. |

See [docs/architecture.md](docs/architecture.md) for the full data flow diagram.

## Boot Sequence

| Step | Function | Description |
|------|----------|-------------|
| 1 | `MCU_Init()` | MPU, NVIC priority grouping, flash latency, SMPS + LDO VOS0 |
| 2 | `ClockTree_Init()` | HSE 12 MHz → PLL1 480 MHz SYSCLK, PLL2/PLL3 128 MHz peripherals |
| 2a | `LL_Init1msTick()` | SysTick at 1 ms, interrupt enabled |
| 3 | `I2C_Driver_Init()` | I2C1 on PB7 (SDA) / PB8 (SCL), 400 kHz Fast Mode |
| 4 | `SPI_Init()` | SPI2 on PC2/PC3/PA9, 16 MHz SCK, 32-bit frames, Mode 0 |
| 5 | `DRV8702_Init()` x3 | TEC H-bridge GPIO init + `DRV8702_Wake()` for all 3 instances |
| 5a | `DAC80508_Init()` | DAC80508 8-ch DAC init (SPI2, nCS on PD2), error code `0x40` on failure |
| 5b | `ADS7066_Init()` x3 | ADS7066 8-ch 16-bit ADC init (SPI2, nCS on PD5/PD4/PD3), error codes `0x50`–`0x52` on failure |
| 5c | `LoadSwitch_Init()` x10 | VN5T016AH high-side load switches (GPIO enable), all OFF on init, error code `0x60` on failure |
| 6 | `USB2517_SetStrapPins()` | Assert RESET_N (PC13), drive CFG_SEL[2:1:0] = 1,0,1 for internal default mode, release reset. Hub attaches automatically — no SMBus config needed |
| 6a | `LL_mDelay(100)` | Wait for USB2517 to exit POR and attach |
| 6b | `RS485_Init()` | USART7 on PF6 (RX) / PF7 (TX), 9600 baud, DE/RE on PF8 (error code `0x70`) |
| 7 | `Protocol_ParserInit()` | Register `OnPacketReceived` callback |
| 7a | `USART_Driver_Init()` | USART10 on PG11 (RX) / PG12 (TX), 115200 baud, DMA streams |
| 8 | `USART_Driver_StartRx()` | Enable circular DMA reception (HT/TC/IDLE interrupts) |
| 9 | `Protocol_ParserInit()` x4 | Init dc1–dc4 parsers with `OnDC_PacketReceived` callback |
| 9a | `DC_Uart_Init()` x4 | DC1 USART1, DC2 USART2, DC3 USART3, DC4 UART4 (error codes `0x12`–`0x15`) |
| 9b | `DC_Uart_StartRx()` x4 | Enable DMA circular RX for all 4 daughtercard UARTs |
| 10 | `Protocol_ParserInit()` x2 | Init act1/act2 parsers with `OnACT_PacketReceived` callback |
| 10a | `Act_Uart_Init()` x2 | ACT1 UART5, ACT2 USART6 — RS485 via LTC2864 (error codes `0x16`–`0x17`) |
| 10b | `Act_Uart_StartRx()` x2 | Enable DMA circular RX for both actuator board UARTs |
| Boot test | — | Sends a `0xDEAD` packet with `{0xAA, 0xBB, 0xCC}` payload |

## Main Loop

The main loop handles deferred tasks that are too heavy or unsafe for ISR context:

```c
while (1) {
    if (burst_request.pending)       →  Command_ExecuteBurstADC()         // 100x SPI reads
    if (dc_forward_request.pending)  →  Forward packet to DC UART        // Daughtercard async routing
    if (act_forward_request.pending) →  Forward packet to ACT UART       // Actuator board async routing
    if (gantry_request.pending)      →  Command_ExecuteGantry()          // RS485 polled TX/RX
    if (dc_list_request.pending)     →  Sequential SET/GET_LIST_OF_SW    // Synchronous bulk switch ops
    if (tx_request.pending)          →  USART_Driver_SendPacket()         // DMA TX
}
```

ISR-context command handlers populate `tx_request`, `burst_request`, `dc_forward_request`, `act_forward_request`, `dc_list_request`, or `gantry_request` and set `.pending = true`. The main loop performs the actual work.

## Communication Protocol

The board communicates using a custom framed protocol over USART at 115200 baud (8N1):

```
[SOF] [MSG1] [MSG2] [LEN_HI] [LEN_LO] [CMD1] [CMD2] [PAYLOAD...] [CRC_HI] [CRC_LO] [EOF]
 0x02                                                                                   0x7E
```

- **SOF:** `0x02`, **EOF:** `0x7E`, **ESC:** `0x2D`
- **CRC:** CRC-16 CCITT (poly 0x1021, init 0xFFFF) over header + payload
- **Byte stuffing:** SOF, EOF, and ESC bytes in data are escaped as `[ESC] [byte ^ ESC]`
- **Max payload:** 4096 bytes

See [docs/packet_protocol.md](docs/packet_protocol.md) for the full specification.

## Implemented Commands

| Command | Code | Payload (Request) | Payload (Response) | Context |
|---------|------|--------------------|--------------------|---------|
| `CMD_PING` | `0xDEAD` | (ignored) | 8 bytes: `DE AD BE EF 01 02 03 04` | ISR → deferred TX |
| `CMD_READ_ADC` | `0x0C01` | (none) | 4 bytes: 18-bit ADC result, LE | ISR → deferred TX |
| `CMD_BURST_ADC` | `0x0C02` | (none) | 400 bytes: 100 x 4-byte LE samples | ISR → deferred burst + TX |
| `CMD_LOAD_*` | `0x0C10`–`0x0C19` | 1 byte: 0x01=ON, 0x00=OFF (or empty for query) | 1 byte: state (0x01/0x00) | ISR → deferred TX |
| `CMD_THERM1`–`CMD_THERM6` | `0x0C20`–`0x0C25` | (none) | 4 bytes: float temperature (°C) | ISR → deferred TX |
| `CMD_GANTRY_CMD` | `0x0C30` | ASCII command string | ASCII response (or "TIMEOUT") | ISR → deferred RS485 TX/RX |
| `CMD_GET_BOARD_TYPE` | `0x0B99` | (ignored) | 5 bytes: `00 00 FF 4D 42` ("MB") | ISR → deferred TX |
| Routed DC commands (25) | `0x0Axx`, `0x0Bxx`, `0xBEEF` | boardID + command-specific | Relayed from daughtercard | ISR → deferred forward/list |
| Routed ACT commands | `0x0F00`–`0x10FF` | boardID (1-2) + command-specific | Relayed from actuator board | ISR → deferred ACT forward |

See [docs/command_reference.md](docs/command_reference.md) for detailed payload layouts.

## Error Handling

`Error_Handler(fault_code)` disables interrupts, writes the fault code to RTC backup register DR0 (survives reset), and halts.

| Fault Code | Source |
|------------|--------|
| `0x01`–`0x08` | Clock tree (HSE, PLL1/2/3 timeouts) |
| `0x10` | I2C1 init failure |
| `0x11` | USART10 init failure |
| `0x12` | DC1 USART1 init failure |
| `0x13` | DC2 USART2 init failure |
| `0x14` | DC3 USART3 init failure |
| `0x15` | DC4 UART4 init failure |
| `0x20` | SPI2 init failure |
| `0x30`–`0x32` | DRV8702 instance 1/2/3 init failure |
| `0x40` | DAC80508 init failure |
| `0x50` | ADS7066 instance 1 init failure |
| `0x51` | ADS7066 instance 2 init failure |
| `0x52` | ADS7066 instance 3 init failure |
| `0x60` | Load switch init failure |
| `0x16` | ACT1 UART5 init failure |
| `0x17` | ACT2 USART6 init failure |
| `0x70` | RS485 / USART7 init failure |

## Adding a New Command

1. Define the command code in `Command.h`:
   ```c
   #define CMD_SET_VOLTAGE    CMD_CODE(0x10, 0x01)
   ```
2. Write a static handler in `Command.c`:
   ```c
   static void Command_HandleSetVoltage(USART_Handle *handle,
                                        const PacketHeader *header,
                                        const uint8_t *payload);
   ```
3. Add a case to `Command_Dispatch()`:
   ```c
   case CMD_SET_VOLTAGE:
       Command_HandleSetVoltage(handle, header, payload);
       break;
   ```
4. **Important:** Never call `USART_Driver_SendPacket()` from the handler (ISR context). Set `tx_request.pending` and let the main loop transmit.

## Adding a New I2C Device

1. Create `device_name.h` and `device_name.c`
2. Define the register map and I2C address in the header
3. Write init/read/write functions that take an `I2C_Handle *`
4. Add the init call to `SystemInit_Sequence()` in `main.c`

See [docs/i2c_devices.md](docs/i2c_devices.md) for the bus address table.

## License

Copyright (c) 2026. All rights reserved.
