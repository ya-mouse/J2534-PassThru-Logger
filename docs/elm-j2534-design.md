# ElmJ2534 — ELM327 Bluetooth J2534 DLL

Design spec for `ElmJ2534/`, a Windows x86 J2534 v04.04 DLL that drives an
ELM327-compatible OBD-II adapter over a Bluetooth SPP COM port. Structured
like `ReplayJ2534/`: 14 J2534 exports, Docker mingw build, registry config,
`install.reg`/`uninstall.reg`, native-testable sans-IO core.

## Bench topology (E2E)

```
Windows laptop                          CAN bus                 Mac (this laptop)
┌─────────────────────────┐        ┌──────────────┐        ┌─────────────────────────┐
│ j2534_elm_test.exe      │        │ ELM327 v1.5  │        │ Feather M4 CAN Express  │
│   └▶ ElmJ2534.dll       │  BT    │ (OBD dongle) │  CAN   │  adapter mode (gs_usb)  │
│        └▶ COM3 (SPP)  ──┼───────▶│  on 12V bench├────────┤        ▲ USB            │
└─────────────────────────┘        └──────────────┘        │ candroid-replay --live  │
                                                           │  (scenario simulation)  │
                                                           └─────────────────────────┘
```

The Mac runs the ECU simulation (`candroid-replay <scenario> --live` in
`/Users/mouse/RPM/dacode/rust/candroid-fw`), the M4 board is the wire, and the
Windows side is the tester through this DLL. Verified with the Python
reference tester (`candroid-fw/tools/elm327.py`): single-frame `0100` and
multi-frame VIN `0902` both answer over COM3.

## Protocol facts (from CanDroid `docs/elm327.md` + `tools/elm327.py`)

- Half-duplex, **one command per `>` prompt** — write, read to next `>`,
  only then write again. No pipelining, no send window.
- Lines end with CR (`ATL0`); `>` can arrive with no CR before it.
- `ATS0` strips spaces — every token comparison is space-free
  (`NO DATA` → `NODATA`).
- Init order is load-bearing (ATSP clears header/filter state):
  `ATZ ATE0 ATL0 ATS0 ATH1 ATCAF1 ATSP6 ATAT1 ATST32`, then addressing
  `ATSH<req> ATCRA<resp> ATFCSH<req> ATFCSD300000 ATFCSM1`.
- With `ATH1`+`ATCAF1` the chip adds TX PCI and performs flow control, but
  **prints raw RX frames** with a 3-hex-char CAN ID prefix:
  `7E81014490201574444` = id 0x7E8, first frame, 0x14 bytes. The host
  reassembles. `ATH0`-shape (`0:6103...`, chip pre-assembled) is supported
  as a fallback for clones that ignore `ATH1`.
- `ATFCSM1` is the fragile one: clones answer OK then ignore it; symptom is
  multi-frame reads stalling with no error. Truncated assemblies are
  reported as errors, never returned partially.
- Error classes → recovery: `NODATA`/`STOPPED` → RETRY;
  `SEARCHING...` → WAIT (not a failure); `CANERROR`/`BUSERROR`/
  `UNABLETOCONNECT` (+ clone spelling `NABLETO`) → RESET_PROTOCOL (`ATPC`,
  re-assert `ATSP6`); `DATAERROR`/`BUFFERFULL`/`<RXERROR` → WARM_START
  (`ATWS` + re-init); `?` → UNSUPPORTED (skip); `LVRESET` → REINITIALISE.
- Opening a BT SPP port **blocks inside the Windows BT stack** and never
  raises (unpowered dongle or stale link key). Open on a worker thread with
  a deadline; never in `DllMain`.
- Windows makes **two** SPP ports per pairing; the `LOCALMFG&0000` one is
  this PC's own incoming server and hangs forever if opened. Auto-detect
  must pick the port whose instance id carries the remote MAC (`BTHENUM`
  without `LOCALMFG`).
- Clones emit junk bytes on reset (0xFF); drop non-printables at byte level.
  Echo is suppressed in software even after `ATE0`. Lines starting `+` are
  unsolicited connection chatter — dropped. 500 ms settle after port open.
- `ATRV` → `"12.3V"` for READ_VBATT. `ATLP` sleeps the adapter on shutdown.

## J2534 mapping

| J2534 | ElmJ2534 behavior |
|---|---|
| `PassThruOpen` | Lazy: open COM port (threaded, deadline, default 15 s) + full ELM init. Fail → `ERR_DEVICE_NOT_CONNECTED`. |
| `PassThruConnect` | ISO15765 only, 500000 baud only (ATSP6 = 11-bit/500k). Others → `ERR_INVALID_PROTOCOL_ID` / `ERR_INVALID_BAUDRATE`. |
| `PassThruWriteMsgs` | Per msg: Data[0..3] = big-endian CAN ID, Data[4..] = payload. `setTarget(id)` (ATSH+ATFCSH when changed), send hex payload, read to prompt, classify, reassemble all ISO-TP groups, push TX echo (when `CAN_ID_BOTH`) + responses to channel RX queue. **Synchronous** — the exchange completes before return. `NODATA` is not a write failure: TX succeeded, nothing answered. Bus errors → `ERR_FAILED` after recovery attempt. |
| `PassThruReadMsgs` | Drain RX queue; block on condition variable up to `Timeout`; empty after timeout → `ERR_TIMEOUT`, `*pNumMsgs=0`. Never exceeds caller's `*pNumMsgs` capacity. |
| `PassThruStartMsgFilter` | FLOW_CONTROL_FILTER: pattern CAN ID → `ATCRA<resp>`; flow-control msg CAN ID recorded as default request ID. PASS/BLOCK: recorded, applied as RX post-filter. NULL mask/pattern → `ERR_NULL_PARAMETER` (project convention, P-LIVE-002). |
| `PassThruIoctl` | `READ_VBATT` → `ATRV` (device- or channel-level handle). `CLEAR_RX_BUFFER` → drain queue. `GET_CONFIG` reads the `pInput` SCONFIG_LIST (project convention) → `DATA_RATE=500000`, `LOOPBACK=0`; unknown params → `ERR_INVALID_IOCTL_VALUE`. `SET_CONFIG` accepts `DATA_RATE`/`LOOPBACK`/`ISO15765_BS`/`ISO15765_STMIN` as validated no-ops (the chip owns flow-control timing); unknown params → `ERR_INVALID_IOCTL_VALUE`. `FIVE_BAUD_INIT`/`FAST_INIT` → `ERR_NOT_SUPPORTED` (no K-line). `CLEAR_MSG_FILTERS` → drop channel filters. Others → `ERR_INVALID_IOCTL_ID`. |
| `PassThruReadVersion` | firmware = ELM banner (`ATI`/`ATZ`), dll/api = fixed version strings. |
| `PassThruSetProgrammingVoltage` | `ERR_NOT_SUPPORTED`. |
| Periodic msgs | Accepted, no-op (like ReplayJ2534) — ELM has no free-running TX. |
| `PassThruClose`/`Disconnect` | `ATLP` + close port on device close. |

RX `PASSTHRU_MSG` convention (mirrors ReplayJ2534 `Simulator.cpp`):
`ProtocolID=J2534_ISO15765`, `Data` = 4-byte big-endian response CAN ID +
reassembled payload, `DataSize = ExtraDataIndex = 4 + payloadLen`,
`Timestamp` = ms since session start, `RxStatus=0` (TX echo:
`TX_MSG_TYPE|TX_INDICATION` = 0x0009, 4-byte ID only — matches the real
device behavior recorded in ReplayJ2534 commit 835f6b5).

`START_OF_MESSAGE` is **not emitted**: the ELM path hands the app an
already-reassembled ISO-TP message, so there is no wire-level arrival to
bracket (Simulator emits SOM because it fakes wire-level arrival). Gate:
if Xentry is ever run against ElmJ2534.dll and times out on multi-ECU
discovery, re-evaluate (SOM was mandatory for Xentry on the replay path).

FLOW_CONTROL filter lifecycle: the pattern CAN ID becomes the expected
response ID (`ATCRA`). When the filter is removed or was never installed,
the session must emit `ATCRA000` (accept-all) if a CRA was previously
set — **omitting** ATCRA leaves the chip's stale filter in force and the
response side silently shut.

## Configuration

Priority: env `ELM_J2534_PORT` → registry `HKCU\Software\ElmJ2534`
(`ComPort` REG_SZ, `BaudRate` REG_DWORD default 38400 — ignored by BT SPP
but the port wants one, `LogLevel`, `LogOutputPath`, `OpenTimeoutSec`
default 15) → best-effort auto-detect (SetupDi `GUID_DEVINTERFACE_COMPORT`,
pick `BTHENUM` hardware id without `LOCALMFG`, deterministic sorted pick
when several pairings exist; if SetupDi misbehaves under mingw,
auto-detect may be dropped — env/registry always work).
Env overrides: `ELM_J2534_BAUD`, `ELM_J2534_LOGLEVEL`,
`ELM_J2534_OPENTIMEOUT`. `\\.\COMx` spellings normalize to `COMx`.

## File layout

```
ElmJ2534/
├── J2534Defs.h        # copied from ReplayJ2534 (ELMJ2534_EXPORTS / ELM_API macro)
├── exports.def        # LIBRARY ElmJ2534, same 14 exports @1..@14
├── Config.h           # ElmConfig struct + globals + setLastError/getLastError
├── Logger.h/.cpp      # ReplayLogger pattern (class ElmLogger, g_logger)
├── ElmProto.h/.cpp    # sans-IO core: parser, classifier, ISO-TP assembler,
│                      #   command builders, init plan  (NO windows.h)
├── ElmLink.h/.cpp     # IElmLink interface + SerialElmLink (Win32 COM, threaded open)
├── PortScan.h/.cpp    # best-effort BTHENUM COM auto-detect (SetupDi)
├── ElmSession.h/.cpp  # exchange loop, initialise, setTarget, request, recover
├── ElmDevice.h          # J2534 state machine (device/channels/filters/RX queues)
├── ElmDevicePriv.h      # shared plumbing for the split TUs (BE id helpers, log shim)
├── ElmDevice.cpp        # lifecycle: open/close/connect/disconnect/version
├── ElmDeviceMsgs.cpp    # writeMsgs/readMsgs, exchange recovery, RX queue/echo
├── ElmDeviceFilters.cpp # msg filters + ioctl
├── J2534Api.cpp       # 14 wrappers → g_device (ReplayJ2534 pattern)
├── dllmain.cpp        # DllMain: config load + logger only (NO port open)
├── install.reg        # PassThruSupport.04.04\ElmJ2534 (ISO15765=1) + HKCU config
├── uninstall.reg
├── Makefile.mingw     # x86, no -march=pentium3 (P-LIVE-001), -ladvapi32 -lsetupapi
└── tests/
    ├── test_elmproto.cpp    # native (clang++ on macOS): parser/classifier/assembler
    ├── test_session.cpp     # native FakeLink suite: exchange/init/setTarget/recover
    ├── test_device.cpp      # native FakeLink suite: J2534 state machine (+ Win32-only
    │                        #   two-thread readMsgs-cancel regression, mingw build)
    ├── fake_link.h          # scripted FakeElmLink shared by session + device suites
    ├── stubs/windows.h      # Win32 stubs (lock/condvar/tick) for native device tests
    ├── j2534_elm_test.cpp   # E2E client: 0100 single-frame + 0902 multi-frame VIN
    ├── Makefile.native      # native suites (no Docker)
    ├── Makefile.test        # mingw cross-build of the three test exes
    └── Makefile.client      # mingw cross-build of j2534_elm_test.exe
```

## ElmProto interfaces (T1 contract)

```cpp
enum class ElmResponseKind { Data, Text, Ok, Prompt, Banner, Waiting, Failure };
enum class ElmRecovery { None, Retry, Wait, ResetProtocol, WarmStart,
                         Unsupported, Reinitialise };
struct ElmResponse { ElmResponseKind kind; std::string raw; ElmRecovery recovery; };

// One cleaned line → response. echoOf = command sent (drop its echo).
// Returns false when the line is chatter ('+' lines, empty) → ignore.
bool elmClassifyLine(const std::string &line, const std::string &echoOf,
                     ElmResponse &out);

class ElmLineParser {           // feed raw bytes → complete lines
public:                         // drops non-printables; '>' becomes a line
    void reset();               // even without preceding CR
    void feed(const char *data, int len, std::vector<std::string> &lines);
};

struct ElmAssembly {
    bool ok;                    // complete message assembled
    bool truncated;             // declared len > received → NEVER return partial
    uint32_t canId;             // response CAN id (ATH1 header; 0 for ATH0 shape)
    std::vector<uint8_t> payload;
};

// Groups ATH1-shaped frames by CAN ID; assembles each group (single/first/
// consecutive, SN checked, out-of-order dropped). ATH0 "N:hex" lines
// accumulate into one assembly with canId = defaultCanId.
std::vector<ElmAssembly> elmAssembleAll(const std::vector<std::string> &dataLines,
                                        uint32_t defaultCanId);

std::string elmHexRequest(const uint8_t *payload, int len); // → "0902"
std::string elmCmdSetHeader(uint32_t canId);                // → "ATSH7DF"
std::string elmCmdSetFilter(uint32_t canId);                // → "ATCRA7E8"
std::string elmCmdFcHeader(uint32_t canId);                 // → "ATFCSH7DF"
bool elmParseVoltage(const std::string &text, int &millivolts); // "12.3V" → 12300

struct ElmStep { const char *command; bool required; const char *purpose; };
const std::vector<ElmStep> &elmInitSequence();
// ATSH<req> ATCRA<resp> ATFCSH<req> ATFCSD300000 ATFCSM1
// (ATCRA omitted when responseCanId == 0 — the SESSION layer then emits
//  ATCRA000 itself when a CRA was previously set, so a stale chip filter
//  can never survive a filter removal)
std::vector<ElmStep> elmAddressingSequence(uint32_t requestCanId,
                                           uint32_t responseCanId);
```

Critical init commands (required=true; `?` or non-OK fails init):
`ATE0 ATH1 ATCAF1 ATSP6 ATFCSH ATFCSD`. `ATZ` is handled by the session
(6 s timeout, `ATI` banner fallback).

## ElmSession interfaces (T3 contract)

```cpp
class IElmLink {                       // injected; FakeLink in tests
public:
    virtual ~IElmLink() {}
    virtual bool open(const char *port, int baud, int openTimeoutSec,
                      std::string &err) = 0;
    virtual void close() = 0;
    virtual bool write(const char *data, int len) = 0;
    virtual int  read(uint8_t *buf, int maxLen, int timeoutMs) = 0; // 0=timeout, -1=err
    virtual void discardInput() = 0;
};

class ElmSession {
public:
    explicit ElmSession(IElmLink &link);
    bool open(const char *port, int baud, int openTimeoutSec, std::string &err);
    bool initialise(std::string &banner, std::string &err); // ATZ + plan + 7DF/7E8
    bool setTarget(uint32_t reqCanId, uint32_t respCanId, std::string &err);
    bool request(const uint8_t *payload, int len, int timeoutMs,
                 std::vector<ElmAssembly> &out, std::string &err,
                 ElmRecovery *recoveryOut);
    bool readVoltageMillivolts(int &mv, std::string &err);
    bool recover(ElmRecovery kind, std::string &err);
    void shutdown();                     // ATLP + link.close()
    const std::string &banner() const;
};
```

Timeouts: default exchange 5 s, reset 6 s (clone worst case ~1.5 s),
shutdown 1 s. Exchange = discardInput → write `cmd + "\r"` → read/parse/
classify until `Prompt` or deadline; deadline with no prompt = link wedged
→ err "no prompt".

## Constraints

- x86 only, C++11, no external deps (Win32 + SetupAPI only), tabs/Win32
  style, ≤500 LOC target / 700 hard max per file.
- Never `-march=pentium3` (P-LIVE-001); test exes link `-static`.
- J2534 constants only from `J2534Defs.h` (P-LIVE-002):
  `J2534_ISO15765=0x06`, `READ_VBATT=0x03`, `CAN_ID_BOTH=0x0800`,
  `FLOW_CONTROL_FILTER=0x03`, `ISO15765_FRAME_PAD=0x0040`.
- All ELM exchanges serialize under one lock (half-duplex hardware).
- No blocking work in `DllMain` ATTACH (config load + logger init only).
  DETACH carve-out: when the app skipped `PassThruClose`, a **bounded**
  (≤2 s worst case: write timeout + 1 s read deadline) `ATLP` + close is
  allowed on the `FreeLibrary` path only
  (`lpReserved == NULL`, via `TryEnterCriticalSection` — never block on a
  lock another terminated thread may hold). On process exit
  (`lpReserved != NULL`) the OS has already terminated other threads:
  `CloseHandle` only, no exchange, no lock — otherwise exit hangs.
- Wrap-safe tick deltas: `(int32_t)(deadline - now)` with DWORD
  arithmetic — never `long` (64-bit on LP64 native hosts misreads wrapped
  diffs as huge positives; see pitfalls-live).

## E2E acceptance (bench is live)

**Bench-verified 2026-09-11: 4/4 PASS** (procedure:
`.agents/knowledge/workflows/elm327-bench-e2e.md`).

1. `make test-elm-native` — parser/classifier/assembler tests pass on macOS.
2. `make elm` — Docker mingw build → `build/Release/ElmJ2534.dll`.
3. `make test-elm` — session/device FakeLink tests build; run on Windows.
4. Windows: merge `install.reg` (COM3), Mac: `candroid-replay
   fw/scenarios/obd2.json --live` (adapter mode), Windows:
   `j2534_elm_test.exe ElmJ2534.dll` →
   `0100` → `41 00 18 3B 00 11`, `0902` → `49 02 01 "WDD2040011A123456"`
   (multi-frame with real flow control), VBATT ≈ 12 V, PASS summary.
