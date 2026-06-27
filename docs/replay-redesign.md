# ReplayJ2534 Redesign — Scenario-Driven State Machine

## Status: Planning

## Motivation

The current `ReplayEngine` is a linear cursor walker: each API call does
`findNextMatch(type, handle)` advancing a single global cursor through parsed log
events, with a fragile shared dedup counter. Problems this redesign fixes:

1. **Order-locked** — if the client calls APIs in a different order than logged,
   the cursor skips real events or matches the wrong one.
2. **No background production** — `ReadMsgs` can only return a logged event sitting
   at the cursor; nothing fires between calls.
3. **Passive writes** — `WriteMsgs` just matches the next logged WRITE event; it
   cannot trigger a canned ECU reply.
4. **No IOCTL config** — IOCTL replies come only from the log; can't override or
   define behavior for unlogged IOCTLs.
5. **Device-scope IOCTLs accidental** — `READ_VBATT` on `DeviceID=1232` only works
   because the handle value happens to match; there's no concept of "device-level"
   vs "channel-level".
6. **Single dedup slot** — shared across all call types, so a READ dedup can
   corrupt a subsequent IOCTL match.

## Decisions

1. **No log overlay.** `scenario.json` is the sole source of replay behavior.
   The old `1.jsonl` log-walker (`ReplayEngine` + `LogParser` linear cursor) is
   removed from the build. `LogParser` source is kept on disk for a future
   log-to-scenario converter tool but is not linked into the DLL.
2. **Multi-session support.** One scenario represents one J2534 pass-thru adapter
   (the USB CAN probe) and may contain many open/close and connect/disconnect
   cycles. See [Terminology](#terminology) and
   [Targets & multi-session](#targets--multi-session).
3. **Real timing.** `intervalMs` and `delayMs` are honored wall-clock by the
   Scheduler thread. A global override (`REPLAY_J2534_INSTANT=1` env or registry
   `Instant=1`) forces instant delivery for debugging.
4. **Symbolic return codes.** IOCTL rules and transitions reference J2534 error
   names (`STATUS_NOERROR`, `ERR_INVALID_CHANNEL_ID`, …), never raw numbers.

## Terminology

| Term | Meaning | J2534 API |
|------|---------|-----------|
| **Device** | The J2534 pass-thru adapter (USB CAN probe) the client app opens. One per scenario. | `PassThruOpen` → `DeviceID` |
| **Channel** | A logical connection from the device to a target ECU. Many per device; can be opened/closed repeatedly. | `PassThruConnect` → `ChannelID` |
| **Target** | A named ECU profile (reply rules + periodic generators) that a channel binds to on connect. | — |

The original log captures one device (the probe) performing one or more connect
cycles to target ECUs. The scenario encodes the device once and one or more
targets; each `PassThruConnect` whose parameters match a target binds that
channel to the target's behavior.

## Architecture

Three layers replace the single `ReplayEngine`:

```
┌─────────────────────────────────────────────────────────┐
│ J2534Api.cpp  (unchanged 14 exports)                     │
└───────────────┬─────────────────────────────────────────┘
                │
┌───────────────▼─────────────────────────────────────────┐
│ Simulator (new) — state machine + dispatcher             │
│  • owns Device + Channel state objects                    │
│  • routes each API call to the right handler              │
│  • validates against state machine transitions            │
└──────┬────────────────────┬─────────────────────────────┘
       │                    │
┌──────▼─────────┐   ┌─────▼──────────────────────────┐
│ ConfigStore(new)│   │ Scheduler (new) — background    │
│  • scenario.json│   │  thread: periodic + delayed    │
│  • IOCTL table  │   │  replies → per-channel RX queue │
│  • targets      │   └────────────────────────────────┘
│  • replies      │
│  • periodic     │
└─────────────────┘
```

### Module responsibilities

- **ConfigStore** — loads `scenario.json`, exposes typed accessors
  (`findIoctl(id)`, `findTarget(protocolId, flags, baud)`, device metadata).
  No runtime state.
- **Simulator** — owns `Device` and `Channel` state objects, validates the state
  machine, dispatches API calls, binds channels to targets on connect. Owns the
  Scheduler thread lifecycle.
- **Scheduler** — single background thread created on `Simulator::init`, joined
  on `shutdown`. Runs periodic generators at `intervalMs` and posts delayed
  replies after `delayMs` into the matching channel's `rxQueue`.

## Scenario JSON schema

```json
{
  "device": {
    "firmwareVersion": "3.37.0",
    "dllVersion": "1.0.0",
    "apiVersion": "04.04",
    "vbatt_mV": 12000
  },

  "ioctls": {
    "READ_VBATT": { "return": "STATUS_NOERROR", "output": "auto", "scope": "device" },
    "SET_CONFIG": { "return": "STATUS_NOERROR", "scope": "channel", "consumeInput": true },
    "GET_CONFIG": { "return": "STATUS_NOERROR", "scope": "channel", "output": "auto" },
    "CLEAR_RX_BUFFER": { "return": "STATUS_NOERROR", "scope": "channel" },
    "0x10ECB":      { "return": "ERR_INVALID_IOCTL_ID", "scope": "any" }
  },

  "targets": [
    {
      "name": "ECM",
      "match": { "protocolId": "ISO15765", "flags": "CAN_ID_BOTH", "baud": 500000 },
      "preferredChannelId": 2,
      "replies": [
        {
          "match": { "data": "00-00-06-02-10-03", "mode": "prefix" },
          "response": {
            "data": "00-00-04-80-50-03-00-14-00-C8",
            "delayMs": 10,
            "rxStatus": 0,
            "txFlags": 0,
            "protocolId": "ISO15765"
          }
        },
        {
          "match": { "data": "00-00-06-02-3E-00", "mode": "prefix" },
          "response": {
            "data": "00-00-04-80-7E-00",
            "delayMs": 8,
            "rxStatus": 0,
            "txFlags": 0,
            "protocolId": "ISO15765"
          }
        }
      ],
      "periodic": [
        {
          "intervalMs": 2000,
          "msg": { "protocolId": "ISO15765", "data": "00-00-04-80-7E-00",
                   "rxStatus": 0, "txFlags": 0 },
          "startOn": "connect",
          "stopOn": "disconnect"
        }
      ]
    }
  ],

  "states": {
    "initial": "CLOSED",
    "transitions": [
      { "event": "PassThruOpen",       "from": "CLOSED",     "to": "OPENED" },
      { "event": "PassThruConnect",    "from": "OPENED",     "to": "OPENED" },
      { "event": "PassThruDisconnect", "from": "OPENED",     "to": "OPENED" },
      { "event": "PassThruClose",      "from": "OPENED",     "to": "CLOSED" }
    ]
  }
}
```

### Schema rules

- **IOCTL key** may be hex (`"0x10ECB"`) or symbolic (`"READ_VBATT"`).
  `ConfigStore` normalizes both via the existing `lookupIoctl` table; unknown hex
  values pass through (for vendor IOCTLs like `0x10ECB` seen in the log).
- **`return`** is always a symbolic J2534 error name resolved via `lookupError`.
- **`output`**: `"auto"` (synthesize a sensible value, e.g. `READ_VBATT` →
  `device.vbatt_mV` as 4-byte LE), a hex byte string (`"00-10-..."`), or
  `null`/omitted (no payload written to caller's buffer).
- **`scope`**: `"device"` (valid on `DeviceID` before/without `Connect` — the
  no-connection IOCTL case), `"channel"` (valid only on `ChannelID`), `"any"`
  (both). `ConfigStore` enforces scope against the handle type in
  `Simulator::handleIoctl`.
- **`consumeInput`** (SET_CONFIG): log the `SCONFIG_LIST` params for diagnostics
  but take no other action.
- **Targets**: `match` keys off the `PassThruConnect` parameters. Multiple
  targets allow the probe to talk to different ECUs. `preferredChannelId` is a
  hint; the Simulator assigns the actual ID (reusing freed IDs where possible).

### Hex string parsing

Hex byte strings in `data` fields ignore non-hex separators (`-`, spaces, `:`),
so `"00-00-06-02-10-03"` and `"000006021003"` are equivalent. `ConfigStore`
parses these into `unsigned char[]` once at load time.

## State machine

### State objects

```cpp
enum class DevState { CLOSED, OPENED };

struct Channel {
    unsigned long id;
    unsigned long protocolId, flags, baud;
    const Target *target;          // bound on connect
    std::deque<PASSTHRU_MSG> rxQueue;
    std::vector<int> activePeriodicIds;  // Scheduler generator handles
};

struct Device {
    unsigned long id;              // assigned on Open
    DevState state;
    char fw[80], dll[80], api[80];
    unsigned long vbatt_mV;
    std::unordered_map<unsigned long, Channel> channels;
    std::unordered_map<unsigned long, unsigned long> filters;
};
```

### Transitions

Driven by `states.transitions` from the scenario. The default transition table
above allows cycling:

```
CLOSED --Open-->       OPENED
OPENED --Connect-->    OPENED   (channel created, target bound)
OPENED --Disconnect--> OPENED   (channel destroyed, periodic stopped)
OPENED --Close-->      CLOSED
```

This explicitly permits multiple open/close and connect/disconnect cycles within
one scenario — a `Connect` after a `Disconnect` rebinds a (possibly new) channel
to the matching target. Invalid transitions return the appropriate J2534 error
(`ERR_DEVICE_IN_USE`, `ERR_INVALID_CHANNEL_ID`, …) instead of silently matching
a log event.

### Device-level IOCTLs (no connection)

`PassThruIoctl(DeviceID, READ_VBATT, …)` called before any `Connect` resolves
to the `device`-scope rule: requires `Device.state == OPENED`, ignores channel
state, returns configured vbatt. This is the "IOCTL without opening a
connection" path observed in `1.jsonl` (line 34: `PassThruIoctl(1232, READ_VBATT,
NULL, …)` before Connect, and line 106: same call after Disconnect returning
`ERR_INVALID_CHANNEL_ID` — note: in the new model this would return
`STATUS_NOERROR` since scope is `device`; if the original `ERR_INVALID_CHANNEL_ID`
behavior is desired for that specific case, scope it as `"channel"` instead).

## Targets & multi-session

A single scenario represents **one device (the USB CAN probe)** that may open
and close many channels to one or more target ECUs over its lifetime.

- Each `PassThruConnect(deviceId, protocolId, flags, baud, &channelId)` looks up
  the first target whose `match` equals the connect parameters, assigns a
  `channelId` (using `preferredChannelId` if free, else auto-allocated), binds
  the target's `replies`/`periodic` to that channel, and starts periodic
  generators with `startOn: "connect"`.
- `PassThruDisconnect(channelId)` stops that channel's periodic generators and
  removes the channel; the `channelId` returns to the free pool for reuse.
- A subsequent `PassThruConnect` with the same parameters rebinds a fresh channel
  to the same target — reply rules apply identically. This models repeated
  diagnostic sessions against the same ECU.
- Different connect parameters (different baud, protocol, or flags) match a
  different target, letting the probe talk to multiple ECUs.

ChannelID assignment: Simulator maintains an ID pool. `preferredChannelId` is
honored if free; otherwise the lowest free ID ≥ 1 is assigned. Freed IDs on
disconnect return to the pool.

## Scheduler

One background thread (created on `Simulator::init`, joined on `shutdown`).
Wakes every ~1 ms (or via condition variable with timeout) to process:

- **Periodic generators** — for each active generator, if `now - lastFire >=
  intervalMs`, push a copy of `msg` into the bound channel's `rxQueue` and update
  `lastFire`.
- **Delayed replies** — when `writeMsgs` matches a reply rule, the Simulator
  enqueues a `(channelId, response, fireAt = now + delayMs)` item onto the
  Scheduler's pending list. The Scheduler thread moves due items into the
  channel's `rxQueue`.

Global instant override: `REPLAY_J2534_INSTANT=1` (env) or registry
`Instant=1` forces `delayMs=0` and `intervalMs` collapsed to a single immediate
fire (useful for tests). Periodic generators still re-arm at their real interval
so cadence is observable, but the first fire is immediate.

Thread safety: `rxQueue` and channel state are accessed under the Simulator's
existing `CRITICAL_SECTION`. The Scheduler takes the lock when pushing to
`rxQueue`; `readMsgs` takes it when draining.

## API call handling

### PassThruOpen / Close
- `Open`: validate `CLOSED → OPENED` transition, assign DeviceID (from config or
  auto), populate version strings. Returns `STATUS_NOERROR` + `*pDeviceID`.
- `Close`: validate `OPENED → CLOSED`, require no open channels (else
  `ERR_CHANNEL_IN_USE` or auto-disconnect per config — default reject).

### PassThruConnect / Disconnect
- `Connect`: validate device `OPENED`, find matching target, assign channelId,
  create `Channel` bound to target, start periodic generators (`startOn:
  connect`). Returns `STATUS_NOERROR` + `*pChannelID`.
- `Disconnect`: validate channel exists, stop periodic generators, remove
  channel, return ID to pool.

### PassThruReadMsgs
- Validate channel exists. Drain up to `*pNumMsgs` from `channel.rxQueue`.
- Non-empty → `STATUS_NOERROR`, `*pNumMsgs` = count returned.
- Empty → `ERR_BUFFER_EMPTY`, `*pNumMsgs` = 0.
- Timeout (`Timeout > 0`): if queue empty, wait up to `Timeout` ms (condition
  variable) for the Scheduler to push something, then drain. `Timeout == 0` is
  non-blocking. This preserves the behavior in `1.jsonl` where the client polls
  with `Timeout=0` and gets `ERR_BUFFER_EMPTY` 48–188 times between real replies.

### PassThruWriteMsgs
- Validate channel exists. For each message in `pMsg[0..*pNumMsgs)`, run the
  bound target's `replies` rules:
  - `match.mode == "prefix"` — fire if `msg.Data` starts with `match.data` bytes.
  - `match.mode == "exact"` — full byte equality.
  - `match.mode == "regex"` — hex-string regex (optional, only if needed).
- Each matched rule enqueues a delayed reply via the Scheduler.
- The written message is not forwarded anywhere (no real bus); it only triggers
  canned replies. `*pNumMsgs` = count accepted. Returns `STATUS_NOERROR`.

### PassThruIoctl
1. Normalize `IoctlID` (numeric → check config by hex key; symbolic name resolved
   via reverse lookup if needed).
2. Look up `ConfigStore::findIoctl(id)`. Enforce `scope`:
   - `device` — require `Device.state == OPENED`; channel may or may not exist.
   - `channel` — require a valid `Channel` for `ChannelID`.
   - `any` — require `Device.state == OPENED`.
3. Resolve `return` symbolic name → numeric code.
4. Apply `output`: `"auto"` (e.g. `READ_VBATT` → `device.vbatt_mV` as 4-byte LE
   into `pOutput`), hex bytes → `memcpy(pOutput, bytes, len)`, `null`/omitted →
   skip.
5. If `consumeInput` and `IoctlID == SET_CONFIG`, log `SCONFIG_LIST` params for
   diagnostics.
6. If no config rule exists, return `ERR_NOT_SUPPORTED` (no log fallback).

### PassThruStartMsgFilter / StopMsgFilter
- `StartFilter`: validate channel, assign filterId (auto, pool-managed), record
  filter type. Returns `STATUS_NOERROR` + `*pFilterID`. (Filters don't actually
  filter in replay — all replies are delivered — but the IDs are tracked for
  state correctness and `CLEAR_MSG_FILTERS`.)
- `StopFilter`: remove filterId, return `STATUS_NOERROR`.

### PassThruReadVersion
- Return `device.firmwareVersion/dllVersion/apiVersion`. No state change.

### PassThruStartPeriodicMsg / StopPeriodicMsg
- The J2534 client-driven periodic API (distinct from scenario `periodic`
  generators). Accept, assign a MsgID, no-op (no real bus). Returns
  `STATUS_NOERROR`.

### PassThruSetProgrammingVoltage
- Return `ERR_NOT_SUPPORTED` (no real hardware).

### PassThruGetLastError
- Return thread-local last error string (unchanged).

## File layout

| File | Change |
|------|-------|
| `ConfigStore.h/.cpp` | **new** — scenario JSON loader + typed accessors |
| `Simulator.h/.cpp` | **new** — state machine, dispatch, owns Scheduler thread |
| `Scheduler.h/.cpp` | **new** — periodic + delayed-reply thread, RX queues |
| `ReplayEngine.h/.cpp` | **remove from build** (replaced by Simulator); delete files |
| `LogParser.h/.cpp` | **remove from build**; keep source on disk for a future log-to-scenario converter, do not link into DLL |
| `J2534Api.cpp` | rewire: `g_engine.*` → `g_simulator.*` (signatures identical, exports unchanged) |
| `dllmain.cpp` | load scenario path (env `REPLAY_J2534_CONFIG` or registry `ScenarioPath`), init `g_simulator` |
| `Config.h` | replace `logFilePath` with `configPath`; keep `logLevel/logOutputPath`; add `instant` flag |
| `Logger.h/.cpp` | keep (used for diagnostics) |
| `scenario.json` | **new** example config |
| `exports.def` | unchanged (same 14 exports) |

## Build & verification

- `Makefile.mingw` gains `ConfigStore.cpp Simulator.cpp Scheduler.cpp`; drops
  `ReplayEngine.o` and `LogParser.o`.
- Build: `make -f Makefile.mingw` (MinGW) or add to the MSBuild solution.
- Verify with `SampleClient` (or a small replay test harness) covering:
  1. `PassThruOpen` → deviceId assigned, version strings correct.
  2. `PassThruIoctl(device, READ_VBATT)` **before** Connect → returns configured
     vbatt (the no-connection IOCTL case).
  3. `PassThruConnect(ISO15765, CAN_ID_BOTH, 500000)` → channelId from
     `preferredChannelId`.
  4. `PassThruWriteMsgs(request)` with data matching a reply rule → after
     `delayMs`, `ReadMsgs` returns the canned reply.
  5. Periodic message appears in `ReadMsgs` at `intervalMs` cadence with no write
     trigger.
  6. `PassThruReadMsgs` with empty queue and `Timeout=0` → `ERR_BUFFER_EMPTY`.
  7. Unknown vendor IOCTL `0x10ECB` → returns `ERR_INVALID_IOCTL_ID` from config.
  8. State machine: `PassThruDisconnect` on a non-existent channel →
     `ERR_INVALID_CHANNEL_ID`.
  9. Multi-session: `Disconnect` then `Connect` again with same params → fresh
     channel, reply rules still fire; periodic restarted.
  10. Multi-target: `Connect` with different baud → binds to a different target
      with its own replies.

## Migration

There is no automatic migration from `1.jsonl` to `scenario.json` in this phase.
The example `scenario.json` is hand-authored from the patterns observed in
`1.jsonl` (the `00-00-06-02-10-03` → `00-00-04-80-…` UDS diagnostic session
exchange and the `3E-00`/`7E-00` tester-present heartbeat).

A future `log2scenario` tool (using the retained `LogParser` source) could
generate a starter scenario from a captured log, but that is out of scope for
this redesign.

## Open items (to resolve during implementation)

- **Timeout semantics in `ReadMsgs`**: implement condition-variable wait vs.
  simple polling sleep. Lean toward condition variable for clean shutdown.
- **Regex match mode**: defer until a concrete use case appears; ship prefix +
  exact only.
- **Filter behavior**: currently a no-op (all replies delivered). If a scenario
  needs real filtering (e.g. BLOCK filter suppressing certain replies), add
  later.
- **Multi-device scenarios**: current schema has one implicit device. If a future
  scenario needs the probe to expose multiple DeviceIDs, wrap `device` in an
  array and key by an index or name. Out of scope for now.
