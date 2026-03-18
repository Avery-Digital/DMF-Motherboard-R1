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

## Command Code Allocation

| Range | Category |
|-------|----------|
| `0xDExx` | Debug / test commands |
| `0x0Cxx` | ADC / sensor read commands (0x0C01–0x0C02) and load switch commands (0x0C10–0x0C19) |
| `0x10xx` | (reserved — future control commands) |
| `0xFFxx` | (reserved — system/reset commands) |
