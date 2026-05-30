# Wire Protocol Specification

## Overview

The wire protocol defines the binary format used for TCP communication between
`PassThruLogger.dll` (client) and `PassThruLoggerControl.exe` (server) over
`localhost:2534`.

All multi-byte integers are **little-endian**. Variable-length integers use
**.NET-style 7-bit encoding** (LEB128 unsigned variant).

## Connection Handshake

Immediately after TCP connection, the client sends:

```
┌──────────────────┬──────────────────┐
│ wireProtocol (2) │ j2534Proto (2)   │
│ uint16 LE        │ uint16 LE        │
└──────────────────┴──────────────────┘
```

- `wireProtocol`: Currently `0x0000` (VER_0_0)
- `j2534Proto`: Currently `0x0404` (J2534 v04.04)

Followed by two `reportParam` messages:

1. **Client identification:**
   ```
   [msgtype=0x00] [param=0x01(client)] [string: client exe path]
   ```

2. **Driver identification:**
   ```
   [msgtype=0x00] [param=0x00(driver)] [string: driver registry key] [int32: load status]
   ```
   Load status: `0` = success, `>0` = ERR_NO_* bitmask, `-1` = DLL not found

## Message Types

| Byte value | Name | Description |
|------------|------|-------------|
| `0x00` | `reportParam` | Connection metadata (client name, driver name) |
| `0x01` | `J2534Msg` | A logged J2534 API call |

## J2534 Message Format

```
┌──────────┬──────────┬─────────────────────────┬──────────┬──────────────┐
│ msgtype  │ funcID   │ params...               │ TYPE_END │ returnCode   │
│ 0x01     │ byte     │ [type][value] repeated  │ 0x00     │ int32 LE     │
└──────────┴──────────┴─────────────────────────┴──────────┴──────────────┘
```

### Function IDs (`J2534_0404func`)

| Value | Function |
|-------|----------|
| 0 | PassThruOpen |
| 1 | PassThruClose |
| 2 | PassThruConnect |
| 3 | PassThruDisconnect |
| 4 | PassThruReadMsgs |
| 5 | PassThruWriteMsgs |
| 6 | PassThruStartPeriodicMsg |
| 7 | PassThruStopPeriodicMsg |
| 8 | PassThruStartMsgFilter |
| 9 | PassThruStopMsgFilter |
| 10 | PassThruSetProgrammingVoltage |
| 11 | PassThruReadVersion |
| 12 | PassThruGetLastError |
| 13 | PassThruIoctl |

## Data Type Tags (`wiredatatype`)

Each parameter is preceded by a 1-byte type tag:

| Value | Name | Payload |
|-------|------|---------|
| 0 | `TYPE_END` | No payload — signals end of parameter list |
| 1 | `TYPE_INT` | 4 bytes, int32 LE |
| 2 | `TYPE_STRING` | 7-bit-encoded length + UTF-8 bytes |
| 3 | `TYPE_ARRAY` | 7-bit-encoded count, followed by that many typed elements |
| 4 | `TYPE_POINTER` | 1 byte: `0x00`=NULL, `0x01`=not-null (followed by actual value) |
| 5 | `TYPE_MSG` | PASSTHRU_MSG struct (see below) |
| 6 | `TYPE_J2534_PROTOCOL_ID` | 7-bit-encoded enum value |
| 7 | `TYPE_J2534_CONNECT_FLAGS` | 7-bit-encoded flags |
| 8 | `TYPE_J2534_FILTER_TYPE` | 7-bit-encoded enum value |
| 9 | `TYPE_J2534_PROG_VOLTAGE_PIN_NUMBER` | 7-bit-encoded enum value |
| 10 | `TYPE_J2534_PROG_VOLTAGE` | 7-bit-encoded enum value |
| 11 | `TYPE_J2534_IOCTL` | 7-bit-encoded IOCTL ID |
| 12 | `TYPE_J2534_TXFLAGS` | 7-bit-encoded flags |
| 13 | `TYPE_J2534_RXSTATUS` | 7-bit-encoded flags |
| 14 | `TYPE_J2534_CONFIG_PARAMS` | 7-bit-encoded config parameter ID |
| 15 | `TYPE_DATAARRAY` | 7-bit-encoded length + raw bytes |
| 16 | `TYPE_INOUT_INT` | Two 7-bit-encoded ints: pre-call value, post-call value |

## PASSTHRU_MSG Encoding (TYPE_MSG)

```
┌────────────────┬─────────────┬─────────────┬─────────────┬────────────────┬─────────────────────┐
│ ProtocolID     │ RxStatus    │ TxFlags     │ Timestamp   │ ExtraDataIndex │ Data                │
│ 7bit-int       │ 7bit-int    │ 7bit-int    │ 7bit-int    │ 7bit-int       │ 7bit-len + bytes    │
└────────────────┴─────────────┴─────────────┴─────────────┴────────────────┴─────────────────────┘
```

## 7-Bit Encoded Integer

Uses the same encoding as .NET's `BinaryWriter.Write7BitEncodedInt`:

- Write 7 bits at a time, LSB first
- Set bit 7 (`0x80`) if more bytes follow
- Final byte has bit 7 clear

Example: value `300` → `0xAC 0x02`

## String Encoding

```
[7-bit-encoded-length][raw ASCII bytes (no null terminator)]
```

A NULL pointer string is encoded as length `0` with no following bytes.

## Buffering

The DLL uses a 512-byte send buffer (`NetworkWriter`). Data accumulates in the buffer
and is flushed:
- At the end of each complete API call (`writeParamEnd` calls `flush()`)
- When the buffer is full (auto-flush on `writeByte`)

## Protocol Versioning

The handshake includes both a wire protocol version and a J2534 protocol version,
allowing future extensions:

| Wire version | Meaning |
|--------------|---------|
| `0x0000` | Current version (this document) |

| J2534 version | Meaning |
|---------------|---------|
| `0x0404` | SAE J2534-1 v04.04 |

The server should reject unsupported versions by closing the connection.
