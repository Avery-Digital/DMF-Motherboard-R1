# DMF Motherboard R1 — Project State (Portable Context)

Portable project context for Claude Code. On a new PC, either tell Claude to
"read memory.md" or copy this file to the Claude Code project memory directory.

Last updated: 2026-04-13

## Current Firmware Version: v1.5.2

## Project Locations
- **Motherboard firmware:** C:\STM32_Firmware\DMF-Motherboard-R1 (GitHub: Avery-Digital/DMF-Motherboard-R1, branch: main)
- **Driver Board firmware:** C:\DMF Board\Firmware\DMFBoard\DMFDriverG1_R1 (GitHub: Avery-Digital/DMF-DriverBoard-Firmware, branch: working-Branch)
- **Actuator Board firmware:** C:\STM32_Firmware\DMF-ActuatorBoard (GitHub: Avery-Digital/DMF-ActuatorBoard, branch: master)
- **GUI (C# WinForms):** C:\DMF Board\Software\DMF_DriverBoard_GUI\DMF_DriverBoard_GUI (GitHub: Avery-Digital/DMF-DriverBoard-GUI, branch: Working-Branch)

## Architecture
- **MCU:** STM32H735IGT6 (176-pin LQFP, Cortex-M7, 480 MHz)
- **Clock:** HSE 12 MHz → PLL1 480 MHz SYSCLK, PLL2Q 128 MHz (USART), PLL3 128 MHz (SPI/I2C)
- **Framework:** Bare-metal, LL drivers only (no HAL, no RTOS)
- **Protocol:** Custom binary framing — SOF(0x02) + header(6B) + payload + CRC-16 CCITT + EOF(0x7E), byte stuffing with ESC(0x2D)
- **Switch state encoding:** GND=0x00, HVSG=0x01, Float=0x04 (GND remapped from 0x02 to avoid SOF collision)

## What's Working
- Droplet sensing via LTC2338-18 ADC (single, burst, measure with deterministic timing)
- 4x daughtercard UART to DMF driver boards (DMA RX, polled TX, command routing by boardID 0-3)
- 2x actuator board UART via RS485/LTC2864 (command routing by boardID 0-1)
- Actuator board firmware: 28 GPIO half-bridges via L293Q, inverse logic, 0-based IDs, switch mapping
- GPIO PWM phase sync: PA12+PC5 pulse → driver board PD3 EXTI3 resets TIM2/TIM1/TIM8
- CMD_MEASURE_ADC (0x0C03): optimized with GET_HVSG_SWITCHES, selective GND, TIM2 32-bit timer, board mask, per-phase timestamps
- CMD_SWEEP_ADC (0x0C05): per-switch ADC sweep
- 10x load switches (VN5T016AH) with current sensing (V_SENSE in mV)
- 6x thermistor readback (Steinhart-Hart conversion)
- TEC manual PWM control: DRV8702-Q1 PH/EN mode, TIM1/TIM8 PWM on EN pins, 0-100% duty

## What's Paused / Not Working
- **TEC PID control** — paused until DRV8702 part verified on PCB
- **Gantry RS485** — IC not rated for 3.3V, needs replacement part
- PID algorithm, thermal runaway protection — not started

## Driver Board UART Position Config
- `MB_UART_POSITION` in DMFDriverConfig.h: `MB_UART_BOTTOM` (USART3) or `MB_UART_TOP` (USART2)
- Compile-time — different firmware build per position
- Motherboard: Conn1 bottom=DC1, Conn1 top=DC2, Conn2 bottom=DC3, Conn2 top=DC4

## Key Design Rules
1. ISR-safe deferred TX: handlers set tx_request.pending, main loop sends
2. DMA buffers in D2 SRAM (.dma_buffer section, 32-byte aligned)
3. All responses: [status1(category)][status2(code)][data...]
4. Every firmware push: bump version + CHANGELOG + README
5. RS485 DE logic inverted (NOT gate on PCB): GPIO LOW=TX, GPIO HIGH=RX

## Command Code Ranges
| Range | Board | Purpose |
|-------|-------|---------|
| 0x0A00–0x0BFF | Driver Board | Switch matrix, PMU, HVSG, INA228, bulk switches |
| 0x0B54 | Driver Board | GET_HVSG_SWITCHES |
| 0x0B99 | All boards | GET_BOARD_TYPE |
| 0x0C00–0x0C05 | Motherboard | ADC commands |
| 0x0C10–0x0C19 | Motherboard | Load switches |
| 0x0C20–0x0C25 | Motherboard | Thermistors |
| 0x0C30 | Motherboard | Gantry RS485 |
| 0x0C40–0x0C49 | Motherboard | Current sense |
| 0x0C50–0x0C54 | Motherboard | TEC control |
| 0x0F00–0x10FF | Actuator Board | Actuator switches |
| 0xBEEF | Driver Board | Debug |
| 0xDEAD | All boards | Ping/FW version |
