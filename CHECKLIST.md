# DMF Motherboard R1 — Development Checklist

## 1. Droplet Sensing (LTC2338-18 ADC)
- [x] SPI2 driver for LTC2338-18 18-bit ADC
- [x] CMD_READ_ADC (0x0C01) — single read
- [x] CMD_BURST_ADC (0x0C02) — 100-sample burst
- [x] CMD_MEASURE_ADC (0x0C03) — switch-controlled deterministic measurement
- [x] GPIO PWM phase sync (v1.3.1) — PA12/PC5 hardware pulse replaces UART PWMPhaseSync
- [x] GUI burst plot with ScottPlot
- [x] Bipolar ±10.24V conversion in GUI
- [x] Works with or without driver boards plugged in

## 2. DMF Driver Board Communication
- [x] 4x daughtercard UART interfaces (USART1/2/3, UART4)
- [x] DMA circular RX + polled TX
- [x] Command routing (0x0A00–0x0BFF) based on boardID (0–3)
- [x] SET_LIST_OF_SW / GET_LIST_OF_SW batched per-board
- [x] GET_ALL_SW for saving/restoring switch states
- [x] Async forward for single commands
- [x] Synchronous list mode with response mailbox

## 3. Actuator Board Communication
- [x] 2x actuator board UART interfaces (UART5, USART6) via RS485 (LTC2864)
- [x] DMA circular RX + polled TX with inverted DE logic (NOT gate)
- [x] Command routing (0x0F00–0x10FF) based on boardID (0–1)
- [x] Response relay back to GUI via USART10
- [x] Actuator board firmware operational (28 GPIO outputs, L293Q, inverse logic)
- [x] GUI tab with 28 toggle buttons, LED panels, FW read, enable control

## 4. TPS Motion Gantry Communication
- [ ] RS485 half-duplex via USART7 + MAX485 (PF6 RX, PF7 TX, PF8 DE/RE)
- [ ] ASCII command protocol to TPS gantry controller
- [ ] CMD_GANTRY_CMD (0x0C30) passthrough from GUI
- [ ] Response parsing and relay to GUI
- [ ] Motion commands: home, move absolute, move relative, velocity
- [ ] Status/position query
- [ ] Error handling and timeout recovery
- [ ] GUI gantry control panel integration

## 5. TEC PID Control (DRV8702 H-Bridge)
- [ ] DRV8702 SPI register configuration (3 instances)
- [ ] PWM output via TIM1 channels for H-bridge direction/enable
- [ ] Thermistor readback via ADS7066 (6 channels on instance 3)
- [ ] PID algorithm implementation (proportional, integral, derivative)
- [ ] Setpoint management (target temperature per TEC)
- [ ] PID tuning parameters (Kp, Ki, Kd) — configurable via command
- [ ] Anti-windup for integral term
- [ ] PID loop execution in main loop (fixed interval via SysTick)
- [ ] TEC enable/disable commands
- [ ] Temperature monitoring and over-temp protection
- [ ] GUI controls: setpoint input, current temp display, PID tuning, enable/disable
- [ ] Thermal runaway detection and safe shutdown

## 6. Supporting Infrastructure
- [x] Clock tree: HSE 12 MHz → PLL1 480 MHz, PLL2/3 128 MHz
- [x] USART10 host PC communication (115200 baud, DMA TX/RX)
- [x] Protocol parser (CRC-16 CCITT, byte stuffing, SOF/EOF)
- [x] Deferred TX pattern (ISR → main loop)
- [x] 10x load switches (VN5T016AH) with GPIO control
- [x] USB2517 hub initialization
- [x] I2C1 driver (400 kHz)
- [x] Error handler with RTC backup register fault codes
- [x] Documentation (architecture, pin assignments, command reference)
