# Command Response Flow — End-to-End

This document traces how commands travel from the PC to the motherboard (and optionally through to downstream boards) and how responses get back. Every function call, data structure, ISR, and DMA transfer is documented.

## System Overview

```
                              ┌─────────────────────┐
                              │   DMF Motherboard    │
                              │                      │
PC (GUI)  ══USB══>  USART10   │  Command_Dispatch()  │
                              │       │              │
                              │  ┌────┴─────────┐    │
                              │  │ Local cmds   │    │
                              │  │ 0xDEAD       │    │    ┌──────────────────┐
                              │  │ 0x0C01-0C30  │    │    │ Driverboard x4   │
                              │  │ 0x0B99       │    ├──> │ (USART1/2/3/4)   │
                              │  ├──────────────┤    │    │ 0x0A00-0x0BFF    │
                              │  │ DC forward   │────┘    └──────────────────┘
                              │  │ 0x0A00-0x0BFF│
                              │  ├──────────────┤         ┌──────────────────┐
                              │  │ ACT forward  │────────>│ Actuator Brd x2  │
                              │  │ 0x0F00-0x10FF│         │ (UART5/USART6)   │
                              │  ├──────────────┤         │ RS485 via LTC2864│
                              │  │ Gantry RS485 │         └──────────────────┘
                              │  │ 0x0C30       │
                              │  └──────────────┘         ┌──────────────────┐
                              │       │                   │ Gantry System    │
                              │       └──────────────────>│ (USART7/MAX485)  │
                              │                           │ 9600 baud ASCII  │
PC (GUI)  <══USB══  USART10   │                           └──────────────────┘
                              └─────────────────────┘
```

## Three Command Categories

| Category | Code Range | Routing | Response Source |
|----------|-----------|---------|-----------------|
| **Motherboard local** | 0xDEAD, 0x0B99, 0x0C01-0x0C30 | Handled locally | Motherboard generates response |
| **Driverboard routed** | 0x0A00-0x0BFF (except 0x0B99) | Forwarded to DC UART by boardID | Driverboard response relayed |
| **Actuator board routed** | 0x0F00-0x10FF | Forwarded to ACT UART by boardID | Actuator response relayed |
| **Gantry passthrough** | 0x0C30 | ASCII via RS485 (USART7) | Gantry ASCII response wrapped |

---

## Flow 1: Motherboard Local Commands

These commands are processed entirely on the motherboard. No downstream boards are involved.

### Step-by-Step

1. **GUI** builds framed packet, sends over USB serial (115200 baud)
2. **USART10 DMA1 Stream 1** (circular) receives into `usart10_rx_dma_buf[4096]`
3. **DMA HT/TC or USART10 IDLE ISR** → `USART_Driver_RxProcessISR(&usart10_handle)`
4. New bytes fed to `Protocol_FeedBytes(&usart10_parser, ...)`
5. Parser validates CRC → calls `OnPacketReceived()` callback
6. `OnPacketReceived()` → `Command_Dispatch()` → matches `case` in `switch(cmd)`
7. Handler populates `tx_request` struct, sets `tx_request.pending = true`
8. **Main loop** detects `pending`, calls `USART_Driver_SendPacket(&usart10_handle, ...)`
9. `Protocol_BuildPacket()` frames response → DMA1 Stream 0 → USART10 TX → USB → GUI

### Exception: Burst ADC (0x0C02)

Too slow for ISR context (~300 µs for 100 SPI reads). Uses two-stage deferral:

1. ISR handler sets `burst_request.pending = true` (fast, ~100 ns)
2. Main loop calls `Command_ExecuteBurstADC()` which does 100 SPI reads
3. Results packed into `tx_request`, sets `tx_request.pending = true`
4. Main loop sends on next iteration

### Exception: Gantry RS485 (0x0C30)

Polled RS485 TX/RX at 9600 baud (~50 ms round trip). Uses deferral:

1. ISR handler copies ASCII payload to `gantry_request`, sets `.pending = true`
2. Main loop calls `Command_ExecuteGantry()` which does:
   - `RS485_SendCommand()` — toggles DE/RE, sends null-terminated ASCII, waits for response
   - Copies response (or "TIMEOUT") to `tx_request`

---

## Flow 2: Driverboard Routed Commands (0x0A00-0x0BFF)

### Async Forward (most commands)

```
GUI ──USB──> Motherboard USART10 RX (ISR)
                │
                ▼ Command_Dispatch() → default case
                │ cmd in range 0x0A00-0x0BFF
                │ Command_HandleDcForward()
                │   boardID = payload[0]
                │   Populate dc_forward_request struct
                │   dc_forward_request.pending = true
                ▼
             Main loop
                │ DC_GetHandle(boardID) → dc1/dc2/dc3/dc4_handle
                │ DC_Uart_SendPacket(dc_handle, ...)
                │   Protocol_BuildPacket() → polled TX byte-by-byte
                ▼
             Driverboard receives, processes, sends response
                │
                ▼ DMA circular RX on DC UART
                │ HT/TC/IDLE ISR → DC_Uart_RxProcessISR()
                │ → Protocol_FeedBytes() → OnDC_PacketReceived()
                ▼
             OnDC_PacketReceived() [ISR context]
                │ if (!dc_list_active):
                │   Copy header+payload to tx_request
                │   tx_request.pending = true
                ▼
             Main loop → USART_Driver_SendPacket() → USB → GUI
```

### Synchronous Bulk (SET_LIST_OF_SW 0x0B51, GET_LIST_OF_SW 0x0B52)

These require sequential per-board processing:

1. ISR copies all groups to `dc_list_request`, sets `.pending = true`
2. Main loop calls `Command_ExecuteDcList()` which:
   - Sets `dc_list_active = true` (redirects `OnDC_PacketReceived` to mailbox)
   - Sorts groups by boardID into per-board buckets
   - For each non-empty bucket: sends batch to DC UART, waits for response (500 ms timeout)
   - Collects responses into aggregate buffer
   - Sets `dc_list_active = false`
   - Sends aggregate response via `tx_request`

---

## Flow 3: Actuator Board Routed Commands (0x0F00-0x10FF)

```
GUI ──USB──> Motherboard USART10 RX (ISR)
                │
                ▼ Command_Dispatch() → default case
                │ cmd in range 0x0F00-0x10FF
                │ Command_HandleActForward()
                │   boardID = payload[0] (1 or 2)
                │   Populate act_forward_request struct
                │   act_forward_request.pending = true
                ▼
             Main loop
                │ ACT_GetHandle(boardID) → act1_handle (UART5) or act2_handle (USART6)
                │ Act_Uart_SendPacket(act_handle, ...)
                │   1. DE pin LOW (NOT gate → HIGH → transmit mode)
                │   2. Polled byte-by-byte TX via Protocol_BuildPacket()
                │   3. Wait for USART TC flag
                │   4. DE pin HIGH (NOT gate → LOW → receive mode)
                ▼
             Actuator board receives, processes, sends response via RS485
                │
                ▼ DMA circular RX on ACT UART (DMA1 Stream 6 or 7)
                │ HT/TC/IDLE ISR → Act_Uart_RxProcessISR()
                │ → Protocol_FeedBytes() → OnACT_PacketReceived()
                ▼
             OnACT_PacketReceived() [ISR context]
                │ Copy header+payload to tx_request
                │ tx_request.pending = true
                ▼
             Main loop → USART_Driver_SendPacket() → USB → GUI
```

---

## Main Loop Priority Order

```c
while (1) {
    if (burst_request.pending)        // 1. Burst ADC (100 SPI reads)
    if (dc_forward_request.pending)   // 2. Driverboard forward
    if (act_forward_request.pending)  // 3. Actuator board forward
    if (gantry_request.pending)       // 4. Gantry RS485 (~50 ms)
    if (dc_list_request.pending)      // 5. Bulk switch ops (blocking)
    if (tx_request.pending)           // 6. Transmit response to GUI
}
```

Only one `tx_request` can be pending at a time. If a response arrives while `tx_request.pending` is already true, it is dropped. This is safe because the main loop processes requests sequentially.

---

## Key Data Structures

### tx_request (shared by all response paths)

```c
typedef struct {
    volatile bool   pending;        // Set by ISR or handler, cleared by main loop
    uint8_t         msg1, msg2;     // Echoed from original request
    uint8_t         cmd1, cmd2;     // Echoed from original request
    uint8_t         payload[4096];  // Response data
    uint16_t        length;         // Response payload length
} TxRequest;
```

### dc_forward_request

```c
typedef struct {
    volatile bool   pending;
    uint8_t         msg1, msg2, cmd1, cmd2;
    uint8_t         payload[4096];
    uint16_t        length;
    uint8_t         board_id;       // 0-3 → dc1..dc4_handle
} DcForwardRequest;
```

### act_forward_request

```c
typedef struct {
    volatile bool   pending;
    uint8_t         msg1, msg2, cmd1, cmd2;
    uint8_t         payload[4096];
    uint16_t        length;
    uint8_t         board_id;       // 1 or 2 → act1 or act2_handle
} ActForwardRequest;
```

### gantry_request

```c
typedef struct {
    volatile bool   pending;
    uint8_t         msg1, msg2, cmd1, cmd2;
    char            cmd_str[128];   // Null-terminated ASCII command
    uint16_t        cmd_len;
} GantryRequest;
```

---

## What Can Go Wrong

| Symptom | Likely Cause |
|---------|-------------|
| No response at all | Button not wired in Designer, wrong command code, board not powered |
| "TIMEOUT" from gantry | Gantry USB plugged in (disables RS485), wrong baud rate, A/B swapped |
| Response from wrong board | boardID mismatch between GUI and firmware |
| Corrupted response | RS485 DE switched too early (before USART TC), CRC failure, noise |
| `tx_request` overwritten | Second command arrives before main loop processes first — response lost |
| Driverboard response missing | `dc_list_active` flag stuck true from previous list operation |
