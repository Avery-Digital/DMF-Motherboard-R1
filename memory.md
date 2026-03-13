# DMF Motherboard R1 — Project Memory

Portable project context for Claude Code. Copy this file to your Claude Code project
memory directory on a new PC to restore full context:

```
~/.claude/projects/<project-slug>/memory/MEMORY.md
```

On Windows: `C:\Users\<username>\.claude\projects\<project-slug>\memory\MEMORY.md`

The project slug is derived from the repo path. Check `~/.claude/projects/` for the correct folder name.

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

## Clock Tree
- HSE: 12 MHz crystal
- PLL1P → 480 MHz SYSCLK (VOS0)
- PLL2Q → 128 MHz (USART10 kernel clock)
- PLL3P → 128 MHz (SPI2 kernel clock)
- PLL3R → 128 MHz (I2C1 kernel clock)
- AHB: 240 MHz, APBx: 120 MHz

## Peripherals
- **USART10**: PG12 TX / PG11 RX, 115200 baud, interrupt-driven circular DMA (DMA1 Stream0=TX, Stream1=RX)
- **I2C1**: PB8 SCL / PB7 SDA, 400 kHz Fast Mode, polled, timing reg = 0x30410F13
- **SPI2**: PC2 MISO / PC3 MOSI / PA9 SCK, 16 MHz, Mode 0, 32-bit default — shared by 5 device types
  - **LTC2338-18**: 18-bit ADC, CNV on PE12, BUSY on PE15, 32-bit Mode 0
  - **DRV8702 x3**: TEC H-bridge, 16-bit Mode 0, nSCS on PD1/PD0/PD6
  - **DAC80508**: 8-ch 16-bit DAC, 24-bit Mode 1, nCS on PD2
  - **ADS7066 x3**: 8-ch 16-bit ADC, 24-bit Mode 0 (regs) / 16-bit Mode 0 (data), nCS on PD5/PD4/PD3
- **USB2517I**: USB hub, strapping pins PG0/PG1, configured via I2C (currently disabled in main.c)

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
- `Src/main.c` — entry point, init sequence, main loop, OnPacketReceived callback
- `Src/Bsp.c` / `Inc/Bsp.h` — all hardware config structs and handles
- `Src/Clock_Config.c` — MCU_Init(), ClockTree_Init()
- `Src/Usart_Driver.c` — DMA TX/RX, protocol parser integration
- `Src/Packet_Protocol.c` — custom binary framing (SOF/EOF/ESC byte stuffing, CRC-16 CCITT)
- `Src/Command.c` — command dispatch (CMD_PING 0xDEAD, CMD_READ_ADC 0x0C01, CMD_BURST_ADC 0x0C02)
- `Src/spi_driver.c` — SPI2 init + LTC2338_Read (polled, 32-bit transfer)
- `Src/i2c_driver.c` — I2C polled write/read/register ops
- `Src/USB2517.c` — USB hub I2C config driver (written, disabled in main)
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

## TX Deferral Pattern
- ISR handlers NEVER call USART_Driver_SendPacket() directly
- Instead they populate tx_request struct and set tx_request.pending = true
- Main loop checks pending flag and performs the actual DMA TX
- For heavy processing (burst ADC), ISR sets burst_request.pending, main loop executes

## DRV8702DQRHBRQ1 — TEC H-Bridge Driver
- 3 instances, all sharing SPI2, each with unique nSCS
- PH/EN mode: PH sets direction, EN enables bridge
- SPI 16-bit frames for register access (temporarily reconfigures from 32-bit)
- TEC convenience: DRV8702_TEC_Heat/Cool/Stop
- PE9/PE11 (instance 1) are also TIM1_CH1/CH2 (AF1) — can repurpose for PWM

## User Preferences
- **Always keep docs in sync with code changes.** Update relevant files in `docs/` and `README.md` after any code modification.
- See `CLAUDE.md` in the repo root for full project instructions that load automatically.

## Current Status
- Clock tree, USART+DMA+protocol stack, SPI ADC read all working
- LTC2338-18 confirmed working (commit: "got LTC2338 ADC working with 32bit SPI")
- USB2517_Init() written but commented out in main.c
- Three commands implemented: CMD_PING, CMD_READ_ADC, CMD_BURST_ADC
- DRV8702 driver written and initialized for all 3 instances (wake on boot)
- DAC80508 driver written and initialized
- ADS7066 driver written and initialized for all 3 instances
- All 7 chip-select lines (PD0–PD6) fully assigned to devices
- Boot test packet sent on startup (cmd=0xDEAD, payload {0xAA,0xBB,0xCC})
