# Development Guide

## Solution Structure

The Visual Studio 2017 solution (`J2534 PassThru Logger.sln`) contains three projects:

| Project | Type | Language | Output |
|---------|------|----------|--------|
| PassThruLogger | Win32 DLL (x86) | C++ | PassThruLogger.dll |
| PassThruLoggerControl | WinForms App | C# (.NET 4.0) | PassThruLoggerControl.exe |
| SampleClient | Console App | C# (.NET 4.0) | AsynchronousClient.exe |

## Build

### Requirements

- Visual Studio 2017+ (or MSBuild 15+)
- Windows SDK
- .NET Framework 4.0 targeting pack

### Commands

```bash
# Full solution build (Release)
msbuild "J2534 PassThru Logger.sln" /p:Configuration=Release

# DLL only
msbuild PassThruLogger\PassThruLogger.vcxproj /p:Configuration=Release /p:Platform=Win32

# Control app only
msbuild PassThruLoggerControl\PassThruLoggerControl.csproj /p:Configuration=Release
```

The DLL is built as **x86 (Win32)** — this is required because J2534 drivers are 32-bit.

## Testing

There is no automated test suite. Testing is done manually:

1. Build the solution.
2. Launch `PassThruLoggerControl.exe`.
3. Run `SampleClient.exe` (AsynchronousClient) to verify the TCP connection and
   protocol deserialization work correctly.
4. For full integration testing, use a real J2534 device with a diagnostic application.

## Key Design Decisions

### Why a proxy DLL?

The J2534 standard defines a DLL-based interface. By registering PassThruLogger as a
J2534 device, any standard-compliant diagnostic application will load it without
modification. The proxy transparently forwards all calls to the real driver.

### Why TCP for logging?

- Decouples the logger DLL (loaded in an external process) from the viewer.
- Allows the viewer to run independently, potentially on a different machine.
- TCP provides reliable, ordered delivery of log data.
- The DLL cannot use stdout/files easily since it runs inside another process.

### Why 7-bit encoded integers?

Compact representation for small values (common in J2534 — channel IDs, protocol IDs).
Uses the same format as .NET's BinaryReader/Writer, simplifying the C# deserializer.

### Buffer strategy

The 512-byte buffer in `NetworkWriter` reduces syscall overhead. Each complete API call
is flushed atomically, ensuring the viewer always receives complete messages.

## Adding Support for New J2534 Protocol Versions

1. Add a new `j2534protover` enum value in `WireProtocolConstants.h`.
2. Create a new header file with the additional API type definitions.
3. Extend `dllmain.cpp` to handle the version switch (currently only 0x0404).
4. Create a new `J2534ProtocolInterpreter_XXXX.cs` class on the C# side.
5. Add the version case in `ConnectionInfo.recvThreadFunc()`.

## Code Conventions

- **C++ DLL**: Win32 API style, `__stdcall` calling convention, `#pragma EXPORT`
- **C# Apps**: Standard .NET naming (PascalCase types, camelCase locals)
- **Error codes**: Always return J2534 `ERR_*` constants from exported functions
- **Null safety**: Always check pointers before dereferencing; serialize with TYPE_POINTER

## Project Dependencies

### PassThruLogger (C++)
- `Ws2_32.lib` — Winsock2
- `Mswsock.lib` — Microsoft Winsock extensions
- `AdvApi32.lib` — Registry API

### PassThruLoggerControl (C#)
- `System.Windows.Forms` — GUI
- `System.Net.Sockets` — TCP server
- `Microsoft.Win32` — Registry access

## Installer

The NSIS script (`install.nsi`) requires:
- NSIS 3.x with MUI2 plugin
- VC++ Redistributable installer in `redist/`
- .NET Framework installer in `redist/`
- A `redist/runtimeinfo.nsh` file (copy from `.sample` and configure for your VS version)
