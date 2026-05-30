# Installation & Setup

## Prerequisites

- Windows (tested on Windows 7+)
- A J2534-compliant PassThru device with its vendor driver installed
- Visual C++ Runtime (matching the build VS version)
- .NET Framework 4.0 (for PassThruLoggerControl GUI)

## Installation Methods

### NSIS Installer (Recommended)

Build the installer using NSIS with `install.nsi`. The installer:
1. Installs the VC++ redistributable
2. Installs the .NET Framework 4.0 runtime
3. Copies `PassThruLogger.dll` and `PassThruLoggerControl.exe` to the install directory
4. Registers `PassThruLogger` as a J2534 device in the Windows Registry
5. Creates a Start Menu shortcut

### Manual Setup

1. Place `PassThruLogger.dll` and `PassThruLoggerControl.exe` in the same directory.
2. Run `PassThruLoggerControl.exe` as Administrator (once) — it will self-register
   `PassThruLogger.dll` in the registry under `HKLM\SOFTWARE\PassThruSupport.04.04\PassThruLogger`.
3. Select your real J2534 device from the dropdown in the control window.

## Usage

1. Launch `PassThruLoggerControl.exe` (it starts the TCP server on port 2534).
2. Select the real J2534 driver to proxy to from the dropdown list.
3. In your diagnostic software, select **"PassThruLogger"** as the J2534 device.
4. Run your diagnostic procedure — all J2534 API calls will appear in the log viewer.
5. Use "Save Log" to export the session.

## Registry Keys

### Logger Settings (per-user)

```
HKCU\Software\Passthru Logger\
    DefaultDriverKey = "<registry key name of real driver>"
```

### Logger Registration (machine-wide, requires admin)

```
HKLM\SOFTWARE\PassThruSupport.04.04\PassThruLogger\
    FunctionLibrary   = "<path>\PassThruLogger.dll"
    Name              = "PassThruLogger"
    Vendor            = "GitHub"
    ConfigApplication = "<path>\PassThruLoggerControl.exe"
    CAN               = 1
    ISO15765          = 1
    ... (all protocols set to 1)
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| PassThruLogger not visible in diag software | Run PassThruLoggerControl.exe as admin to register |
| "ERR_DEVICE_NOT_CONNECTED" on all calls | Ensure PassThruLoggerControl is running before starting diag software |
| Connection shows "MissingDriver" | The selected default driver DLL file was not found |
| Connection shows "BadDriver" | The driver DLL loaded but some API functions are missing |
| No connection appears in control app | Check that port 2534 is not blocked by a firewall |
