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

| Device | Part Number | 7-bit Address | 8-bit Write | 8-bit Read | Description | Status |
|--------|-------------|---------------|-------------|------------|-------------|--------|
| USB Hub | USB2517I-JZX | 0x2C | 0x58 | 0x59 | USB 2.0 7-port hub controller | **Not used** — hub uses GPIO strapping (internal defaults), no SMBus config |

### Future Devices

As I2C slaves are added to the bus, document them here:

| Device | Part Number | 7-bit Address | Description |
|--------|-------------|---------------|-------------|
| | | | |

## USB2517I-JZX — USB Hub Controller

### Overview

The USB2517I is a USB 2.0 hub controller. It is configured via **GPIO strapping pins** to use internal default register values. No SMBus/I2C configuration is required — the hub attaches automatically after reset release.

### Configuration Mode

The hub's configuration mode is selected by the CFG_SEL[2:1:0] strap pins sampled at power-on reset:

| CFG_SEL2 (PG0) | CFG_SEL1 (PG1) | CFG_SEL0 (SCL) | Mode |
|-----------------|-----------------|----------------|------|
| **1 (HIGH)** | **0 (LOW)** | **1 (HIGH)** | **Internal default — dynamic power switching, LED=USB activity** |

The MCU drives these strap pins via `USB2517_SetStrapPins()` before releasing RESET_N (PC13, Pin 9).

### Initialization Sequence

1. `USB2517_SetStrapPins()` — hold RESET_N LOW, drive CFG_SEL pins, release RESET_N
2. `LL_mDelay(100)` — wait for hub to exit POR and attach
3. Hub uses internal default register values (Table 7-1 in datasheet)
4. Hub attaches to upstream USB host automatically
5. FT231XQ enumerates as COM port on host PC

### GPIO Pins

| Pin # | Port.Pin | Function | Direction | State |
|-------|----------|----------|-----------|-------|
| 9 | PC13 | RESET_N | Output | Pulsed LOW, then HIGH |
| 63 | PG0 | CFG_SEL2 | Output | HIGH |
| 66 | PG1 | CFG_SEL1 | Output | LOW |

### SMBus Address (Reference Only)

The USB2517I SMBus slave address is 0x2C (7-bit). SMBus configuration is **not used** in the current firmware — the hub uses internal defaults. The `USB2517_Init()` SMBus write function and register table remain in the source code for reference but are not called during boot.

### Troubleshooting

| Symptom | Likely Cause |
|---------|-------------|
| No USB enumeration | RESET_N stuck LOW, CFG_SEL pins wrong, USB cable issue |
| Code 43 on Windows | Hub not released from reset before USB host polls |
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
