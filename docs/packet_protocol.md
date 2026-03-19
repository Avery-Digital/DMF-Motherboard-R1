# Packet Protocol Specification

## Overview

Communication between the host PC and the MCU uses a custom framed serial protocol over USART at 115200 baud (8N1). Each packet is delimited by start/end markers, uses byte-stuffing to avoid marker collisions in data, and includes a CRC-16 for integrity verification.

All UART reception is interrupt-driven via circular DMA with three interrupt sources (DMA HT, DMA TC, USART IDLE). No polling is required.

## Frame Format

```
┌─────┬──────┬──────┬────────┬────────┬──────┬──────┬──────────────┬────────┬────────┬─────┐
│ SOF │ MSG1 │ MSG2 │ LEN_HI │ LEN_LO │ CMD1 │ CMD2 │ PAYLOAD[0..N]│ CRC_HI │ CRC_LO │ EOF │
└─────┴──────┴──────┴────────┴────────┴──────┴──────┴──────────────┴────────┴────────┴─────┘
 0x02                                                                                  0x7E
```

All fields between SOF and EOF are byte-stuffed (see below).

## Field Definitions

| Field | Size | Description |
|-------|------|-------------|
| SOF | 1 byte | Start of frame: `0x02`. Never escaped. |
| MSG1 | 1 byte | Message type byte 1. Application-defined. |
| MSG2 | 1 byte | Message type byte 2. Application-defined. |
| LEN_HI | 1 byte | Payload length, high byte (big-endian). |
| LEN_LO | 1 byte | Payload length, low byte (big-endian). |
| CMD1 | 1 byte | Command byte 1 (high byte of 16-bit command code). |
| CMD2 | 1 byte | Command byte 2 (low byte of 16-bit command code). |
| PAYLOAD | 0–4096 bytes | Application data. Length is LEN_HI:LEN_LO. |
| CRC_HI | 1 byte | CRC-16 result, high byte (big-endian). |
| CRC_LO | 1 byte | CRC-16 result, low byte (big-endian). |
| EOF | 1 byte | End of frame: `0x7E`. Never escaped. |

## Length Field

The length field is a 16-bit big-endian (MSB first) value representing the number of payload bytes only. It does not include the header or CRC.

Example: a 5-byte payload sends LEN_HI = `0x00`, LEN_LO = `0x05`.

Maximum payload length: 4096 bytes (`PKT_MAX_PAYLOAD`).

## Command Code

The 16-bit command code is formed by combining CMD1 (high byte) and CMD2 (low byte):

```
command = (CMD1 << 8) | CMD2
```

Example: CMD1 = `0xDE`, CMD2 = `0xAD` → command = `0xDEAD`

### Defined Commands

| Command | CMD1 | CMD2 | Code | Description |
|---------|------|------|------|-------------|
| `CMD_PING` | `0xDE` | `0xAD` | `0xDEAD` | Echo / link test. Returns a fixed 8-byte payload. |
| `CMD_READ_ADC` | `0x0C` | `0x01` | `0x0C01` | Single LTC2338-18 ADC read. Returns 4-byte result. |
| `CMD_BURST_ADC` | `0x0C` | `0x02` | `0x0C02` | Burst 100x ADC reads. Returns 400-byte payload. |
| `CMD_LOAD_VALVE1` | `0x0C` | `0x10` | `0x0C10` | Load switch: Valve 1. |
| `CMD_LOAD_VALVE2` | `0x0C` | `0x11` | `0x0C11` | Load switch: Valve 2. |
| `CMD_LOAD_MICROPLATE` | `0x0C` | `0x12` | `0x0C12` | Load switch: Microplate. |
| `CMD_LOAD_FAN` | `0x0C` | `0x13` | `0x0C13` | Load switch: Fan. |
| `CMD_LOAD_TEC1` | `0x0C` | `0x14` | `0x0C14` | Load switch: TEC 1 power. |
| `CMD_LOAD_TEC2` | `0x0C` | `0x15` | `0x0C15` | Load switch: TEC 2 power. |
| `CMD_LOAD_TEC3` | `0x0C` | `0x16` | `0x0C16` | Load switch: TEC 3 power. |
| `CMD_LOAD_ASSEMBLY` | `0x0C` | `0x17` | `0x0C17` | Load switch: Assembly station. |
| `CMD_LOAD_DAUGHTER1` | `0x0C` | `0x18` | `0x0C18` | Load switch: Daughter board 1. |
| `CMD_LOAD_DAUGHTER2` | `0x0C` | `0x19` | `0x0C19` | Load switch: Daughter board 2. |
| `CMD_THERM1` | `0x0C` | `0x20` | `0x0C20` | Thermistor 1 (ADS7066 inst3 ch0) |
| `CMD_THERM2` | `0x0C` | `0x21` | `0x0C21` | Thermistor 2 (ADS7066 inst3 ch1) |
| `CMD_THERM3` | `0x0C` | `0x22` | `0x0C22` | Thermistor 3 (ADS7066 inst3 ch2) |
| `CMD_THERM4` | `0x0C` | `0x23` | `0x0C23` | Thermistor 4 (ADS7066 inst3 ch3) |
| `CMD_THERM5` | `0x0C` | `0x24` | `0x0C24` | Thermistor 5 (ADS7066 inst3 ch4) |
| `CMD_THERM6` | `0x0C` | `0x25` | `0x0C25` | Thermistor 6 (ADS7066 inst3 ch5) |
| `CMD_DC_DEBUG` | `0xBE` | `0xEF` | `0xBEEF` | Debug test loopback (routed to DC) |
| `AllSwitchesFloat` | `0x0A` | `0x00` | `0x0A00` | Float all switches (routed to DC) |
| `AllSwitchesToVIn1` | `0x0A` | `0x01` | `0x0A01` | All switches to HVSG (routed to DC) |
| `AllSwitchesToVIn2` | `0x0A` | `0x02` | `0x0A02` | All switches to GND (routed to DC) |
| `SetSingleSwitch` | `0x0A` | `0x10` | `0x0A10` | Set one switch (routed to DC) |
| `GetSingleSwitch` | `0x0A` | `0x11` | `0x0A11` | Get one switch state (routed to DC) |
| `PMUTurnCh0On` | `0x0A` | `0x50` | `0x0A50` | PMU ch0 on (routed to DC) |
| `PMUTurnCh0Off` | `0x0A` | `0x51` | `0x0A51` | PMU ch0 off (routed to DC) |
| `PMUTurnCh1On` | `0x0A` | `0x52` | `0x0A52` | PMU ch1 on (routed to DC) |
| `PMUTurnCh1Off` | `0x0A` | `0x53` | `0x0A53` | PMU ch1 off (routed to DC) |
| `EHVGSetPWMFreq` | `0x0A` | `0xC4` | `0x0AC4` | Set PWM frequency (routed to DC) |
| `EHVGGetPWMFreq` | `0x0A` | `0xC5` | `0x0AC5` | Get PWM frequency (routed to DC) |
| `EHVGSetPWMDC` | `0x0A` | `0xC6` | `0x0AC6` | Set PWM duty cycle (routed to DC) |
| `EHVGGetPWMDC` | `0x0A` | `0xC7` | `0x0AC7` | Get PWM duty cycle (routed to DC) |
| `EHVGTurnCh0On` | `0x0A` | `0xD0` | `0x0AD0` | HVSG ch0 on (routed to DC) |
| `EHVGTurnCh0Off` | `0x0A` | `0xD1` | `0x0AD1` | HVSG ch0 off (routed to DC) |
| `EHVGTurnCh1On` | `0x0A` | `0xD2` | `0x0AD2` | HVSG ch1 on (routed to DC) |
| `EHVGTurnCh1Off` | `0x0A` | `0xD3` | `0x0AD3` | HVSG ch1 off (routed to DC) |
| `EHVGSetVCh0` | `0x0A` | `0xD4` | `0x0AD4` | Set HVSG ch0 voltage (routed to DC) |
| `EHVGGetVCh0` | `0x0A` | `0xD5` | `0x0AD5` | Get HVSG ch0 voltage (routed to DC) |
| `EHVGSetVCh1` | `0x0A` | `0xD6` | `0x0AD6` | Set HVSG ch1 voltage (routed to DC) |
| `EHVGGetVCh1` | `0x0A` | `0xD7` | `0x0AD7` | Get HVSG ch1 voltage (routed to DC) |
| `GET_INA228_CH0` | `0x0B` | `0x01` | `0x0B01` | Read INA228 ch0 (routed to DC) |
| `GET_INA228_CH1` | `0x0B` | `0x02` | `0x0B02` | Read INA228 ch1 (routed to DC) |
| `SET_LIST_OF_SW` | `0x0B` | `0x51` | `0x0B51` | Bulk switch set (synchronous, routed to DC) |
| `GET_LIST_OF_SW` | `0x0B` | `0x52` | `0x0B52` | Bulk switch get (synchronous, routed to DC) |
| `GET_ALL_SW` | `0x0B` | `0x53` | `0x0B53` | Get all 600 switch states (routed to DC) |
| `GET_BOARD_TYPE` | `0x0B` | `0x99` | `0x0B99` | Board identification (routed to DC) |

See [command_reference.md](command_reference.md) for detailed payload layouts and response formats.

## CRC-16 CCITT

| Parameter | Value |
|-----------|-------|
| Algorithm | CRC-16 CCITT |
| Polynomial | `0x1021` (x^16 + x^12 + x^5 + 1) |
| Initial value | `0xFFFF` |
| Final XOR | None |
| Input reflection | No |
| Output reflection | No |

The CRC is computed over the **6-byte header + payload** (everything between SOF and CRC, after byte-unstuffing). The CRC bytes themselves are NOT included in the CRC calculation.

```
CRC input:  [MSG1] [MSG2] [LEN_HI] [LEN_LO] [CMD1] [CMD2] [PAYLOAD...]
CRC output: 16-bit value transmitted as CRC_HI (MSB first), CRC_LO
```

Implementation: lookup table in `crc16.c`, callable via `CRC16_Calc()` (buffer) or `CRC16_Update()` (incremental byte-at-a-time).

## Byte Stuffing

Three byte values have special meaning and must be escaped when they appear in data:

| Value | Name | Purpose |
|-------|------|---------|
| `0x02` | SOF | Start of frame marker |
| `0x7E` | EOF | End of frame marker |
| `0x2D` | ESC | Escape character |

**Encoding (TX):** If a data byte equals SOF, EOF, or ESC, transmit:
```
[ESC] [byte XOR ESC]
```

**Decoding (RX):** When ESC is received, the next byte is XOR'd with ESC to recover the original value.

### Escape Examples

| Original byte | Transmitted as |
|---------------|----------------|
| `0x02` (SOF) | `0x2D 0x2F` (ESC, 0x02 XOR 0x2D) |
| `0x7E` (EOF) | `0x2D 0x53` (ESC, 0x7E XOR 0x2D) |
| `0x2D` (ESC) | `0x2D 0x00` (ESC, 0x2D XOR 0x2D) |
| `0x41` ('A') | `0x41` (no escaping needed) |

## Worst-Case Frame Size

If every data byte requires escaping (unlikely but possible):

```
SOF (1) + (6 header + 4096 payload + 2 CRC) x 2 + EOF (1) = 8,210 bytes
```

The TX DMA buffer is sized to 8,512 bytes to accommodate this with margin.

## Parser State Machine

```
                 ┌──────────────┐
         ───────►│ WAIT_SOF     │◄──── EOF received (valid or invalid)
        │        │              │◄──── Frame error / length overflow
        │        └──────┬───────┘
        │               │ SOF received
        │               ▼
        │        ┌──────────────┐
        │        │ IN_FRAME     │───── ESC received ──►┌───────────┐
        │        │              │◄──── byte restored ──│ ESCAPED   │
        │        │ Store byte   │                      │ XOR w/ESC │
        │        │ by position  │                      └───────────┘
        │        └──────┬───────┘
        │               │ EOF received
        │               ▼
        │        Validate CRC
        │        ┌─Yes──┴──No─┐
        │        ▼            ▼
        │    Callback     packets_err++
        │    packets_ok++
        └────────────────────────┘
```

### Parser byte routing by position:

| Byte index (after unstuffing) | Destination |
|-------------------------------|-------------|
| 0–5 | Header: msg1, msg2, len_hi, len_lo, cmd1, cmd2 |
| 6 to 6+len-1 | Payload (stored in assembly buffer) |
| Next 2 bytes | CRC (accumulated separately, not stored in buffer) |

After byte index 3 (len_lo), the parser decodes the expected payload length and validates it against `PKT_MAX_PAYLOAD`.

### Error recovery:

- **Unexpected SOF mid-frame:** Treated as new frame start; previous partial frame is silently dropped, `packets_err++`.
- **Extra bytes after CRC:** Frame error, parser resets to `WAIT_SOF`.
- **Payload length > 4096:** Parser immediately resets, `packets_err++`.

## Buffer Sizes

| Buffer | Size | Location | Purpose |
|--------|------|----------|---------|
| RX DMA ring | 4,096 bytes | D2 SRAM (.dma_buffer) | Circular DMA reception |
| TX DMA buffer | 8,512 bytes | D2 SRAM (.dma_buffer) | Framed packet for DMA TX |
| Parser assembly | 4,104 bytes | Regular RAM (in ProtocolParser struct) | Header + payload accumulation |

## Reception Architecture

UART reception is fully interrupt-driven with zero polling:

| Interrupt Source | Trigger | Purpose |
|------------------|---------|---------|
| DMA1 Stream1 HT | Buffer 50% full | Process first half of ring buffer |
| DMA1 Stream1 TC | Buffer 100% full (wrap) | Process second half of ring buffer |
| USART10 IDLE | Line idle after last byte | Catch end-of-packet with low latency |

All three ISRs call `USART_Driver_RxProcessISR()` which compares the DMA NDTR register to the last read position, extracts new bytes (handling ring wrap), and feeds them to `Protocol_FeedBytes()`.

## Daughtercard UART Reception

Each of the 4 daughtercard UARTs (USART1, USART2, USART3, UART4) uses the same DMA circular RX + HT/TC/IDLE interrupt architecture as USART10. Each has its own `ProtocolParser` instance initialized with an `OnDC_PacketReceived` callback.

| Interrupt Source | Trigger | Purpose |
|------------------|---------|---------|
| DMA1 Stream 2/3/4/5 HT | Buffer 50% full | Process first half of DC ring buffer |
| DMA1 Stream 2/3/4/5 TC | Buffer 100% full (wrap) | Process second half of DC ring buffer |
| USART1/2/3/UART4 IDLE | Line idle after last byte | Catch end-of-packet with low latency |

`OnDC_PacketReceived` operates in dual mode controlled by the `dc_list_active` flag:
- **Normal mode:** Deposits the response into `tx_request` for relay back to the GUI via USART10.
- **List mode:** Deposits the response into a `dc_response` mailbox, used by the synchronous SET/GET_LIST_OF_SW main-loop handler.

DC UART TX is polled (no DMA) since outbound command packets are small and infrequent.
