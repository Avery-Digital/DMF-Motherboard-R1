# Timing Analysis — CMD_MEASURE_ADC and Driver Board Communication

## v1.2.0 Optimization Note

As of v1.2.0, switch state values have been remapped: GND=0x00, HVSG=0x01, Float=0x04. The SOF collision (GND was 0x02) is eliminated — GET_ALL_SW is now ~55 ms regardless of switch state. CMD_MEASURE_ADC now uses GET_HVSG_SWITCHES (0x0B54) instead of GET_ALL_SW, selective GND instead of AllGND, and TIM2 (100 ns) instead of TIM6 (1 µs).

---

## UART Byte Stuffing Impact on Switch Data (Pre-v1.2.0)

Switch states previously used values that collided with protocol framing bytes:
- `0x00` = Float — **no escape**
- `0x01` = HVSG — **no escape**
- `0x02` = GND — **ESCAPES to 2 bytes** (0x02 = SOF, escaped as `[0x2D][0x2F]`)

This means the wire size of GET_ALL_SW responses varies dramatically based on switch state:

| Switch State | Payload Bytes | Escaped Bytes | Frame Size | TX Time @ 115200 |
|---|---|---|---|---|
| All Float (0x00) | 603 | 0 | ~620 | **53.8 ms** |
| All HVSG (0x01) | 603 | 0 | ~620 | **53.8 ms** |
| All GND (0x02) | 603 | +600 | ~1220 | **105.9 ms** |
| Mixed (50% GND) | 603 | +300 | ~920 | **79.9 ms** |

**Confirmed on oscilloscope: 107 ms with all switches at GND.**

The length field byte `0x02` (len_hi for 603 = 0x025B) also escapes, adding 1 more byte.

---

## GET_ALL_SW (0x0B53) — Per Board Round-Trip

### Best Case (all Float/HVSG)

| Phase | Time |
|---|---|
| Request TX (10 bytes) | 1.0 ms |
| Processing (SRAM copy + CRC + escape) | 0.1 ms |
| Response TX (~620 bytes) | 53.8 ms |
| **Total** | **~55 ms** |

### Worst Case (all GND — confirmed on scope)

| Phase | Time |
|---|---|
| Request TX (10 bytes) | 1.0 ms |
| Processing (SRAM copy + CRC + escape) | 0.1 ms |
| Response TX (~1220 bytes) | 105.9 ms |
| **Total** | **~107 ms** |

---

## AllSwitchesGND (0x0A02) — Per Board Round-Trip

| Phase | Time |
|---|---|
| Request TX (10 bytes) | 1.0 ms |
| Processing (20 block programs via SPI/I2C) | 17.0 ms |
| Response TX (~15 bytes) | 1.3 ms |
| **Total** | **~19 ms** |

---

## SET_LIST_OF_SW (0x0B51) — Per Board Round-Trip (600 switches)

| Phase | Time |
|---|---|
| Request TX (~3020 bytes worst-case) | 262 ms |
| Processing (20 block programs) | 17.0 ms |
| Response TX (~15 bytes) | 1.3 ms |
| **Total** | **~280 ms** |

Note: SET_LIST request payload is 600 × 5 bytes = 3000 bytes. With escaping this can grow significantly.

---

## CMD_MEASURE_ADC (0x0C03) — Full Sequence Timing

### With N Connected Boards + M Empty Slots (DC_LIST_TIMEOUT = 200 ms)

#### Phase 1: Save Switch States (GET_ALL_SW × 4)

| Scenario | Time |
|---|---|
| 4 boards, all GND | 4 × 107 ms = **428 ms** |
| 4 boards, all Float | 4 × 55 ms = **220 ms** |
| 1 board GND + 3 empty | 107 ms + 3 × 200 ms = **707 ms** |
| 0 boards | 4 × 200 ms = **800 ms** |

#### Phase 2: Set All GND (0x0A02 × N connected)

| Scenario | Time |
|---|---|
| 4 boards | 4 × 19 ms = **76 ms** |
| 1 board | **19 ms** |
| 0 boards | **0 ms** (skipped) |

#### Phase 3: Enable Measurement Switches (SET_LIST × N boards with switches)

Depends on how many switches are specified. Typical case (1 board, 10 switches):

| Scenario | Time |
|---|---|
| 1 board, 10 switches (50 bytes payload) | ~10 ms |
| 1 board, 100 switches (500 bytes payload) | ~60 ms |

#### Phase 4: Deterministic Wait (TIM6)

Configurable: 1–100 ms (set by GUI)

#### Phase 5: ADC Burst Read (100 samples via SPI)

~0.3 ms (100 × 3 µs per SPI read)

#### Phase 6: Restore (AllGND + SET_LIST × N connected)

Similar to Phase 2 + Phase 3:

| Scenario | Time |
|---|---|
| 4 boards, restore 100 non-GND switches each | 4 × (19 + 60) ms = **316 ms** |
| 1 board, 50 non-GND switches | 19 + 30 ms = **49 ms** |

#### Phase 7: Vpp Calculation + Response

~1 ms (CPU float math + 406-byte response TX)

---

## Total CMD_MEASURE_ADC Estimates

### Best Case: 0 boards connected, 10 ms delay

| Phase | Time |
|---|---|
| Phase 1 (save) | 800 ms (4 × 200 ms timeout) |
| Phase 2–3 (GND + set) | 0 ms (skipped) |
| Phase 4 (delay) | 10 ms |
| Phase 5 (ADC) | 0.3 ms |
| Phase 6 (restore) | 0 ms (skipped) |
| Phase 7 (response) | 1 ms |
| **Total** | **~811 ms** |

### Typical Case: 1 board (GND state), 10 switches, 10 ms delay

| Phase | Time |
|---|---|
| Phase 1 (save) | 107 ms + 3 × 200 ms = 707 ms |
| Phase 2 (GND) | 19 ms |
| Phase 3 (set 10 switches) | 10 ms |
| Phase 4 (delay) | 10 ms |
| Phase 5 (ADC) | 0.3 ms |
| Phase 6 (restore) | 49 ms |
| Phase 7 (response) | 1 ms |
| **Total** | **~796 ms** |

### Worst Case: 4 boards (all GND), 600 switches each, 100 ms delay

| Phase | Time |
|---|---|
| Phase 1 (save) | 428 ms |
| Phase 2 (GND) | 76 ms |
| Phase 3 (set 600 switches × 4) | 1120 ms |
| Phase 4 (delay) | 100 ms |
| Phase 5 (ADC) | 0.3 ms |
| Phase 6 (restore 600 × 4) | 1196 ms |
| Phase 7 (response) | 1 ms |
| **Total** | **~2921 ms (~3 seconds)** |

---

## Timeout Considerations

`DC_LIST_TIMEOUT` must be > worst-case single-board response time:

| Operation | Worst Case |
|---|---|
| GET_ALL_SW (all GND) | **107 ms** |
| AllGND (0x0A02) | **19 ms** |
| SET_LIST_OF_SW (600 switches) | **280 ms** |

**Current setting: 200 ms** — sufficient for GET_ALL_SW and AllGND, but **too short for large SET_LIST_OF_SW**.

**Recommendation: 300 ms** — covers all operations with margin.

---

## Root Cause of Byte Stuffing Overhead

The switch state encoding `0x02 = GND` collides with the protocol's `SOF = 0x02`. Every GND byte in the 600-switch payload gets escaped to 2 bytes, doubling the response size in worst case.

### Potential Mitigations (future)

1. **Change GND encoding** to a non-colliding value (e.g., 0x03) on the driver board — requires driver board firmware change
2. **Increase baud rate** to 230400 — halves all TX times
3. **Compress response** — run-length encode repeated states
4. **Selective read** — allow reading single bank (300 bytes instead of 600)
