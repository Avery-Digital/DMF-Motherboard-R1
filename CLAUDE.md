# Claude Code — Project Instructions

## Project Overview

This is bare-metal STM32H735IGT6 firmware for the DMF Motherboard R1. LL drivers only — no HAL, no RTOS.

## Key Rules

- **Always update documentation when code changes.** After any change to commands, peripherals, pin assignments, protocol, or architecture, update the relevant files in `docs/` and `README.md`. Stale docs are worse than no docs.
- **Never use HAL.** All peripheral access uses ST LL (Low-Layer) drivers.
- **BSP separation.** All hardware config structs (pins, clocks, addresses) live in `Src/Bsp.c` / `Inc/Bsp.h`. Drivers reference them via handle pointers and contain no board-specific constants.
- **TX deferral pattern.** ISR-context command handlers must NEVER call `USART_Driver_SendPacket()` directly. Populate `tx_request` and set `.pending = true`. The main loop performs the DMA transfer.
- **SPI2 bus sharing.** SPI2 is shared by 5 device types (LTC2338 32-bit Mode 0, DRV8702 16-bit Mode 0, DAC80508 24-bit Mode 1, ADS7066 24/16-bit Mode 0). Each driver reconfigures data width (and CPHA if needed) before its transfer and restores 32-bit Mode 0 afterward. Do not interleave SPI2 calls.
- **DMA buffers in D2 SRAM.** STM32H7 DMA1/DMA2 cannot access DTCM. Buffers must use the `.dma_buffer` linker section (0x30000000), 32-byte aligned.
- **Error codes.** `Error_Handler(fault_code)` writes to RTC backup DR0 and halts. See README.md for the fault code table.

## Documentation Files

When updating code, keep these in sync:
- `README.md` — project overview, boot sequence, command table, error codes
- `docs/architecture.md` — layer diagram, data flow diagrams
- `docs/packet_protocol.md` — frame format, byte stuffing, command list
- `docs/command_reference.md` — command codes, payloads, response formats
- `docs/spi_adc.md` — SPI2 config, LTC2338-18 read sequence, bus sharing matrix
- `docs/drv8702.md` — TEC driver GPIO/SPI API, pin assignments
- `docs/dac80508.md` — DAC80508 register map, SPI sharing, API
- `docs/ads7066.md` — ADS7066 register map, pipeline, API
- `docs/clock_config.md` — PLL config, peripheral clock assignments
- `docs/pin_assignments.md` — all MCU pin mappings
- `docs/i2c_devices.md` — I2C bus device table
- `docs/load_switches.md` — VN5T016AH load switch instances and API

## Datasheets

Located at `Datasheets/` (in repo) and `../Datasheets/` (parent directory):
- `ADC - ADS7066IRTER.pdf` — ADS7066 8-ch 16-bit ADC
- `ADC - LTC2338IMS-18#PBF.pdf` — LTC2338-18 18-bit ADC
- `DAC - DAC80508ZRTER.pdf` — DAC80508 8-ch 16-bit DAC
- `MCU - STM32H735IGT6.pdf` — STM32H735 MCU
- `TEC DRIVER - DRV8702DQRHBRQ1.pdf` — DRV8702 H-bridge driver
- `POWER LOAD SWITCHES - VN5T016AHTR-E.pdf`

## Adding a New Command

1. Define code in `Inc/Command.h` with `CMD_CODE(hi, lo)`
2. Write static handler in `Src/Command.c` — set `tx_request.pending`, never call SendPacket
3. Add case to `Command_Dispatch()`
4. For heavy processing (>10 µs), use deferred pattern: ISR sets request struct, main loop executes
5. Update `docs/command_reference.md`

## Adding a New SPI Device

1. Create `Inc/DeviceName.h` (register map, API) and `Src/DeviceName.c` (implementation)
2. Add config/handle types to `Inc/Bsp.h`, instances to `Src/Bsp.c`
3. Transfer function must: disable SPI2, set data width/mode, re-enable, do transfer, restore 32-bit Mode 0
4. Add init call to `SystemInit_Sequence()` in `Src/main.c` with a unique error code
5. Update docs (pin_assignments, spi_adc, architecture, README)
