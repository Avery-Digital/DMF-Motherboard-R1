# Software Architecture

## Layer Diagram

```
┌─────────────────────────────────────────────────────┐
│                    APPLICATION                       │
│                                                     │
│   main.c              command.c                     │
│   ┌──────────┐        ┌──────────────────┐          │
│   │ Init     │        │ Command_Dispatch │          │
│   │ Sequence │        │ CMD_PING (0xBEEF)│          │
│   │ Main Loop│        │ (add more here)  │          │
│   └──────────┘        └──────────────────┘          │
├─────────────────────────────────────────────────────┤
│                      PROTOCOL                        │
│                                                     │
│   packet_protocol.c            crc16.c              │
│   ┌───────────────────┐       ┌──────────┐          │
│   │ Protocol_FeedBytes│◄──────│ CRC16    │          │
│   │ Protocol_Build    │       │ _Calc    │          │
│   │   Packet          │       │ _Update  │          │
│   │ ProtocolParser    │       └──────────┘          │
│   │   state machine   │                             │
│   └───────────────────┘                             │
├─────────────────────────────────────────────────────┤
│                      DRIVERS                         │
│                                                     │
│   usart_driver.c           i2c_driver.c             │
│   ┌──────────────────┐    ┌──────────────────┐      │
│   │ USART_Driver_    │    │ I2C_Driver_      │      │
│   │   Init           │    │   Init           │      │
│   │   PollRx         │    │   Write / Read   │      │
│   │   SendPacket     │    │   WriteReg       │      │
│   │   Transmit       │    │   ReadReg        │      │
│   └──────────────────┘    │   IsDeviceReady  │      │
│                            └──────────────────┘      │
├─────────────────────────────────────────────────────┤
│                      DEVICES                         │
│                                                     │
│   usb2517.c                                         │
│   ┌──────────────────┐                              │
│   │ USB2517_Init     │  (uses i2c_driver)           │
│   │ USB2517_IsPresent│                              │
│   └──────────────────┘                              │
├─────────────────────────────────────────────────────┤
│                        BSP                           │
│                                                     │
│   bsp.c / bsp.h          clock_config.c             │
│   ┌──────────────────┐   ┌──────────────────┐       │
│   │ Type definitions │   │ MCU_Init         │       │
│   │ Const configs:   │   │ ClockTree_Init   │       │
│   │  sys_clk_config  │   └──────────────────┘       │
│   │  usart10_cfg     │                              │
│   │  i2c1_cfg        │                              │
│   │ DMA buffers      │                              │
│   │ Pin_Init()       │                              │
│   └──────────────────┘                              │
├─────────────────────────────────────────────────────┤
│              STM32H7 LL DRIVERS (ST)                 │
│         stm32h7xx_ll_*.h / stm32h7xx_ll_utils.c     │
├─────────────────────────────────────────────────────┤
│                    HARDWARE                          │
│          STM32H735IGT6 @ 480 MHz                     │
└─────────────────────────────────────────────────────┘
```

## Data Flow — UART Packet Reception

```
USB Host (PC)
    │
    ▼
FT231XQ (USB-UART bridge)
    │ TX → PG11 (USART10 RX)
    ▼
DMA1 Stream 1 (circular mode)
    │ Writes bytes into rx_dma_buf (D2 SRAM)
    ▼
USART_Driver_PollRx()          ← called from main loop
    │ Compares DMA NDTR to last read position
    │ Extracts new bytes, handles ring wrap
    ▼
Protocol_FeedBytes()
    │ State machine: WAIT_SOF → IN_FRAME → ESCAPED
    │ Byte-unstuffing, header decode, CRC accumulation
    │ On EOF: CRC16_Calc() over header+payload vs received CRC
    ▼
OnPacketReceived() callback
    │
    ▼
Command_Dispatch()
    │ switch on CMD_CODE(cmd1, cmd2)
    ▼
Command handler (e.g. Command_HandlePing)
    │
    ▼
USART_Driver_SendPacket()
    │ Protocol_BuildPacket() → framed into tx_dma_buf
    │ DMA1 Stream 0 fires (normal mode)
    ▼
USART10 TX → PG12
    │
    ▼
FT231XQ → USB Host (PC)
```

## Data Flow — USB Hub Initialization

```
MCU Boot
    │
    ▼
I2C_Driver_Init()
    │ Configures I2C1 on PB7 (SDA) / PB8 (SCL)
    ▼
USB2517_Init()
    │ Writes default config registers (VID, PID, hub config, port config)
    │ Sends USB_ATTACH command (reg 0xFF = 0x01)
    ▼
USB2517I Hub
    │ Enumerates on upstream USB port
    ▼
FT231XQ
    │ Appears as COM port on host PC
    ▼
Host PC recognizes device
```

## Configuration vs. State Separation

All hardware configuration data is **const** (stored in flash):

```c
const USART_Config usart10_cfg = { ... };     /* Flash — never changes */
const I2C_Config   i2c1_cfg    = { ... };     /* Flash — never changes */
```

Runtime state is **mutable** (stored in RAM) and points to the const configs:

```c
USART_Handle usart10_handle = {
    .cfg     = &usart10_cfg,      /* Pointer to flash config */
    .tx_busy = false,             /* Mutable runtime state   */
    .rx_head = 0,
};
```

This separation means:
- Config structs consume zero RAM (flash only)
- Porting to a new board means changing only `bsp.c`
- Drivers are fully reusable — no embedded pin numbers or clock values

## Adding a New Peripheral

1. **Define types** in `bsp.h` (if a new peripheral type is needed)
2. **Add config struct** in `bsp.c` (pins, clock, peripheral settings)
3. **Write driver** in `xxx_driver.h/c` (generic, takes handle pointer)
4. **Write device file** if it's an external IC (e.g. `ina228.h/c`)
5. **Add init call** in `main.c` `SystemInit_Sequence()`
