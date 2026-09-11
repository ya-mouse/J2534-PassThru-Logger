# ReplayJ2534 Scenario Authoring & Log Conversion

> **Canonical copy — synced to CanDroid as of 2026-09-11** (sibling copy:
> `../CanDroid/.agents/knowledge/workflows/replay-scenario-authoring.md`).
> Bodies are kept byte-identical across the two repos; only this header
> differs. Shared semantics (matching, sequence mode, hex parsing,
> capacity) were aligned across ReplayJ2534 (C++), CanDroid
> `ReplayTransport` (Kotlin) and candroid-fw `scenario-core` (Rust) in
> that pass.

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

Verify converter output parses before deploying it:
`make test-replay-roundtrip` runs the converter over `ReplayJ2534/1.jsonl`
and loads the result through the native ConfigStore test binary. For other
captures: `make -f ReplayJ2534/tests/Makefile.native roundtrip
RT_INPUT=<capture.jsonl>`. A scenario ConfigStore rejects (unknown `return`
name, malformed JSON) fails here instead of on the target machine.

The converter extracts:
1. **Device metadata** — firmware/dll/api versions from `PassThruReadVersion`,
   vbatt from `PassThruIoctl(READ_VBATT)` output (filtered to plausible
   5000–24000 mV range; raw pointer values are rejected).
2. **IOCTL table** — every observed `PassThruIoctl` call, with scope inferred
   (READ_VBATT → device, SET_CONFIG/CLEAR_* → channel, UNK(hex) → any).
   Default IOCTLs (SET_CONFIG, GET_CONFIG, CLEAR_*) are added even if not
   in the log. Return-code names are validated against exactly the table
   `ConfigStore::lookupReturnCode` accepts — an unknown name would fail the
   WHOLE scenario at load, so the converter substitutes `STATUS_NOERROR`
   and warns on stderr naming the offending code and log line
   (log2scenario.py:608-616).
3. **Targets** — one per unique `PassThruConnect` parameter set (protocol,
   flags, baud). `preferredChannelId` is set to the logged channel ID.
4. **Reply rules** — pairs each `WriteMsgs` request with all matching
   `ReadMsgs` responses on the same channel. Measures actual `delayMs`
   from timestamps. For each unique request, collects ALL response
   variations (keyed by `(target, write_hex)` to avoid cross-ECU
   pollution). If a request has >3 unique responses (live data with
   changing values), emits a **sequence-mode** rule; otherwise emits a
   single-response rule. Deduplicates identical request data.
   `7F <service> 78` (responsePending) reads are NEVER exported as reply
   data — the write stays pending until the real response arrives
   (log2scenario.py:690-705), otherwise the replay would answer
   "pending" forever and the client would time out.
5. **Periodic generators** — detects recurring read patterns (same data
   appearing ≥3 times at regular intervals within 30% tolerance). Computes
   `intervalMs` from average inter-arrival time. Only runs at disconnect
   (when all reads for a channel have been collected). Loopback echoes and
   START_OF_MESSAGE phantoms (the 4-byte CAN-ID-only reads the device emits
   after writes) are excluded via `rx_status & 0x000B`
   (log2scenario.py:719-727) so they never become bogus generators.

### Scenario JSON schema

See `docs/replay-redesign.md` § "Scenario JSON schema" for the full
specification. Key rules:

- **IOCTL key**: hex (`"0x10ECB"`) or symbolic (`"READ_VBATT"`).
- **`return`**: always a symbolic J2534 error name from the 27-name table
  in `ConfigStore::lookupReturnCode` — an unknown name fails the whole
  file at load.
- **`output`**: `"auto"` (synthesize), hex byte string, or omitted.
- **`scope`**: `"device"`, `"channel"`, or `"any"`.
- **Hex data strings**: the C++ `ConfigStore::parseHexBytes` and CanDroid's
  `HexCodec.parseHex` mirror each other — ANY non-hex character is a
  separator, a dangling odd nibble becomes a final byte, empty input is a
  catch-all pattern, neither throws. candroid-fw is stricter: it accepts
  `-`, space, `:`, `,`, tab, CR, LF as separators (`hex.rs:40-42`) and
  ERRORS on an odd digit count or any other character. **Authoring
  guidance: always use dashes** (`"00-00-07-E0-21-03"`) with an even
  number of hex digits per byte — the only style every parser accepts.
- **Match mode**: `"prefix"` (default) or `"exact"`.
- **Response mode**: sequence mode is detected by the PRESENCE of a
  non-empty `sequence[]` array — the `"mode"` key is informational only
  (see below).

#### Rule ordering — first match wins

All three replay engines answer a write from the FIRST matching rule in
**document order** and skip later matches (ReplayJ2534
`Simulator.cpp:289-341`, CanDroid `ReplayTransport.matchRule`, candroid-fw
`index.rs` `Index::find`) — a real ECU answers a request once. For
hand-authored scenarios:

- Put **specific rules before general prefixes**: a broad
  `"00-00-07-E0-22"` rule placed first shadows every `22-xx` rule after it.
- A rule with EMPTY match data (`"data": ""`) is a live catch-all in
  ReplayJ2534 and CanDroid (`Simulator::matchReply`; CanDroid
  `ReplayTransport.matchRule` — an empty prefix matches every request).
  A rule whose `match` object or `match.data` key is MISSING entirely is
  skipped and counted (CanDroid `Scenario.skippedRules`); candroid-fw
  skips and counts a pattern-less rule at load too (the skip-and-count
  path in `build.rs`). Place a catch-all last, if at all — and know it
  never catches on the firmware.
- `log2scenario.py` enforces this itself: it emits rules sorted
  longest-match-first (stable), checks collapse candidates against ALL
  replies including sequences, and fails the conversion outright if any
  emitted rule would still be shadowed
  (`_assert_first_match_order`).

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

**Default `timeWindowMs`**: 600 ms when the key is absent — the canonical
cross-repo default, identical in all three engines since the 2026-09-11
alignment (CanDroid `ScenarioLoader.DEFAULT_TIME_WINDOW_MS`; ReplayJ2534
`ConfigStore.cpp:538-542`, previously 1000; candroid-fw `build.rs`,
previously 0). An EXPLICIT `"timeWindowMs": 0` still means advance-on-
every-request everywhere (that is what instant mode uses).

**Instant mode**: when `REPLAY_J2534_INSTANT=1`, `timeWindowMs` is
treated as 0 — every read advances one step. Useful for testing the
sequence machinery without real-time waits.

**Cursor scope**: the sequence cursor is per-connection AND per-rule, and
resets on disconnect in all three engines — ReplayJ2534 keeps `seqStates`
inside the `Channel` object, which is destroyed on disconnect
(`Simulator.h:65`); CanDroid clears its `seqStates` map in `disconnect()`;
candroid-fw exposes `Index::reset_cursors`
for the firmware to call on disconnect (`index.rs:243`). A reconnect
always restarts at `sequence[0]`.

**Detection by presence**: a rule enters sequence mode when its
`response` carries a non-empty `sequence[]` array, REGARDLESS of the
`"mode"` key — the behavior of all three engines (ReplayJ2534
`ConfigStore.cpp:521-542`, CanDroid `ScenarioLoader`, candroid-fw
`build.rs` sequence arm). Before the 2026-09-11 alignment, C++ required
`"mode":"sequence"` and answered a sequence-without-mode rule with a
0-byte message. An EMPTY `sequence` array **downgrades the rule to
single mode** using `response.data` in all three engines (C++
`ConfigStore.cpp:535`; CanDroid mirrors it in `ScenarioLoader` with a
defensive guard in `ReplayTransport.getResponse`; candroid-fw resolves
it at parse time in `build.rs`, key-order independent). Remaining edge
divergence: empty sequence AND no `data` — candroid-fw and CanDroid skip
and count the rule; ReplayJ2534 keeps it as a single rule that answers a
0-length message. Still: do not ship empty `sequence` arrays.

### emitEcho — the 3-message response cycle

With `"emitEcho": true` (top level), each accepted write produces up to
three queued messages, mirroring what the real J2534 device shows the app:

1. **TX echo** — the write's first 4 bytes (CAN ID), `RxStatus 0x0009`
   (`TX_MSG_TYPE | TX_INDICATION`), delivered **+12 ms** after the write
   (`Simulator.cpp:271-286`). Emitted for EVERY write, whether or not a
   rule matched.
2. **START_OF_MESSAGE** — the response's first 4 bytes, `RxStatus 0x0002`,
   delivered **+27 ms** — ONLY when the response CAN ID differs from the
   write CAN ID (`Simulator.cpp:322-337`). Same-ID responses skip it.
3. **The reply** — the first matching rule's response (single payload or
   current sequence entry) at the rule's `delayMs`
   (`Simulator.cpp:339-340`).

Instant mode collapses all three delays to 0 but preserves the order.
Clients such as Xentry depend on this cycle during module enumeration;
tests that drain the rxQueue must account for up to 3 messages per write.

### Periodic generator startup stagger

Periodic generators do NOT fire immediately on connect. The first fire is
`5000 + i·50 ms + intervalMs` after connect, where `i` is the generator's
index within the target (`Scheduler.cpp:80-85`). CanDroid applies the same
formula but counts `i` as a GLOBAL index across all targets
(`ReplayTransport.startPeriodicGenerators`) — multi-target scenarios therefore stagger
slightly differently between engines. Without the stagger, every generator
fires within ~40 ms of connect and buries request/response traffic in the
rxQueue. Instant mode removes the stagger (first fire immediate) — tests
asserting periodic content must run in instant mode or wait out the ~5 s.

### Capacity limits per engine

| Limit | ReplayJ2534 (C++) | candroid-fw (Rust) | CanDroid (Kotlin) |
|---|---|---|---|
| Scenario size | 5 MB file cap (`ConfigStore::load`) | flash-mapped document | unbounded (heap) |
| Match pattern | 512 B (`HexBytes`) | 128 B (`build.rs` pattern buffer) | unbounded (`ByteArray`) |
| Reply rules | unbounded (`std::vector`) | 384 (`MAX_RULES`, `index.rs:17`) | unbounded (`List`) |
| Pattern arena | n/a | 3072 B total (`MATCH_ARENA`, `index.rs:19`) | n/a |
| Sequence entries | unbounded | count stored as `u16` | unbounded |

Scenarios that fit the candroid-fw column replay identically on all three
engines, with two caveats: candroid-fw's parser is KEY-ORDER dependent
when a response carries BOTH `data` and a NON-EMPTY `sequence` — the last
key seen wins (`build.rs` response-key loop) — so emit one or the other, never both (an
EMPTY `sequence` is exempt: it downgrades to the data single in any
order); and the startup-stagger index scope differs (per-target vs.
global, see above).
Over-capacity rules are skipped (and counted/reported at load) on the
firmware, not rejected — the host engines accept them, so a scenario can
silently behave differently on the device. Keep generated scenarios
within the firmware limits when they are meant for candroid-fw.

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
  - Rule order — first match wins: put specific rules before general
    prefixes (see "Rule ordering" above).
  - Hex strings — always dash-separated with even digit pairs; C++ and
    CanDroid tolerate any separator, candroid-fw errors on odd digit
    counts and unknown characters.
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
- **Malformed entries are skipped, not fatal**: a `periodic` entry without
  `msg`/`msg.data`, or with an empty data payload, is dropped with a log
  warning (ConfigStore.cpp:549-566) — before the 2026-09-11 alignment a
  missing `msg` dereferenced NULL and crashed the host app at load, and a
  0-length payload would flood the rxQueue with empty frames every
  interval. CanDroid now skips-and-counts the same cases (plus periodic
  data <4 bytes, which could never carry a CAN ID); candroid-fw has no
  periodic engine. An unknown IOCTL `return` name, by contrast, still
  fails the WHOLE file — which is why the converter validates names
  before emitting.
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
- CanDroid `transport/replay/.../ScenarioLoader.kt` + `ReplayTransport.kt` —
  Kotlin engine sharing this scenario format
- candroid-fw `crates/scenario-core/src/{build,index}.rs` — Rust/firmware
  engine sharing this scenario format
- `make test-replay-roundtrip` — converter output → ConfigStore load check
