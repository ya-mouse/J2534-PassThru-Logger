# Live Pitfalls (Auto-Updated)
# Template version: 0.1

> **Scope awareness:** Each pitfall entry must clearly state its scope —
> which component, phase, or toolchain version it applies to.
> Live pitfalls are temporary workarounds, not permanent design decisions.
>
> **Do not carry these restrictions into:**
> - Project design documents or specifications
> - Public APIs or user-facing interfaces
> - Documentation aimed at end users
> - Code that will run under a fixed/upgraded toolchain

---

## How This Works

1. **Post-struggle**: When a build/fix cycle requires ≥2 iterations on the
   same root cause, the orchestrator appends an entry here.
2. **Post-review**: When a reviewer flags a bug class that workers should
   avoid, it gets recorded here too.
3. **Worker prompts**: Include both `pitfalls.md` (static) and
   `pitfalls-live.md` (this file) — workers see the latest known issues.
4. **Consolidation**: At each milestone gate, merge confirmed entries into
   `pitfalls.md` and reset this file.

### Quality Gate for New Entries

Before adding or committing a new pitfall entry, verify:

- [ ] **Scoped correctly** — States explicitly which component/phase/tool
  version this applies to.
- [ ] **Fix doesn't harm the project** — The workaround is a process
  constraint, not a design compromise. If the workaround would change
  public APIs or architecture, flag it for human review instead.
- [ ] **Temporary** — The entry notes what future work will make this
  pitfall obsolete.

### Entry Format

```markdown
### P-LIVE-NNN: <short title>
- **Scope**: <component/phase/tool this applies to>
- **Discovered**: <task-id>, <date>
- **Symptom**: What went wrong (error message, crash, etc.)
- **Root cause**: Why it happened
- **Workaround**: What to do instead
- **Obsoleted by**: Which future work removes this limitation
- **Source**: struggle | review
```

---

## Entries

### P-LIVE-001: mingw `-march=pentium3` causes segfault on modern Windows
- **Scope**: ReplayJ2534 tests, mingw-w64 cross-compile (Docker j2534-builder image)
- **Discovered**: replay-redesign, 2026-06-25
- **Symptom**: `test_simulator.exe` segfaults immediately (before `main()`) on
  real Windows. No output, even `--help` crashes. Works fine under Wine on x86.
- **Root cause**: `-march=pentium3` in `Makefile.test` generates SSE instructions
  that crash during static initialization on some Windows CPUs/configurations.
  The global `ReplayLogger g_logger` constructor calls `InitializeCriticalSection`,
  which triggers the crash.
- **Workaround**: Remove `-march=pentium3` from CFLAGS. Use `-static` (full
  static linking) instead of `-static-libgcc -static-libstdc++` to avoid runtime
  DLL dependencies.
- **Obsoleted by**: Not applicable — this is a mingw codegen bug, not project-specific.
- **Source**: struggle

### P-LIVE-002: J2534 constant values differ from common assumptions
- **Scope**: Any code calling J2534 API, test clients, scenario.json authoring
- **Discovered**: replay-redesign, 2026-06-25
- **Symptom**: `PassThruConnect` returns wrong channel ID (1 instead of preferred
  2). `PassThruIoctl(0x05, ...)` returns `ERR_INVALID_IOCTL_ID` instead of
  `STATUS_NOERROR` for READ_VBATT. `PassThruStartMsgFilter` returns
  `ERR_NULL_PARAMETER` (4) when passing NULL mask/pattern.
- **Root cause**: Several J2534 constants have non-obvious values:
  - `J2534_ISO15765 = 0x06` (not 5 as some docs suggest; 5 is `J2534_CAN`)
  - `READ_VBATT = 0x03` (not 0x05; 0x05 is `FAST_INIT`)
  - `ERR_NULL_PARAMETER = 0x04` (not `ERR_INVALID_FLAGS`; 0x06 is `ERR_INVALID_FLAGS`)
  - `CAN_ID_BOTH = 0x0800` (not a simple 0/1 flag)
  - `FLOW_CONTROL_FILTER = 0x03` (same value as READ_VBATT, but different context)
- **Workaround**: Always reference constants from `J2534Defs.h` — never hardcode
  numeric values. When writing test clients, `#include` the header or copy the
  exact `#define` values. The `PassThruStartMsgFilter` wrapper in `J2534Api.cpp`
  requires non-NULL `pMaskMsg` and `pPatternMsg` — always provide them.
- **Obsoleted by**: Not applicable — these are spec constants.
- **Source**: struggle

### P-LIVE-003: Channel ID allocation must skip preferred IDs when taken
- **Scope**: ReplayJ2534 Simulator, `allocateChannelId()` in `Simulator.cpp`
- **Discovered**: replay-redesign, 2026-06-25
- **Symptom**: When two channels are open simultaneously and the first takes the
  `preferredChannelId` (e.g. 2), the second gets channel ID 1 instead of 3.
- **Root cause**: `allocateChannelId()` fell back to `nextChannelId_` (starting
  at 1) when the preferred ID was taken, returning the first free ID from 1
  instead of continuing past the preferred ID.
- **Workaround**: When preferred is taken, search starting from `preferred + 1`,
  not from `nextChannelId_`. This ensures channel IDs are allocated above the
  preferred range when the preferred slot is occupied.
- **Obsoleted by**: Fixed in `Simulator.cpp:85-91`. This entry remains as
  documentation of the pattern.
- **Source**: struggle

---

## Archive

<!-- At each milestone gate, resolved entries are moved here for history. -->
