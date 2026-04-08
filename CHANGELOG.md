# Changelog

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
