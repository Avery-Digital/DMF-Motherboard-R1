# DMF Motherboard R1 — Project Memory

Portable project context for Claude Code. On a new PC, either tell Claude to
"read memory.md" or copy this file to the Claude Code project memory directory:

Windows: `C:\Users\<username>\.claude\projects\<project-slug>\memory\MEMORY.md`
Linux/Mac: `~/.claude/projects/<project-slug>/memory/MEMORY.md`

---

## Target Hardware
- **MCU:** STM32H735IGT6 (Cortex-M7, 480 MHz, LQFP-176)
- **Project:** DMF-Motherboard-R1
- **IDE:** STM32CubeIDE (Eclipse-based), LL library (not HAL)
- **Board:** DMF (Digital Microfluidics) Motherboard Rev 1

## Architecture
- Bare-metal, no RTOS
- LL (Low-Layer) drivers only — no HAL
- BSP pattern: all hardware config structs live in `Src/Bsp.c` / `Inc/Bsp.h`
- DMA buffers placed in D2 SRAM (.dma_buffer section) — required for DMA1/DMA2 access on H7
- Motherboard connects to up to 4 daughter (driver) boards via 2 mezzanine connectors (2 boards per connector)
- 4 dedicated UARTs (USART1/2/3/UART4) for daughtercard communication, polled TX + DMA circular RX

## Clock Tree
- HSE: 12 MHz crystal
- PLL1P → 480 MHz SYSCLK (VOS0)
- PLL2Q → 128 MHz (USART10 kernel clock)
- PLL3P → 128 MHz (SPI2 kernel clock)
- PLL3R → 128 MHz (I2C1 kernel clock)
- AHB: 240 MHz, APBx: 120 MHz

## Peripherals
- **USART10**: PG12 TX / PG11 RX, 115200 baud, interrupt-driven DMA TX/RX (host PC link)
- **USART1/2/3/UART4**: Daughtercard UARTs, polled TX + DMA circular RX, error codes 0x12–0x15
- **I2C1**: PB8 SCL / PB7 SDA, 400 kHz Fast Mode, polled
- **SPI2**: PC2 MISO / PC3 MOSI / PA9 SCK, 16 MHz, Mode 0, 32-bit default — shared by 5 device types
  - **LTC2338-18**: 18-bit ADC, CNV on PE12, BUSY on PE15, 32-bit Mode 0
  - **DRV8702 x3**: TEC H-bridge, 16-bit Mode 0, nSCS on PD1/PD0/PD6
  - **DAC80508**: 8-ch 16-bit DAC, 24-bit Mode 1, nCS on PD2
  - **ADS7066 x3**: 8-ch 16-bit ADC, 24-bit Mode 0 (regs) / 16-bit Mode 0 (data), nCS on PD5/PD4/PD3
- **USB2517I**: USB hub, RESET_N on PC13 (MCU-controlled)
  - CFG_SEL[2:1:0] = 1,0,1 → internal defaults, dynamic power, LED=USB mode
  - No SMBus needed — hub auto-attaches after POR
  - USB2517_SetStrapPins() holds reset, sets straps, releases

## Chip Select Map (GPIOD PD0–PD6)
| Pin | Device |
|-----|--------|
| PD0 | DRV8702 instance 2 nSCS |
| PD1 | DRV8702 instance 1 nSCS |
| PD2 | DAC80508 nCS |
| PD3 | ADS7066 instance 3 nCS |
| PD4 | ADS7066 instance 2 nCS |
| PD5 | ADS7066 instance 1 nCS |
| PD6 | DRV8702 instance 3 nSCS |

Each driver's Init() configures its own nCS pin — no bulk GPIO init needed.

## Load Switches (VN5T016AHTR-E x10)
| ID | Name | Enable Pin | Pin # |
|----|------|-----------|-------|
| 0 | VALVE1 | PE10 | 72 |
| 1 | VALVE2 | PE8 | 68 |
| 2 | MICROPLATE | PE7 | 67 |
| 3 | FAN | PG2 | 110 |
| 4 | TEC1_PWR | PK2 | 109 |
| 5 | TEC2_PWR | PK1 | 108 |
| 6 | TEC3_PWR | PJ11 | 104 |
| 7 | ASSEMBLY_STATION | PJ9 | 102 |
| 8 | DAUGHTER_1 | PE6 | 5 |
| 9 | DAUGHTER_2 | PD14 | 97 |

All default OFF after init. GPIO push-pull, LOW = off, HIGH = on.

## Source Files
- `Src/main.c` — entry point, init sequence, main loop, packet callbacks
- `Src/Bsp.c` / `Inc/Bsp.h` — all hardware config structs and handles
- `Src/Clock_Config.c` — MCU_Init(), ClockTree_Init()
- `Src/Usart_Driver.c` — USART10 DMA TX/RX, protocol parser integration
- `Src/DC_Uart_Driver.c` / `Inc/DC_Uart_Driver.h` — Daughtercard UART driver (polled TX + DMA RX)
- `Src/Packet_Protocol.c` — custom binary framing (SOF/EOF/ESC byte stuffing, CRC-16 CCITT)
- `Src/Command.c` / `Inc/Command.h` — command dispatch and handlers
- `Src/spi_driver.c` — SPI2 init + LTC2338_Read (polled, 32-bit transfer)
- `Src/i2c_driver.c` — I2C polled write/read/register ops
- `Src/USB2517.c` — USB hub strap pin + reset control
- `Src/DRV8702.c` / `Inc/DRV8702.h` — TEC H-bridge motor driver (3 instances)
- `Src/DAC80508.c` / `Inc/DAC80508.h` — 8-channel 16-bit DAC driver
- `Src/ADS7066.c` / `Inc/ADS7066.h` — 8-channel 16-bit SAR ADC driver (3 instances)
- `Src/VN5T016AH.c` / `Inc/VN5T016AH.h` — High-side load switch driver (10 instances)
- `Src/crc16.c` — CRC-16 CCITT lookup table
- `Src/ll_tick.c` — SysTick ms counter (LL_GetTick / LL_IncTick)
- `Src/stm32h7xx_it.c` — ISR handlers

## Error Codes (Error_Handler fault_code → RTC BKP DR0)
| Code | Source |
|------|--------|
| 0x01–0x08 | Clock tree (HSE, PLL1/2/3 timeouts) |
| 0x10 | I2C1 init failure |
| 0x11 | USART10 init failure |
| 0x12–0x15 | Daughtercard UART 1–4 init failure |
| 0x20 | SPI2 init failure |
| 0x30–0x32 | DRV8702 instance 1/2/3 init failure |
| 0x40 | DAC80508 init failure |
| 0x50–0x52 | ADS7066 instance 1/2/3 init failure |
| 0x60 | Load switch init failure |

## Packet Protocol
- Frame: SOF(0x02) | msg1 | msg2 | len_hi | len_lo | cmd1 | cmd2 | payload | CRC16_hi | CRC16_lo | EOF(0x7E)
- ESC: 0x2D — byte stuffing for SOF/EOF/ESC in body
- CRC-16 CCITT (poly 0x1021, init 0xFFFF) over header + payload

## Implemented Commands
| Command | Code | Description |
|---------|------|-------------|
| CMD_PING | 0xDEAD | Echo test, returns fixed 8 bytes |
| CMD_READ_ADC | 0x0C01 | Single LTC2338-18 read, 4-byte LE response |
| CMD_BURST_ADC | 0x0C02 | 100x LTC2338-18 reads, 400-byte response, deferred to main loop |
| CMD_LOAD_* | 0x0C10–0x0C19 | Load switch on/off/query (10 instances) |
| CMD_THERM1–6 | 0x0C20–0x0C25 | Read ADS7066 instance 3, channels 0–5 (thermistors) |
| CMD_GET_BOARD_TYPE | 0x0C99 | Returns motherboard ID: [0x00][0x00][0xFF][0x4D][0x42] ("MB") |
| DC range | 0x0A00–0x0BFF | Auto-forwarded to driverboard by boardID (payload[0]) |
| CMD_DC_SET_LIST_SW | 0x0B51 | Bulk switch set — batched by boardID, synchronous |
| CMD_DC_GET_LIST_SW | 0x0B52 | Bulk switch get — batched by boardID, synchronous |
| CMD_DC_DEBUG | 0xBEEF | Driverboard debug test (forwarded) |

## Daughtercard Communication
- 4 UARTs (USART1/2/3/UART4), one per daughtercard slot
- DC_Uart_Driver: polled TX (blocking, main loop only) + DMA circular RX with HT/TC/IDLE ISRs
- Each has its own ProtocolParser instance
- boardID 0–3 maps to dc1_handle through dc4_handle
- Async forwarding: GUI → motherboard extracts boardID → defers to main loop → DC_Uart_SendPacket
- Synchronous list ops: SET/GET_LIST_OF_SW groups bucketed by boardID, sent one board at a time with response mailbox (dc_response) and timeout, aggregated response back to GUI
- dc_list_active flag switches DC RX callback between mailbox mode (sync) and relay mode (async)

## TX Deferral Pattern
- ISR handlers NEVER call USART_Driver_SendPacket() directly
- Populate tx_request / burst_request / dc_forward_request / dc_list_request and set .pending = true
- Main loop checks pending flags and performs the actual work

## Driver Board Context
- Located at: `C:\DMF Board\Firmware\DMFBoard\DMFDriverG1_R1\`
- Same MCU (STM32H735IGT6), 16 MHz HSE, 512 MHz SYSCLK
- Controls 600 HV switches (2 banks × 300), signal generators (0–100V), INA228 power monitors, AD7380 ADCs
- Schematic at: `schematic/DMFDriver.pdf`, datasheets at: `Datasheets/`
- ADCnDriver page: ADA4084-1 voltage divider (1M/25k = ÷41), LT1990 current sense across 1k (2x2k parallel)
- ADA4945-1 SE-to-diff drivers feed AD7380 (VREF=2.5V, VOCM=1.25V)
- For ±100V measurement via current sense path: need 39k load resistor → ±2.5V at ADC input
- Board ID command: 0x0B99, returns 0xCACA
- boardID currently hardcoded to 0xFF — needs mezzanine position detection

## User Preferences
- **Always keep docs in sync with code changes.** Update relevant files in `docs/` and `README.md` after any code modification.
- See `CLAUDE.md` in the repo root for full project instructions that load automatically.
- Datasheets in `Datasheets/` folder (ignored by build — .cproject only compiles Inc/Src/Startup)

## Current Status
- All core peripherals working: clock tree, USART+DMA, SPI ADC, I2C
- USB2517 changed to internal defaults mode (RESET_N on PC13, no SMBus)
- 20+ commands implemented including driverboard routing
- Daughtercard UART communication implemented and tested
- All 7 chip-select lines fully assigned
- All 10 load switches with commands
- 6 thermistor channels via ADS7066 instance 3
- CMD_GET_BOARD_TYPE (0x0C99) returns "MB" to distinguish from driverboards
