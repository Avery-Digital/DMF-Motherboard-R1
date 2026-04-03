# Changelog

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
