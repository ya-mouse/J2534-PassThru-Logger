#!/usr/bin/env python3
"""
test_log2scenario.py — converter regression tests (plain python3, no framework)

Focus: reply-rule emission ORDER under first-match-wins. All three replay
engines (ReplayJ2534 Simulator.cpp, CanDroid ReplayTransport.kt, candroid-fw
index.rs) answer a write from the FIRST matching rule in document order, so
a broader rule emitted before a more specific one silently kills the latter.
The reviewer's repro shape: two PIDs answering the same NRC (7F-22-31, a
prefix-collapse candidate with LCP 00-00-06-02-22) plus one live-data PID
with >3 unique responses (sequence rule on 00-00-06-02-22-1B). Before the
fix, _collapse_prefix_rules returned singles-before-sequences and the broad
rule shadowed the sequence rule forever.

Run:  python3 ReplayJ2534/tools/test_log2scenario.py [--load-with BIN]
      --load-with: also feed each built scenario through the native
      ConfigStore test binary (`test_configstore --load <file>`) to prove
      the converter never emits a scenario the parser rejects.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from datetime import datetime, timedelta

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import log2scenario as l2s  # noqa: E402

G_RUN = 0
G_FAILED = 0

TS0 = datetime(2026, 9, 11, 12, 0, 0)


def check(name, cond, detail=""):
    global G_RUN, G_FAILED
    G_RUN += 1
    if cond:
        print(f"  {name:<58} [PASS]")
    else:
        G_FAILED += 1
        print(f"  {name:<58} [FAIL] {detail}")


def ts(ms):
    return (TS0 + timedelta(milliseconds=ms)).isoformat() + "Z"


def entry(ms, text, index=0):
    return {"timestamp": ts(ms), "text": text, "index": index, "count": 1}


def msg_text(func, data_hex, channel=2):
    n = len(data_hex.split("-"))
    return (f"{func}({channel}, "
            f"[{{ISO15765; RxStatus: 0; TxFlags: 0; TS: 00:00:00; "
            f"LEN: {n}; ExtraIndex: 0; Data: {data_hex}}}], 1=>1, 0) "
            f"-> STATUS_NOERROR")


def capture(pairs, extra_entries=()):
    """Build a synthetic log: Open/ReadVersion/Connect, then (write, reply)
    pairs at 100 ms cadence (reply +20 ms), then Disconnect."""
    entries = [
        entry(0, "PassThruOpen(NULL, 1) -> STATUS_NOERROR"),
        entry(1, 'PassThruReadVersion(1, "3.37.0", "1.0.0", "04.04") '
                 "-> STATUS_NOERROR"),
        entry(2, "PassThruConnect(1, ISO15765, CAN_ID_BOTH, 500000, 2) "
                 "-> STATUS_NOERROR"),
    ]
    t = 100
    for write_hex, replies in pairs:
        if not isinstance(replies, list):
            replies = [replies]
        for rep in replies:
            entries.append(entry(t, msg_text("PassThruWriteMsgs", write_hex)))
            entries.append(entry(t + 20,
                                 msg_text("PassThruReadMsgs", rep)))
            t += 100
    entries.extend(extra_entries)
    entries.append(entry(t, "PassThruDisconnect(2) -> STATUS_NOERROR"))
    return {"entries": entries}


def build(doc):
    with tempfile.NamedTemporaryFile("w", suffix=".jsonl", delete=False) as f:
        json.dump(doc, f)
        path = f.name
    try:
        events = l2s.load_log(path)
        b = l2s.ScenarioBuilder()
        b._service_offset = l2s.detect_service_offset(events)
        for ev in events:
            b.process_event(ev)
        return b.build()
    finally:
        os.unlink(path)


def match_bytes(rule):
    return l2s.ScenarioBuilder._hex_to_bytes(rule["match"]["data"])


def no_shadow(replies):
    """True iff no earlier rule's match-data is a byte-prefix of (or equal
    to) a later rule's — the first-match-wins dead-rule condition."""
    datas = [match_bytes(r) for r in replies]
    return not any(datas[j].startswith(datas[i])
                   for j in range(len(datas)) for i in range(j))


def find_rule(replies, data_hex):
    want = l2s.ScenarioBuilder._hex_to_bytes(data_hex)
    for i, r in enumerate(replies):
        if match_bytes(r) == want:
            return i, r
    return None, None


NRC = "00-00-04-80-7F-22-31"
LIVE = ["00-00-04-80-62-1B-%02X" % v for v in range(1, 6)]

# ── Repro A (reviewer's shape): collapse candidate + overlapping sequence ────
# Two PIDs share response 7F-22-31 → collapse group with LCP 00-00-06-02-22;
# the sequence rule 00-00-06-02-22-1B starts with that LCP, so the widened
# conflict check must SKIP the collapse — a 5-byte LCP rule would shadow the
# sequence rule under first-match-wins.
DOC_A = capture([
    ("00-00-06-02-22-1A", NRC),
    ("00-00-06-02-22-31", NRC),
    ("00-00-06-02-22-1B", LIVE),
])

# ── Repro B (exercises the sort): a broad single that is a strict prefix ────
# The bare 00-00-06-02-22 single (no collapse partner) is a byte-prefix of
# the sequence rule's match — the longest-first sort must emit the sequence
# BEFORE it, or the sequence rule is dead.
DOC_B = capture([
    ("00-00-06-02-22-1A", NRC),
    ("00-00-06-02-22-31", NRC),
    ("00-00-06-02-22-1B", LIVE),
    ("00-00-06-02-22", "00-00-04-80-62-00"),
])


def test_repro_a_no_lcp_shadow():
    replies = build(DOC_A)["targets"][0]["replies"]
    check("A: three rules emitted (collapse skipped on conflict)",
          len(replies) == 3, f"got {len(replies)}")
    lcp_i, _ = find_rule(replies, "00-00-06-02-22")
    check("A: no 5-byte LCP rule emitted (would shadow the sequence)",
          lcp_i is None)
    seq_i, seq = find_rule(replies, "00-00-06-02-22-1B")
    check("A: sequence rule present for the live-data PID",
          seq is not None and seq["response"].get("mode") == "sequence")
    check("A: no rule before the sequence shadows it",
          seq_i is not None and no_shadow(replies[:seq_i + 1]))
    check("A: full emission order shadow-free", no_shadow(replies))


def test_repro_b_sort_puts_sequence_before_broad():
    replies = build(DOC_B)["targets"][0]["replies"]
    check("B: four rules emitted", len(replies) == 4, f"got {len(replies)}")
    seq_i, seq = find_rule(replies, "00-00-06-02-22-1B")
    broad_i, _ = find_rule(replies, "00-00-06-02-22")
    check("B: sequence rule and broad single both present",
          seq_i is not None and broad_i is not None)
    check("B: sequence emitted BEFORE the broad prefix rule",
          seq_i is not None and broad_i is not None and seq_i < broad_i,
          f"seq={seq_i} broad={broad_i}")
    check("B: broad rule is last (longest-first sort)",
          broad_i == len(replies) - 1)
    check("B: full emission order shadow-free", no_shadow(replies))
    check("B: sequence rule intact (5 entries)",
          seq is not None and len(seq["response"]["sequence"]) == 5)


def test_shadow_guard_trips():
    broad = {"match": {"data": "00-00-06-02-22", "mode": "prefix"},
             "response": {"data": "AA"}}
    specific = {"match": {"data": "00-00-06-02-22-1B", "mode": "prefix"},
                "response": {"mode": "sequence", "sequence": ["BB"]}}
    try:
        l2s.ScenarioBuilder._assert_first_match_order(
            "T", [broad, specific])
        check("guard: shadowed pair raises AssertionError", False)
    except AssertionError:
        check("guard: shadowed pair raises AssertionError", True)
    try:
        l2s.ScenarioBuilder._assert_first_match_order(
            "T", [specific, broad])
        check("guard: specific-before-general passes", True)
    except AssertionError as e:
        check("guard: specific-before-general passes", False, str(e))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--load-with", metavar="BIN",
                        help="native test_configstore binary for --load checks")
    args = parser.parse_args()

    print("\n=== log2scenario converter tests ===\n")
    test_repro_a_no_lcp_shadow()
    test_repro_b_sort_puts_sequence_before_broad()
    test_shadow_guard_trips()

    if args.load_with:
        for name, doc in (("A", DOC_A), ("B", DOC_B)):
            scn = build(doc)
            with tempfile.NamedTemporaryFile("w", suffix=".json",
                                             delete=False) as f:
                json.dump(scn, f)
                path = f.name
            rc = subprocess.run([args.load_with, "--load", path],
                                capture_output=True).returncode
            os.unlink(path)
            check(f"load: repro {name} scenario parses via ConfigStore",
                  rc == 0, f"exit={rc}")

    print(f"\n=== Results: {G_RUN - G_FAILED} passed, {G_FAILED} failed, "
          f"{G_RUN} total ===\n")
    return 1 if G_FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
