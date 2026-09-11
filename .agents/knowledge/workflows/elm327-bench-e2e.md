# Workflow: ElmJ2534 Bench E2E (Mac simulation ↔ Windows tester)

End-to-end test of `ElmJ2534.dll` against a live ECU simulation. No car, no
real ECU: the Mac runs the scenario replay through an M4 CAN Express board
(adapter mode = the wire), and the Windows laptop is the tester through an
ELM327 Bluetooth clone.

```
Windows (192.168.1.133)                CAN bus              Mac
┌──────────────────────────┐      ┌───────────────┐   ┌──────────────────────────┐
│ j2534_elm_test.exe       │      │ ELM327 v1.5   │   │ Feather M4 CAN Express   │
│  └▶ ElmJ2534.dll         │  BT  │ (OBD dongle,  │CAN│  adapter mode (gs_usb)   │
│       └▶ COM3 (SPP) ─────┼─────▶│  12V bench)   ├───┤   ▲ USB                  │
└──────────────────────────┘      └───────────────┘   │ candroid-replay --live   │
                                                      │  (../rust/candroid-fw)   │
                                                      └──────────────────────────┘
```

Verified 2026-09-11: 4/4 PASS — `0100` → `41 00 18 3B 00 11` (single frame),
`0902` → VIN `WDD2040011A123456` (multi-frame, real flow control `30 00 00`
visible on the adapter trace), VBATT 12.1 V, `ATLP` clean close.

## Prerequisites

- M4 board on Mac USB, in **adapter mode**: `cd /Users/mouse/RPM/dacode/rust/candroid-fw && python3 tools/shell.py mode` (must say `running adapter`; switch with `python3 tools/shell.py "mode adapter"`)
- ELM327 dongle powered from the 12 V bench supply, CAN wired to the board
  (CANH–CANH, CANL–CANL, **grounds common**, 120 Ω at each physical end)
- Dongle paired with the Windows laptop; its SPP port is the `BTHENUM`
  COM port **without** `LOCALMFG` in the instance id (currently COM3)
- `candroid-replay` built: `cargo build -p candroid-replay`

## Procedure

```bash
# 1. Mac — build + deploy (repo root: J2534-PassThru-Logger)
make elm elm-client test-elm
for f in build/Release/ElmJ2534.dll build/Release/install.reg build/Release/uninstall.reg \
         build/tests/j2534_elm_test.exe build/tests/test_device.exe \
         build/tests/test_session.exe build/tests/test_elmproto.exe; do
  scp -O "$f" "DESKTOP-JGDBINM\\fartud@192.168.1.133:Downloads/PassThruLogger/"
done    # scp -O is MANDATORY (cygwin); see remote-windows-debugging.md

# 2. Windows — register (one-time; HKCU config ComPort=COM3 + HKLM PassThru device)
ssh "DESKTOP-JGDBINM\\fartud@192.168.1.133" 'c:\cygwin64\bin\bash -lc \
  "cd ~/Downloads/PassThruLogger && reg import install.reg && \
   reg query HKCU\\\\Software\\\\ElmJ2534"'

# 3. Windows — unit suites (no hardware needed)
ssh ... '"cd ~/Downloads/PassThruLogger && ./test_elmproto.exe && \
  ./test_session.exe && ./test_device.exe"'      # 34 + 20 + 20 (Win32 build)

# 4. Mac — start the simulation FIRST (window must cover the client run)
cd /Users/mouse/RPM/dacode/rust/candroid-fw
nohup target/debug/candroid-replay fw/scenarios/obd2.json --live --trace \
  --for 240 > /tmp/e2e-replay.log 2>&1 &
sleep 3 && head -5 /tmp/e2e-replay.log           # "answering for 240 s"

# 5. Windows — run the E2E client (open does BT connect + ~14 AT commands)
ssh ... '"cd ~/Downloads/PassThruLogger && \
  ELM_J2534_LOGLEVEL=1 ./j2534_elm_test.exe ElmJ2534.dll"'
# Expect: === 4/4 PASS ===

# 6. Mac — verify from the bus side (both-ends evidence)
grep -E "rx|tx" /tmp/e2e-replay.log
# Expect: 7DF 02 01 00 → 7E8 06 41 00...; 7DF 02 09 02 → 7E8 10 14 49 02...
#         → 7DF 30 00 00 (the dongle's flow control) → 7E8 21/22 consecutive frames

# 7. Mac — put the board back when done
python3 tools/shell.py "mode replay"
```

Against the W204 capture instead of `:obd2`-style traffic: replay
`scenario.json` on the Mac and pass the ids to the client:
`./j2534_elm_test.exe ElmJ2534.dll 602 480`.

## Expected DLL behavior (reading the client log)

- `PassThruOpen` ≈ 4–5 s (RFCOMM connect + ATZ…ATFCSM1 + ATSH7DF/ATCRA7E8).
- WriteMsgs is **synchronous**: one hex-payload command per write, responses
  are already queued when it returns; ReadMsgs never blocks on the bus.
- Per write with `CAN_ID_BOTH`: TX echo (`RxStatus=0x9`, 4-byte id) then the
  reassembled response (`00 00 07 E8 …`). No `START_OF_MESSAGE` by design
  (see docs/elm-j2534-design.md — Xentry gate).
- `NODATA` is not a write failure: echo only, then `ERR_TIMEOUT` on read.
- Recovery is automatic and single-shot: `CANERROR` → `ATPC`+`ATSP6`+re-address
  +one retry; wedged link (no `>`) → reinit/reopen → `ERR_DEVICE_NOT_CONNECTED`.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Open hangs ~15 s then fails | BT stack blocked in CreateFile: dongle unpowered or stale link key. Check System event log `BTHUSB` (16+37 = stale key → unpair BOTH sides, power-cycle, re-pair). The DLL's error text says this. |
| Opened the wrong port | `LOCALMFG&0000` COM port is the PC's own incoming SPP server — hangs forever. Pick the port whose instance id carries the remote MAC. Config: `ELM_J2534_PORT` env > `HKCU\Software\ElmJ2534\ComPort` > auto-scan. |
| Single-frame OK, every multi-frame times out | Clone fakes `ATFCSM1`. DLL reports `truncated multi-frame reply…` via `PassThruGetLastError` and never delivers partial payloads. Try another dongle. |
| `CAN ERROR` on every write | Nothing ACKs: replayer not `--live` (or `--for` window lapsed — it takes the transceiver with it), board not in adapter mode, wiring/termination, or **12 V supply ground not common**. Power-cycle order for the dongle: 12 V down → attach → 12 V up. |
| Works from Python, not from the DLL | Another process holds COM3 — BT SPP is single-client. Close `elm327.py`/serial monitors first. |
| Suite passes natively, fails on Windows | If `open_failure_paths` fails with `got 0`: a paired dongle on the test machine leaked into the scan — fixed by the testLink_ guard (commit c6bccb1); make sure the deployed exe is current. |

## Related

- `docs/elm-j2534-design.md` — DLL design spec (protocol facts, J2534 mapping)
- `.agents/knowledge/workflows/remote-windows-debugging.md` — SSH/scp/cygwin basics
- `.agents/knowledge/workflows/replay-scenario-authoring.md` — scenario format
- `/Users/mouse/RPM/dacode/rust/candroid-fw/README.md` — adapter mode, `--live`, `--trace`
- `/Users/mouse/RPM/dacode/CanDroid/docs/elm327.md` — ELM327 protocol deep-dive
