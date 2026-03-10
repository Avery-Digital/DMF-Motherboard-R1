# Pin Assignments

## MCU: STM32H735IGT6 (LQFP-176)

### Currently Assigned Pins

| Pin # | Port.Pin | Function | AF | Direction | Config | Connected To |
|-------|----------|----------|-----|-----------|--------|-------------|
| 31 | PH0 | HSE_OSC_IN | — | Input | — | 12 MHz crystal |
| 155 | PG11 | USART10_RX | AF11 | Input | Pull-up | FT231XQ TX |
| 156 | PG12 | USART10_TX | AF11 | Output | Push-pull | FT231XQ RX |
| 166 | PB7 | I2C1_SDA | AF4 | Bidir | Open-drain, pull-up | USB2517I SDA |
| 168 | PB8 | I2C1_SCL | AF4 | Output | Open-drain, pull-up | USB2517I SCL |

### Debug Interface (SWD)

| Pin # | Port.Pin | Function | Notes |
|-------|----------|----------|-------|
| — | PA13 | SWDIO | Debug data |
| — | PA14 | SWCLK | Debug clock |
| — | PB3 | SWO | Trace output (optional) |

### Pins Reserved for Future Use

As peripherals are added, document them here:

| Pin # | Port.Pin | Planned Function | AF | Connected To |
|-------|----------|-----------------|-----|-------------|
| | | SPI (TBD) | | |
| | | ADC (TBD) | | |
| | | Status LED (TBD) | | |

## GPIO Configuration Details

### USART10 Pins

Both USART10 pins use AF11. The RX pin has an internal pull-up to keep the line idle-high when no data is being transmitted.

```
PG11 (RX):  Mode=AF, AF=11, Speed=Very High, Pull=Up,    Output=Push-pull
PG12 (TX):  Mode=AF, AF=11, Speed=Very High, Pull=None,  Output=Push-pull
```

### I2C1 Pins

I2C requires open-drain outputs per the bus specification. Internal pull-ups are enabled but external pull-ups (typically 4.7 kΩ to 3.3V) should be present on the PCB for reliable operation at 400 kHz.

```
PB7 (SDA):  Mode=AF, AF=4,  Speed=High, Pull=Up, Output=Open-drain
PB8 (SCL):  Mode=AF, AF=4,  Speed=High, Pull=Up, Output=Open-drain
```

## Alternate Function Quick Reference

For the STM32H735, common AF assignments used in this project:

| AF | Function |
|----|----------|
| AF4 | I2C1, I2C2, I2C3 |
| AF5 | SPI1, SPI2, SPI4, SPI5 |
| AF7 | USART1, USART2, USART3 |
| AF11 | USART10 |

Refer to the STM32H735 datasheet Table 10 (Alternate Function mapping) for the complete list.
