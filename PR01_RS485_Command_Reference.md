# PR01 Series Microplate Handling Robot
## RS485 Communication & Command Reference

**Source:** TPA Motion PR01 User's Guide V1.0 + Nippon Pulse Commander CMD-4CR Documentation

---

## 1. RS485 Physical Connection

### Pinout on the Controller (Power/Comms Screw Terminal Block)

| Pin | Signal | Description |
|-----|--------|-------------|
| 1 | Power GND | System ground |
| 2 | +24VDC | Power input (18–36VDC) |
| 3 | RS485 GND | Signal ground for RS485 |
| 4 | RS485 B– | Inverting line (differential pair) |
| 5 | RS485 A+ | Non-inverting line (differential pair) |

**Wiring:** Connect A+ to A+ and B– to B– on your USB-to-RS485 adapter. Connect RS485 GND between devices. Use a twisted pair for A+/B–.

### Bus Topology
- **Daisy-chain** configuration only — no star or stub topologies.
- Stubs should be shorter than 6 inches.
- Terminate with a 120Ω resistor across A+/B– at each end of the bus.
- The CMD-4EX-SA (Commander evaluation board) has built-in RC termination (120Ω). If the TPA controller also has termination, you may not need an external resistor.
- Up to 32 nodes on a single bus.

### Important Note
> USB and RS485 share the same Commander core. **RS485 communication is disabled whenever the USB connector is plugged in.** Disconnect USB to use RS485.

---

## 2. RS485 Serial Port Settings

| Parameter | Setting |
|-----------|---------|
| **Baud Rate** | 9600 (default) — changeable via `DB` command |
| **Data Bits** | 8 |
| **Parity** | None |
| **Stop Bits** | 1 |
| **Flow Control** | None |
| **Protocol** | ASCII, half-duplex |

### Changing Baud Rate (DB Command)

| DB Value | Baud Rate |
|----------|-----------|
| 1 | 9600 |
| 2 | 19200 |
| 3 | 38400 |
| 4 | 57600 |
| 5 | 115200 |

**Example:** Set baud to 115200:
```
@01DB=5\0
@01STORE\0      ;save to flash
```
Power-cycle the controller after STORE for the new baud rate to take effect.

---

## 3. ASCII Protocol (RS485 & USB)

### Sending a Command
```
@[ID][COMMAND]\0
```
- `@` — start character
- `[ID]` — 2-digit device ID (default `01`)
- `[COMMAND]` — ASCII command string (ALL CAPS)
- `\0` — null terminator

### Receiving a Reply
```
[RESPONSE]\0
```
- Response data followed by null terminator
- Write commands return `OK\0`
- Read commands return the value, e.g. `4664\0`

### Examples
| Action | Send | Reply |
|--------|------|-------|
| Query X polarity | `@01POLX\0` | `4664\0` |
| Read digital inputs | `@01DI\0` | `8\0` |
| Store settings to flash | `@01STORE\0` | `OK\0` |
| Read X motor position | `@01PX\0` | `11340\0` |
| Read X encoder position | `@01EX\0` | `11340\0` |
| Move X axis to 5000 | `@01X5000\0` | `OK\0` |
| Query motor status (X) | `@01MSTX\0` | *(see MST section)* |

### Device ID
- Default ID: `01`
- Changeable via `ID` command (range 0–63)
- Allows up to 64 controllers on one RS485 bus

---

## 4. PR01 Robot-Specific Configuration

### Motor Resolution
- **1260 motor microsteps per mm** (200 steps/rev × 16 microsteps/step ÷ 2.54 mm/rev)
- **1260 encoder counts per mm** (800 lines/rev × 4 counts/line ÷ 2.54 mm/rev)
- **96-well plate well spacing:** 9 mm = **11,340 steps** between adjacent wells

### Axis Travel
| Axis | Direction | Sensor-to-Sensor | Stop-to-Stop |
|------|-----------|-------------------|--------------|
| X | Left–Right | 114 mm | 118 mm |
| Y | Front–Back | 164 mm | 168 mm |
| Z | Vertical | 32 mm | 36 mm |

### Factory-Set Polarity
```
POLX=4664
POLY=4664
POLZ=4664
POLU=4664
```
These are stored in nonvolatile memory at the factory. Don't change them unless you know what you're doing.

---

## 5. Essential Command Reference

### System / Initialization

| Command | Description | Example |
|---------|-------------|---------|
| `EO=n` | Enable/disable axes. Bits: X=1, Y=2, Z=4. **EO=7 enables X+Y+Z.** EO=0 disables all. | `@01EO=7\0` |
| `ABS` | Set absolute positioning mode (recommended) | `@01ABS\0` |
| `INC` | Set incremental positioning mode | `@01INC\0` |
| `STORE` | Save current settings to flash (nonvolatile) | `@01STORE\0` |
| `IERR=n` | 0=limit errors enabled (default), 1=limit errors suppressed (useful for dev) | `@01IERR=1\0` |
| `CLR` / `CLEAR` | Clear axis error state | `@01CLR\0` |
| `%RST` | Soft reset the controller | `@01%RST\0` |
| `VER` | Read firmware version | `@01VER\0` |
| `ID` | Read/set device identification number | `@01ID\0` |
| `DB` | Read/set baud rate (see table above) | `@01DB\0` |
| `SN` | Read serial number | `@01SN\0` |

### Motion Parameters

| Command | Description | Unit | Example |
|---------|-------------|------|---------|
| `HSPD=n` | High (slew) speed | steps/sec | `@01HSPD=10000\0` |
| `LSPD=n` | Low (start/stop) speed | steps/sec | `@01LSPD=1000\0` |
| `ACC=n` | Acceleration ramp time | ms | `@01ACC=100\0` |
| `DEC=n` | Deceleration ramp time | ms | `@01DEC=100\0` |
| `SCV=n` | S-curve acceleration (smoother motion) | — | `@01SCV=50\0` |

### Single-Axis Motion

| Command | Description | Example |
|---------|-------------|---------|
| `X[pos]` | Move X to position (ABS) or by distance (INC) | `@01X11340\0` (go to well A2) |
| `Y[pos]` | Move Y to position | `@01Y6300\0` |
| `Z[pos]` | Move Z to position | `@01Z5000\0` |
| `U[pos]` | Move 4th axis (if configured) | `@01U1000\0` |
| `J[axis][+/-]` | Jog axis continuously in + or – direction | `@01JX+\0` |

### Homing

| Command | Description |
|---------|-------------|
| `HX-6` | Home X toward negative limit using mode 6 (recommended) |
| `HY-6` | Home Y toward negative limit using mode 6 |
| `HZ-6` | Home Z toward negative limit using mode 6 |

**TPA-recommended homing sequence:** Home Z first, Y second, X third — to avoid collisions.

### Wait / Synchronization

| Command | Description |
|---------|-------------|
| `WAITX` | Wait for X axis to complete its move |
| `WAITY` | Wait for Y axis to complete its move |
| `WAITZ` | Wait for Z axis to complete its move |
| `INP` | Wait for in-position |

### Stop Commands

| Command | Description |
|---------|-------------|
| `STOP` | Controlled deceleration stop (uses DEC ramp) |
| `ABORT` | Immediate hard stop (no decel) |
| `ESTOP` | Emergency stop (disables axes) |

### Position Read/Write

| Command | Description | Example |
|---------|-------------|---------|
| `PX` / `PY` / `PZ` | Read/set motor step counter | `@01PX\0` → `11340\0` |
| `EX` / `EY` / `EZ` | Read/set encoder counter | `@01EX=-1000\0` |
| `PP` | Read all motor positions at once | `@01PP\0` |
| `EP` | Read all encoder positions at once | `@01EP\0` |
| `PS` | Read pulse speed (current speed) | `@01PSX\0` |

### Motor Status (MST)

`MSTX`, `MSTY`, `MSTZ` return a bitmask with axis status:

| Bit | Meaning |
|-----|---------|
| 0 | Axis is moving |
| 1 | Direction (0=neg, 1=pos) |
| 2 | Negative limit active |
| 3 | Positive limit active |
| 4 | Error condition |
| 5 | Reserved |
| 6 | Home input active (**MSTZ bit 6 = Microplate Detect sensor**) |
| 7+ | Additional status bits |

**Key:** To check if a plate is present, read `MSTZ` and check bit 6.

### Digital I/O

| Command | Description |
|---------|-------------|
| `DI` | Read digital input status (bitmask) |
| `DIP` | Read/set digital input polarity |
| `DO=n` | Set digital output (bitmask) |
| `DOP` | Read/set digital output polarity |
| `IO` | Read general-purpose I/O status |
| `IOCFG` | Configure I/O as input or output |

### Polarity Configuration (POL)

| Command | Description |
|---------|-------------|
| `POLX=n` | X-axis polarity config (sensor levels, encoder, direction) |
| `POLY=n` | Y-axis polarity config |
| `POLZ=n` | Z-axis polarity config |
| `POLU=n` | U-axis polarity config |

Factory default for PR01: `4664` for all axes. **Do not change unless directed by TPA Motion.**

### StepNLoop (Closed-Loop Encoder Feedback)

| Command | Description |
|---------|-------------|
| `SL[axis]=n` | Enable StepNLoop (closed-loop) on axis |
| `SLE[axis]=n` | Set acceptable error range |
| `SLA[axis]=n` | Max correction attempts |
| `SLT[axis]=n` | In-position tolerance |
| `SLS[axis]` | Read StepNLoop status |
| `SLR[axis]=n` | Pulse conversion ratio |

### Interpolation (Multi-Axis Coordinated Motion)

| Command | Description |
|---------|-------------|
| `EINT=n` | Enable interpolation on axes |
| `I X[pos] Y[pos]` | 2-axis linear interpolation |
| `I X[pos] Y[pos] Z[pos]` | 3-axis linear interpolation |
| `CIR` / `ARC` | Circular interpolation |

### Standalone Programming (A-Script)

| Command | Description |
|---------|-------------|
| `PRG` / `END` | Begin/end program definition |
| `SACTRL=1` | Start standalone program |
| `SACTRL=0` | Stop standalone program |
| `GS=n` | Execute subroutine n from host (ASCII mode) |
| `V[n]=val` | Set variable (32 variables available) |
| `IF/ELSEIF/ELSE/ENDIF` | Conditional logic |
| `WHILE/ENDWHILE` | Loop |
| `DELAY=n` | Delay in ms |
| `SLOAD=n` | Auto-start program on power-up |

---

## 6. Complete Homing + Well Access Example

```
; --- INITIALIZATION ---
@01EO=7\0           ; Enable X, Y, Z axes
@01ABS\0            ; Absolute positioning mode
@01HSPD=10000\0     ; High speed = 10000 steps/sec
@01LSPD=1000\0      ; Low speed = 1000 steps/sec
@01ACC=100\0        ; Acceleration = 100ms ramp

; --- HOMING (Z first, then Y, then X) ---
@01HZ-6\0           ; Home Z toward negative limit
@01WAITZ\0          ; Wait for Z homing complete
@01HY-6\0           ; Home Y toward negative limit
@01WAITY\0          ; Wait for Y homing complete
@01HX-6\0           ; Home X toward negative limit
@01WAITX\0          ; Wait for X homing complete

; --- SET COORDINATE ORIGIN TO WELL A1 ---
; (Replace 1000/2000/500 with your measured offsets)
@01PX=-1000\0       ; Set motor position to negative offset
@01EX=-1000\0       ; Set encoder position to match
@01PY=-2000\0
@01EY=-2000\0
@01PZ=-500\0
@01EZ=-500\0

; --- MOVE TO WELL A1 (0,0) ---
@01X0\0
@01Y0\0
@01Z0\0
@01WAITX\0
@01WAITY\0
@01WAITZ\0

; --- MOVE TO WELL A2 (one column over = +11340 X steps) ---
@01X11340\0
@01WAITX\0

; --- MOVE TO WELL B1 (one row down = +11340 Y steps) ---
@01X0\0
@01Y11340\0
@01WAITX\0
@01WAITY\0

; --- CHECK FOR PLATE PRESENCE ---
@01MSTZ\0           ; Read Z status — check bit 6 for plate detect
```

### Well Position Formula (96-well plate, 9mm spacing)
```
X_position = (column - 1) × 11340    ; columns 1–12
Y_position = (row - 1) × 11340       ; rows A(1) – H(8)
```

---

## 7. Quick Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| No response on RS485 | USB cable is plugged in | **Unplug USB** — RS485 is disabled when USB is connected |
| No response on RS485 | Wrong baud rate | Default is 9600. Try that first. |
| No response on RS485 | Wrong device ID | Default is `01`. Send `@01VER\0` to test. |
| No response on RS485 | A+/B– swapped | Swap your wiring |
| Motor commands sent but no motion | Axes not enabled | Send `@01EO=7\0` |
| Axis won't move off limit sensor | Error state latched | Send `@01CLR\0` then `@01IERR=1\0` for dev |
| Position counters counting but no motion | Axes not enabled (EO=0) | Send `@01EO=7\0` |

---

## 8. Key Reference Links

| Resource | URL |
|----------|-----|
| **TPA Motion PR01 User's Guide (PDF)** | https://tpamotion.com/wp-content/uploads/2025/02/robot-usersguide-v1.pdf |
| **Commander Command Reference** | https://support.nipponpulse.com/NPA_Command_Reference/CMD_Command_Reference.html |
| **Commander Communication Manual** | https://support.nipponpulse.com/CommanderCommunications/CommanderCommunications.html |
| **Commander User Manual** | https://support.nipponpulse.com/Commander/Commander_Manual.html |
| **RS485 ASCII Protocol** | https://support.nipponpulse.com/CommanderCommunications/ASCIIProtocol.html |
| **GUI Utility Download** | https://support.nipponpulse.com (Downloads section) |
| **Commander DLL/API** | https://support.nipponpulse.com (Downloads section) |
| **TPA Motion Contact** | 800-284-9784 / www.tpamotion.com |
| **Nippon Pulse Contact** | (540) 633-1677 / info@nipponpulse.com |
