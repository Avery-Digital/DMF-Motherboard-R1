# Command Reference

All commands are dispatched by `Command_Dispatch()` in `Command.c`. Command handlers run in **ISR context** (from DMA HT/TC or USART IDLE interrupt). They must not call `USART_Driver_SendPacket()` directly — instead they populate the `tx_request` struct and set `.pending = true` for the main loop to transmit.

For commands that require significant processing time (e.g. burst ADC reads), the ISR handler sets `burst_request.pending` and the main loop calls a dedicated execution function.

## Command Code Format

Commands are 16-bit codes formed from two bytes in the packet header:

```c
#define CMD_CODE(c1, c2)    ((uint16_t)((c1) << 8) | (c2))
```

## CMD_PING — `0xDEAD`

**Purpose:** Echo / link test. Verifies the UART communication path is working end-to-end.

**Request payload:** Ignored (any length, any content).

**Response payload:** 8 bytes (fixed):

| Byte | Value |
|------|-------|
| 0 | `0xDE` |
| 1 | `0xAD` |
| 2 | `0xBE` |
| 3 | `0xEF` |
| 4 | `0x01` |
| 5 | `0x02` |
| 6 | `0x03` |
| 7 | `0x04` |

**Response header:** Echoes back the request's msg1, msg2, cmd1, cmd2.

**Execution context:** ISR → deferred TX via `tx_request`.

---

## CMD_READ_ADC — `0x0C01`

**Purpose:** Trigger a single conversion on the LTC2338-18 18-bit ADC and return the result.

**Request payload:** None (length = 0).

**Response payload:** 4 bytes, little-endian unsigned 32-bit integer:

| Byte | Content |
|------|---------|
| 0 | Bits [7:0] (LSB) |
| 1 | Bits [15:8] |
| 2 | Bits [17:16] (only 2 valid bits) |
| 3 | `0x00` (reserved) |

**Value range:** 0 to 262143 (0x0003FFFF) for a valid 18-bit result.

**Error sentinel:** All four bytes set to `0xFF` (`0xFFFFFFFF`) indicates a failed SPI read (timeout, overrun, or bus error). This is distinguishable from any valid ADC result since the maximum 18-bit value is 0x3FFFF.

**Response header:** Echoes back the request's msg1, msg2, cmd1, cmd2.

**Execution context:** ISR (SPI polling is ~3 µs total, acceptable in ISR) → deferred TX via `tx_request`.

**Hardware sequence:**
1. Pulse CNV pin HIGH for ≥30 ns
2. Wait for BUSY pin to go LOW (~1 µs conversion time)
3. 32-bit SPI transfer at 16 MHz SCK
4. Right-shift raw word by 14 to extract 18-bit result
5. Mask to 18 bits: `(raw >> 14) & 0x0003FFFF`

---

## CMD_BURST_ADC — `0x0C02`

**Purpose:** Read the LTC2338-18 ADC 100 times in rapid succession and return all results in a single response packet.

**Request payload:** None (length = 0).

**Response payload:** 400 bytes (100 samples x 4 bytes each):

```
Byte offset  Content
─────────────────────────
[4n + 0]     Sample n, bits [7:0]   (LSB)
[4n + 1]     Sample n, bits [15:8]
[4n + 2]     Sample n, bits [17:16] (only 2 valid bits)
[4n + 3]     0x00 (reserved) — or 0xFF if sample failed
```

Where `n` ranges from 0 to 99.

**Error sentinel per sample:** If an individual SPI read fails, that 4-byte slot is set to `0xFFFFFFFF`. Other samples in the burst are still valid — the host can detect partial failures without discarding the entire burst.

**Response header:** Echoes back the request's msg1, msg2, cmd1, cmd2.

**Execution context:** This command uses a **two-stage deferred pattern** because 100 SPI reads (~300 µs) is too slow for ISR context:

1. **ISR stage** (`Command_HandleBurstADC`): Saves header fields to `burst_request`, sets `burst_request.pending = true`. Returns immediately.
2. **Main loop stage** (`Command_ExecuteBurstADC`): Performs the 100 SPI reads, packs results into a static 400-byte buffer, copies to `tx_request`, sets `tx_request.pending = true`.

**Debug support:** `Command_ExecuteBurstADC()` maintains a `static uint32_t burst_raw[100]` array that stores the raw ADC samples. This array is visible in the debugger for offline analysis without needing UART output.

**Constants:**

```c
#define ADC_BURST_COUNT         100U    /* Samples per burst */
#define ADC_BURST_PAYLOAD_SIZE  400U    /* 100 x 4 bytes    */
```

---

## CMD_LOAD_* — Load Switch Control (0x0C10–0x0C19)

**Purpose:** Enable, disable, or query the state of the 10 high-side load switches (VN5T016AH). All 10 commands share a single handler (`Command_HandleLoadSwitch`) that receives a `LoadSwitch_ID` parameter to select the target switch.

### Command Codes

| Command | Code | Load |
|---------|------|------|
| `CMD_LOAD_VALVE1` | `0x0C10` | Valve 1 |
| `CMD_LOAD_VALVE2` | `0x0C11` | Valve 2 |
| `CMD_LOAD_MICROPLATE` | `0x0C12` | Microplate |
| `CMD_LOAD_FAN` | `0x0C13` | Fan |
| `CMD_LOAD_TEC1` | `0x0C14` | TEC 1 power |
| `CMD_LOAD_TEC2` | `0x0C15` | TEC 2 power |
| `CMD_LOAD_TEC3` | `0x0C16` | TEC 3 power |
| `CMD_LOAD_ASSEMBLY` | `0x0C17` | Assembly station |
| `CMD_LOAD_DAUGHTER1` | `0x0C18` | Daughter board 1 |
| `CMD_LOAD_DAUGHTER2` | `0x0C19` | Daughter board 2 |

### Request Payload

- **1 byte:** `0x01` = ON, `0x00` = OFF. Sets the load switch to the requested state.
- **0 bytes (empty):** Status query. The switch state is not changed; the response reports the current state.

### Response Payload

| Byte | Content |
|------|---------|
| 0 | Current state: `0x01` = ON, `0x00` = OFF |

The response always reports the actual state of the switch after the command is processed. For a set command, this confirms the new state. For a query, this reports the existing state.

**Response header:** Echoes back the request's msg1, msg2, cmd1, cmd2.

**Execution context:** ISR → deferred TX via `tx_request`.

### Shared Handler Pattern

All 10 commands dispatch to the same function:

```c
static void Command_HandleLoadSwitch(USART_Handle *handle,
                                     const PacketHeader *header,
                                     const uint8_t *payload,
                                     LoadSwitch_ID id);
```

Each `case` in `Command_Dispatch()` passes the appropriate `LoadSwitch_ID` enum value to select which switch to control.

---

## Adding a New Command

1. Define the command code in `Command.h`:

   ```c
   #define CMD_NEW_COMMAND    CMD_CODE(0xXX, 0xYY)
   ```

2. Write a static handler function in `Command.c`:

   ```c
   static void Command_HandleNewCommand(USART_Handle *handle,
                                        const PacketHeader *header,
                                        const uint8_t *payload)
   {
       (void)handle;
       (void)payload;

       uint8_t response[] = { /* ... */ };

       tx_request.msg1    = header->msg1;
       tx_request.msg2    = header->msg2;
       tx_request.cmd1    = header->cmd1;
       tx_request.cmd2    = header->cmd2;
       memcpy(tx_request.payload, response, sizeof(response));
       tx_request.length  = sizeof(response);
       tx_request.pending = true;  /* Must be last — acts as the commit */
   }
   ```

3. Add forward declaration and a case to `Command_Dispatch()`:

   ```c
   case CMD_NEW_COMMAND:
       Command_HandleNewCommand(handle, header, payload);
       break;
   ```

4. If the command requires heavy processing (>10 µs), use the deferred pattern:
   - Create a request struct similar to `BurstRequest` in `main.h`
   - ISR handler sets the request fields and `.pending = true`
   - Main loop checks `.pending` and calls the execution function
   - Execution function does the work and sets `tx_request.pending` when done

5. Update this document with the new command's specification.

---

---

## CMD_THERM1–CMD_THERM6 — Thermistor Reads (`0x0C20`–`0x0C25`)

**Purpose:** Read a thermistor channel from ADS7066 instance 3.

| Command | Code | ADS7066 Channel |
|---------|------|-----------------|
| `CMD_THERM1` | `0x0C20` | Instance 3, ch0 |
| `CMD_THERM2` | `0x0C21` | Instance 3, ch1 |
| `CMD_THERM3` | `0x0C22` | Instance 3, ch2 |
| `CMD_THERM4` | `0x0C23` | Instance 3, ch3 |
| `CMD_THERM5` | `0x0C24` | Instance 3, ch4 |
| `CMD_THERM6` | `0x0C25` | Instance 3, ch5 |

**Request payload:** None (length = 0).

**Response payload:** 2 bytes, little-endian unsigned 16-bit ADC result.

**Execution context:** ISR → deferred TX via `tx_request`.

---

## Daughtercard Routed Commands

The motherboard does not execute these commands locally. Instead, it routes them to one of 4 daughtercard UARTs based on `boardID` (payload byte 0). The response from the daughtercard is relayed back to the GUI over USART10.

### BoardID Mapping

| boardID | UART | Connector |
|---------|------|-----------|
| 0 | USART1 | Connector 1 bottom |
| 1 | USART2 | Connector 1 top |
| 2 | USART3 | Connector 2 bottom |
| 3 | UART4 | Connector 2 top |

### Mode 1 — Async Forward Commands

These 26 commands are forwarded asynchronously. The ISR defers to the main loop via `dc_forward_request`. The main loop sends the packet to the correct DC UART, and the response is relayed back to the GUI when it arrives.

| Code | Name | Description |
|------|------|-------------|
| `0xBEEF` | `CMD_DC_DEBUG` | Debug test loopback |
| `0x0A00` | `AllSwitchesFloat` | Float all switches |
| `0x0A01` | `AllSwitchesToVIn1` | All switches to HVSG |
| `0x0A02` | `AllSwitchesToVIn2` | All switches to GND |
| `0x0A10` | `SetSingleSwitch` | Set one switch |
| `0x0A11` | `GetSingleSwitch` | Get one switch state |
| `0x0A50` | `PMUTurnCh0On` | PMU ch0 on |
| `0x0A51` | `PMUTurnCh0Off` | PMU ch0 off |
| `0x0A52` | `PMUTurnCh1On` | PMU ch1 on |
| `0x0A53` | `PMUTurnCh1Off` | PMU ch1 off |
| `0x0AC4` | `EHVGSetPWMFreq` | Set PWM frequency |
| `0x0AC5` | `EHVGGetPWMFreq` | Get PWM frequency |
| `0x0AC6` | `EHVGSetPWMDC` | Set PWM duty cycle |
| `0x0AC7` | `EHVGGetPWMDC` | Get PWM duty cycle |
| `0x0AD0` | `EHVGTurnCh0On` | HVSG ch0 on |
| `0x0AD1` | `EHVGTurnCh0Off` | HVSG ch0 off |
| `0x0AD2` | `EHVGTurnCh1On` | HVSG ch1 on |
| `0x0AD3` | `EHVGTurnCh1Off` | HVSG ch1 off |
| `0x0AD4` | `EHVGSetVCh0` | Set HVSG ch0 voltage |
| `0x0AD5` | `EHVGGetVCh0` | Get HVSG ch0 voltage |
| `0x0AD6` | `EHVGSetVCh1` | Set HVSG ch1 voltage |
| `0x0AD7` | `EHVGGetVCh1` | Get HVSG ch1 voltage |
| `0x0B01` | `GET_INA228_CH0` | Read INA228 ch0 |
| `0x0B02` | `GET_INA228_CH1` | Read INA228 ch1 |
| `0x0B53` | `GET_ALL_SW` | Get all 600 switch states |
| `0x0B99` | `GET_BOARD_TYPE` | Board identification |

**Request payload:** Byte 0 = boardID (0–3), remaining bytes = command-specific payload.

**Response payload:** Relayed from the daughtercard without modification.

**Execution context:** ISR → deferred to main loop via `dc_forward_request` → polled TX to DC UART → DMA RX response → relay to GUI via `tx_request`.

### Mode 2 — SET_LIST_OF_SW (`0x0B51`)

**Purpose:** Bulk-set multiple switches across multiple daughtercards in a single command. The motherboard sequentially sends each group to the target board and waits for a response.

**Request payload:** Groups of 5 bytes, repeated N times:

| Offset | Size | Content |
|--------|------|---------|
| 0 | 1 | boardID (0–3) |
| 1 | 1 | bank |
| 2 | 1 | SW_hi |
| 3 | 1 | SW_lo |
| 4 | 1 | state |

**Behavior:** For each 5-byte group, the main loop sends a `SetSingleSwitch` (`0x0A10`) command to the target daughtercard and waits up to 10 ms for a response. The `dc_list_active` flag causes `OnDC_PacketReceived` to deposit responses into the `dc_response` mailbox instead of relaying directly.

**Response payload:** Aggregate response sent to GUI after all groups are processed.

**Execution context:** ISR → deferred to main loop → synchronous sequential send/receive.

### Mode 3 — GET_LIST_OF_SW (`0x0B52`)

**Purpose:** Bulk-query multiple switch states across multiple daughtercards.

**Request payload:** Groups of 4 bytes, repeated N times:

| Offset | Size | Content |
|--------|------|---------|
| 0 | 1 | boardID (0–3) |
| 1 | 1 | bank |
| 2 | 1 | SW_hi |
| 3 | 1 | SW_lo |

**Behavior:** Same sequential pattern as SET_LIST_OF_SW, but each group is forwarded as `GetSingleSwitch` (`0x0A11`).

**Response payload:** Aggregate response sent to GUI after all groups are processed.

**Execution context:** ISR → deferred to main loop → synchronous sequential send/receive.

---

## Command Code Allocation

| Range | Category |
|-------|----------|
| `0xDExx` | Debug / test commands |
| `0x0Axx` | Driverboard switch/PMU/EHVG commands (routed to daughtercards) |
| `0x0Bxx` | Driverboard bulk/query commands (routed to daughtercards) |
| `0x0Cxx` | Motherboard-local: ADC reads (0x0C01–0x0C02), load switches (0x0C10–0x0C19), thermistors (0x0C20–0x0C25) |
| `0x10xx` | (reserved — future control commands) |
| `0xBExx` | Debug routed commands (0xBEEF) |
| `0xFFxx` | (reserved — system/reset commands) |
