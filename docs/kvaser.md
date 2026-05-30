# J2534 Proxy Shim — Auto-Connect Injection Plan

## Goal

Make a pass-through proxy DLL (sitting between Xentry and the real Kvaser
`j2534api.dll`) tolerate a client that issues a **channel-scoped `PassThruIoctl`
(e.g. `READ_VBATT`) before any `PassThruConnect`**. The shim transparently:

1. Detects the premature channel-scoped call.
2. Lazily injects a `PassThruConnect` to obtain a real channel ID.
3. Substitutes the client-supplied **device ID** with the **real channel ID**
   on channel-scoped calls.
4. Schedules `PassThruDisconnect` of the injected channel before `PassThruClose`.

> ⚠️ Before building this: try the simpler path first — intercept `READ_VBATT`
> alone and return a synthetic voltage. If that gets Xentry past its probe, you
> need none of the machinery below. This plan is for the case where Xentry
> requires a genuinely connected channel.

---

## Core concepts

### Handle namespaces in J2534
- **Device ID**: returned by `PassThruOpen`. Scope = the physical adapter.
- **Channel ID**: returned by `PassThruConnect`. Scope = one protocol channel
  on that device.

Both are opaque `unsigned long` handles. The bug is that the client reuses the
**device ID** where a **channel ID** is required.

### Which calls are device-scoped vs channel-scoped
You must **not** blindly rewrite every occurrence of the device ID. Maintain an
explicit classification:

| Function                     | Scope            | Rewrite handle? |
|------------------------------|------------------|-----------------|
| `PassThruOpen`               | device (creates) | n/a             |
| `PassThruClose`              | device           | **No**          |
| `PassThruConnect`            | device (creates) | **No**          |
| `PassThruDisconnect`         | channel          | **Yes**         |
| `PassThruReadMsgs`           | channel          | **Yes**         |
| `PassThruWriteMsgs`          | channel          | **Yes**         |
| `PassThruStartPeriodicMsg`   | channel          | **Yes**         |
| `PassThruStopPeriodicMsg`    | channel          | **Yes**         |
| `PassThruStartMsgFilter`     | channel          | **Yes**         |
| `PassThruStopMsgFilter`      | channel          | **Yes**         |
| `PassThruReadVersion`        | device           | **No**          |
| `PassThruIoctl`              | **depends on IoctlID** | see below |

### `PassThruIoctl` is split by `IoctlID`
- **Channel-scoped IoctlIDs** (rewrite handle): `GET_CONFIG`, `SET_CONFIG`,
  `READ_VBATT` *(on Kvaser)*, `FIVE_BAUD_INIT`, `FAST_INIT`,
  `CLEAR_TX_BUFFER`, `CLEAR_RX_BUFFER`, `CLEAR_PERIODIC_MSGS`,
  `CLEAR_MSG_FILTERS`, `CLEAR_FUNCT_MSG_LOOKUP_TABLE`,
  `ADD_TO_FUNCT_MSG_LOOKUP_TABLE`, `DELETE_FROM_FUNCT_MSG_LOOKUP_TABLE`.
- **Device-scoped IoctlIDs** (do NOT rewrite): `READ_PROG_VOLTAGE`,
  `SET_PROG_VOLTAGE`, and other programming-voltage / device-global controls.

> The whole point of the bug: on Kvaser, `READ_VBATT` is channel-scoped, while
> on OEM adapters it is effectively device-scoped. That single mismatch is what
> you're bridging.

---

## State to maintain in the shim

Per opened device, keep a small record:

```c
typedef struct {
    unsigned long deviceId;        // real device ID from PassThruOpen
    unsigned long injectedChannel; // channel ID from our injected Connect, or 0
    int           injected;        // bool: have we injected a Connect?
    unsigned long clientChannel;   // channel ID the client later opened itself, or 0
    CRITICAL_SECTION lock;         // injection must be idempotent / thread-safe
} DeviceMap;
```

Key invariants:
- `injectedChannel` is created **at most once** per device (refcount or bool guard).
- Once `clientChannel != 0` (client did its own Connect), **stop rewriting** —
  the client now owns a valid channel and passes it correctly. Optionally tear
  down `injectedChannel` at that point to avoid a leaked/duplicate channel.

---

## Step-by-step flow

### Step 1 — `PassThruOpen` (pass through, record)
- Forward unchanged to the real DLL.
- On `STATUS_NOERROR`, create a `DeviceMap` keyed by the returned device ID.
- Init `injected = 0`, `injectedChannel = 0`, `clientChannel = 0`.

### Step 2 — `PassThruReadVersion` (pass through)
- Device-scoped; forward unchanged. No rewriting.

### Step 3 — Intercept channel-scoped call arriving with a device ID
On any channel-scoped call (table above), inspect the incoming handle:

```
if (handle == map->deviceId && map->clientChannel == 0) {
    // client used the device ID where a channel is required → inject
    ensureInjectedChannel(map);
    handle = map->injectedChannel;   // substitute
}
else if (handle == map->deviceId && map->clientChannel != 0) {
    handle = map->clientChannel;     // client has its own channel; use it
}
// else: handle is already a real channel ID → leave as-is
```

### Step 4 — `ensureInjectedChannel` (lazy, idempotent)
```
EnterCriticalSection(&map->lock);
if (!map->injected) {
    unsigned long ch = 0;
    long rc = real_PassThruConnect(map->deviceId,
                                   PROTOCOL,   // see "Connect params" below
                                   CONNECT_FLAGS,
                                   BAUDRATE,
                                   &ch);
    if (rc == STATUS_NOERROR) {
        map->injectedChannel = ch;
        map->injected = 1;
    } else {
        // propagate failure up; do NOT fake success here
    }
}
LeaveCriticalSection(&map->lock);
```
Idempotent because the doubled `ReadVersion`/`Ioctl` in the log shows the client
probes more than once — connect once, reuse.

### Step 5 — Forward the original call with substituted handle
- Call the real function with the rewritten channel ID.
- Return its status/output buffers to the client unchanged.

### Step 6 — Client's own `PassThruConnect` (if it happens)
- Forward unchanged; capture returned channel ID into `map->clientChannel`.
- **Decision point:**
  - **Option A (recommended):** immediately `PassThruDisconnect(injectedChannel)`
    and set `injectedChannel = 0`. From now on the client passes its own valid
    channel, so no rewriting is needed and there's only one live channel.
  - **Option B:** keep both, but you now carry two channel lifecycles — more
    race surface. Avoid unless something forces it.

### Step 7 — `PassThruDisconnect` from client
- Channel-scoped; rewrite if it still carries the device ID, else pass through.

### Step 8 — `PassThruClose` (schedule injected Disconnect first)
```
if (map->injectedChannel != 0) {
    real_PassThruDisconnect(map->injectedChannel);
    map->injectedChannel = 0;
}
long rc = real_PassThruClose(map->deviceId);
destroyDeviceMap(map);
return rc;
```
Always disconnect the injected channel **before** Close so you don't leak a
channel or trip "channel still open" errors inside the Kvaser driver.

---

## Connect parameters — straightforward, or extra params needed?

`PassThruConnect(DeviceID, ProtocolID, Flags, BaudRate, &ChannelID)` — four
inputs matter. For the `READ_VBATT`-probe case specifically:

- **ProtocolID**: `READ_VBATT` itself doesn't care which protocol the channel
  is, but the channel must be *some* valid protocol. For Mercedes/Xentry the
  practical choice is `ISO15765` (or plain `CAN`) — that's the powertrain
  transport you'll be using anyway, so the injected channel matches the eventual
  real session.
- **BaudRate**: must be valid for the protocol. Mercedes high-speed CAN is
  **500000**. Use that for `CAN`/`ISO15765`.
- **Flags**: `0` is fine for a vanilla 500k CAN/ISO15765 channel. Set
  `CAN_29BIT_ID` only if you specifically need extended IDs; for a probe you
  don't. Do **not** set `ISO9141_NO_CHECKSUM` etc. — irrelevant here.
- **ChannelID (out)**: store it.

So: **mostly straightforward.** The only "extra" judgment is picking a protocol
+ baud that won't conflict with what Xentry connects with later. Matching it
(ISO15765 @ 500k) is the safe default. If you went with a mismatched speed and
then *forwarded* client filter/message calls onto your injected channel, you'd
get subtle failures — but if you follow Step 6 Option A (tear down on the
client's real Connect), the injected channel only ever serves the VBATT probe,
so even a generic CAN@500k channel is sufficient.

---

## Failure modes to guard against

1. **Blindly rewriting device-scoped calls** → breaks `Close`, `ReadVersion`,
   `SET_PROG_VOLTAGE`. Use the scope table.
2. **Double-connect** (yours + client's both live) → divergent protocol state.
   Fix via Step 6 Option A.
3. **Leaked injected channel** if Close happens without disconnect → Step 8.
4. **Re-entrant / repeated probes** → idempotent injection (Step 4 guard).
5. **Faking success on a failed injected Connect** → don't; propagate the real
   error so you can diagnose, rather than masking it and failing later.
6. **Wrong baud on injected channel** if you ever forward real traffic over it →
   match ISO15765 @ 500k, or tear it down early.

---

## Recommended build order

1. First implement **passthrough-only logging** and confirm the exact raw
   `READ_VBATT` return from Kvaser (error code + buffer).
2. Try **synthetic-VBATT interception only** (return `STATUS_NOERROR` + ~12000
   mV). If Xentry proceeds, **stop — you're done**, none of the channel
   machinery is needed.
3. Only if Xentry requires a real connected channel, implement the
   inject/rewrite/teardown flow above, using Step 6 Option A.
