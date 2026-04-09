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

## CMD_MEASURE_ADC (0x0C03) — Full Sequence Timing (v1.2.0+)

### Request Format

```
[board_mask][delay_lo][delay_hi][switch_groups...]
```

- `board_mask`: bitmask bits 0-3 = boards 0-3 (only masked boards are contacted)
- `delay_ms`: uint16 LE, clamped 1–100 ms
- Switch groups: 5 bytes each `[boardID][bank][SW_hi][SW_lo][state]`

### Response Format (422 bytes)

```
Byte  0      status_1 (0x00=OK, 0x06=ADC error)
Byte  1      status_2 (0x00=OK, 0x05=restore fail)
Bytes 2-5    Vpp (IEEE 754 float, little-endian)
Bytes 6-9    total_elapsed_ms (uint32 LE)
Bytes 10-11  phase1_ms — Save HVSG (uint16 LE)
Bytes 12-13  phase2_ms — GND HVSG (uint16 LE)
Bytes 14-15  phase3_ms — PWM Sync (uint16 LE)
Bytes 16-17  phase4_ms — Deterministic: timer + fire + burst ADC (uint16 LE)
Bytes 18-19  phase5_ms — Drain fire responses (uint16 LE)
Bytes 20-21  phase6_ms — Restore HVSG (uint16 LE)
Bytes 22-421 100 × 4-byte ADC samples (little-endian)
```

### Phase Timing Breakdown

| Phase | Operation | Per Board | Timeout | Notes |
|---|---|---|---|---|
| 1 — Save HVSG | GET_HVSG_SWITCHES (0x0B54) | Send + wait | DC_LIST_TIMEOUT (200 ms) | Returns only HVSG switches as 3-byte triplets (~50 bytes vs 603 for GET_ALL_SW) |
| 2 — GND HVSG | SET_LIST_OF_SW (0x0B51) | Send + wait | DC_LIST_TIMEOUT (200 ms) | Selectively grounds only saved HVSG switches |
| 3 — PWM Sync | PWMPhaseSync (0x0A81) | Send + wait | DC_RESPONSE_TIMEOUT (10 ms) | Resets TIM2/TIM1/TIM8 on driver board. Fast response (~1-2 ms) |
| 4 — Deterministic | TIM2 one-pulse + fire + burst ADC | N/A | Timer-controlled | Timer: `(num_switches × 3000 µs) + (delay_ms × 1000 µs)`. Fire: SET_LIST_OF_SW (no wait). ADC: 100 × SPI read (~0.3 ms) |
| 5 — Drain | Wait for Phase 4 responses | Per board | DC_LIST_TIMEOUT (200 ms) | Consumes stale SET_LIST_OF_SW responses |
| 6 — Restore | SET_LIST_OF_SW (0x0B51) | Send only | N/A (fire-and-forget) | Restores saved switches to HVSG. No response wait — measurement already captured |

### Board Mask Impact

Boards not in `board_mask` are skipped in all phases. No UART traffic, no timeouts.

| Boards Connected | Mask | Timeout Waste (old) | Timeout Waste (new) |
|---|---|---|---|
| 1 of 4 | 0x01 | 3 × 200 ms × 5 phases = **3000 ms** | **0 ms** |
| 2 of 4 | 0x03 | 2 × 200 ms × 5 phases = **2000 ms** | **0 ms** |
| 3 of 4 | 0x07 | 1 × 200 ms × 5 phases = **1000 ms** | **0 ms** |
| 4 of 4 | 0x0F | 0 ms | **0 ms** |

### Estimated Totals

#### 1 board, 1 switch, 10 ms delay (mask=0x01)

| Phase | Time | Notes |
|---|---|---|
| 1 — Save HVSG | ~5 ms | GET_HVSG_SWITCHES: small response (~10 bytes) |
| 2 — GND HVSG | ~5 ms | SET_LIST_OF_SW with few entries |
| 3 — PWM Sync | ~2 ms | Fast response, 10 ms timeout |
| 4 — Deterministic | ~13 ms | (1 × 3 ms) + 10 ms delay + 0.3 ms ADC |
| 5 — Drain | ~5 ms | 1 response to consume |
| 6 — Restore | ~1 ms | Fire-and-forget |
| **Total** | **~31 ms** | **Was ~796 ms before board mask** |

#### 4 boards, 10 switches each, 50 ms delay (mask=0x0F)

| Phase | Time | Notes |
|---|---|---|
| 1 — Save HVSG | ~20 ms | 4 × ~5 ms |
| 2 — GND HVSG | ~20 ms | 4 × ~5 ms |
| 3 — PWM Sync | ~8 ms | 4 × ~2 ms |
| 4 — Deterministic | ~80 ms | (10 × 3 ms) + 50 ms + 0.3 ms |
| 5 — Drain | ~20 ms | 4 responses |
| 6 — Restore | ~4 ms | 4 × fire-and-forget |
| **Total** | **~152 ms** |

#### 4 boards, 100 switches each, 100 ms delay (mask=0x0F)

| Phase | Time | Notes |
|---|---|---|
| 1 — Save HVSG | ~40 ms | 4 × ~10 ms (larger HVSG lists) |
| 2 — GND HVSG | ~80 ms | 4 × ~20 ms (more SET_LIST entries) |
| 3 — PWM Sync | ~8 ms | 4 × ~2 ms |
| 4 — Deterministic | ~400 ms | (100 × 3 ms) + 100 ms + 0.3 ms |
| 5 — Drain | ~20 ms | 4 responses |
| 6 — Restore | ~4 ms | 4 × fire-and-forget |
| **Total** | **~552 ms** |

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
