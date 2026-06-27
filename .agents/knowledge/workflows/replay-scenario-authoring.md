# ReplayJ2534 Scenario Authoring & Log Conversion

## When to Use

- You have a PassThruLogger `.jsonl` capture from a real diagnostic session
  and want to create a ReplayJ2534 `scenario.json` from it.
- You need to author a scenario from scratch and want to understand the
  schema and common patterns.
- You are debugging a scenario that doesn't produce expected ECU replies.

## Pattern / Procedure

### Converting a log to a scenario

```bash
python3 ReplayJ2534/tools/log2scenario.py <input.jsonl> [output.json]
```

If no output path is given, the converter writes alongside the input with
a `.scenario.json` suffix.

The converter extracts:
1. **Device metadata** — firmware/dll/api versions from `PassThruReadVersion`,
   vbatt from `PassThruIoctl(READ_VBATT)` output (filtered to plausible
   5000–24000 mV range; raw pointer values are rejected).
2. **IOCTL table** — every observed `PassThruIoctl` call, with scope inferred
   (READ_VBATT → device, SET_CONFIG/CLEAR_* → channel, UNK(hex) → any).
   Default IOCTLs (SET_CONFIG, GET_CONFIG, CLEAR_*) are added even if not
   in the log.
3. **Targets** — one per unique `PassThruConnect` parameter set (protocol,
   flags, baud). `preferredChannelId` is set to the logged channel ID.
4. **Reply rules** — pairs each `WriteMsgs` request with the next
   `ReadMsgs` that returns messages on the same channel. Measures actual
   `delayMs` from timestamps. Deduplicates identical request data.
5. **Periodic generators** — detects recurring read patterns (same data
   appearing ≥3 times at regular intervals within 30% tolerance). Computes
   `intervalMs` from average inter-arrival time. Only runs at disconnect
   (when all reads for a channel have been collected).

### Scenario JSON schema

See `docs/replay-redesign.md` § "Scenario JSON schema" for the full
specification. Key rules:

- **IOCTL key**: hex (`"0x10ECB"`) or symbolic (`"READ_VBATT"`).
- **`return`**: always a symbolic J2534 error name.
- **`output`**: `"auto"` (synthesize), hex byte string, or omitted.
- **`scope`**: `"device"`, `"channel"`, or `"any"`.
- **Hex data strings**: separators (`-`, spaces, `:`) are ignored.
- **Match mode**: `"prefix"` (default) or `"exact"`.

### Manual authoring tips

- Start with `log2scenario.py` output, then adjust:
  - `delayMs` — logged delays include app processing overhead; round to
    realistic ECU response times (10–50ms typical).
  - `intervalMs` — for periodic tester-present, 2000ms is standard.
  - Match `mode` — use `"prefix"` for UDS service+subfunction matching,
    `"exact"` for full-frame matching.
- The `preferredChannelId` should match what the real client expects.
  Xentry/open-port uses channel ID 2.
- Add the standard state machine (CLOSED↔OPENED) unless you need
  different lifecycle semantics.
- Set `REPLAY_J2534_INSTANT=1` env var for testing — all delays and
  intervals become 0ms, making tests deterministic and fast.

### Registering the DLL on Windows

```reg
[HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\PassThruSupport.04.04\ReplayJ2534]
"FunctionLibrary"="C:\\path\\to\\ReplayJ2534.dll"

[HKEY_CURRENT_USER\SOFTWARE\ReplayJ2534]
"ScenarioPath"="C:\\path\\to\\scenario.json"
"Instant"=dword:00000001
```

Or use the `REPLAY_J2534_CONFIG` environment variable to override the
scenario path (takes priority over registry).

## Pitfalls

- **VBATT value extraction**: The log's `PassThruIoctl(READ_VBATT, NULL, <num>)`
  4th argument can be a pointer address, not a voltage. The converter filters
  to 5000–24000 mV; if no valid value is found, defaults to 12000.
- **Empty reads between write and reply**: The log often shows
  `ReadMsgs → ERR_BUFFER_EMPTY` between a `WriteMsgs` and the actual reply.
  The converter skips empty reads when pairing — don't pair with them.
- **Periodic vs. reply disambiguation**: A tester-present response (`3E 00`)
  appears as both a reply rule and a periodic candidate. The converter
  creates both — the reply rule handles the initial response, the periodic
  handles subsequent unsolicited repetitions. Review and remove the periodic
  if it's actually just repeated request-response cycles.
- **Wine/arm64 incompatibility**: The mingw-built `test_simulator.exe`
  cannot run under Wine on arm64 (crashes with "Unhandled illegal
  instruction"). Test on real Windows or x86 Wine.

## References

- `ReplayJ2534/tools/log2scenario.py` — the converter tool
- `ReplayJ2534/scenario.json` — example scenario
- `docs/replay-redesign.md` — design doc with full schema
- `ReplayJ2534/LogParser.cpp` — original C++ log parser (kept for reference,
  not linked into DLL)
