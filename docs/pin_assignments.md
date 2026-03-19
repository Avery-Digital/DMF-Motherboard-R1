# Pin Assignments

## MCU: STM32H735IGT6 (LQFP-176)

### USART10 — Host Communication

| Pin # | Port.Pin | Function | AF | Direction | Config | Connected To |
|-------|----------|----------|-----|-----------|--------|-------------|
| 155 | PG11 | USART10_RX | AF4 | Input | Pull-up, Very High speed | FT231XQ TX |
| 156 | PG12 | USART10_TX | AF4 | Output | Push-pull, Very High speed | FT231XQ RX |

### Daughtercard UARTs (4 instances)

| Pin # | Port.Pin | Function | AF | Direction | Config | Connected To |
|-------|----------|----------|-----|-----------|--------|-------------|
| — | PB14 | USART1_TX | AF4 | Output | Push-pull, Very High speed | DC1 (Connector 1 bottom) |
| — | PB15 | USART1_RX | AF4 | Input | Pull-up, Very High speed | DC1 (Connector 1 bottom) |
| — | PA2 | USART2_TX | AF7 | Output | Push-pull, Very High speed | DC2 (Connector 1 top) |
| — | PA3 | USART2_RX | AF7 | Input | Pull-up, Very High speed | DC2 (Connector 1 top) |
| — | PB10 | USART3_TX | AF7 | Output | Push-pull, Very High speed | DC3 (Connector 2 bottom) |
| — | PB11 | USART3_RX | AF7 | Input | Pull-up, Very High speed | DC3 (Connector 2 bottom) |
| — | PC10 | UART4_TX | AF8 | Output | Push-pull, Very High speed | DC4 (Connector 2 top) |
| — | PC11 | UART4_RX | AF8 | Input | Pull-up, Very High speed | DC4 (Connector 2 top) |

DMA assignments: DMA1 Stream 2 (DC1 RX), Stream 3 (DC2 RX), Stream 4 (DC3 RX), Stream 5 (DC4 RX). TX is polled.

### I2C1 — Peripheral Bus

| Pin # | Port.Pin | Function | AF | Direction | Config | Connected To |
|-------|----------|----------|-----|-----------|--------|-------------|
| 166 | PB7 | I2C1_SDA | AF4 | Bidir | Open-drain, pull-up, High speed | USB2517I SDA |
| 168 | PB8 | I2C1_SCL | AF4 | Output | Open-drain, pull-up, High speed | USB2517I SCL |

### SPI2 — ADC + TEC Driver Bus

| Pin # | Port.Pin | Function | AF | Direction | Config | Connected To |
|-------|----------|----------|-----|-----------|--------|-------------|
| 36 | PC2 | SPI2_MISO | AF5 | Input | No pull, High speed | LTC2338-18 SDO / DRV8702 SDO |
| 37 | PC3 | SPI2_MOSI | AF5 | Output | Push-pull, High speed | DRV8702 SDI (unused by ADC) |
| 128 | PA9 | SPI2_SCK | AF5 | Output | Push-pull, High speed | LTC2338-18 SCK / DRV8702 SCLK |

### LTC2338-18 ADC Control Pins

| Pin # | Port.Pin | Function | AF | Direction | Config | Connected To |
|-------|----------|----------|-----|-----------|--------|-------------|
| 74 | PE12 | CNV | GPIO | Output | Push-pull, High speed | LTC2338-18 CNV (conversion trigger) |
| 77 | PE15 | BUSY | GPIO | Input | No pull, Low speed | LTC2338-18 BUSY (conversion status) |

### Chip Select Lines (GPIOD)

All configured as push-pull outputs, High speed, initialized HIGH (deasserted).

| Pin # | Port.Pin | Assigned To | Notes |
|-------|----------|-------------|-------|
| — | PD0 | DRV8702 instance 2 nSCS | |
| 144 | PD1 | DRV8702 instance 1 nSCS | |
| 145 | PD2 | DAC80508 nCS | 8-ch 16-bit DAC |
| 146 | PD3 | ADS7066 instance 3 nCS | 8-ch 16-bit slow ADC |
| 147 | PD4 | ADS7066 instance 2 nCS | 8-ch 16-bit slow ADC |
| 148 | PD5 | ADS7066 instance 1 nCS | 8-ch 16-bit slow ADC |
| — | PD6 | DRV8702 instance 3 nSCS | |

### DRV8702 TEC H-Bridge — Instance 1

| Pin # | Port.Pin | Function | Direction | Config | Signal |
|-------|----------|----------|-----------|--------|--------|
| 69 | PE9 | IN1/PH | Output | Push-pull, High speed | Direction control |
| 73 | PE11 | IN2/EN | Output | Push-pull, High speed | Enable / PWM |
| 115 | PG5 | nSLEEP | Output | Push-pull, Low speed | Sleep control (active low) |
| 116 | PG6 | MODE | Output | Push-pull, Low speed | PH/EN vs PWM mode |
| 144 | PD1 | nSCS | Output | Push-pull, High speed | SPI chip select (active low) |
| 117 | PG7 | nFAULT | Input | Pull-up, Low speed | Fault indicator (active low) |

**Note:** PE9 = TIM1_CH1 (AF1), PE11 = TIM1_CH2 (AF1) — available for PWM in future.

### DRV8702 TEC H-Bridge — Instance 2

| Pin # | Port.Pin | Function | Direction | Config | Signal |
|-------|----------|----------|-----------|--------|--------|
| — | PE13 | IN1/PH | Output | Push-pull, High speed | Direction control |
| — | PE14 | IN2/EN | Output | Push-pull, High speed | Enable / PWM |
| — | PF0 | nSLEEP | Output | Push-pull, Low speed | Sleep control |
| — | PF1 | MODE | Output | Push-pull, Low speed | PH/EN vs PWM mode |
| — | PD0 | nSCS | Output | Push-pull, High speed | SPI chip select |
| — | PF2 | nFAULT | Input | Pull-up, Low speed | Fault indicator |

### DRV8702 TEC H-Bridge — Instance 3

| Pin # | Port.Pin | Function | Direction | Config | Signal |
|-------|----------|----------|-----------|--------|--------|
| — | PJ8 | IN1/PH | Output | Push-pull, High speed | Direction control |
| — | PJ10 | IN2/EN | Output | Push-pull, High speed | Enable / PWM |
| — | PF12 | nSLEEP | Output | Push-pull, Low speed | Sleep control |
| — | PF13 | MODE | Output | Push-pull, Low speed | PH/EN vs PWM mode |
| — | PD6 | nSCS | Output | Push-pull, High speed | SPI chip select |
| — | PF14 | nFAULT | Input | Pull-up, Low speed | Fault indicator |

### DAC80508ZRTER — 8-Channel 16-bit DAC

| Pin # | Port.Pin | Function | Direction | Config | Signal |
|-------|----------|----------|-----------|--------|--------|
| 145 | PD2 | nCS | Output | Push-pull, High speed | SPI chip select (active low) |

Uses shared SPI2 bus (PC2 MISO, PC3 MOSI, PA9 SCK). 24-bit SPI frames, Mode 1 (CPOL=0, CPHA=1).

### ADS7066IRTER — 8-Channel 16-bit ADC (3 instances)

| Pin # | Port.Pin | Instance | Direction | Config | Signal |
|-------|----------|----------|-----------|--------|--------|
| 148 | PD5 | Instance 1 nCS | Output | Push-pull, High speed | SPI chip select (active low) |
| 147 | PD4 | Instance 2 nCS | Output | Push-pull, High speed | SPI chip select (active low) |
| 146 | PD3 | Instance 3 nCS | Output | Push-pull, High speed | SPI chip select (active low) |

Uses shared SPI2 bus (PC2 MISO, PC3 MOSI, PA9 SCK). 24-bit SPI frames for register access, 16-bit for data reads, Mode 0 (CPOL=0, CPHA=0). 250 kSPS, internal 2.5V reference.

### VN5T016AHTR-E — High-Side Load Switches (10 instances)

All configured as push-pull outputs, Low speed, initialized LOW (OFF).

| Pin # | Port.Pin | Instance | Direction | Config | Signal |
|-------|----------|----------|-----------|--------|--------|
| 72 | PE10 | VALVE1 | Output | Push-pull, Low speed | Enable (active high) |
| 68 | PE8 | VALVE2 | Output | Push-pull, Low speed | Enable (active high) |
| 67 | PE7 | MICROPLATE | Output | Push-pull, Low speed | Enable (active high) |
| 110 | PG2 | FAN | Output | Push-pull, Low speed | Enable (active high) |
| 109 | PK2 | TEC1_PWR | Output | Push-pull, Low speed | Enable (active high) |
| 108 | PK1 | TEC2_PWR | Output | Push-pull, Low speed | Enable (active high) |
| 104 | PJ11 | TEC3_PWR | Output | Push-pull, Low speed | Enable (active high) |
| 102 | PJ9 | ASSEMBLY_STATION | Output | Push-pull, Low speed | Enable (active high) |
| 5 | PE6 | DAUGHTER_1 | Output | Push-pull, Low speed | Enable (active high) |
| 97 | PD14 | DAUGHTER_2 | Output | Push-pull, Low speed | Enable (active high) |

### USB2517I Control and Strapping Pins

| Pin # | Port.Pin | Function | Direction | Config | State |
|-------|----------|----------|-----------|--------|-------|
| 9 | PC13 | RESET_N | Output | Push-pull, Low speed | Pulsed LOW during boot, then HIGH (release) |
| 63 | PG0 | CFG_SEL2 | Output | Push-pull, Low speed | HIGH (set before reset release) |
| 66 | PG1 | CFG_SEL1 | Output | Push-pull, Low speed | LOW (set before reset release) |

CFG_SEL0 = SCL line (idles high via pull-up). Combined: CFG_SEL[2:1:0] = 1,0,1 → Internal default mode (dynamic power switching, LED=USB activity). No SMBus configuration required — hub uses internal defaults and attaches automatically after reset release.

### Clock Source

| Pin # | Port.Pin | Function | Connected To |
|-------|----------|----------|-------------|
| 31 | PH0 | HSE_OSC_IN | 12 MHz crystal |

### Debug Interface (SWD)

| Pin # | Port.Pin | Function | Notes |
|-------|----------|----------|-------|
| — | PA13 | SWDIO | Debug data |
| — | PA14 | SWCLK | Debug clock |
| — | PB3 | SWO | Trace output (optional) |

## GPIO Configuration Summary

### Alternate Function Quick Reference

| AF | Function |
|----|----------|
| AF4 | I2C1, USART10, USART1 |
| AF5 | SPI2 |
| AF7 | USART2, USART3 |
| AF8 | UART4 |

Refer to the STM32H735 datasheet Table 10 (Alternate Function mapping) for the complete list.

### GPIO Port Clock Usage

| Port | Peripherals Using It |
|------|---------------------|
| GPIOA | SPI2_SCK (PA9), USART2_TX (PA2), USART2_RX (PA3) |
| GPIOB | I2C1_SDA (PB7), I2C1_SCL (PB8), USART1_TX (PB14), USART1_RX (PB15), USART3_TX (PB10), USART3_RX (PB11) |
| GPIOC | SPI2_MISO (PC2), SPI2_MOSI (PC3), USB2517 RESET_N (PC13), UART4_TX (PC10), UART4_RX (PC11) |
| GPIOD | Chip selects PD0-PD6 (DRV8702 x3, DAC80508, ADS7066 x3), VN5T016AH DAUGHTER_2 (PD14) |
| GPIOE | ADC CNV (PE12), ADC BUSY (PE15), DRV8702 PH/EN (PE9/11/13/14), VN5T016AH (PE6/7/8/10) |
| GPIOF | DRV8702 nSLEEP/MODE/nFAULT (PF0-2, PF12-14) |
| GPIOG | USART10 (PG11/12), USB2517 straps (PG0/1), DRV8702 nSLEEP/MODE/nFAULT (PG5-7), VN5T016AH FAN (PG2) |
| GPIOJ | DRV8702 instance 3 PH/EN (PJ8/10), VN5T016AH TEC3_PWR (PJ11), ASSEMBLY_STATION (PJ9) |
| GPIOK | VN5T016AH TEC1_PWR (PK2), TEC2_PWR (PK1) |
