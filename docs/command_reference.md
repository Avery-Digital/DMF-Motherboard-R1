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

**Response payload:** 4 bytes, IEEE 754 little-endian float — temperature in degrees C.

The firmware converts the raw ADC code to temperature using Steinhart-Hart coefficients for the SC50G104WH NTC thermistor (Material Type G, R25 = 100KΩ). The conversion accounts for the measurement circuit: 100 µA constant-current source driving R_th in parallel with a resistor divider (R1 = 10K, R2 = 1K), with the ADC reading across R2.

Returns `NaN` (0x7FC00000) if the ADC read fails or the thermistor is absent/open.

**Execution context:** ISR → deferred TX via `tx_request`.

See [docs/thermistor.md](thermistor.md) for circuit details and conversion math.

---

## CMD_GANTRY_CMD — Gantry RS485 Passthrough (`0x0C30`)

**Purpose:** Forward an ASCII command to the gantry system via RS485 (USART7 + MAX485) and return the response.

**Request payload:** ASCII command string (e.g. `@01VER`), no null terminator. The firmware appends a CR (`0x0D`) terminator before sending over RS485.

**Response payload:** `[status1][status2]` followed by ASCII response string from the gantry (e.g. `PR01 V1.0`), no null terminator. If the gantry does not respond within 500 ms, the response is `[0x08][0x01]` + `TIMEOUT` (9 bytes total). On success, status bytes are `[0x00][0x00]`.

**Execution context:** ISR → deferred to main loop via `gantry_request` → `Command_ExecuteGantry()` performs polled RS485 TX/RX → result via `tx_request`.

**Constants:**

```c
#define GANTRY_RESPONSE_MAX  128U   /* Max ASCII response bytes */
#define GANTRY_TIMEOUT_MS    500U   /* RS485 response timeout   */
```

See [docs/rs485_gantry.md](rs485_gantry.md) for hardware details and protocol.

---

## CMD_GET_BOARD_TYPE — Board Identification (`0x0B99`)

**Purpose:** Returns a fixed identifier so the host can distinguish the motherboard from a driverboard. Uses the same command code as the driverboard's GET_BOARD_TYPE. The motherboard intercepts this command and responds directly — it is **not** forwarded to daughtercards.

**Request payload:** Ignored.

**Response payload:** 5 bytes:

| Byte | Value | Meaning |
|------|-------|---------|
| 0 | `0x00` | status_1 |
| 1 | `0x00` | status_2 (success) |
| 2 | `0xFF` | boardID (0xFF = motherboard) |
| 3 | `0x4D` | 'M' |
| 4 | `0x42` | 'B' |

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

---

## Actuator Board Routed Commands (0x0F00–0x10FF)

The motherboard does not execute these commands locally. Instead, it routes any command with a code in the `0x0F00`–`0x10FF` range to one of 2 actuator board UARTs based on `boardID` (payload byte 0). The response from the actuator board is relayed back to the GUI over USART10.

This uses RS485 half-duplex communication via LTC2864 transceivers (UART5 for ACT1, USART6 for ACT2) at 115200 baud, 8N1.

### BoardID Mapping (Actuator Boards)

| boardID | UART | RS485 Transceiver | DE Pin |
|---------|------|--------------------|--------|
| 0 | UART5 | LTC2864 (ACT1) | PC8 (inverted) |
| 1 | USART6 | LTC2864 (ACT2) | PG8 (inverted) |

### Request Payload

Byte 0 = boardID (0 or 1), remaining bytes = actuator-board-specific payload.

### Response Payload

Relayed from the actuator board without modification via the `OnACT_PacketReceived` callback.

### Execution Context

ISR detects command code in range `CMD_ACT_RANGE_START` (`0x0F00`) to `CMD_ACT_RANGE_END` (`0x10FF`) and defers to the main loop via `act_forward_request`. The main loop calls `Act_Uart_SendPacket()` which performs polled RS485 TX with DE toggling, then the response arrives via DMA circular RX and is relayed to the GUI.

### Constants

```c
#define CMD_ACT_RANGE_START CMD_CODE(0x0F, 0x00)  /* 0x0F00 */
#define CMD_ACT_RANGE_END   CMD_CODE(0x10, 0xFF)  /* 0x10FF */
#define ACT_MAX_BOARDS      2U
```

---

## Detailed Packet Examples (Byte-Level)

For software engineers integrating with the motherboard, here is the exact byte structure for every motherboard-local command. All values are hex. Byte stuffing omitted for clarity.

**Notation:** `→` = request to motherboard, `←` = response from motherboard. `[m1][m2]` = message IDs (echoed). CRC is over header+payload.

---

### CMD_PING (0xDEAD)

```
→ [02] [m1] [m2] [00] [00] [DE] [AD] [CRC_hi] [CRC_lo] [7E]
                  len=0    cmd=DEAD

← [02] [m1] [m2] [00] [0A] [DE] [AD] [s1] [s2] [DE AD BE EF 01 02 03 04] [CRC] [7E]
                  len=10   cmd=DEAD   status     fixed test payload
```

**Note:** As of v1.0.1, the firmware version string reported via PING is `"MB_R1 v1.0.1"`.

---

### CMD_READ_ADC (0x0C01)

```
→ [02] [m1] [m2] [00] [00] [0C] [01] [CRC] [7E]
                  len=0

← [02] [m1] [m2] [00] [06] [0C] [01] [s1] [s2] [b0] [b1] [b2] [b3] [CRC] [7E]
                  len=6               status     32-bit LE, 18-bit ADC result
                                                 (0xFFFFFFFF = read error)
```

---

### CMD_BURST_ADC (0x0C02)

```
→ [02] [m1] [m2] [00] [00] [0C] [02] [CRC] [7E]

← [02] [m1] [m2] [01] [92] [0C] [02] [s1] [s2] [400 bytes: 100 x 4-byte LE samples] [CRC] [7E]
                  len=402             status     each sample: 32-bit LE, 18-bit ADC result
                                                 (0xFFFFFFFF per failed sample)
```

---

### CMD_LOAD_* (0x0C10-0x0C19) — Set

```
→ [02] [m1] [m2] [00] [01] [0C] [1x] [state] [CRC] [7E]
                  len=1              x=switch ID    0x01=ON, 0x00=OFF

← [02] [m1] [m2] [00] [03] [0C] [1x] [s1] [s2] [actual] [CRC] [7E]
                  len=3              echoed cmd     status  actual state after command
```

### CMD_LOAD_* (0x0C10-0x0C19) — Query (empty payload)

```
→ [02] [m1] [m2] [00] [00] [0C] [1x] [CRC] [7E]
                  len=0

← [02] [m1] [m2] [00] [03] [0C] [1x] [s1] [s2] [state] [CRC] [7E]
                  len=3              status         current state (0x01=ON, 0x00=OFF)
```

Load switch IDs: x=0 Valve1, x=1 Valve2, x=2 Microplate, x=3 Fan, x=4 TEC1, x=5 TEC2, x=6 TEC3, x=7 Assembly, x=8 Daughter1, x=9 Daughter2.

---

### CMD_THERM1-6 (0x0C20-0x0C25)

```
→ [02] [m1] [m2] [00] [00] [0C] [2x] [CRC] [7E]
                  len=0              x=0 therm1 ... x=5 therm6

← [02] [m1] [m2] [00] [06] [0C] [2x] [s1] [s2] [f0] [f1] [f2] [f3] [CRC] [7E]
                  len=6              status         IEEE 754 float, LE, temperature in °C
                                                    NaN (0x0000C07F LE) = read error or absent
```

Conversion: `memcpy(&float_val, &payload[2], 4)` on little-endian systems (offset by 2 for status bytes).

---

### CMD_GANTRY_CMD (0x0C30)

```
→ [02] [m1] [m2] [00] [06] [0C] [30] [40 30 31 56 45 52] [CRC] [7E]
                  len=6              "@01VER" as ASCII bytes (no null)

← [02] [m1] [m2] [00] [nn] [0C] [30] [s1] [s2] [ASCII response bytes...] [CRC] [7E]
                  len=nn             status     gantry response (no null)
                                     or [0x08][0x01] + "TIMEOUT" (9 bytes total) on 500ms timeout
```

On timeout, the status bytes are `[0x08][0x01]` (Gantry category, timeout error). On success, status bytes are `[0x00][0x00]`.

---

### CMD_GET_BOARD_TYPE (0x0B99) — Intercepted by Motherboard

```
→ [02] [m1] [m2] [00] [00] [0B] [99] [CRC] [7E]
                  len=0    (payload ignored)

← [02] [m1] [m2] [00] [05] [0B] [99] [00] [00] [FF] [4D] [42] [CRC] [7E]
                  len=5               s1   s2   bid   'M'  'B'
                                      0xFF = motherboard boardID
```

This command is NOT forwarded to driverboards even though 0x0B99 is in the DC range. The motherboard intercepts it in an explicit `case` before the default driverboard routing.

---

### Driverboard Routed Commands (0x0A00-0x0BFF)

These are **not processed** by the motherboard. The payload is forwarded to the target driverboard UART and the response is relayed back unchanged.

```
GUI → Motherboard:
  [02] [m1] [m2] [len_hi] [len_lo] [cmd1] [cmd2] [boardID] [data...] [CRC] [7E]
                                                    ↑ byte 0 of payload = boardID (0-3)

Motherboard → Driverboard:
  [02] [m1] [m2] [len_hi] [len_lo] [cmd1] [cmd2] [boardID] [data...] [CRC] [7E]
                                                    (same packet, re-framed)

Driverboard → Motherboard → GUI:
  [02] [m1] [m2] [len_hi] [len_lo] [cmd1] [cmd2] [response...] [CRC] [7E]
                                                    (relayed unchanged)
```

The msg1/msg2 and cmd1/cmd2 fields are preserved end-to-end so the GUI can match responses to requests.

---

### Actuator Board Routed Commands (0x0F00-0x10FF)

Same forwarding pattern as driverboard commands, but via RS485:

```
GUI → Motherboard:
  [02] [m1] [m2] [len_hi] [len_lo] [cmd1] [cmd2] [boardID] [data...] [CRC] [7E]
                                                    ↑ boardID = 0 or 1

Motherboard → Actuator Board (RS485):
  [02] [m1] [m2] [len_hi] [len_lo] [cmd1] [cmd2] [boardID] [data...] [CRC] [7E]

Actuator Board → Motherboard → GUI (RS485 → USART10):
  [02] [m1] [m2] [len_hi] [len_lo] [cmd1] [cmd2] [s1] [s2] [boardID] [data...] [CRC] [7E]
```

See the [DMF-ActuatorBoard README](https://github.com/Avery-Digital/DMF-ActuatorBoard) for detailed per-command actuator payloads.

---

### Response Flow Documentation

For a detailed trace of how commands and responses flow through every layer (ISR, DMA, main loop, RS485 direction control), see [response_flow.md](response_flow.md).

---

## Command Code Allocation

| Range | Category |
|-------|----------|
| `0xDExx` | Debug / test commands |
| `0x0Axx` | Driverboard switch/PMU/EHVG commands (routed to daughtercards) |
| `0x0Bxx` | Driverboard bulk/query commands (routed to daughtercards) |
| `0x0B99` | Board identification (intercepted by motherboard, not forwarded) |
| `0x0Cxx` | Motherboard-local: ADC (0x0C01–0x0C02), load switches (0x0C10–0x0C19), thermistors (0x0C20–0x0C25), gantry (0x0C30) |
| `0x0Fxx`–`0x10xx` | Actuator board commands (routed to ACT1/ACT2 via RS485) |
| `0xBExx` | Debug routed commands (0xBEEF) |
| `0xFFxx` | (reserved — system/reset commands) |
