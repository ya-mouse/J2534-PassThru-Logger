# Remote Debugging on Windows via SSH

## When to Use

- Building DLLs/tests on macOS/Linux (via Docker mingw cross-compile) and
  running them on a real Windows machine.
- Testing ReplayJ2534.dll or KvaserDirect.dll on Windows without switching
  to the Windows desktop.
- Running integration tests that need real Windows API calls (CONDITION_VARIABLE,
  CRITICAL_SECTION, registry, etc.) that Wine can't fully emulate.

## Environment

| Item | Value |
|------|-------|
| Host | `DESKTOP-JGDBINM` |
| SSH user | `fartud` (Windows local user, home is `C:\Users\Fartud`) |
| IP | `192.168.1.134` |
| Cygwin | `c:\cygwin64\cygwin.bat` |
| Software dir | `~/Downloads/PassThruLogger/` (cygwin: `/cygdrive/c/Users/Fartud/Downloads/PassThruLogger/`) |
| Network drive | `J:` — mapped in interactive desktop session, **NOT available via SSH** (see below) |

## Procedure

### SSH connection

```bash
ssh -o StrictHostKeyChecking=no "DESKTOP-JGDBINM\\fartud@192.168.1.134" '<command>'
```

The double backslash is required in shell quotes. The Windows username
includes the hostname prefix.

### Running commands via Cygwin

Windows `cmd.exe` doesn't have `ls`, `cp`, etc. Always invoke cygwin bash:

```bash
ssh "DESKTOP-JGDBINM\\fartud@192.168.1.134" \
  'c:\cygwin64\bin\bash -lc "<cygwin command>"'
```

Cygwin paths use `/cygdrive/c/` prefix for `C:\` drive. The home directory
is `/cygdrive/c/Users/Fartud` (note capital **F** — SSH user is lowercase
`fartud` but the Windows profile folder is `Fartud`).

### Copying files to Windows (scp)

**Use the `-O` flag** (old SCP protocol). The default new protocol tries
to `mkdir` remote paths and fails on cygwin. Also, the default scp remote
directory may not exist — copy to `~` (home) first, then move:

```bash
# Works — copies to ~/ (home dir)
scp -O -o StrictHostKeyChecking=no <local-file> "DESKTOP-JGDBINM\\fartud@192.168.1.134:<filename>"

# Then move to the target directory via SSH
ssh "DESKTOP-JGDBINM\\fartud@192.168.1.134" \
  'c:\cygwin64\bin\bash -lc "mv ~/<filename> ~/Downloads/PassThruLogger/"'
```

**Path case matters**: scp to `fartud@...` works, but the cygwin home is
`/cygdrive/c/Users/Fartud` (capital F). Using lowercase in scp full paths
fails with "No such file or directory".

### J: drive — why it doesn't work via SSH

`J:` is a network drive mapped to `\\admins-MacBook-Pro.local\J2534-PassThru-Logger`.
Network drive mappings in Windows are **per logon session** — the interactive
desktop session has the mapping, but SSH creates a new session without it.
`net use` shows it as "Unavailable" from SSH.

Re-mapping from SSH fails with error 53 (network path not found) because
the SMB share requires the desktop session's credentials. **Use scp instead.**

### Building on macOS, running on Windows

The workflow is: cross-compile in Docker → scp the exe/dll to Windows →
run via SSH + cygwin.

```bash
# 1. Build (macOS)
make replay          # → build/Release/ReplayJ2534.dll
make test-replay     # → build/tests/test_simulator.exe

# 2. Copy to Windows
scp -O build/Release/ReplayJ2534.dll "DESKTOP-JGDBINM\\fartud@192.168.1.134:ReplayJ2534.dll"
scp -O build/tests/test_simulator.exe "DESKTOP-JGDBINM\\fartud@192.168.1.134:test_simulator.exe"
scp -O ReplayJ2534/scenario.json "DESKTOP-JGDBINM\\fartud@192.168.1.134:scenario.json"

# 3. Move to software dir and run
ssh "DESKTOP-JGDBINM\\fartud@192.168.1.134" \
  'c:\cygwin64\bin\bash -lc "cp ~/{ReplayJ2534.dll,test_simulator.exe,scenario.json} ~/Downloads/PassThruLogger/ && cd ~/Downloads/PassThruLogger && ./test_simulator.exe"'
```

### Running the integration test client

```bash
ssh "DESKTOP-JGDBINM\\fartud@192.168.1.134" \
  'c:\cygwin64\bin\bash -lc "cd ~/Downloads/PassThruLogger && \
    REPLAY_J2534_CONFIG=scenario.json REPLAY_J2534_INSTANT=1 \
    ./j2534_test.exe ReplayJ2534.dll"'
```

### Registering the DLL as a J2534 PassThru device

Import the `.reg` file (requires admin for HKLM, but HKCU works without):

```bash
ssh "DESKTOP-JGDBINM\\fartud@192.168.1.134" \
  'cmd /c "reg import C:\Users\Fartud\Downloads\PassThruLogger\replay_install.reg"'
```

Verify registration:
```bash
ssh "DESKTOP-JGDBINM\\fartud@192.168.1.134" \
  'c:\cygwin64\bin\bash -lc "reg query \"HKLM\\SOFTWARE\\WOW6432Node\\PassThruSupport.04.04\\ReplayJ2534\""'
```

### Checking DLL exports

```bash
ssh "DESKTOP-JGDBINM\\fartud@192.168.1.134" \
  'c:\cygwin64\bin\bash -lc "grep -a -o \"[A-Za-z0-9_]*\\.dll\" ~/Downloads/PassThruLogger/test_simulator.exe | sort -u"'
```

## Pitfalls

- **scp new protocol fails on cygwin** — always use `scp -O`. The new
  protocol tries to create directories via SFTP and cygwin's SFTP server
  doesn't handle Windows paths correctly.
- **Username case** — SSH user is `fartud` (lowercase), but the Windows
  profile directory is `C:\Users\Fartud` (capital F). Cygwin home is
  `/cygdrive/c/Users/Fartud`. scp with lowercase path fails.
- **No gdb on the Windows machine** — if a test crashes, rebuild with
  `-g -O0` and inspect via Windows Error Reporting or add diagnostic
  printf. No gdb or strace equivalent available.
- **`-march=pentium3` segfault** — mingw builds with this flag crash on
  real Windows during static init. Never use it (see P-LIVE-001).
- **Wine on arm64** — the mingw-built exes crash under Wine on Apple
  Silicon ("Unhandled illegal instruction"). Use real Windows for testing.

## References

- `Dockerfile.mingw` — Docker image for mingw cross-compile
- `ReplayJ2534/tests/j2534_test.cpp` — integration test client
- `ReplayJ2534/install.reg` — registry registration for ReplayJ2534 DLL
- `.agents/pitfalls-live.md` — P-LIVE-001 (mingw -march segfault)
