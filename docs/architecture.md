# Software Architecture

## Layer Diagram

```
┌─────────────────────────────────────────────────────┐
│                    APPLICATION                       │
│                                                     │
│   main.c              command.c                     │
│   ┌──────────┐        ┌──────────────────┐          │
│   │ Init     │        │ Command_Dispatch │          │
│   │ Sequence │        │ CMD_PING  0xDEAD │          │
│   │ Main Loop│        │ CMD_READ_ADC     │          │
│   │ (deferred│        │ CMD_BURST_ADC    │          │
│   │  TX +    │        │ CMD_LOAD_* x10   │          │
│   │  burst + │        │ CMD_THERM1-6     │          │
│   │  DC fwd) │        │ DC routing (26)  │          │
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
│   usart_driver.c      spi_driver.c   i2c_driver.c  │
│   ┌──────────────┐   ┌────────────┐ ┌────────────┐ │
│   │ USART_Driver_│   │ SPI_Init   │ │ I2C_Driver_│ │
│   │   Init       │   │ SPI_LTC2338│ │   Init     │ │
│   │   StartRx    │   │   _Read    │ │   Write    │ │
│   │   SendPacket │   │ SPI_DeInit │ │   Read     │ │
│   │   RxProcess  │   └────────────┘ │   WriteReg │ │
│   │    ISR       │                  │   ReadReg  │ │
│   │   TxComplete │                  │   IsDevice │ │
│   │    ISR       │                  │    Ready   │ │
│   └──────────────┘                  └────────────┘ │
│                                                     │
│   dc_uart_driver.c                                  │
│   ┌──────────────────┐                              │
│   │ DC_Uart_Init     │  4 instances (USART1/2/3,   │
│   │ DC_Uart_StartRx  │   UART4) — polled TX,       │
│   │ DC_Uart_Send     │   DMA circular RX            │
│   │ DC_Uart_RxProcess│  (HT/TC/IDLE interrupts)    │
│   │    ISR           │                              │
│   └──────────────────┘                              │
├─────────────────────────────────────────────────────┤
│                      DEVICES                         │
│                                                     │
│   DRV8702.c                   usb2517.c             │
│   ┌──────────────────┐       ┌──────────────────┐   │
│   │ DRV8702_Init     │       │ USB2517_SetStrap │   │
│   │ DRV8702_Wake     │       │   Pins           │   │
│   │ DRV8702_TEC_Heat │       └──────────────────┘   │
│   │ DRV8702_TEC_Cool │        (GPIO only — no I2C)  │
│   │ DRV8702_TEC_Stop │                              │
│   │ DRV8702_ReadReg  │                              │
│   │ DRV8702_WriteReg │                              │
│   └──────────────────┘                              │
│    (uses GPIO + spi_driver)                         │
│                                                     │
│   DAC80508.c                                        │
│   ┌──────────────────┐                              │
│   │ DAC80508_Init    │                              │
│   │ DAC80508_SetChan │                              │
│   │ DAC80508_SetAll  │                              │
│   │ DAC80508_ReadReg │                              │
│   │ DAC80508_WriteReg│                              │
│   └──────────────────┘                              │
│    (uses spi_driver / shared SPI2)                  │
│                                                     │
│   ADS7066.c                                         │
│   ┌────────────────────┐                            │
│   │ ADS7066_Init       │                            │
│   │ ADS7066_ReadChannel│                            │
│   │ ADS7066_SelectChan │                            │
│   │ ADS7066_ReadConv   │                            │
│   │ ADS7066_WriteReg   │                            │
│   │ ADS7066_ReadReg    │                            │
│   └────────────────────┘                            │
│    (3 instances, shared SPI2, Mode 0)               │
│                                                     │
│   VN5T016AH.c                                       │
│   ┌────────────────────┐                            │
│   │ LoadSwitch_Init    │                            │
│   │ LoadSwitch_On      │                            │
│   │ LoadSwitch_Off     │                            │
│   │ LoadSwitch_Set     │                            │
│   │ LoadSwitch_IsOn    │                            │
│   │ LoadSwitch_AllOff  │                            │
│   └────────────────────┘                            │
│    (10 instances, GPIO enable only)                 │
├─────────────────────────────────────────────────────┤
│                        BSP                           │
│                                                     │
│   bsp.c / bsp.h          clock_config.c             │
│   ┌──────────────────┐   ┌──────────────────┐       │
│   │ Type definitions │   │ MCU_Init         │       │
│   │ Const configs:   │   │ ClockTree_Init   │       │
│   │  sys_clk_config  │   └──────────────────┘       │
│   │  usart10_cfg/hdl │                              │
│   │  dc1-4_cfg/hdl   │                              │
│   │  spi2_cfg/hdl    │   ll_tick.c                  │
│   │  i2c1_cfg/hdl    │   ┌──────────────────┐       │
│   │  drv8702_x_cfg   │   │ LL_IncTick       │       │
│   │  dac80508_cfg    │   │ LL_GetTick       │       │
│   │  ads7066_x_cfg   │                              │
│   │ DMA buffers      │   └──────────────────┘       │
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

## Data Flow — UART Packet Reception (Interrupt-Driven)

```
USB Host (PC)
    │
    ▼
FT231XQ (USB-UART bridge)
    │ TX → PG11 (USART10 RX)
    ▼
DMA1 Stream 1 (circular mode)
    │ Writes bytes into rx_dma_buf (D2 SRAM, 4096 bytes)
    │
    ├── DMA HT interrupt (buffer 50% full)
    ├── DMA TC interrupt (buffer 100% full / wrap)
    └── USART IDLE interrupt (line idle after data)
         │
         ▼
    USART_Driver_RxProcessISR()       ← called from ISR context
         │ Compares DMA NDTR to last read position
         │ Extracts new bytes, handles ring wrap
         ▼
    Protocol_FeedBytes()
         │ State machine: WAIT_SOF → IN_FRAME → ESCAPED
         │ Byte-unstuffing, header decode, CRC accumulation
         │ On EOF: CRC16_Calc() over header+payload vs received CRC
         ▼
    OnPacketReceived() callback       ← still in ISR context
         │
         ▼
    Command_Dispatch()
         │ switch on CMD_CODE(cmd1, cmd2)
         ▼
    Command handler (e.g. Command_HandlePing)
         │ Populates tx_request or burst_request struct
         │ Sets .pending = true
         │ DOES NOT call SendPacket (ISR-unsafe)
         ▼
    Returns to ISR
```

## Data Flow — TX Deferral Pattern

```
ISR context                           Main loop context
─────────────                         ──────────────────

Command handler sets:                 while (1) {
  tx_request.msg1 = ...                 if (burst_request.pending) {
  tx_request.cmd1 = ...                   burst_request.pending = false;
  tx_request.payload = ...                Command_ExecuteBurstADC();
  tx_request.length = ...                 // fills tx_request internally
  tx_request.pending = true  ──────►    }
                                        if (tx_request.pending) {
                                          tx_request.pending = false;
                                          USART_Driver_SendPacket()
                                          │ Protocol_BuildPacket()
                                          │ DMA1 Stream 0 (normal mode)
                                          ▼
                                        USART10 TX → PG12
                                          │
                                          ▼
                                        FT231XQ → USB Host (PC)
                                        }
                                      }
```

## Data Flow — ADC Read (Single)

```
CMD_READ_ADC (0x0C01) received
    │
    ▼
Command_HandleReadADC()               ← ISR context
    │
    ▼
SPI_LTC2338_Read(&spi2_handle)        ← polled, ~3 µs total
    │ 1. Pulse CNV HIGH (≥30 ns)
    │ 2. Wait BUSY LOW (~1 µs conversion)
    │ 3. 32-bit SPI transfer @ 16 MHz
    │ 4. Right-shift raw >> 14 → 18-bit result
    ▼
Pack as 4-byte LE → tx_request.payload
Set tx_request.pending = true
    │
    ▼
Main loop → USART_Driver_SendPacket() → DMA TX
```

## Data Flow — ADC Burst Read

```
CMD_BURST_ADC (0x0C02) received
    │
    ▼
Command_HandleBurstADC()              ← ISR context (fast)
    │ Saves header fields to burst_request
    │ Sets burst_request.pending = true
    ▼
Returns to ISR

    ═══════════════════════════════════

Main loop detects burst_request.pending
    │
    ▼
Command_ExecuteBurstADC()             ← main loop context
    │ for i = 0..99:
    │   SPI_LTC2338_Read() → sample
    │   burst_raw[i] = sample (debug array, visible in debugger)
    │   Pack as 4-byte LE at burst_payload[i*4]
    │   (0xFFFFFFFF sentinel on failure)
    ▼
Copy 400 bytes → tx_request.payload
Set tx_request.pending = true
    │
    ▼
Main loop → USART_Driver_SendPacket() → DMA TX
```

## Data Flow — DRV8702 TEC Control

```
DRV8702_TEC_Heat(handle)
    │
    ├── DRV8702_IsFaulted(handle)     ← read nFAULT GPIO pin
    │     nFAULT LOW = fault → return DRV8702_ERR_FAULT
    │
    ├── DRV8702_SetDirection(handle, FORWARD)
    │     PH pin → HIGH
    │
    └── DRV8702_Enable(handle)
          EN pin → HIGH
          → Current flows through TEC in forward direction

DRV8702_ReadFaultStatus(handle)       ← optional SPI register read
    │
    ├── SPI2 reconfigured to 16-bit
    ├── nSCS asserted (LOW)
    ├── 16-bit SPI transfer (read IC_STAT)
    ├── nSCS deasserted (HIGH)
    └── SPI2 restored to 32-bit
```

## Data Flow — USB Hub Initialization

```
MCU Boot
    │
    ├── USB2517_SetStrapPins()
    │     1. Hold RESET_N LOW (PC13)
    │     2. Drive CFG_SEL2 HIGH (PG0), CFG_SEL1 LOW (PG1)
    │        CFG_SEL0 = SCL (idles HIGH via pull-up)
    │        CFG_SEL[2:1:0] = 1,0,1 → Internal default mode
    │          (dynamic power switching, LED=USB activity)
    │     3. Release RESET_N HIGH — hub exits POR
    │
    ├── LL_mDelay(100)  — wait for hub POR + attach
    │
    ▼
USB2517I Hub
    │ Uses internal default register values (no SMBus config)
    │ Attaches automatically to upstream USB port
    ▼
FT231XQ
    │ Appears as COM port on host PC
    ▼
Host PC recognizes device
```

## Data Flow — Daughtercard Command Routing

The motherboard routes driverboard commands to one of 4 daughtercard UARTs based on `boardID` (byte 0 of payload). Three routing modes exist:

### Mode 1 — Async Forward (26 commands)

```
GUI → USART10 RX → Command_Dispatch()
    │ boardID = payload[0]
    │ ISR sets dc_forward_request.pending
    ▼
Main loop
    │ Selects DC UART by boardID (0-3)
    │ DC_Uart_Send() — polled TX to target UART
    ▼
Daughtercard processes command
    │ Response arrives via DMA circular RX
    │ HT/TC/IDLE interrupt → DC_Uart_RxProcessISR()
    │ → Protocol_FeedBytes() → OnDC_PacketReceived()
    ▼
tx_request.pending = true → USART10 TX → GUI
```

### Mode 2 — Synchronous SET_LIST_OF_SW (0x0B51)

```
GUI sends payload: [boardID][bank][SW_hi][SW_lo][state] x N groups
    │
Main loop iterates each 5-byte group:
    │ Sends SetSingleSwitch (0x0A10) to target DC UART
    │ Waits for response (10 ms timeout per group)
    │ dc_list_active flag → OnDC_PacketReceived deposits to mailbox
    ▼
Aggregate response → tx_request → USART10 TX → GUI
```

### Mode 3 — Synchronous GET_LIST_OF_SW (0x0B52)

Same sequential pattern as Mode 2 but with 4-byte groups `[boardID][bank][SW_hi][SW_lo]`, forwarded as GetSingleSwitch (0x0A11).

### BoardID → UART Mapping

| boardID | Handle | UART | TX / RX Pins | Connector |
|---------|--------|------|-------------|-----------|
| 0 | dc1_handle | USART1 | PB14 TX / PB15 RX | Connector 1 bottom |
| 1 | dc2_handle | USART2 | PA2 TX / PA3 RX | Connector 1 top |
| 2 | dc3_handle | USART3 | PB10 TX / PB11 RX | Connector 2 bottom |
| 3 | dc4_handle | UART4 | PC10 TX / PC11 RX | Connector 2 top |

### DMA Stream Assignments

| Stream | Peripheral | Direction | Mode |
|--------|-----------|-----------|------|
| DMA1 Stream 0 | USART10 TX | TX | Normal |
| DMA1 Stream 1 | USART10 RX | RX | Circular |
| DMA1 Stream 2 | DC1 USART1 RX | RX | Circular |
| DMA1 Stream 3 | DC2 USART2 RX | RX | Circular |
| DMA1 Stream 4 | DC3 USART3 RX | RX | Circular |
| DMA1 Stream 5 | DC4 UART4 RX | RX | Circular |

DC UART TX is polled (no DMA needed).

## Configuration vs. State Separation

All hardware configuration data is **const** (stored in flash):

```c
const USART_Config usart10_cfg = { ... };     /* Flash — never changes */
const SPI_Config   spi2_cfg    = { ... };     /* Flash — never changes */
const I2C_Config   i2c1_cfg    = { ... };     /* Flash — never changes */
DRV8702_Config     drv8702_1_cfg = { ... };   /* Pin config for instance 1 */
```

Runtime state is **mutable** (stored in RAM) and points to the const configs:

```c
USART_Handle usart10_handle = {
    .cfg     = &usart10_cfg,      /* Pointer to flash config */
    .tx_busy = false,             /* Mutable runtime state   */
    .rx_head = 0,
};

DRV8702_Handle drv8702_1_handle = {
    .cfg         = &drv8702_1_cfg,
    .initialised = false,
    .faulted     = false,
};
```

This separation means:
- Config structs consume zero RAM (flash only)
- Porting to a new board means changing only `Bsp.c`
- Drivers are fully reusable — no embedded pin numbers or clock values

## DMA Buffer Placement

STM32H7 DMA1/DMA2 **cannot access DTCM** (0x20000000). All DMA buffers must reside in D1 AXI-SRAM or D2 SRAM.

Buffers are placed in the `.dma_buffer` linker section mapped to RAM_D2 (0x30000000) and 32-byte aligned for cache maintenance:

```c
__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t usart10_tx_dma_buf[8512];

__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t usart10_rx_dma_buf[4096];
```

## Interrupt Priority Map

| IRQ | Priority | Handler | Purpose |
|-----|----------|---------|---------|
| SysTick | 15 | `SysTick_Handler` | 1 ms tick counter |
| DMA1_Stream1 (RX) | 4 | `DMA_STR1_IRQHandler` | USART10 RX HT/TC |
| USART10 | 5 | `USART10_IRQHandler` | IDLE line detection |
| DMA1_Stream0 (TX) | 6 | `DMA_STR0_IRQHandler` | USART10 TX complete |
| DMA1_Stream2 | — | `DMA_STR2_IRQHandler` | DC1 USART1 RX DMA HT/TC |
| DMA1_Stream3 | — | `DMA_STR3_IRQHandler` | DC2 USART2 RX DMA HT/TC |
| DMA1_Stream4 | — | `DMA_STR4_IRQHandler` | DC3 USART3 RX DMA HT/TC |
| DMA1_Stream5 | — | `DMA_STR5_IRQHandler` | DC4 UART4 RX DMA HT/TC |
| USART1 | — | `USART1_IRQHandler` | DC1 IDLE line detection |
| USART2 | — | `USART2_IRQHandler` | DC2 IDLE line detection |
| USART3 | — | `USART3_IRQHandler` | DC3 IDLE line detection |
| UART4 | — | `UART4_IRQHandler` | DC4 IDLE line detection |

## Adding a New Peripheral

1. **Define types** in `Bsp.h` (if a new peripheral type is needed)
2. **Add config struct** in `Bsp.c` (pins, clock, peripheral settings)
3. **Write driver** in `xxx_driver.h/c` (generic, takes handle pointer)
4. **Write device file** if it's an external IC (e.g. `ina228.h/c`)
5. **Add init call** in `main.c` `SystemInit_Sequence()`
6. **Add error code** to `Error_Handler()` fault code table
