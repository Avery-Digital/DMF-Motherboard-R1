# I2C Bus Devices

## Bus Configuration

| Parameter | Value |
|-----------|-------|
| Peripheral | I2C1 |
| SDA | PB7 (Pin 166) |
| SCL | PB8 (Pin 168) |
| Speed | 400 kHz (Fast Mode) |
| Kernel Clock | 128 MHz (PLL3R) |
| Pull-ups | Internal + external (4.7 kΩ recommended) |
| Addressing | 7-bit |

## Device Address Table

| Device | Part Number | 7-bit Address | 8-bit Write | 8-bit Read | Description |
|--------|-------------|---------------|-------------|------------|-------------|
| USB Hub | USB2517I-JZX | 0x2C | 0x58 | 0x59 | USB 2.0 7-port hub controller |

### Future Devices

As I2C slaves are added to the bus, document them here:

| Device | Part Number | 7-bit Address | Description |
|--------|-------------|---------------|-------------|
| | | | |

## USB2517I-JZX — USB Hub Controller

### Overview

The USB2517I is a USB 2.0 hub controller that requires I2C (SMBus) configuration before it will attach to the upstream USB host. On power-up in SMBus mode, the hub waits for the MCU to write configuration registers and send the USB_ATTACH command.

### SMBus Write Format

The USB2517I uses SMBus Write Block protocol, not standard I2C register writes. Each register write includes a byte count:

```
[START] [0x58 (addr+W)] [reg_addr] [byte_count=0x01] [value] [STOP]
```

This is handled by the `USB2517_WriteReg()` function in `usb2517.c`.

### Register Map (Configured Registers)

| Register | Address | Default | Description |
|----------|---------|---------|-------------|
| VID_LSB | 0x00 | 0x24 | Vendor ID low byte (Microchip/SMSC) |
| VID_MSB | 0x01 | 0x04 | Vendor ID high byte |
| PID_LSB | 0x02 | 0x17 | Product ID low byte |
| PID_MSB | 0x03 | 0x25 | Product ID high byte |
| DID_LSB | 0x04 | 0x00 | Device ID low byte |
| DID_MSB | 0x05 | 0x00 | Device ID high byte |
| HUB_CFG1 | 0x06 | 0x9B | Self-powered, HS capable, MTT, individual port power/OC |
| HUB_CFG2 | 0x07 | 0x20 | Compound device = 0, OC timer default |
| HUB_CFG3 | 0x08 | 0x02 | String support disabled, port indicators disabled |
| PORT_SWAP | 0x30 | 0x00 | No port swapping |
| PORT_DIS | 0x31 | 0x00 | All 7 ports enabled |
| USB_ATTACH | 0xFF | — | Write 0x01 to connect hub to upstream USB host |

### HUB_CFG1 (0x06) Bit Definitions

| Bit | Name | Default | Description |
|-----|------|---------|-------------|
| 7 | SELF_PWR | 1 | 1 = Self-powered, 0 = Bus-powered |
| 6:5 | — | 0 | Reserved |
| 4 | HS_DISABLE | 0 | 0 = High-speed enabled |
| 3 | MTT_ENABLE | 1 | 1 = Multi-TT enabled (one TT per port) |
| 2 | — | 0 | Reserved |
| 1 | IND_PWR_SW | 1 | 1 = Individual port power switching |
| 0 | IND_OC | 1 | 1 = Individual overcurrent sensing |

### Initialization Sequence

1. MCU boots, I2C1 initialized at 400 kHz
2. `USB2517_IsPresent()` — verify hub ACKs at address 0x2C
3. Write all config registers (VID through PORT_DIS) with default values
4. Small delay between each register write (~1000 NOP cycles)
5. Write `USB_ATTACH` register (0xFF) with value 0x01
6. Hub connects to upstream USB host
7. FT231XQ enumerates as COM port on host PC

### Troubleshooting

| Symptom | Likely Cause |
|---------|-------------|
| Hub not ACKing (IsPresent fails) | CFG_SEL pins misconfigured, hub not powered, I2C pull-ups missing |
| Hub ACKs but no USB enumeration | USB_ATTACH not sent, USB cable issue, hub RESET pin held low |
| Code 43 on Windows | Hub not configured before USB host polls it, or init sequence incomplete |
| COM port not appearing | FT231 issue (separate from hub), check FTDI VCP driver installation |

## Adding a New I2C Device

1. Create `device_name.h` with register map defines and function prototypes
2. Create `device_name.c` with init and read/write functions
3. All device functions take `I2C_Handle *` as first parameter
4. Use `I2C_Driver_WriteReg()` / `I2C_Driver_ReadReg()` for register access
5. Use `I2C_Driver_IsDeviceReady()` to verify device presence
6. Add a new entry to the device address table above
7. Add init call to `SystemInit_Sequence()` in `main.c`

Example skeleton:

```c
/* device_name.h */
#define DEVICE_I2C_ADDR     0x48U
InitResult Device_Init(I2C_Handle *i2c);

/* device_name.c */
InitResult Device_Init(I2C_Handle *i2c)
{
    if (I2C_Driver_IsDeviceReady(i2c, DEVICE_I2C_ADDR) != INIT_OK)
        return INIT_ERR_I2C;

    uint8_t cfg = 0x42;
    return I2C_Driver_WriteReg(i2c, DEVICE_I2C_ADDR, 0x00, &cfg, 1);
}
```
