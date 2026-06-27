---
name: j2534-logger
description: "Project domain knowledge and orchestrator. Use when writing, reviewing, analyzing, or generating code for this project. Manages implementation workers and reviewers."
---

# j2534-logger — Domain Knowledge & Orchestrator

<!-- ═══════════════════════════════════════════════════════════════════════
     This skill file is the COMPLETE reference that workers and reviewers
     receive when they need domain knowledge about the project. It should
     be self-contained — an agent reading only this file should understand
     enough to write correct code.

     It also embeds the ORCHESTRATION PROTOCOL — execution loop, mandatory
     review, commit format, escalation. This ensures that when the skill
     is activated, the agent has BOTH domain knowledge AND orchestration
     behavior (following the Vela stage2 pattern). Detailed spawning
     templates and escalation rules remain in .agents/orchestrator.md.

     Sections marked <!-- FILL: ... --> need to be populated by /skill-discover
     or manually. Remove the FILL comments once populated.
     ═══════════════════════════════════════════════════════════════════════ -->

## Inherited Base Instructions

This skill inherits the mandatory protocols defined in
[`.agents/skill-base.md`](../../skill-base.md):

- **Pre-Task Knowledge Search** — read module AGENTS.md, knowledge/, pitfalls-live.md before starting
- **Device/Environment Interaction Pre-Flight** — load `.agents/local.env`, consult `knowledge/workflows/`, use canonical tools
- **Mandatory Review** — dual sign-off before every code commit
- **Post-Task Knowledge Evolution** — capture new patterns, workflows, pitfalls

**Read `.agents/skill-base.md` in full before resuming work in this skill.**

## Orchestration Protocol

When activated via `/project-skill`, you are the **orchestrator** for j2534-logger.
You decompose work into tasks, spawn workers and reviewers, track progress,
handle failures, and commit reviewed code.

### Resuming Work

1. Check current state: `SELECT * FROM todos WHERE status != 'done' ORDER BY id`
2. Read the relevant TODO/task file for persistent tracker
3. Identify which tasks are ready (no pending dependencies)
4. Continue the execution loop

### ⚠️ MANDATORY: Review Before Every Commit

> **NEVER commit code without review.**
> When `dual_review: true` (see `.agents/config/agent-rules.yaml`), every code
> commit MUST have `Reviewed-by:` stamps from BOTH model families.
> Only documentation-only changes (.md) and task tracker updates are exempt.
> **If you are about to `git commit` without review stamps — STOP and review first.**

### Execution Loop

```
1. Read task list → identify ready tasks (no pending dependencies)
2. SELECT t.* FROM todos t WHERE t.status = 'pending'
   AND NOT EXISTS (
     SELECT 1 FROM todo_deps td JOIN todos dep ON td.depends_on = dep.id
     WHERE td.todo_id = t.id AND dep.status != 'done'
   );
3. Spawn up to 2 parallel workers (task tool, agent_type: general-purpose)
4. When worker completes → spawn DUAL reviewers in parallel
5. If BOTH APPROVED → commit with dual sign-off, mark task done
6. If one requests changes → address feedback, re-review with that model only
7. If one BLOCKS while other approves → escalate to human
8. If BOTH request changes → address combined feedback, re-review with both
9. After all tasks done → run verification gate (build + tests)
```

### Spawning Workers & Reviewers

Workers are **stateless** — include EVERYTHING they need:

```
task(agent_type="general-purpose", model="<per config>", prompt=f"""
  {read('.agents/implement.md')}
  {read('.agents/skills/j2534-logger/SKILL.md')}   # domain knowledge
  {relevant('.agents/pitfalls.md')}
  {read('.agents/pitfalls-live.md')}
  Task: {task_id}, Files: {files}, Spec: {spec}
""")
```

Spawn **both reviewers in parallel** (read `.agents/reviewer.md` for checklist):

| Role | Model | Strength |
|------|-------|----------|
| Primary | gpt-5.4 | Deep semantic/logic analysis |
| Secondary | claude-opus-4.6 | Pattern consistency, spec compliance |

### Commit Format

```
task-id — description

Reviewed-by: gpt-5.4 ✓
Reviewed-by: claude-opus-4.6 ✓
Review-date: YYYY-MM-DD
Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>
```

### Escalation

| Phase | Max Retries | Action on Exhaust |
|-------|-------------|-------------------|
| 1 — Same model | 5 | Switch to alternate model |
| 2 — Switched model | 3 | Generate failure report, fresh worker |
| 3 — Fresh worker | 8 total | Mark FAILED, save to `.agents/failures/` |

For detailed spawning templates and escalation rules, see `.agents/orchestrator.md`.

### Key Reference Files

| File | Purpose |
|------|---------|
| `.agents/orchestrator.md` | Detailed spawning templates, verdict merge rules, interface tracking |
| `.agents/implement.md` | Worker coding standards, output format, design analysis framework |
| `.agents/reviewer.md` | Review checklist, verdict format, severity guide |
| `.agents/knowledge/` | Shared procedural knowledge — patterns, workflows, how-tos |
| `.agents/pitfalls.md` | Fixed lessons learned — include relevant sections in worker prompts |
| `.agents/pitfalls-live.md` | **Adaptive** — always include ALL entries in worker prompts |
| `.agents/config/agent-rules.yaml` | Models, review config, escalation thresholds, verification gates |

### Knowledge Evolution Protocol

Agents maintain a shared knowledge base alongside code. Three knowledge tiers:

| Tier | Location | What goes there |
|------|----------|-----------------|
| Module-specific | Per-module `AGENTS.md` | Architecture, APIs, internal patterns |
| Cross-cutting | `.agents/knowledge/{patterns,workflows}/` | Shared how-tos, procedures |
| Pitfalls | `.agents/pitfalls-live.md` | Bug classes, gotchas, workarounds |
| Design docs | `docs/<domain>/` | Architecture specs, design decisions, exploration results |

**Pre-task (search existing knowledge):**
1. If working on a module: READ its `AGENTS.md` (if it exists)
2. SEARCH `.agents/knowledge/` for relevant patterns/workflows
3. READ `.agents/pitfalls.md` + `pitfalls-live.md` for known gotchas
4. SEARCH `docs/` for architecture context

**During task:** Track noteworthy discoveries, design decisions, and explored
domain knowledge that are not yet captured in `docs/` or `.agents/knowledge/`.

**Post-commit (update knowledge):**
1. ASSESS — did this task reveal new patterns, gotchas, or design decisions?
2. If yes, UPDATE the appropriate tier:
   - Module pattern/API change → module `AGENTS.md`
   - Cross-cutting procedure → `.agents/knowledge/<category>/<article>.md`
   - Bug class / workaround → `.agents/pitfalls-live.md` (entry format in that file)
3. ASSESS `docs/` — did the work change architecture described in existing docs,
   or did exploration produce knowledge worth a new doc?
   - Stale existing doc → update it
   - New domain knowledge → create `docs/<domain>/<topic>.md`
   - Suggest to user before committing: "These docs should be updated/created: ..."
4. COMMIT knowledge + docs together: `knowledge: update <topic>`

Knowledge and docs updates are **exempt from dual review** (same as docs-only changes).
Skip this step only if the task was purely mechanical with no new learnings.

### Rules

- **Max 2 parallel workers** (increase only with human approval)
- **Never skip review** — every code change gets reviewed before commit
- **Never commit tmp/** — temporary debugging artifacts only
- **LOC:** 500 target, 700 hard max per source file
- **Commits:** Include co-author trailer + review sign-off
- **Verification:** `msbuild "J2534 PassThru Logger.sln" /p:Configuration=Release`

<!-- If the project has managed dependencies (e.g., deps/ directory with manifest):
- **deps/ changes:** MANDATORY — follow `.agents/knowledge/workflows/deps-workflow.md`
  (unshallow → branch → commit → manifest update → offline build verify)
-->

---

## Project Identity

- **Name:** j2534-logger
- **Description:** Windows proxy DLL that sits between a real J2534 device and a client for monitoring events and logging PassThru API calls
- **Primary language:** C++ (.cpp)
- **Status:** Active development

- **Platform:** Windows x86 (32-bit DLL required by J2534 spec)
- **Paradigm:** Man-in-the-middle proxy with out-of-process TCP logging
- **Languages:** C++ (DLL) + C# (WinForms GUI + sample client)

### Core Principles

1. **Transparent proxying** — the DLL must not alter J2534 behavior; always forward to real driver and return its result
2. **Fail-safe** — if TCP connection or driver load fails, return `ERR_DEVICE_NOT_CONNECTED` gracefully
3. **Complete capture** — every parameter (pre-call and post-call) must be serialized, including NULL pointers
4. **Decoupled architecture** — DLL and viewer communicate only via TCP wire protocol
5. **No external dependencies** — only Windows SDK and .NET Framework 4.0

---

## Architecture & Pipeline

```
Diagnostic App ──DLL calls──▶ PassThruLogger.dll ──DLL calls──▶ Real J2534 Driver
                                     │
                              TCP :2534 (binary wire protocol)
                                     │
                                     ▼
                           PassThruLoggerControl.exe (WinForms GUI)
```

**DLL lifecycle:**
1. `DLL_PROCESS_ATTACH` → WSAStartup → TCP connect → registry lookup → LoadLibrary(real driver)
2. Each API call → forward to real driver → serialize params + result → TCP send
3. `DLL_PROCESS_DETACH` → UnloadJ2534Dll → close socket → WSACleanup

### Key Design Notes

- DLL is **x86 only** (J2534 spec mandates 32-bit)
- TCP port 2534 (hardcoded) on localhost
- Wire protocol uses 7-bit encoded integers (.NET BinaryWriter compatible)
- 512-byte send buffer, flushed after each complete API call
- Handshake includes wire protocol version + J2534 protocol version for extensibility
- All 14 J2534 v04.04 API functions are intercepted

### Module Map

| Module | Language | Purpose |
|--------|----------|---------|
| PassThruLogger/ | C++ | Proxy DLL — intercepts, forwards, and serializes J2534 API calls |
| PassThruLoggerControl/ | C# | TCP server + WinForms GUI — deserializes and displays logged calls |
| SampleClient/ | C# | Test client for development (sends synthetic messages) |
| KvaserDirect/ | C++ | Direct J2534 DLL using Kvaser CANlib (no proxy, talks to real CAN hardware) |
| ReplayJ2534/ | C++ | Scenario-driven replay DLL — simulates ECU replies from JSON config, no hardware needed |

---

## Syntax & API Reference

### J2534 API Wrapper Pattern (C++ DLL)

Every exported function follows this pattern:

```cpp
PANDAJ2534DLL_API long PTAPI PassThruXxx(params...) {
    #pragma EXPORT
    if (!loadedFine) return ERR_DEVICE_NOT_CONNECTED;
    auto res = LocalXxx(params...);           // Forward to real driver

    writeApiMsgHeader(J2534_0404func::API_PassThruXxx);

    // Serialize each parameter with type tag
    writeParamInt(intParam);
    if (writeParamPointer(ptrParam))          // Returns true if non-NULL
        writeParamXxx(*ptrParam);             // Serialize pointed-to value

    return writeParamEnd(res);                // Write TYPE_END + return code + flush
}
```

### Serialization Helpers (C++ DLL)

| Function | Type tag | Payload |
|----------|----------|---------|
| `writeParamInt(int)` | TYPE_INT | 4 bytes LE |
| `writeParamString(char*)` | TYPE_STRING | 7bit-len + chars |
| `writeParamDataArray(char*, len)` | TYPE_DATAARRAY | 7bit-len + raw bytes |
| `writeParamPointer(void*)` | TYPE_POINTER | 1 byte (NULL/NOTNULL) |
| `writeParamEnumValue(type, val)` | `type` | 7bit-encoded int |
| `writeParamMsg(PASSTHRU_MSG&)` | TYPE_MSG | ProtocolID+RxStatus+TxFlags+Timestamp+ExtraDataIdx+Data |
| `writeParamArrayStart(count)` | TYPE_ARRAY | 7bit-encoded count |
| `writeParamInOutInt(pre, post)` | TYPE_INOUT_INT | Two 7bit-encoded ints |
| `writeParamEnd(ret)` | TYPE_END | int32 return code + flush |

### Wire Protocol Enums (shared between C++ and C#)

See `WireProtocolConstants.h` (C++) and `J2534SerializationConstants.cs` (C#).

### Deserialization Pattern (C# Control App)

```csharp
// ConnectionInfo.recvThreadFunc() — main loop
msgtype mtype = (msgtype)streamreader.ReadByte();
switch (mtype) {
    case msgtype.reportParam: // handle client/driver identification
    case msgtype.J2534Msg:    // delegate to J2534ProtocolInterpreter_0404.interpret()
}

// J2534ProtocolInterpreter_0404.interpret() — per-message
J2534_0404func f = (J2534_0404func)reader.ReadByte();
// Loop: read type tag → parseDataField() until TYPE_END
// Read int32 return code
// Return human-readable string like "PassThruConnect(1, CAN, ...) -> STATUS_NOERROR"
```

### Registry Access Pattern

```cpp
// C++ (DLL) — read from HKCU
HKEY key;
RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Passthru Logger", 0, KEY_READ, &key);
GetStringRegKey(key, "DefaultDriverKey", driverKeyName, "");
```

```csharp
// C# (Control) — read/write with RegistryKey
using (var reg32 = RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, RegistryView.Registry32))
using (var entry = reg32.CreateSubKey(@"Software\Passthru Logger"))
    entry.SetValue("DefaultDriverKey", driver.key, RegistryValueKind.String);
```

---

## Build & Test

### Building

**C++ DLLs (mingw cross-compile via Docker):**
```bash
make                    # all: PassThruLogger + KvaserDirect + ReplayJ2534 + C# apps
make replay             # ReplayJ2534 DLL only → build/Release/ReplayJ2534.dll
make kvaser             # KvaserDirect DLL only
make test-replay        # ReplayJ2534 test exe (run on Windows or Wine)
make test-replay-native # ConfigStore native tests (macOS/Linux, no Docker)
```

**C# apps (MSBuild or dotnet):**
```bash
msbuild "J2534 PassThru Logger.sln" /p:Configuration=Release
```

**Prerequisites:**
- Docker (for mingw cross-compile; uses `j2534-builder` image)
- Visual Studio 2017+ or .NET SDK for C# projects
- Windows SDK, .NET Framework 4.0 targeting pack
- All C++ DLLs build as **x86/Win32** only (J2534 spec mandate)

### Testing

**ReplayJ2534 unit tests** (22 tests: ConfigStore + Simulator + Scheduler):
```bash
make test-replay        # build test exe
# Run on Windows:
./build/tests/test_simulator.exe
# Or via Wine:
wine build/tests/test_simulator.exe
```

**ConfigStore native tests** (11 tests, runs on macOS/Linux without Docker):
```bash
make test-replay-native
```

**ReplayJ2534 integration test** (on Windows with real DLL):
```bash
# Set scenario path and instant mode
set REPLAY_J2534_CONFIG=scenario.json
set REPLAY_J2534_INSTANT=1
# Run test client against DLL
j2534_test.exe ReplayJ2534.dll
```

**Manual protocol testing:**
1. Build the full solution
2. Launch `PassThruLoggerControl.exe`
3. Run `SampleClient.exe` to verify TCP + deserialization
4. For integration testing: use a real J2534 device + diagnostic app

### Linting & Formatting

No automated linting configured. Follow existing code style (tabs, Win32 conventions).

---

## Debugging

The DLL runs inside the diagnostic application's process. Debugging requires
attaching to that process or using the SampleClient for protocol-level testing.

### Quick Diagnosis Recipes

#### Verify TCP connectivity
Run `SampleClient.exe` while `PassThruLoggerControl.exe` is running.
A connection should appear in the grid with status "Connected".

#### Debug DLL loading
Check `dllmain.cpp` — if `loadedFine` stays FALSE, all API calls return
`ERR_DEVICE_NOT_CONNECTED`. Common causes:
- PassThruLoggerControl not running (TCP connect fails)
- Registry key `HKCU\Software\Passthru Logger\DefaultDriverKey` not set
- Real driver DLL path invalid or missing

### Common Error Patterns

| Symptom | Likely Cause |
|---------|-------------|
| All API calls return `ERR_DEVICE_NOT_CONNECTED` | DLL failed to initialize (`loadedFine=false`) |
| Connection shows "Unsupported" | Wire protocol or J2534 version mismatch in handshake |
| Connection shows "MissingDriver" | Real driver DLL file not found at registry path |
| Connection shows "BadDriver" | Real driver loaded but missing required API functions |
| InvalidEnumException in control app | Wire protocol desync — DLL sending unexpected byte |
| `SOCKET_ERROR` in NetworkWriter | PassThruLoggerControl closed/crashed mid-session |

### Verification Steps

1. Solution builds without errors (`msbuild /p:Configuration=Release`)
2. PassThruLoggerControl launches and shows driver list
3. SampleClient connects successfully and messages appear in log
4. Wire protocol enums in `WireProtocolConstants.h` match `J2534SerializationConstants.cs`

---

## Project Structure

```
./
├── PassThruLogger/                  # C++ Win32 proxy DLL
│   ├── dllmain.cpp                  # DLL entry: Winsock, TCP, registry, driver load
│   ├── PassThruLogger.cpp           # 14 J2534 API wrappers with serialization
│   ├── Loader4.cpp/.h               # Dynamic J2534 DLL loader (GPL, NCS/Drew Tech)
│   ├── NetworkWriter.cpp/.h         # Buffered TCP socket writer
│   ├── RegUtils.cpp/.h              # Windows Registry read helpers
│   ├── J2534_v0404.h                # J2534 types, constants, function typedefs
│   └── WireProtocolConstants.h      # Wire protocol enum definitions
├── KvaserDirect/                    # C++ direct J2534 DLL (Kvaser CANlib)
│   ├── J2534Api.cpp                 # 14 J2534 API wrappers → CANlib calls
│   ├── KvaserCan.cpp/.h             # CANlib integration layer
│   ├── IsoTpEngine.cpp/.h           # ISO 15765-2 (ISO-TP) transport
│   ├── HandleMgr.cpp/.h             # Device/channel handle management
│   ├── install.reg                  # Registry registration for PassThru
│   └── tools/kvio_enum.cpp          # Kvaser I/O pin enumeration tool
├── ReplayJ2534/                     # C++ scenario-driven replay DLL
│   ├── Simulator.cpp/.h             # State machine + API dispatcher
│   ├── ConfigStore.cpp/.h           # Scenario JSON loader (embedded rjson parser)
│   ├── Scheduler.cpp/.h             # Background thread: periodic + delayed replies
│   ├── J2534Api.cpp                 # 14 J2534 API wrappers → Simulator calls
│   ├── dllmain.cpp                  # DLL entry: config loading, g_simulator init
│   ├── Logger.cpp/.h                # ReplayLogger (diagnostics)
│   ├── J2534Defs.h                  # J2534 v04.04 types & constants
│   ├── exports.def                  # 14 J2534 API exports
│   ├── scenario.json                # Example scenario config
│   ├── LogParser.cpp/.h             # Old log parser (NOT in build, ref for converter)
│   ├── ReplayEngine.cpp/.h          # Old replay engine (NOT in build, kept on disk)
│   ├── Makefile.mingw               # DLL build rules
│   ├── tests/                       # Unit tests (mingw + native)
│   │   ├── test_simulator.cpp       # 22-test full suite (mingw)
│   │   ├── test_configstore.cpp     # 11-test ConfigStore suite (native)
│   │   ├── Makefile.test            # mingw cross-compile test build
│   │   ├── Makefile.native          # native test build (macOS/Linux)
│   │   └── stubs/windows.h          # Win32 stubs for native testing
│   └── tools/
│       └── log2scenario.py          # Convert .jsonl logs → scenario.json
├── PassThruLoggerControl/           # C# WinForms control/viewer app
│   ├── Program.cs                   # Entry point
│   ├── J2534LogController.cs        # Main form: TCP server + GUI
│   ├── ConnectionInfo.cs            # Per-connection state + recv thread
│   ├── J2534ProtocolInterpreter.cs  # Abstract deserializer base
│   ├── J2534ProtocolInterpreter_0404.cs  # v04.04 message deserializer
│   ├── J2534SerializationConstants.cs    # All wire + J2534 enums (C#)
│   ├── RegistryHelper.cs            # Driver scanning + self-registration
│   ├── J2534Driver.cs               # Driver data model
│   └── FullBinaryReader.cs          # BinaryReader with Read7BitEncodedInt
├── SampleClient/                    # C# test client (synthetic messages)
│   └── Program.cs
├── docs/                            # Project documentation
│   ├── architecture.md              # System design and data flow
│   ├── wire-protocol.md             # Binary protocol specification
│   ├── installation.md              # Setup and usage guide
│   ├── development.md               # Build, test, development guide
│   ├── replay-redesign.md           # ReplayJ2534 design doc
│   ├── canlib-j2534-design.md       # KvaserDirect design doc
│   └── kvaser.md                    # Kvaser hardware setup
├── Makefile                         # Top-level: dll, kvaser, replay, control, sample
├── Dockerfile.mingw                 # Docker image for mingw cross-compile
├── install.nsi                      # NSIS installer script
├── redist/                          # Runtime redistributables for installer
├── .agents/                         # Agent instructions, config & skills
│   ├── VERSION
│   ├── config/agent-rules.yaml
│   ├── orchestrator.md
│   ├── implement.md
│   ├── reviewer.md
│   ├── pitfalls.md
│   ├── pitfalls-live.md
│   ├── knowledge/
│   │   ├── patterns/
│   │   └── workflows/
│   │       └── replay-scenario-authoring.md  # log2scenario + scenario authoring
│   └── skills/j2534-logger/SKILL.md # THIS FILE
└── .github/
    └── copilot-instructions.md
```

---

## Coding Conventions

### Naming

- **Functions:** camelCase
- **Types:** PascalCase
- **Constants:** SCREAMING_SNAKE_CASE
- **Modules:** PascalCase

### Error Handling

Return J2534 error codes (STATUS_NOERROR, ERR_*). Log errors before returning.

```cpp
// Pattern: guard against uninitialized DLL
if (!loadedFine) return ERR_DEVICE_NOT_CONNECTED;

// Pattern: safely handle NULL pointers
if (writeParamPointer(pDeviceID))   // serializes NULL/NOTNULL, returns true if non-null
    writeParamInt(*pDeviceID);       // only dereference if non-null
```

### Testing Patterns

- **ReplayJ2534**: 22-test mingw suite (`test_simulator.cpp`) covering
  ConfigStore, Simulator state machine, IOCTL scoping, ReadMsgs/WriteMsgs
  reply matching, and periodic generators. 11-test native suite
  (`test_configstore.cpp`) for ConfigStore JSON parsing.
- **KvaserDirect**: Unit tests for handle management and ISO-TP engine.
- **PassThruLogger**: No automated tests — use `SampleClient` for manual
  protocol verification.

---

## Design Decisions Reference

| Decision | Topic | When to Check |
|----------|-------|---------------|
| Proxy DLL pattern | Why DLL not hook | Adding new interception methods |
| TCP for IPC | Why not shared memory/pipes | Changing communication layer |
| 7-bit encoding | Why not fixed-width ints | Adding new data types to wire protocol |
| 512-byte buffer | Buffer size choice | Performance tuning, large message handling |
| x86 only | J2534 spec constraint | Any platform/architecture discussions |
| Scenario-driven replay | ReplayJ2534 design | Adding ECU simulation, replay behavior |
| Embedded JSON parser | No external deps in ConfigStore | Changing scenario config format |
| log2scenario converter | Log → scenario workflow | Creating new scenarios from captures |

See `docs/architecture.md`, `docs/wire-protocol.md`, and `docs/replay-redesign.md` for details.

---

## What to Avoid

1. **Breaking the proxy contract** — never modify parameters or return values passed to/from the real driver
2. **Blocking in the DLL** — the DLL runs in the diagnostic app's thread; never block on TCP writes (use buffering)
3. **Wire protocol desync** — always end every message with `TYPE_END` + return code; mismatched C++/C# enums cause `InvalidEnumException`
4. **Hardcoded paths** — use registry for DLL paths; installation directory varies
5. **64-bit builds of the DLL** — J2534 spec mandates 32-bit; diagnostic apps load x86 DLLs
6. **Dereferencing unchecked pointers** — always use `writeParamPointer()` guard before dereferencing
7. **Hardcoding J2534 constant values** — always use `#define` from `J2534Defs.h`; values like `ISO15765=0x06` and `READ_VBATT=0x03` are non-obvious (see P-LIVE-002)
8. **Using `-march=pentium3` in mingw builds** — causes segfault on modern Windows during static init (see P-LIVE-001)
9. **Passing NULL mask/pattern to `PassThruStartMsgFilter`** — the J2534Api wrapper requires non-NULL `pMaskMsg` and `pPatternMsg`
