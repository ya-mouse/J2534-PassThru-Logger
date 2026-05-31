# KvaserDirect — Custom J2534 DLL using Kvaser CANlib

## Motivation

The stock Kvaser `j2534api.dll` reports excessive "frame error received" messages
when using ISO-TP (ISO 15765) channels. This appears to be a bug/limitation in
their ISO-15765 implementation built on top of CANlib. By implementing our own
J2534 DLL that uses CANlib directly, we gain:

1. **Full control over ISO-TP framing** — custom state machine, no spurious errors
2. **Direct CAN access** — no abstraction overhead, better error diagnostics
3. **VBATT access** — via `kvIoPinGetAnalog()` on supported hardware (or mock)
4. **Drop-in replacement** — exports same J2534 API, swap the registry path

## Architecture

```
Diagnostic App (Xentry, etc.)
       │
       │  J2534 v04.04 API calls
       ▼
┌──────────────────────────────┐
│   KvaserDirect.dll           │  ◀── OUR NEW DLL
│                              │
│  ┌────────────────────────┐  │
│  │ J2534 API Layer        │  │  PassThru* exports
│  │ (handle management)    │  │
│  └────────┬───────────────┘  │
│           │                  │
│  ┌────────▼───────────────┐  │
│  │ Protocol Engines       │  │
│  │ • CAN (raw passthru)   │  │
│  │ • CAN FD              │  │
│  │ • ISO 15765 (ISO-TP)  │  │
│  │ • ISO 9141 / 14230    │  │  (future, if needed)
│  └────────┬───────────────┘  │
│           │                  │
│  ┌────────▼───────────────┐  │
│  │ CANlib Abstraction     │  │  Dynamic load canlib32.dll
│  │ canOpenChannel()       │  │
│  │ canRead() / canWrite() │  │
│  │ canSetBusParams()      │  │
│  │ kvIoPinGetAnalog()     │  │
│  └────────────────────────┘  │
└──────────────────────────────┘
       │
       │  canlib32.dll API
       ▼
   Kvaser Hardware (USB CAN adapter)
```

## J2534 → CANlib API Mapping

### Handle Model

| J2534 Concept | CANlib Equivalent |
|---------------|-------------------|
| DeviceID (PassThruOpen) | Logical device — maps to a set of canlib channels |
| ChannelID (PassThruConnect) | `CanHandle` from `canOpenChannel(ch, flags)` |
| FilterID | Internal filter state (software filtering for ISO-TP) |
| PeriodicMsgID | Internal timer + canWrite |

### Function Mapping

| J2534 Function | CANlib Implementation |
|----------------|----------------------|
| `PassThruOpen(name)` | `canGetNumberOfChannels()`, validate device exists |
| `PassThruClose(devId)` | `canClose()` all handles for device |
| `PassThruConnect(dev, proto, flags, baud, &ch)` | `canOpenChannel(ch, flags)` + `canSetBusParams(baud)` + `canBusOn()` |
| `PassThruDisconnect(ch)` | `canBusOff()` + `canClose()` |
| `PassThruReadMsgs(ch, msgs, &num, timeout)` | `canReadWait()` loop + ISO-TP reassembly if ISO15765 |
| `PassThruWriteMsgs(ch, msgs, &num, timeout)` | ISO-TP segmentation + `canWrite()` + `canWriteSync()` |
| `PassThruStartPeriodicMsg(ch, msg, &id, interval)` | Internal timer thread + periodic `canWrite()` |
| `PassThruStopPeriodicMsg(ch, id)` | Cancel timer |
| `PassThruStartMsgFilter(ch, type, mask, pattern, fc, &id)` | Store filter in software state |
| `PassThruStopMsgFilter(ch, id)` | Remove filter |
| `PassThruIoctl(ch, ioctl, in, out)` | Dispatch per ioctl (see below) |
| `PassThruReadVersion(dev, fw, dll, api)` | `canGetChannelData(CARD_FIRMWARE_REV)` + hardcoded dll/api versions |
| `PassThruGetLastError(&buf)` | Thread-local error string |
| `PassThruSetProgrammingVoltage` | Return `ERR_NOT_SUPPORTED` (Kvaser has no J1962) |

### IOCTL Dispatch

| IoctlID | Implementation |
|---------|----------------|
| `GET_CONFIG` / `SET_CONFIG` | Map J2534 params to CANlib equivalents |
| `READ_VBATT` | `kvIoPinGetAnalog()` if available, else registry mock (mV) |
| `CLEAR_TX_BUFFER` | `canIoCtl(canIOCTL_FLUSH_TX_BUFFER)` |
| `CLEAR_RX_BUFFER` | `canIoCtl(canIOCTL_FLUSH_RX_BUFFER)` |
| `CLEAR_MSG_FILTERS` | Clear software filter table |
| `CLEAR_PERIODIC_MSGS` | Cancel all periodic timers |

## ISO-TP Implementation (ISO 15765-2)

The most complex part. The stock Kvaser DLL has issues here; we implement cleanly.

### Frame Types

| PCI byte (high nibble) | Type | Purpose |
|------------------------|------|---------|
| 0x0 | Single Frame (SF) | Complete message ≤ 7 bytes (CAN) / ≤ 62 bytes (FD) |
| 0x1 | First Frame (FF) | First segment of multi-frame message |
| 0x2 | Consecutive Frame (CF) | Subsequent segments (SN wraps 0-F) |
| 0x3 | Flow Control (FC) | Receiver → Sender: BS, STmin, FS |

### State Machine (per ISO-TP session)

```
IDLE ──[TX request]──▶ TX_SEND_FF ──[FF sent]──▶ TX_WAIT_FC
                                                      │
                                          [FC received, FS=CTS]
                                                      │
                                                      ▼
                                               TX_SEND_CF ──[all CF sent or BS reached]──▶ TX_WAIT_FC
                                                      │
                                               [last CF sent]
                                                      │
                                                      ▼
                                                   IDLE (TX complete)

IDLE ──[FF received]──▶ RX_RECEIVING ──[send FC]──▶ RX_WAIT_CF
                                                        │
                                              [CF received, not last]
                                                        │
                                                        ▼
                                                  RX_WAIT_CF (loop until complete)
                                                        │
                                                  [last CF received]
                                                        │
                                                        ▼
                                                     IDLE (deliver to app)
```

### Timeouts (ISO 15765-2)

| Timer | Default | Purpose |
|-------|---------|---------|
| N_As | 1000 ms | Time for sender to transmit a frame on CAN |
| N_Ar | 1000 ms | Time for receiver to transmit FC |
| N_Bs | 1000 ms | Time for sender to receive FC after FF/CF |
| N_Cr | 1000 ms | Time for receiver to get next CF |

### Flow Control Parameters (configurable via SET_CONFIG)

| J2534 Config | ISO-TP meaning |
|--------------|----------------|
| `ISO15765_BS` | Block Size for received FC |
| `ISO15765_STMIN` | Separation Time minimum (0-127 ms or 100-900 µs) |
| `BS_TX` | Block Size we send in our FC |
| `STMIN_TX` | STmin we send in our FC |
| `ISO15765_PAD_VALUE` | Padding byte for unused bytes (default 0xCC) |

### Message Filtering (ISO 15765)

J2534 requires "flow control filters" for ISO-TP:
- **Mask + Pattern**: match incoming CAN IDs for reception
- **FlowControl ID**: the CAN ID we use to send FC frames

We maintain a software filter table per channel and match against raw CAN frames.

## VBATT Implementation

### kvIo Analog Pin API

Kvaser devices with I/O add-on modules expose analog/digital/relay pins via the
`kvIo` API. The workflow to read voltage:

```cpp
// 1. Open a CAN channel (required — kvIo is per-handle)
CanHandle hnd = canOpenChannel(ch, flags);
canBusOn(hnd);

// 2. Check if device has I/O pins
unsigned int pinCount = 0;
canStatus st = kvIoGetNumberOfPins(hnd, &pinCount);
if (st != canOK || pinCount == 0) {
    // No I/O module — fall back to mock
}

// 3. Confirm I/O configuration (REQUIRED before any Get/Set)
st = kvIoConfirmConfig(hnd);
if (st != canOK) { /* handle error */ }

// 4. Enumerate pins to find analog input
for (unsigned int i = 0; i < pinCount; i++) {
    unsigned int pinType = 0;
    kvIoPinGetInfo(hnd, i, kvIO_INFO_GET_PIN_TYPE, &pinType, sizeof(pinType));
    if (pinType == kvIO_PIN_TYPE_ANALOG) {
        unsigned int direction = 0;
        kvIoPinGetInfo(hnd, i, kvIO_INFO_GET_DIRECTION, &direction, sizeof(direction));
        if (direction == kvIO_PIN_DIRECTION_IN) {
            // Found analog input — read voltage
            float voltage = 0.0f;
            st = kvIoPinGetAnalog(hnd, i, &voltage);
            if (st == canOK) {
                // voltage is in Volts, convert to millivolts for J2534
                *pOutput = (unsigned long)(voltage * 1000.0f);
                return STATUS_NOERROR;
            }
        }
    }
}
```

**Supported devices**: Only Kvaser products with I/O modules (DIN Rail SE400S,
devices with analog add-on boards). Standard USB interfaces (Leaf, U100, etc.)
return `canERR_xxx` from `kvIoGetNumberOfPins()` — no I/O available.

**CAN_V+ pin**: On an OBD-II J1962 connector, pin 6 (CAN_H) and pin 16 (battery+)
carry vehicle voltage. If a Kvaser I/O module has an analog input wired to pin 16
(or through a voltage divider), `kvIoPinGetAnalog()` returns the measured voltage.

### VBATT Strategy (priority order)

1. **kvIo analog pin**: If device has I/O pins and a configured analog input,
   read real voltage. Registry key `VbattIoPin` specifies which pin index.
2. **Mock value**: Configurable from registry (`MockVbattMv`, default 12000 = 12.0V).
   Used when hardware doesn't support analog I/O.
3. **OBD-II PID 0x42** (future): Query Control Module Voltage via ISO-TP if a
   channel is already open. Not for initial implementation.

### Registry Configuration for VBATT

```
HKCU\Software\KvaserDirect\
├── VbattSource  (DWORD)  0=mock, 1=kvIo, 2=auto (try kvIo, fall back to mock)
├── VbattIoPin   (DWORD)  Pin index for kvIoPinGetAnalog (default 0)
├── MockVbattMv  (DWORD)  Mock voltage in millivolts (default 12000)
```

Default `VbattSource=2` (auto): attempt kvIo first, if unavailable use mock.

## Registry Configuration

### J2534 PassThru Registration (HKLM, requires admin)

J2534 applications discover available devices by scanning:
```
HKLM\SOFTWARE\PassThruSupport.04.04\<DeviceName>\
```

Required values for our DLL:
```
HKLM\SOFTWARE\PassThruSupport.04.04\KvaserDirect\
├── Vendor            (REG_SZ)    "KvaserDirect"
├── Name              (REG_SZ)    "KvaserDirect (CANlib)"
├── FunctionLibrary   (REG_SZ)    "C:\...\KvaserDirect.dll"
├── ConfigApplication (REG_SZ)    "C:\...\PassThruLoggerControl.exe"
├── CAN               (DWORD)     1  (supported)
├── ISO15765          (DWORD)     1  (supported)
├── CAN_PS            (DWORD)     1
├── ISO15765_PS       (DWORD)     1
├── ISO9141           (DWORD)     0  (not supported — no K-line)
├── ISO14230          (DWORD)     0
└── ...other protocols...         0
```

Use `install.reg` (edit paths first) or the Control app's registration helper.

### DLL Configuration (HKCU, user-level)

```
HKCU\Software\KvaserDirect\
├── CanlibChannel         (DWORD, default 0)   — CANlib channel index
├── ShareCanlibChannels   (DWORD, default 0)   — 0=exclusive, 1=shared
├── AcceptVirtualChannels (DWORD, default 0)   — allow virtual channels
├── VbattSource           (DWORD, default 2)   — 0=mock, 1=kvIo, 2=auto
├── VbattIoPin            (DWORD, default 0)   — kvIo analog pin index
├── MockVbattMv           (DWORD, default 12000) — mock VBATT millivolts
├── LogEnabled            (DWORD, default 0)
├── LogFilePath           (REG_SZ, empty)      — file log path
├── LogFormat             (DWORD, 0=text, 1=json)
└── DefaultBaudRate       (DWORD, default 500000)
```

## Build Strategy

### Dynamic Loading of canlib32.dll

We do NOT statically link `canlib32.lib`. Instead, we load at runtime using
`LoadLibrary("canlib32.dll")` + `GetProcAddress()` for each function. This means:
- Our DLL compiles without the Kvaser SDK installed
- canlib32.dll must be present at runtime (installed with Kvaser drivers)
- Graceful error if canlib32.dll not found

### Required CANlib Functions to Load

```cpp
// Initialization
canInitializeLibrary
canGetNumberOfChannels
canGetChannelData

// Channel lifecycle
canOpenChannel
canClose
canBusOn
canBusOff
canSetBusParams
canSetBusParamsFd

// I/O
canRead
canReadWait
canWrite
canWriteSync
canIoCtl
canReadStatus
canRequestBusStatistics
canGetBusStatistics

// I/O pins (optional)
kvIoGetNumberOfPins
kvIoConfirmConfig
kvIoPinGetInfo
kvIoPinGetAnalog

// Error handling
canGetErrorText
```

### Cross-compilation (Docker + mingw-w64)

Same approach as PassThruLogger: build in Docker with `i686-w64-mingw32-g++`.
No Kvaser SDK needed at compile time (all dynamic loading).

```makefile
# In Makefile.mingw (or new Makefile.kvaserdirect)
SOURCES = dllmain.cpp J2534Api.cpp CanlibLoader.cpp \
          IsoTpEngine.cpp PeriodicMsgTimer.cpp HandleManager.cpp
```

## File Structure (planned)

```
KvaserDirect/
├── dllmain.cpp              # DLL entry, canlib load, registry config
├── J2534Api.cpp             # All 14 PassThru* exports
├── J2534Api.h               # J2534 types, constants, error codes
├── CanlibLoader.h           # Function pointer typedefs + loader
├── CanlibLoader.cpp         # LoadLibrary + GetProcAddress for all funcs
├── HandleManager.h          # Device/Channel/Filter handle allocation
├── HandleManager.cpp
├── IsoTpEngine.h            # ISO 15765-2 state machine
├── IsoTpEngine.cpp          # Segmentation, reassembly, flow control
├── PeriodicMsgTimer.h       # Periodic message scheduler
├── PeriodicMsgTimer.cpp
├── CanChannel.h             # Per-channel state (protocol, params, filters)
├── CanChannel.cpp
├── VbattProvider.h          # VBATT reading strategy
├── VbattProvider.cpp
├── exports.def              # DLL export definitions
└── Makefile.mingw           # Cross-compilation rules
```

## Differences from Stock Kvaser j2534api.dll

| Aspect | Stock Kvaser | KvaserDirect (ours) |
|--------|-------------|---------------------|
| ISO-TP | Built-in, reports frame errors | Custom state machine, clean handling |
| VBATT | Not supported (ERR_NOT_SUPPORTED) | Mock or kvIo analog pin |
| Logging | Optional via registry | Optional file + TCP to our Control app |
| Error reporting | Standard J2534 | Enhanced: specific error strings via GetLastError |
| Channel mapping | Complex auto-discovery | Simple: one configured canlib channel |
| ISO 9141/14230 | Supported | Not initially (CAN-only focus) |
| CAN FD ISO-TP | Preliminary support | Full support planned |

## Implementation Phases

### Phase 1: Core CAN + ISO-TP (MVP)
- PassThruOpen/Close/Connect/Disconnect for CAN and ISO15765
- Raw CAN read/write pass-through
- ISO-TP state machine (SF, FF, CF, FC)
- Basic message filtering
- VBATT mock from registry

### Phase 2: Full J2534 Compliance
- Periodic messages
- All IOCTL commands
- CAN FD support
- GET_CONFIG/SET_CONFIG full parameter set

### Phase 3: Advanced Features
- kvIo analog VBATT (if hardware present)
- TCP streaming to PassThruLoggerControl
- OBD-II VBATT via PID 0x42
- Error frame filtering / reporting control

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| ISO-TP timing precision | Use high-resolution timers (QueryPerformanceCounter) |
| Thread safety | Mutex per channel, separate RX thread |
| canlib32.dll version compat | Dynamic loading, version check via canGetVersion |
| Missing canlib32.dll | Graceful ERR_DEVICE_NOT_CONNECTED on load failure |
| Multiple device support | Handle manager with device enumeration |

## References

- SAE J2534-1 (DEC2004) — PassThru API specification
- SAE J2534-2 (JAN2019) — Extended API (CAN FD, discovery)
- ISO 15765-2 — Network layer for CAN transport protocol
- Kvaser CANlib SDK 5.51 — `INC/canlib.h`, `INC/canstat.h`
- Kvaser j2534api-readme.txt — stock DLL limitations and behavior
