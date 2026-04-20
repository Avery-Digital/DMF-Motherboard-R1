# Changelog

## v2.0.0 — 2026-04-20

### Breaking: unified big-endian wire format + scaled integers

The protocol is now big-endian end-to-end. All existing BE fields (framing, CRC,
command codes, switch numbers, actuator mask, MEASURE_ADC delay) keep their
order. All previously little-endian fields flip to BE, and floats are replaced
with scaled integers so hex dumps are human-readable on every board.

New shared helper header `Inc/endian_be.h` (`be16_pack/unpack`, `be32_pack/unpack`).

| Command | Field | Before | After |
|---|---|---|---|
| `CMD_THERM1..6` (0x0C20–25) | temperature | IEEE 754 float LE (4B) | `temp_c × 100` as int16 BE (2B); `0x8000` sentinel on read error |
| `CMD_CSENSE_*` (0x0C40–49) | V_SENSE mV | IEEE 754 float LE (4B) | `v_sense_mV × 10` as uint16 BE (2B); `0xFFFF` sentinel on error |
| `CMD_READ_ADC` (0x0C01) | 18-bit ADC result | uint32 LE | uint32 BE (byte[0] reserved/top) |
| `CMD_BURST_ADC` (0x0C02) | 100 samples | uint32 LE × 100 | uint32 BE × 100 |
| `CMD_MEASURE_ADC` (0x0C03) | Vpp | IEEE 754 float LE | `vpp × 10000` as int32 BE; `0x80000000` sentinel on error |
| `CMD_MEASURE_ADC` | elapsed_ms, total_ms | uint32 LE | uint32 BE |
| `CMD_MEASURE_ADC` | 6 × phase timestamps | uint16 LE | uint16 BE |
| `CMD_MEASURE_ADC` | 100 burst samples | uint32 LE × 100 | uint32 BE × 100 |
| `CMD_SWEEP_ADC` (0x0C05) | total_ms | uint32 LE | uint32 BE |
| `CMD_SWEEP_ADC` | per-switch Vpp array | IEEE 754 float LE × N | `vpp × 10000` int32 BE × N |

Thermistor response shrinks 6 → 4 bytes. Current-sense response shrinks 6 → 4
bytes. MEASURE_ADC response size unchanged (426 bytes). SWEEP_ADC response
structure unchanged.

**Paired release required.** Every firmware image and the GUI move to v2.0.0
together. Any old firmware talking to new GUI (or vice versa) will decode
garbage.

Firmware version string: `"MB_R1 v2.0.0"`.

---

## v1.5.3 — 2026-04-20

### Breaking: MEASURE_ADC / SWEEP_ADC delay now big-endian
- `CMD_MEASURE_ADC` (0x0C03) and `CMD_SWEEP_ADC` (0x0C05) request payloads now encode the 2-byte `delay_ms` field as **big-endian** (was little-endian).
- Example: 10 ms was `[0x0A][0x00]` on the wire, now `[0x00][0x0A]`.
- Payload layout unchanged otherwise: `[board_mask(1B)][delay_ms(2B BE)][5-byte switch groups…]`.
- Switch-number field inside each 5-byte group was already big-endian — delay now matches.
- Response format unchanged (no `delay_ms` in response).
- **Paired change required on host side.** GUI `MEASURE_ADC_BUTTON_Click` in `Form1.cs` was updated in lockstep; older GUI builds will silently send the wrong delay (e.g. 10 ms LE → decoded as 2560 ms, clamped to 100 ms).

---

## v1.5.2 — 2026-04-13

### CMD_TEC_RESET (0x0C54) — Full Power-Cycle Reset
- Stop PWM → Sleep → delay → Wake → Clear faults via SPI
- Re-latches MODE=0 (PH/EN mode) on wake
- Response includes fault status after reset: `[s1][s2][tec_id][fault]`
- Use to recover from overcurrent, overtemperature, or gate driver faults

---

## v1.5.1 — 2026-04-13

### TEC PWM Fix — Correct PH/EN Mode for DRV8702-Q1
- **Part clarification**: Actual part is DRV8702-Q1 (full H-bridge), NOT DRV8702D-Q1 (half-bridge)
- DRV8702-Q1 in PH/EN mode (MODE=0): PH=direction GPIO, EN=PWM for power
- Reverted TEC_PWM from dual-channel (IN1+IN2 PWM) to single-channel (EN PWM only)
- PH pins (PE9, PE13, PJ8) remain GPIO — direction set via DRV8702_SetDirection()
- EN pins (PE11, PE14, PJ10) reconfigured to AF for timer PWM
- Timer mapping: TEC1=TIM1_CH2, TEC2=TIM1_CH4, TEC3=TIM8_CH2
- PH/EN truth table (from DRV8702-Q1 datasheet Table 7-3):
  - PH=X, EN=LOW → brake (both low-side FETs on)
  - PH=LOW, EN=HIGH → reverse (current SH2→SH1 = cool)
  - PH=HIGH, EN=HIGH → forward (current SH1→SH2 = heat)

---

## v1.5.0 — 2026-04-13

### TEC Manual Control Commands (0x0C50–0x0C53)
- Commands: SET (0x0C50), GET (0x0C51), STOP (0x0C52), STOP_ALL (0x0C53)
- Default 20 kHz PWM, 0–100% duty per TEC

---

## v1.4.2 — 2026-04-13

### Load Switch Current Sense — Return V_SENSE in mV
- Changed response from computed I_LOAD (mA) to raw V_SENSE (mV)
- kILIS is non-linear with load current — raw sense voltage is more accurate
- Removed CSENSE_KILIS and CSENSE_R_OHM constants (calibration done on host side)
- Response format unchanged: `[status1][status2][v_sense_mV float LE (4B)]`

---

## v1.4.1 — 2026-04-13

### Load Switch Current Sensing (0x0C40–0x0C49)
- 10 new commands for reading load current via VN5T016AH CSENSE pin
- Current sense path: CSENSE → 1 kΩ to GND → ADS7066 analog input
- Response: `[status1][status2][current_mA float LE (4B)]`
- Conversion: `V = (ADC/65536) × 2.5V`, `I_load = (V / 1kΩ) × kILIS × 1000 mA`
- `CSENSE_KILIS = 1600` (typical, calibrate on hardware)

| Command | Code | ADC Instance | Channel | Load |
|---------|------|-------------|---------|------|
| CMD_CURR_VALVE1 | 0x0C40 | Instance 2 (PD4) | AIN6 | Valve 1 |
| CMD_CURR_VALVE2 | 0x0C41 | Instance 2 (PD4) | AIN7 | Valve 2 |
| CMD_CURR_MICROPLATE | 0x0C42 | Instance 1 (PD5) | AIN2 | Microplate |
| CMD_CURR_FAN | 0x0C43 | Instance 1 (PD5) | AIN3 | Fan |
| CMD_CURR_TEC1 | 0x0C44 | Instance 2 (PD4) | AIN0 | TEC 1 |
| CMD_CURR_TEC2 | 0x0C45 | Instance 2 (PD4) | AIN1 | TEC 2 |
| CMD_CURR_TEC3 | 0x0C46 | Instance 1 (PD5) | AIN0 | TEC 3 |
| CMD_CURR_ASSEMBLY | 0x0C47 | Instance 3 (PD3) | AIN6 | Assembly Station |
| CMD_CURR_DAUGHTER1 | 0x0C48 | Instance 1 (PD5) | AIN4 | Daughter 1 |
| CMD_CURR_DAUGHTER2 | 0x0C49 | Instance 1 (PD5) | AIN5 | Daughter 2 |

---

## v1.4.0 — 2026-04-09

### CMD_SWEEP_ADC (0x0C05) — Per-Switch ADC Sweep
- New command: measures each switch individually and returns N × Vpp values
- Same payload format as CMD_MEASURE_ADC (board_mask + delay + switch groups)
- Per-switch loop: PWM sync → timer → enable 1 switch → ADC burst → Vpp → GND
- Response: `[s1][s2][total_ms uint32 LE][N × Vpp float LE]`
- Full save/restore with SwitchSaveEntry (HVSG + non-HVSG switches)

### CMD_MEASURE_ADC Improvements
- Added `SwitchSaveEntry` struct for clean save/restore of non-HVSG measurement switches
- Phase 1b: queries original state of measurement switches not in HVSG list via GET_LIST_OF_SW (0x0B52)
- Phase 6: restores both HVSG and non-HVSG switches to original state
- Aborts entire command if Phase 1b times out (no partial measurement)
- Added `total_ms` timestamp (start to before TX) in response — response now 426 bytes
- `PWM_SyncPulse()` prototype added to `main.h` (fixes implicit declaration warning)

### Driver Board Fix
- Fixed `GetListOfSwitches` (0x0B52): response was echoing garbled SW_hi/SW_lo bytes
- Status bytes changed from 0xAB/0xCD to STATUS_CAT_OK/STATUS_CODE_OK

---

## v1.3.2 — 2026-04-09

### Standalone PWM Sync Command
- Added `CMD_PWM_SYNC` (0x0C04) — triggers GPIO sync pulse on demand
- Calls `PWM_SyncPulse()` (PA12 + PC5), responds with `[status1][status2]`
- Use to sync AC waveforms independently of CMD_MEASURE_ADC

---

## v1.3.1 — 2026-04-09

### Hardware GPIO PWM Phase Sync
- **Replaced UART PWMPhaseSync (0x0A81) with hardware GPIO pulse** in CMD_MEASURE_ADC Phase 3
- PA12 (pin 131) → Connector 1 → boards 0+1 (PD3 on driver board)
- PC5 (pin 54) → Connector 2 → boards 2+3 (PD3 on driver board)
- Rising edge triggers EXTI3 on driver boards, resetting TIM2/TIM1/TIM8 counters
- Sub-microsecond sync across all boards (was ~2 ms per board via UART)
- Both connectors pulsed back-to-back — ~10 ns gap at 480 MHz, negligible at 10 kHz PWM
- `PWM_SyncPulse()` function added to main.c, sync GPIO pins in Bsp.c

---

## v1.3.0 — 2026-04-08

### CMD_MEASURE_ADC — Board Mask, Phase Timing, Timeout Tuning
- **Board mask**: New `board_mask` byte (bits 0-3) in request payload. Only masked boards are contacted — eliminates 200 ms timeout per missing board per phase.
- **Request format**: `[board_mask][delay_lo][delay_hi][switch_groups...]` (was `[delay_lo][delay_hi][groups...]`)
- **Per-phase timestamps**: Response now includes 6 × uint16 phase timings (Save, GND, Sync, Deterministic, Drain, Restore)
- **Response format**: 422 bytes = `[s1][s2][Vpp(4B)][elapsed_ms(4B)][phase_ms×6(12B)][samples(400B)]` (was 410 bytes)
- **PWM Sync timeout**: Reduced from DC_LIST_TIMEOUT (200 ms) to DC_RESPONSE_TIMEOUT (10 ms). PWMPhaseSync responds in ~1-2 ms.
- **Fire-and-forget restore**: Phase 6 sends SET_LIST_OF_SW without waiting for response. Measurement is already captured.
- **Delay minimum**: Changed from 1 ms to 0 ms. Timer still runs for `num_switches × 3 ms`.
- **Performance**: 1 board / 1 switch / 0 ms delay → ~31 ms (was ~796 ms before board mask)

### Bug Fix
- **DC_MAX_BOARDS**: Changed from 1U to 4U — was causing "excess elements in array initializer" warnings and only using board 0
- **burst_raw**: Removed unused debug array from `Command_ExecuteBurstADC()`

---

## v1.2.0 — 2026-04-08

### CMD_MEASURE_ADC Optimization
- **Phase 1**: Replaced GET_ALL_SW (0x0B53, 603-byte response) with GET_HVSG_SWITCHES (0x0B54) — returns only HVSG switch triplets. Typical response: ~10-50 bytes instead of 603+.
- **Phase 2**: Selective GND — only sets HVSG switches to GND via SET_LIST_OF_SW instead of AllGND on all 600 switches. Saves ~14 ms per board.
- **Phase 4**: Upgraded timer from TIM6 (16-bit, 1 µs resolution) to TIM2 (32-bit, 100 ns resolution, PSC=23 → 10 MHz tick). Max period ~429 seconds.
- **Phase 6**: Restores only saved HVSG switches. No AllGND clean-slate needed.
- **Elapsed timestamp**: Response now includes `elapsed_ms` (uint32 LE) measuring total firmware execution time from command entry to response build.

### Switch State Remapping (Breaking)
- GND: 0x02 → 0x00 (eliminates SOF byte stuffing — GET_ALL_SW now ~55 ms regardless of state)
- Float: 0x00 → 0x04
- HVSG: unchanged (0x01)
- Added named constants: `SW_STATE_GND`, `SW_STATE_HVSG`, `SW_STATE_FLOAT`
- New driver board command constant: `CMD_GET_HVSG_SW_CMD1/CMD2` (0x0B54)

### Response Format Change (CMD_MEASURE_ADC)
- Old: `[s1][s2][Vpp(4B)][samples(400B)]` = 406 bytes
- New: `[s1][s2][Vpp(4B)][elapsed_ms(4B)][samples(400B)]` = 410 bytes

### Expected Performance
- 1 board, 10 HVSG switches, 10 ms delay: ~233 ms → ~98 ms (58% faster)
- Primary savings from eliminating 600-byte GET_ALL_SW and full-board AllGND

---

## v1.1.0 — 2026-04-05

### New Command: CMD_MEASURE_ADC (0x0C03)
- Atomic switch-controlled ADC measurement with deterministic timing
- Full sequence: save all 4 boards' switch states → AllGND all 4 boards → enable specified switches → hardware-timed delay → 100-sample ADC burst → restore all boards → return Vpp + raw data
- Uses TIM6 one-pulse mode for µs-precision deterministic timing between switch settle and ADC read
- Configurable delay via payload (1–100 ms, uint16 LE)
- Returns IEEE 754 float Vpp (peak-to-peak voltage) + 400 bytes raw ADC samples
- Peak-to-peak calculated as simple min/max on signed 18-bit two's complement samples
- ADC voltage scaling: ±10.24 V bipolar, LSB = 20.48/262144 = 78.125 µV (matches GUI PlotBurstADC)

### Request Payload Format
```
[delay_ms_lo][delay_ms_hi][5-byte switch groups...]
```
Each group: `[boardID][bank][SW_hi][SW_lo][state]` (same as SET_LIST_OF_SW)

### Response Payload Format (406 bytes)
```
[status1][status2][Vpp float LE (4B)][100 × 4-byte ADC samples (400B)]
```

### New Status Codes
- `0x06 / 0x04` — Daughtercard timeout during measure sequence
- `0x06 / 0x05` — Switch restore failed (ADC data still valid)

### New Constants
- `ADC_FULL_SCALE_V = 20.48f` (±10.24 V bipolar span)
- `ADC_FULL_SCALE_CODES = 262144.0f` (2^18)
- `MEASURE_ADC_DELAY_MIN_MS = 1`, `MEASURE_ADC_DELAY_MAX_MS = 100`
- `MEASURE_ADC_SW_STATES = 600` (2 banks × 300 switches per board)

### Files Modified
- `Inc/Command.h` — New command code, constants, status codes, function declaration
- `Inc/main.h` — New `MeasureAdcRequest` struct
- `Src/Command.c` — ISR handler `Command_HandleMeasureADC` + dispatch case
- `Src/main.c` — Global request instance, main loop check, `Command_ExecuteMeasureADC()` (7-phase blocking function using TIM6)
- `docs/command_reference.md` — Full CMD_MEASURE_ADC documentation

---

## v1.0.1 — 2026-04-03

### Unified Status Bytes
- All motherboard local commands now return `[status1][status2]` as the first two bytes of every response payload
- Previously, CMD_PING, CMD_READ_ADC, CMD_BURST_ADC, CMD_LOAD_*, CMD_THERM, and CMD_GANTRY_CMD returned raw data without status bytes
- Status byte format: `[category][code]` where both `0x00` = success
- Error categories: 0x01=General, 0x05=LoadSwitch, 0x06=ADC/Sensor, 0x08=Gantry, 0x09=Routing

### Firmware Version Reporting
- CMD_PING (0xDEAD) now returns `[status1][status2]["MB_R1 v1.0.1"]` instead of fixed test bytes
- Added FW_VERSION_MAJOR/MINOR/PATCH defines in Command.h

### Actuator Board Routing — 0-Based BoardID
- Changed actuator board boardID from 1-based (1, 2) to 0-based (0, 1)
- ACT_GetHandle now maps boardID 0 → UART5 (ACT1), boardID 1 → USART6 (ACT2)
- Command_HandleActForward validation changed from `< 1 || > 2` to `>= ACT_MAX_BOARDS`

### RS485 Gantry Protocol Fix
- Changed command terminator from null (0x00) to carriage return (0x0D) per Nippon Pulse Commander CMD-4CR manual
- Response parser now accepts CR (0x0D) or LF (0x0A) as terminators, skips leading CR/LF
- Gantry timeout response now includes status bytes: `[0x08][0x01]["TIMEOUT"]`

### Command Response Format Changes (Breaking)

| Command | Old Response | New Response |
|---------|-------------|--------------|
| CMD_PING (0xDEAD) | `DE AD BE EF 01 02 03 04` | `[s1][s2]["MB_R1 v1.0.1"]` |
| CMD_READ_ADC (0x0C01) | `[4B ADC LE]` | `[s1][s2][4B ADC LE]` |
| CMD_BURST_ADC (0x0C02) | `[400B samples]` | `[s1][s2][400B samples]` |
| CMD_LOAD_* (0x0C10-19) | `[state]` | `[s1][s2][state]` |
| CMD_THERM (0x0C20-25) | `[4B float]` | `[s1][s2][4B float]` |
| CMD_GANTRY_CMD (0x0C30) | `[ASCII]` or `"TIMEOUT"` | `[s1][s2][ASCII]` |
| CMD_GET_BOARD_TYPE (0x0B99) | `[s1][s2][bid][M][B]` | (unchanged) |

### Status Code Table

| status1 (category) | status2 (code) | Meaning |
|---------------------|----------------|---------|
| 0x00 | 0x00 | Success |
| 0x01 | 0x01 | Payload too short |
| 0x01 | 0x02 | Unknown command |
| 0x05 | 0x01 | Invalid load switch ID |
| 0x05 | 0x02 | Load switch set failed |
| 0x06 | 0x01 | LTC2338-18 SPI read failed |
| 0x06 | 0x02 | ADS7066 read failed |
| 0x06 | 0x03 | Thermistor out of range |
| 0x08 | 0x01 | Gantry RS485 timeout |
| 0x08 | 0x02 | Gantry communication error |
| 0x09 | 0x01 | Invalid driverboard boardID |
| 0x09 | 0x02 | Invalid actuator boardID |
| 0x09 | 0x03 | Downstream board timeout |

---

## v1.0.0 — 2026-03-31

- Initial release with full command set
- Thermistor Steinhart-Hart conversion module
- RS485 gantry driver (USART7 + MAX485)
- Actuator board routing via LTC2864 RS485
- 4 daughtercard UART interfaces
- 10 load switches, LTC2338-18 ADC, ADS7066 thermistors
