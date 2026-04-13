# DMF System — Issues & Complications Log

Tracks all hardware and firmware problems encountered during development, along with their root causes and solutions.

---

## Hardware Issues (PCB / Physical Modifications)

### HW-001: UART TX/RX Swapped on All DMF Driver Board Connectors
- **Boards affected:** Motherboard R1 (all 4 daughtercard connectors)
- **Symptom:** No communication between motherboard and driver boards
- **Root cause:** PCB routing error — TX was connected to TX and RX to RX instead of crossing over
- **Fix:** Physical wire modification on the board to cross TX↔RX on all 4 mezzanine connectors
- **Status:** Resolved (hardware rework)

### HW-002: Load Switch Pull-Down Resistors Prevent Turn-On
- **Boards affected:** Motherboard R1
- **Symptom:** VN5T016AH load switches would never turn on regardless of GPIO state
- **Root cause:** Pull-down resistors on the enable lines were too strong, holding the gate low
- **Fix:** Physically removed pull-down resistors from all 10 load switch enable lines
- **Status:** Resolved (hardware rework)

### HW-003: USB2517 Hub Failure — Oscillator and Capacitor Issues
- **Boards affected:** Motherboard R1
- **Symptom:** USB hub not enumerating, no downstream USB devices detected
- **Root cause:** Two issues — (1) oscillator had poor solder joint or was not making good connection, (2) wrong capacitor values were used for the USB2517
- **Fix:** Erik re-soldered the oscillator and removed the two incorrect capacitors
- **Status:** Resolved (hardware rework)

### HW-004: RS485 Gantry IC Not Rated for 3.3V Operation
- **Boards affected:** Motherboard R1
- **Symptom:** Unreliable or no communication with TPS motion gantry system
- **Root cause:** The RS485 transceiver IC (MAX485) on the gantry interface is not designed to operate at 3.3V logic levels — may need a part rated for 3.3V
- **Fix:** Under investigation — may need to source and replace with a 3.3V-compatible RS485 transceiver
- **Status:** OPEN

### HW-005: DRV8702D-Q1 vs DRV8702-Q1 Part Number Confusion
- **Boards affected:** Motherboard R1
- **Symptom:** Documentation and schematic references listed the TEC driver as DRV8702D-Q1 (DRV8702DQRHBRQ1)
- **Root cause:** The DRV8702**D**-Q1 is a **half-bridge** driver (2 FETs), while the actual part on the board is the **DRV8702-Q1** which is a **full H-bridge** driver (4 FETs). The "D" suffix denotes the half-bridge variant. This distinction matters for driver configuration (PH/EN mode with 4 FETs vs 2 FETs) and current handling capability.
- **Fix:** Corrected all documentation and firmware comments to reference DRV8702-Q1 (full H-bridge, 4 FETs). Updated README.md Target Hardware table.
- **Status:** Resolved (2026-04-13, documentation corrected)

---

## Firmware Issues — Motherboard

### FW-MB-001: RS485 DE/RE Polarity Inverted (NOT Gate)
- **Commit:** `1f53c9d`
- **Symptom:** RS485 communication with actuator boards not working
- **Root cause:** PCB has a NOT gate between the MCU GPIO and the LTC2864 DE/RE pins. Original code used non-inverted logic
- **Fix:** Inverted DE control logic — GPIO LOW = transmit, GPIO HIGH = receive (idle)

### FW-MB-002: DMA1 Stream 7 IRQn Name Mismatch
- **Commit:** `5b926e9`
- **Symptom:** Build error — `DMA1_STR7_IRQn` undeclared
- **Root cause:** Used incorrect IRQ enum name. CMSIS header defines `DMA1_Stream7_IRQn`, not `DMA1_STR7_IRQn` (Stream 7 has a different naming convention than Streams 0–6)
- **Fix:** Changed to `DMA1_Stream7_IRQn` in Bsp.c

### FW-MB-003: RS485 Gantry Wrong Command Terminator
- **Commit:** `c593881`
- **Symptom:** TPS gantry not responding to commands
- **Root cause:** Firmware was sending null terminator (0x00). Nippon Pulse Commander CMD-4CR manual requires carriage return (0x0D)
- **Fix:** Changed terminator to CR (0x0D), updated response parser to accept CR or LF

### FW-MB-004: CMD_MEASURE_ADC Fails Without Driver Boards
- **Commit:** `7a63e41`
- **Symptom:** ADC measurement command hangs for 2+ seconds then returns timeout error when no driver boards are plugged in
- **Root cause:** Phase 1 (save switch states) looped through all 4 board slots and aborted entirely if any board didn't respond
- **Fix:** Each board is now independent — timeout boards are marked as not connected and skipped in all subsequent phases. ADC measurement always proceeds.

### FW-MB-005: DC_LIST_TIMEOUT Too Long for Board Detection
- **Commit:** `5d891c8`
- **Symptom:** With empty board slots, CMD_MEASURE_ADC Phase 1 takes 2 seconds (4 × 500 ms timeout)
- **Root cause:** DC_LIST_TIMEOUT was 500 ms — very conservative. A full 600-switch operation takes ~17 ms.
- **Fix:** Reduced to 100 ms (5× margin for real operations, 400 ms total for 4 empty slots)

### FW-MB-006: Missing Include Caused Build Failure
- **Commit:** `804ec26`
- **Symptom:** Build error — `GANTRY_RESPONSE_MAX` undeclared
- **Root cause:** `main.h` used `GANTRY_RESPONSE_MAX` from `command.h` but didn't include it
- **Fix:** Added `#include "command.h"` to `main.h`

### FW-MB-007: Unified Status Bytes Breaking Change
- **Commit:** `8da8176`
- **Symptom:** GUI parsing wrong bytes from responses after adding status bytes
- **Root cause:** All motherboard commands were updated to return `[status1][status2]` prefix, but GUI parsers had hardcoded byte offsets
- **Fix:** Updated all GUI response handlers with +2 byte offset. Added categorized status codes across all boards for consistency.

---

## Firmware Issues — Actuator Board

### FW-ACT-001: Cold Boot Failure — VOS0 Timeout
- **Commit:** `0ce99b9` (v1.0.1)
- **Symptom:** Actuator board hangs at boot, never reaches main loop
- **Root cause:** SMPS+LDO power supply needs settling time before requesting VOS0 voltage scaling. Original delays were too short for cold power-on.
- **Fix:** Increased settle delay from 50K to 1M iterations (~50 ms), VOS0 timeout from 1M to 5M, SYSCLK switch timeout from 50K to 1M, post-switch stabilization from 100K to 1M

### FW-ACT-002: Actuators 26/27 Floating HIGH — False Activation
- **Commit:** `0ce99b9` (v1.0.1)
- **Symptom:** Two actuator outputs were active on boot (inverse logic — floating HIGH = OFF, but unassigned pins were floating)
- **Root cause:** Actuators 26 and 27 had no pin assignments in the original code (`{0}` entries in pin array)
- **Fix:** Assigned Act 26 → PA1 (pin 18), Act 27 → PA0 (pin 17)

### FW-ACT-003: BoardID Convention Mismatch
- **Commit:** `0ce99b9` (v1.0.1)
- **Symptom:** GUI commands not reaching correct actuator board
- **Root cause:** GUI was sending 1-based boardID (1, 2) but motherboard routing expected 0-based (0, 1)
- **Fix:** Changed both GUI and motherboard to 0-based. Actuator board echoes whatever boardID it receives.

---

## Firmware Issues — Driver Board

### FW-DB-001: Baud Rate Typo
- **Commit:** `b2bc1c9`
- **Symptom:** No UART communication with motherboard
- **Root cause:** UART_Baudrate was set to 112500 instead of 115200 in DMFDriverConfig.c
- **Fix:** Corrected to 115200

### FW-DB-002: RAM_D2 Overflow
- **Commit:** `1dca303`
- **Symptom:** Build failure or runtime crash
- **Root cause:** DMA buffers exceeded the 32 KB D2 SRAM region
- **Fix:** Reduced buffer sizes to fit within RAM_D2

### FW-DB-003: READ_FW Response Not Echoing Message IDs
- **Commit:** `dd3f1d6`
- **Symptom:** GUI couldn't match firmware version response to the request
- **Root cause:** READ_FW (0xBEEF) was sending hardcoded msg1/msg2 instead of echoing the received values
- **Fix:** Echo received msg1/msg2 and cmd1/cmd2 in the response

---

## GUI Issues

### GUI-001: HVSG Voltage Read Byte Offset Wrong
- **Changelog:** 2026-03-31
- **Symptom:** Voltage display showing garbage values
- **Root cause:** `Get_CH0_Voltage` and `Get_CH1_Voltage` were reading from `rxDataBuffer[9..12]` instead of `rxDataBuffer[8..11]`
- **Fix:** Corrected byte offset

### GUI-002: Status Byte Offset After Unified Status Codes
- **Changelog:** 2026-04-03
- **Symptom:** All response parsers reading wrong data after firmware status byte update
- **Root cause:** Firmware added `[status1][status2]` to all responses, shifting all data by 2 bytes
- **Fix:** Updated all GUI response handlers (thermistor, load switches, burst ADC, gantry, FW version) with +2 byte offset

### GUI-003: Actuator BoardID Mismatch (1-based vs 0-based)
- **Changelog:** 2026-04-03
- **Symptom:** Actuator commands going to wrong board or not being routed
- **Root cause:** ComboBox had items "1"/"2", motherboard expected 0-based
- **Fix:** Changed ComboBox items to "0"/"1", SelectedActuatorBoardID returns raw index

### GUI-004: Actuator Switch IDs 1-Based on Wire
- **Changelog:** 2026-04-06
- **Symptom:** Actuator 1 button controlling wrong physical switch
- **Root cause:** Button click sent 1-based ID (button name number), firmware expected 0-based
- **Fix:** GUI sends `actId - 1`, response handler does `actId + 1` for panel lookup

---

## Known Noise / Signal Issues

### SIG-001: 58 Hz Mains-Coupled Noise on ADC Input
- **Commit:** `23022f9`
- **Symptom:** LTC2338-18 burst ADC plot shows ~58 Hz noise with ±1 V amplitude superimposed on signal of interest
- **Root cause:** Mains-coupled noise on the analog input path (likely through power supply or ground loops)
- **Status:** Documented. Y-axis fixed at ±5 V in GUI for stable reference. See `docs/spi_adc.md` for mitigation options.
