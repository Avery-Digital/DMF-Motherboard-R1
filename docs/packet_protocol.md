# Packet Protocol Specification

## Overview

Communication between the host PC and the MCU uses a custom framed serial protocol over USART at 115200 baud (8N1). Each packet is delimited by start/end markers, uses byte-stuffing to avoid marker collisions in data, and includes a CRC-16 for integrity verification.

## Frame Format

```
┌─────┬──────┬──────┬────────┬────────┬──────┬──────┬──────────────┬────────┬────────┬─────┐
│ SOF │ MSG1 │ MSG2 │ LEN_HI │ LEN_LO │ CMD1 │ CMD2 │ PAYLOAD[0..N]│ CRC_HI │ CRC_LO │ EOF │
└─────┴──────┴──────┴────────┴────────┴──────┴──────┴──────────────┴────────┴────────┴─────┘
  0x02                                                                                 0x7E
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

Maximum payload length: 4096 bytes.

## Command Code

The 16-bit command code is formed by combining CMD1 (high byte) and CMD2 (low byte):

```
command = (CMD1 << 8) | CMD2
```

Example: CMD1 = `0xBE`, CMD2 = `0xEF` → command = `0xBEEF`

### Defined Commands

| Command | CMD1 | CMD2 | Description |
|---------|------|------|-------------|
| `CMD_PING` | `0xBE` | `0xEF` | Echo / link test. Returns a test payload. |

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
SOF (1) + (6 header + 4096 payload + 2 CRC) × 2 + EOF (1) = 8,210 bytes
```

The TX DMA buffer is sized to 8,512 bytes to accommodate this with margin.

## Example Packet

Sending a ping command with 4 bytes of payload `[0x01, 0x02, 0x03, 0x04]`:

**Before byte-stuffing (logical frame):**
```
MSG1=0x10  MSG2=0x01  LEN=0x0004  CMD1=0xBE  CMD2=0xEF
PAYLOAD=[0x01, 0x02, 0x03, 0x04]
CRC=CRC16([0x10, 0x01, 0x00, 0x04, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04])
```

**On the wire (assuming no data bytes need escaping):**
```
02 10 01 00 04 BE EF 01 02 03 04 [CRC_HI] [CRC_LO] 7E
```

Note: if CRC_HI or CRC_LO happen to equal `0x02`, `0x7E`, or `0x2D`, those bytes will be escaped.

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

## Buffer Sizes

| Buffer | Size | Location | Purpose |
|--------|------|----------|---------|
| RX DMA ring | 4,096 bytes | D2 SRAM (.dma_buffer) | Circular DMA reception |
| TX DMA buffer | 8,512 bytes | D2 SRAM (.dma_buffer) | Framed packet for DMA TX |
| Parser assembly | 4,104 bytes | Regular RAM (in ProtocolParser struct) | Header + payload accumulation |
