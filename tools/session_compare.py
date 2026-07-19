#!/usr/bin/env python3
"""
session_compare.py — Compare two PassThruLogger viewer JSON session logs.

Usage:
    python3 session_compare.py <log_a.json> <log_b.json> [label_a] [label_b]

Shows: event counts, CAN ID coverage, unique write payloads per CAN ID,
CAN IDs only in one session, and timeline comparison.

Useful for:
- Comparing an original car-session capture with a replay session
- Checking which CAN IDs are missing from the replay (→ missing reply rules)
- Verifying that a replay covers the same ECU traffic as the original
"""

import json
import re
import sys
from collections import Counter, defaultdict
from datetime import datetime


def load_log(path):
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    entries = []
    for entry in data.get("entries", []):
        text = entry.get("text", "")
        ts_str = entry.get("timestamp", "")
        ts = None
        if ts_str:
            try:
                ts = datetime.fromisoformat(ts_str.rstrip("Z"))
            except ValueError:
                pass
        entries.append({
            "text": text,
            "ts": ts,
            "index": entry.get("index", 0),
            "count": entry.get("count", 1),
        })
    return entries


def extract_msgs(text):
    """Extract message data from a PassThruReadMsgs/WriteMsgs text line."""
    msgs = []
    for m in re.finditer(r'\{([^{}]*)\}', text):
        content = m.group(1)
        parts = [p.strip() for p in content.split(';')]
        rx = ""
        data = ""
        for part in parts:
            if part.startswith("RxStatus:"):
                rx = part.split(":", 1)[1].strip()
            elif part.startswith("Data:"):
                data = part.split(":", 1)[1].strip()
        if data:
            hex_parts = data.split('-')
            can_id = '-'.join(hex_parts[:4]).upper() if len(hex_parts) >= 4 else None
            service = hex_parts[4].upper() if len(hex_parts) > 4 else None
            is_loopback = "TX_MSG_TYPE" in rx or "TX_INDICATION" in rx
            is_som = "0x0002" in rx or rx == "2" or "START_OF_MESSAGE" in rx
            msgs.append({
                "can_id": can_id,
                "service": service,
                "data": data,
                "rx": rx,
                "is_loopback": is_loopback,
                "is_som": is_som,
            })
    return msgs


def categorize_entries(entries):
    """Categorize log entries into a structured summary."""
    stats = {
        "funcs": Counter(),
        "writes_by_can": defaultdict(list),
        "reads_by_can": defaultdict(list),
        "can_ids_seen": set(),
        "connects": [],
        "disconnects": [],
        "filters": [],
    }
    for e in entries:
        text = e["text"]
        if text.startswith("Client:") or text.startswith("Driver:"):
            stats["funcs"]["meta"] += e["count"]
            continue
        arrow = text.find(" -> ")
        if arrow < 0:
            continue
        call = text[:arrow].strip()
        ret = text[arrow + 4:].strip()
        paren = call.find("(")
        if paren < 0:
            continue
        func = call[:paren].strip()
        stats["funcs"][func] += e["count"]
        if func == "PassThruConnect":
            stats["connects"].append(e)
        elif func == "PassThruDisconnect":
            stats["disconnects"].append(e)
        elif func == "PassThruStartMsgFilter":
            stats["filters"].append(e)
        elif func in ("PassThruWriteMsgs", "PassThruReadMsgs"):
            for msg in extract_msgs(text):
                cid = msg["can_id"]
                if cid:
                    stats["can_ids_seen"].add(cid)
                    if func == "PassThruWriteMsgs":
                        stats["writes_by_can"][cid].append(msg["data"])
                    elif not msg["is_loopback"] and not msg["is_som"]:
                        stats["reads_by_can"][cid].append(msg["data"])
    return stats


def compare_logs(path_a, path_b, label_a="A", label_b="B"):
    entries_a = load_log(path_a)
    entries_b = load_log(path_b)
    stats_a = categorize_entries(entries_a)
    stats_b = categorize_entries(entries_b)

    print("=" * 80)
    print(f"SESSION COMPARISON: {label_a} vs {label_b}")
    print("=" * 80)

    # Basic stats
    total_a = sum(e["count"] for e in entries_a)
    total_b = sum(e["count"] for e in entries_b)
    print(f"\n{'Metric':<40} {label_a:>15} {label_b:>15}")
    print("-" * 72)
    print(f"{'Total events (with count)':<40} {total_a:>15} {total_b:>15}")
    print(f"{'Log entries':<40} {len(entries_a):>15} {len(entries_b):>15}")
    print(f"{'Connect count':<40} {len(stats_a['connects']):>15} {len(stats_b['connects']):>15}")
    print(f"{'Disconnect count':<40} {len(stats_a['disconnects']):>15} {len(stats_b['disconnects']):>15}")
    print(f"{'Filter count':<40} {len(stats_a['filters']):>15} {len(stats_b['filters']):>15}")
    for func in sorted(set(list(stats_a["funcs"].keys()) + list(stats_b["funcs"].keys()))):
        ca = stats_a["funcs"].get(func, 0)
        cb = stats_b["funcs"].get(func, 0)
        if ca > 0 or cb > 0:
            print(f"  {func:<38} {ca:>15} {cb:>15}")

    # CAN ID comparison
    all_cids = sorted(stats_a["can_ids_seen"] | stats_b["can_ids_seen"])
    print(f"\n{'='*80}")
    print("CAN ID COVERAGE")
    print(f"{'='*80}")
    print(f"\n{'CAN ID':<14} {'wr A':>6} {'wr B':>6} {'rd A':>6} {'rd B':>6} {'uniq wr A':>10} {'uniq wr B':>10} {'uniq rd A':>10} {'uniq rd B':>10}  notes")
    print("-" * 110)
    for cid in all_cids:
        wa = len(stats_a["writes_by_can"].get(cid, []))
        wb = len(stats_b["writes_by_can"].get(cid, []))
        ra = len(stats_a["reads_by_can"].get(cid, []))
        rb = len(stats_b["reads_by_can"].get(cid, []))
        uwa = len(set(stats_a["writes_by_can"].get(cid, [])))
        uwb = len(set(stats_b["writes_by_can"].get(cid, [])))
        ura = len(set(stats_a["reads_by_can"].get(cid, [])))
        urb = len(set(stats_b["reads_by_can"].get(cid, [])))
        flag = ""
        if wa > 0 and wb == 0:
            flag = "← only in " + label_a
        elif wa == 0 and wb > 0:
            flag = "← only in " + label_b
        elif wa > 0 and wb > 0 and uwa != uwb:
            flag = "← different payloads"
        print(f"{cid:<14} {wa:>6} {wb:>6} {ra:>6} {rb:>6} {uwa:>10} {uwb:>10} {ura:>10} {urb:>10}  {flag}")

    # CAN IDs only in one session
    only_a = sorted(stats_a["can_ids_seen"] - stats_b["can_ids_seen"])
    only_b = sorted(stats_b["can_ids_seen"] - stats_a["can_ids_seen"])
    print(f"\n{'='*80}")
    print(f"CAN IDs ONLY IN {label_a} ({len(only_a)} IDs — missing from {label_b})")
    print(f"{'='*80}")
    for cid in only_a:
        wa = len(stats_a["writes_by_can"].get(cid, []))
        ra = len(stats_a["reads_by_can"].get(cid, []))
        print(f"  {cid}: {wa} writes, {ra} reads")
    if not only_a:
        print("  (none)")

    print(f"\n{'='*80}")
    print(f"CAN IDs ONLY IN {label_b} ({len(only_b)} IDs — missing from {label_a})")
    print(f"{'='*80}")
    for cid in only_b:
        wb = len(stats_b["writes_by_can"].get(cid, []))
        rb = len(stats_b["reads_by_can"].get(cid, []))
        print(f"  {cid}: {wb} writes, {rb} reads")
    if not only_b:
        print("  (none)")

    # Unique write payloads comparison (per CAN ID)
    print(f"\n{'='*80}")
    print("UNIQUE WRITE PAYLOAD DIFFERENCES (per CAN ID)")
    print(f"{'='*80}")
    any_diff = False
    for cid in all_cids:
        uwa = set(stats_a["writes_by_can"].get(cid, []))
        uwb = set(stats_b["writes_by_can"].get(cid, []))
        if uwa == uwb:
            continue
        only_in_a = uwa - uwb
        only_in_b = uwb - uwa
        if not only_in_a and not only_in_b:
            continue
        any_diff = True
        print(f"\n  CAN ID {cid}:")
        if only_in_a:
            print(f"    Only in {label_a} ({len(only_in_a)} unique):")
            for w in sorted(only_in_a)[:5]:
                print(f"      {w[:80]}")
            if len(only_in_a) > 5:
                print(f"      ... and {len(only_in_a) - 5} more")
        if only_in_b:
            print(f"    Only in {label_b} ({len(only_in_b)} unique):")
            for w in sorted(only_in_b)[:5]:
                print(f"      {w[:80]}")
            if len(only_in_b) > 5:
                print(f"      ... and {len(only_in_b) - 5} more")
    if not any_diff:
        print("  (no differences — all write payloads match)")

    # Timeline
    if entries_a and entries_b:
        ts_a = entries_a[0]["ts"]
        ts_b = entries_b[0]["ts"]
        ts_a_end = entries_a[-1]["ts"]
        ts_b_end = entries_b[-1]["ts"]
        if ts_a and ts_b and ts_a_end and ts_b_end:
            dur_a = (ts_a_end - ts_a).total_seconds()
            dur_b = (ts_b_end - ts_b).total_seconds()
            print(f"\n{'='*80}")
            print("TIMELINE")
            print(f"{'='*80}")
            print(f"  {label_a}: {ts_a.isoformat()} → {ts_a_end.isoformat()} ({dur_a / 60:.1f} min)")
            print(f"  {label_b}: {ts_b.isoformat()} → {ts_b_end.isoformat()} ({dur_b / 60:.1f} min)")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <log_a.json> <log_b.json> [label_a] [label_b]")
        print(f"  Compares two PassThruLogger viewer JSON exports.")
        print(f"  Shows: event counts, CAN ID coverage, unique payloads, missing IDs.")
        sys.exit(1)
    label_a = sys.argv[3] if len(sys.argv) > 3 else "A"
    label_b = sys.argv[4] if len(sys.argv) > 4 else "B"
    compare_logs(sys.argv[1], sys.argv[2], label_a, label_b)
