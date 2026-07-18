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
python3 ReplayJ2534/tools/log2scenario.py <input.json> [output.json] [--max-sequence-len N]
```

If no output path is given, the converter writes alongside the input with
a `.scenario.json` suffix. `--max-sequence-len` (default 200, min 2) caps
the number of entries per sequence rule (evenly sampled from the full
capture) to control scenario.json size.

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
4. **Reply rules** — pairs each `WriteMsgs` request with all matching
   `ReadMsgs` responses on the same channel. Measures actual `delayMs`
   from timestamps. For each unique request, collects ALL response
   variations (keyed by `(target, write_hex)` to avoid cross-ECU
   pollution). If a request has >3 unique responses (live data with
   changing values), emits a **sequence-mode** rule; otherwise emits a
   single-response rule. Deduplicates identical request data.
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
- **Response mode**: `"single"` (default) or `"sequence"`.

#### Single-mode response (1:1 request→response)

Always returns the same fixed response. Use for NRCs, session control,
security access, tester present — any request with one fixed reply.

```json
{
  "match": {"data": "00-00-07-E0-3E-00", "mode": "prefix"},
  "response": {
    "data": "00-00-07-E8-7E-00",
    "delayMs": 10,
    "protocolId": "ISO15765"
  }
}
```

#### Sequence-mode response (time-gated looping sequence)

Cycles through an array of response payloads. Use for live-data PIDs
that are polled repeatedly with changing values (RPM, temperatures,
pressures). The replay advances through the sequence based on read timing:

- **First read**: returns `sequence[0]` (initialization, no advance).
- **Burst reads** (within `timeWindowMs` of last advance): return the
  current value — no advance. This prevents burning through the sequence
  when the app polls rapidly.
- **Spread reads** (beyond `timeWindowMs`): advance one step. Never skips
  ahead, even if multiple windows have elapsed.
- **Loop**: when the sequence ends, wraps to index 0 (modulo).

```json
{
  "match": {"data": "00-00-07-E0-21-03", "mode": "prefix"},
  "response": {
    "mode": "sequence",
    "sequence": [
      "00-00-07-E8-61-03-00-00-01-F0",
      "00-00-07-E8-61-03-0B-9B-06-5B",
      "00-00-07-E8-61-03-01-F4-03-4E"
    ],
    "timeWindowMs": 600,
    "delayMs": 10,
    "protocolId": "ISO15765"
  }
}
```

The `protocolId`, `rxStatus`, `txFlags` from `response` apply to ALL
sequence entries (they're the common ECU/protocol fields). Only the `data`
varies across the sequence.

**Auto-detection**: `log2scenario.py` uses sequence mode when a request
has >3 unique responses in the capture. The `timeWindowMs` is
auto-computed from the 25th percentile of inter-arrival times in the
log (before sampling), ensuring burst reads collapse but spread reads
advance. You can override `timeWindowMs` by hand-editing the scenario.

**Instant mode**: when `REPLAY_J2534_INSTANT=1`, `timeWindowMs` is
treated as 0 — every read advances one step. Useful for testing the
sequence machinery without real-time waits.

**Empty sequence fallback**: if a hand-authored rule has
`"mode":"sequence"` but an empty or missing `sequence` array,
ConfigStore downgrades it to single mode at load time.

### Manual authoring tips

- Start with `log2scenario.py` output, then adjust:
  - `delayMs` — logged delays include app processing overhead; round to
    realistic ECU response times (10–50ms typical).
  - `intervalMs` — for periodic tester-present, 2000ms is standard.
  - `timeWindowMs` — for sequence rules, the auto-computed value (25th
    percentile of inter-arrival) is usually good. Raise it to make the
    sequence advance slower (more burst tolerance); lower it to advance
    faster. Typical range: 200–2000ms.
  - `--max-sequence-len` — raise from 200 to capture more variation
    (larger scenario file, more faithful replay). Lower to 50–100 for
    a compact test scenario.
  - Match `mode` — use `"prefix"` for UDS service+subfunction matching,
    `"exact"` for full-frame matching.
- The `preferredChannelId` should match what the real client expects.
  Xentry/open-port uses channel ID 2.
- Add the standard state machine (CLOSED↔OPENED) unless you need
  different lifecycle semantics.
- Set `REPLAY_J2534_INSTANT=1` env var for testing — all delays and
  intervals become 0ms, making tests deterministic and fast. In instant
  mode, sequence rules also advance on every read (window=0).
- Both single-mode and sequence-mode rules can coexist in the same
  scenario — the Simulator checks each rule's `responseMode` independently.

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
- **START_OF_MESSAGE notifications**: The J2534 device emits a 4-byte
  CAN-ID-only read (RxStatus=0x0002) before the full response arrives in
  the next `ReadMsgs`. The converter correctly skips these (length <
  service offset) and pairs with the subsequent full read.
- **Live data collapse (why few rules)**: A 1-hour live-data capture may
  have 15k WriteMsgs but only ~326 unique request payloads. The converter
  deduplicates by request data, producing one rule per unique request.
  Without sequence mode, this drops 97% of unique response variations
  (live values that change on each poll). Sequence mode preserves these
  variations — requests with >3 unique responses get a sequence rule
  that cycles through captured values.
- **Periodic vs. reply disambiguation**: A tester-present response (`3E 00`)
  appears as both a reply rule and a periodic candidate. The converter
  creates both — the reply rule handles the initial response, the periodic
  handles subsequent unsolicited repetitions. Review and remove the periodic
  if it's actually just repeated request-response cycles.
- **Wine/arm64 incompatibility**: The mingw-built `test_simulator.exe`
  cannot run under Wine on arm64 (crashes with "Unhandled illegal
  instruction"). Test on real Windows or x86 Wine.
- **Sequence test timing**: When testing sequence rules in non-instant
  mode on Windows, use `readMsgs(timeout > 0)` (e.g., 500ms) rather than
  `timeout=0` + `Sleep`. The Scheduler delivers replies asynchronously
  via a background thread; `timeout=0` returns `ERR_BUFFER_EMPTY` if the
  reply hasn't been queued yet. See `sim_sequence_time_gated_advance` in
  `test_simulator.cpp` for the correct pattern.

## References

- `ReplayJ2534/tools/log2scenario.py` — the converter tool
- `ReplayJ2534/scenario.json` — example scenario
- `docs/replay-redesign.md` — design doc with full schema
- `ReplayJ2534/LogParser.cpp` — original C++ log parser (kept for reference,
  not linked into DLL)
