# Architecture

## Overview

J2534 PassThru Logger is a **man-in-the-middle logging tool** for the SAE J2534 PassThru API.
It intercepts all J2534 API calls between a diagnostic application and a real J2534 hardware
device, serializing each call and its parameters over a TCP socket to a control/viewer application.

## System Diagram

```
┌─────────────────────┐         ┌─────────────────────────┐         ┌──────────────────┐
│  Diagnostic App     │ ──DLL── │  PassThruLogger.dll     │ ──DLL── │  Real J2534 DLL  │
│  (e.g. Ford IDS)    │  calls  │  (C++ proxy)            │  calls  │  (vendor driver)  │
└─────────────────────┘         └───────────┬─────────────┘         └──────────────────┘
                                            │
                                     TCP :2534
                                            │
                                ┌───────────▼─────────────┐
                                │  PassThruLoggerControl   │
                                │  (C# WinForms GUI)      │
                                │  - TCP server            │
                                │  - log viewer            │
                                │  - driver selector       │
                                └─────────────────────────┘
```

## Components

### PassThruLogger (C++ Win32 DLL)

**Role:** Proxy DLL that exports the full J2534 v04.04 API surface.

**Behavior:**
1. On `DLL_PROCESS_ATTACH`, initializes Winsock, connects to `localhost:2534` via TCP.
2. Reads the Windows Registry (`HKCU\Software\Passthru Logger\DefaultDriverKey`) to find
   which real J2534 driver DLL to load.
3. Dynamically loads the real driver using `LoadLibrary` + `GetProcAddress` for all 14
   J2534 API functions.
4. Each exported API function:
   - Forwards the call to the real driver
   - Serializes the function ID, all parameters (pre- and post-call values), and the
     return code over the TCP socket using a custom binary wire protocol.

**Key files:**
| File | Purpose |
|------|---------|
| `dllmain.cpp` | DLL entry point; Winsock init, TCP connect, registry lookup, driver load |
| `PassThruLogger.cpp` | All 14 J2534 API wrapper functions with serialization |
| `Loader4.cpp/.h` | Dynamic J2534 DLL loader (based on NCS/Drew Technologies code) |
| `NetworkWriter.cpp/.h` | Buffered TCP socket writer with 7-bit encoded int support |
| `RegUtils.cpp/.h` | Windows Registry read helpers |
| `J2534_v0404.h` | J2534 v04.04 type definitions, constants, function pointer typedefs |
| `WireProtocolConstants.h` | Enums defining the binary wire protocol between DLL and GUI |

### PassThruLoggerControl (C# WinForms)

**Role:** TCP server, protocol deserializer, and log viewer GUI.

**Behavior:**
1. On startup, scans the registry for installed J2534 drivers and auto-registers
   `PassThruLogger.dll` if needed (requires admin elevation once).
2. Opens a TCP listener on port 2534.
3. For each incoming connection, spawns a `ConnectionInfo` thread that reads the wire
   protocol stream and deserializes J2534 API calls into human-readable log strings.
4. Displays live log output in a WinForms ListBox with per-connection tabs.
5. Allows saving logs to file.

**Key files:**
| File | Purpose |
|------|---------|
| `Program.cs` | Entry point |
| `J2534LogController.cs` | Main form: TCP server, driver list, connection grid, log viewer |
| `ConnectionInfo.cs` | Per-connection state: socket thread, stream reader, log writer |
| `J2534ProtocolInterpreter.cs` | Abstract base for protocol version interpreters |
| `J2534ProtocolInterpreter_0404.cs` | Deserializer for J2534 v04.04 wire protocol |
| `J2534SerializationConstants.cs` | Wire protocol enums + all J2534 constant enums (C#) |
| `RegistryHelper.cs` | Scans for J2534 drivers, self-registers PassThruLogger |
| `J2534Driver.cs` | Data model for a registered J2534 driver |
| `FullBinaryReader.cs` | BinaryReader extension exposing `Read7BitEncodedInt` |

### SampleClient (C# Console)

**Role:** Test client that simulates the proxy DLL sending data to PassThruLoggerControl
without needing an actual J2534 device. Useful for development/debugging of the control UI.

**Key file:** `Program.cs` — connects to localhost:2534, sends handshake + synthetic J2534 messages.

## Data Flow (Runtime)

1. Diagnostic app loads `PassThruLogger.dll` (registered as a J2534 device in the registry).
2. DLL connects to PassThruLoggerControl via TCP:2534.
3. DLL sends handshake: wire protocol version, J2534 protocol version, client name, driver name.
4. DLL loads the actual vendor J2534 DLL (identified from registry settings).
5. For each J2534 API call from the diagnostic app:
   - DLL forwards the call to the real driver, capturing the return code.
   - DLL serializes: `[msgtype=J2534Msg][funcID][params...][TYPE_END][returnCode]`
   - Serialized data is sent over TCP in buffered 512-byte chunks.
6. PassThruLoggerControl deserializes and displays the call in human-readable format:
   `PassThruConnect(1, CAN, CAN_29BIT_ID, 500000, 2) -> STATUS_NOERROR`

## Registry Layout

```
HKLM\SOFTWARE\PassThruSupport.04.04\
├── <VendorDriver1>\
│   ├── FunctionLibrary = "C:\...\vendor.dll"
│   ├── Name = "Vendor Device"
│   └── Vendor = "Vendor Inc"
├── PassThruLogger\                    ← registered by installer/control app
│   ├── FunctionLibrary = "...\PassThruLogger.dll"
│   ├── Name = "J2534 PassThru Logger"
│   ├── Vendor = "GitHub"
│   ├── ConfigApplication = "...\PassThruLoggerControl.exe"
│   └── <protocol flags> = 1          ← claims all protocols

HKCU\Software\Passthru Logger\
└── DefaultDriverKey = "<VendorDriver1>"   ← which real driver to proxy to
```

## J2534 API Coverage

All 14 functions from J2534-1 v04.04 are intercepted:

| Function | Logged Parameters |
|----------|-------------------|
| PassThruOpen | pName, pDeviceID |
| PassThruClose | DeviceID |
| PassThruConnect | DeviceID, ProtocolID, Flags, BaudRate, pChannelID |
| PassThruDisconnect | ChannelID |
| PassThruReadMsgs | ChannelID, pMsg[], pNumMsgs (in/out), Timeout |
| PassThruWriteMsgs | ChannelID, pMsg[], pNumMsgs (in/out), Timeout |
| PassThruStartPeriodicMsg | ChannelID, pMsg, pMsgID, TimeInterval |
| PassThruStopPeriodicMsg | ChannelID, MsgID |
| PassThruStartMsgFilter | ChannelID, FilterType, Mask, Pattern, FlowCtrl, pFilterID |
| PassThruStopMsgFilter | ChannelID, FilterID |
| PassThruSetProgrammingVoltage | DeviceID, PinNumber, Voltage |
| PassThruReadVersion | DeviceID, FirmwareVer, DllVer, ApiVer |
| PassThruGetLastError | pErrorDescription |
| PassThruIoctl | ChannelID, IoctlID, pInput, pOutput (with SCONFIG_LIST parsing) |
