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
