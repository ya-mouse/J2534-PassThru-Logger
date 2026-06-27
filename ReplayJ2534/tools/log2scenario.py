#!/usr/bin/env python3
"""
log2scenario.py — Convert PassThruLogger JSON logs to ReplayJ2534 scenario.json

Usage:
    python3 log2scenario.py <input.jsonl> [output.json]

The converter analyzes a PassThruLogger capture and produces a scenario.json
suitable for ReplayJ2534. It:

  • Extracts device metadata (firmware/dll/api versions, vbatt)
  • Builds the IOCTL table from observed PassThruIoctl calls
  • Infers targets from PassThruConnect parameters (protocol/flags/baud)
  • Pairs WriteMsgs requests with subsequent ReadMsgs replies into reply rules
  • Detects recurring ReadMsgs patterns as periodic generators
  • Generates the standard CLOSED↔OPENED state machine

The output is a starting point — review and adjust delays, periodic intervals,
and match modes before use.
"""

import json
import re
import sys
from collections import defaultdict
from datetime import datetime


# ─── J2534 constant tables (mirrors J2534Defs.h / ConfigStore.cpp) ───────────

PROTOCOLS = {
    "J1850VPW": 1, "J1850PWM": 2, "ISO9141": 3, "ISO14230": 4,
    "CAN": 5, "ISO15765": 6,
    "SCI_A_ENGINE": 7, "SCI_A_TRANS": 8, "SCI_B_ENGINE": 9, "SCI_B_TRANS": 10,
}
PROTOCOL_NAMES = {v: k for k, v in PROTOCOLS.items()}

CONNECT_FLAGS = {
    "CAN_29BIT_ID": 0x0100, "ISO9141_NO_CHECKSUM": 0x0200,
    "CAN_ID_BOTH": 0x0800, "ISO9141_K_LINE_ONLY": 0x1000,
}
CONNECT_FLAG_NAMES = {v: k for k, v in CONNECT_FLAGS.items()}

TX_FLAGS = {
    "ISO15765_FRAME_PAD": 0x0040, "WAIT_P3_MIN_ONLY": 0x0200,
    "ISO15765_ADDR_TYPE": 0x0080,
}
TX_FLAG_NAMES = {v: k for k, v in TX_FLAGS.items()}

FILTER_TYPES = {
    "PASS_FILTER": 1, "BLOCK_FILTER": 2, "FLOW_CONTROL_FILTER": 3,
}

IOCTLS = {
    "GET_CONFIG": 0x01, "SET_CONFIG": 0x02, "READ_VBATT": 0x03,
    "FIVE_BAUD_INIT": 0x04, "FAST_INIT": 0x05,
    "CLEAR_TX_BUFFER": 0x07, "CLEAR_RX_BUFFER": 0x08,
    "CLEAR_PERIODIC_MSGS": 0x09, "CLEAR_MSG_FILTERS": 0x0A,
    "CLEAR_FUNCT_MSG_LOOKUP_TABLE": 0x0B,
    "ADD_TO_FUNCT_MSG_LOOKUP_TABLE": 0x0C,
    "DELETE_FROM_FUNCT_MSG_LOOKUP_TABLE": 0x0D,
    "READ_PROG_VOLTAGE": 0x0E,
}
IOCTL_NAMES = {v: k for k, v in IOCTLS.items()}

CONFIG_PARAMS = {
    "DATA_RATE": 0x01, "LOOPBACK": 0x03, "NODE_ADDRESS": 0x04,
    "NETWORK_LINE": 0x05, "BIT_SAMPLE_POINT": 0x06, "SYNC_JUMP_WIDTH": 0x07,
    "ISO15765_BS": 0x1E, "ISO15765_STMIN": 0x1F,
    "ISO15765_BS_TX": 0x22, "ISO15765_STMIN_TX": 0x23,
    "ISO15765_WFT_MAX": 0x25,
    "CAN_MIXED_FORMAT": 0x80, "ISO15765_PAD_VALUE": 0x8028,
    "FD_CAN_DATA_PHASE_RATE": 0x8029,
}

ERROR_CODES = {
    "STATUS_NOERROR": 0x00, "ERR_NOT_SUPPORTED": 0x01,
    "ERR_INVALID_CHANNEL_ID": 0x02, "ERR_INVALID_PROTOCOL_ID": 0x03,
    "ERR_NULL_PARAMETER": 0x04, "ERR_INVALID_IOCTL_VALUE": 0x05,
    "ERR_INVALID_FLAGS": 0x06, "ERR_FAILED": 0x07,
    "ERR_DEVICE_NOT_CONNECTED": 0x08, "ERR_TIMEOUT": 0x09,
    "ERR_INVALID_MSG": 0x0A, "ERR_EXCEEDED_LIMIT": 0x0B,
    "ERR_DEVICE_IN_USE": 0x0C, "ERR_INVALID_IOCTL_ID": 0x0D,
    "ERR_BUFFER_EMPTY": 0x10, "ERR_BUFFER_FULL": 0x11,
    "ERR_BUFFER_OVERFLOW": 0x12, "ERR_CHANNEL_IN_USE": 0x13,
    "ERR_INVALID_FILTER_ID": 0x14, "ERR_NO_FLOW_CONTROL": 0x15,
    "ERR_INVALID_BAUDRATE": 0x16, "ERR_INVALID_DEVICE_ID": 0x17,
}
ERROR_NAMES = {v: k for k, v in ERROR_CODES.items()}


def lookup(name, table):
    """Look up a symbolic name in a table dict, returning the value or None."""
    return table.get(name)


def lookup_name(value, name_table):
    """Reverse-lookup a value to its symbolic name."""
    return name_table.get(value)


def parse_iso_timestamp(ts):
    """Parse an ISO 8601 timestamp like 2026-06-09T19:01:54.2258231Z."""
    ts = ts.rstrip("Z")
    try:
        return datetime.fromisoformat(ts)
    except ValueError:
        return None


# ─── Log line parser ─────────────────────────────────────────────────────────

class ParsedMsg:
    """A single PASSTHRU_MSG extracted from a log line."""
    def __init__(self):
        self.protocol_id = 0
        self.rx_status = 0
        self.tx_flags = 0
        self.data = b""
        self.extra_data_index = 0

    @property
    def data_hex(self):
        return "-".join(f"{b:02X}" for b in self.data)

    def to_msg_spec(self):
        spec = {"data": self.data_hex, "protocolId": "ISO15765"}
        proto_name = lookup_name(self.protocol_id, PROTOCOL_NAMES)
        if proto_name:
            spec["protocolId"] = proto_name
        else:
            spec["protocolId"] = self.protocol_id
        if self.rx_status:
            spec["rxStatus"] = self.rx_status
        if self.tx_flags:
            tx_name = lookup_name(self.tx_flags, TX_FLAG_NAMES)
            spec["txFlags"] = tx_name if tx_name else self.tx_flags
        return spec


# Regex to parse a single {...} message from the log
MSG_RE = re.compile(
    r'\{(?P<content>[^{}]*)\}'
)


def parse_message(msg_text):
    """Parse a single PASSTHRU_MSG from text like:
    'ISO15765; RxStatus: 0; TxFlags: ISO15765_FRAME_PAD; TS: 00:00:00; LEN: 6; ExtraIndex: 0; Data: 00-00-06-02-10-03'
    """
    m = MSG_RE.match("{" + msg_text + "}")
    if not m:
        return None

    content = msg_text
    parts = [p.strip() for p in content.split(";")]
    if not parts:
        return None

    msg = ParsedMsg()

    # First field is protocol name
    proto_name = parts[0]
    msg.protocol_id = lookup(proto_name, PROTOCOLS) or 0

    for part in parts[1:]:
        if ":" not in part:
            continue
        key, _, val = part.partition(":")
        key = key.strip()
        val = val.strip()

        if key == "RxStatus":
            msg.rx_status = int(val, 0)
        elif key == "TxFlags":
            named = lookup(val, TX_FLAGS)
            if named is not None:
                msg.tx_flags = named
            else:
                msg.tx_flags = int(val, 0)
        elif key == "LEN":
            pass  # data length is inferred from Data field
        elif key == "ExtraIndex":
            msg.extra_data_index = int(val, 0)
        elif key == "Data":
            hex_str = val.replace("-", "").replace(" ", "")
            if hex_str:
                msg.data = bytes.fromhex(hex_str)
        elif key == "TS":
            pass  # timestamp not needed for scenario

    return msg


def parse_messages_array(arr_text):
    """Parse '[{...}, {...}]' into a list of ParsedMsg."""
    msgs = []
    # Find all {...} groups, handling nested braces is not needed here
    # since PASSTHRU_MSG serialization has no nested braces.
    for m in re.finditer(r'\{([^{}]*)\}', arr_text):
        msg = parse_message(m.group(1))
        if msg and msg.data:
            msgs.append(msg)
    return msgs


class LogEvent:
    """A parsed PassThru* log entry."""
    def __init__(self):
        self.text = ""
        self.timestamp = None
        self.index = 0
        self.count = 1
        self.func = ""
        self.args_str = ""
        self.return_code = ""
        # Parsed data
        self.device_id = 0
        self.channel_id = 0
        self.protocol_id = 0
        self.connect_flags = 0
        self.baud_rate = 0
        self.out_channel_id = 0
        self.ioctl_name = ""
        self.ioctl_id = 0
        self.ioctl_id_known = True
        self.filter_type = ""
        self.filter_id = 0
        self.firmware_version = ""
        self.dll_version = ""
        self.api_version = ""
        self.msgs = []  # list of ParsedMsg
        self.num_msgs_in = 0
        self.num_msgs_out = 0
        self.timeout = 0
        self.ioctl_output_val = None
        self.vbatt_value = None


def split_top_level_commas(s):
    """Split a string by commas that are at the top level (not inside [] or {})."""
    parts = []
    depth = 0
    current = []
    for c in s:
        if c in "[{(":
            depth += 1
        elif c in "]})":
            depth -= 1
        if c == "," and depth == 0:
            parts.append("".join(current).strip())
            current = []
        else:
            current.append(c)
    if current:
        parts.append("".join(current).strip())
    return parts


def parse_log_entry(text, timestamp=None, index=0, count=1):
    """Parse a single log line text into a LogEvent, or return None."""
    if text.startswith("Client:") or text.startswith("Driver:"):
        return None

    # Split on " -> " to separate call from return code
    arrow_idx = text.find(" -> ")
    if arrow_idx < 0:
        return None

    call_str = text[:arrow_idx].strip()
    ret_str = text[arrow_idx + 4:].strip()

    ev = LogEvent()
    ev.text = text
    ev.timestamp = timestamp
    ev.index = index
    ev.count = count
    ev.return_code = ret_str

    # Identify function
    paren_idx = call_str.find("(")
    if paren_idx < 0:
        return None

    ev.func = call_str[:paren_idx].strip()

    # Extract args between outer parens
    last_paren = call_str.rfind(")")
    if last_paren < 0 or last_paren <= paren_idx:
        return None

    ev.args_str = call_str[paren_idx + 1:last_paren]

    # Parse based on function
    if ev.func == "PassThruOpen":
        _parse_open(ev)
    elif ev.func == "PassThruClose":
        ev.device_id = int(ev.args_str.strip(), 10)
    elif ev.func == "PassThruConnect":
        _parse_connect(ev)
    elif ev.func == "PassThruDisconnect":
        ev.channel_id = int(ev.args_str.strip(), 10)
    elif ev.func == "PassThruReadVersion":
        _parse_read_version(ev)
    elif ev.func == "PassThruIoctl":
        _parse_ioctl(ev)
    elif ev.func == "PassThruWriteMsgs":
        _parse_read_write(ev)
    elif ev.func == "PassThruReadMsgs":
        _parse_read_write(ev)
    elif ev.func == "PassThruStartMsgFilter":
        _parse_filter_start(ev)
    elif ev.func == "PassThruStopMsgFilter":
        _parse_filter_stop(ev)
    else:
        return None

    return ev


def _parse_open(ev):
    args = split_top_level_commas(ev.args_str)
    if not args:
        return
    # First arg: NULL or "name"
    if args[0].strip() == "NULL":
        pass
    elif args[0].startswith('"'):
        pass
    if len(args) > 1:
        ev.device_id = int(args[1].strip(), 10)


def _parse_connect(ev):
    args = split_top_level_commas(ev.args_str)
    if len(args) < 5:
        return
    ev.device_id = int(args[0].strip(), 10)
    ev.protocol_id = lookup(args[1].strip(), PROTOCOLS) or 0
    ev.connect_flags = lookup(args[2].strip(), CONNECT_FLAGS) or 0
    ev.baud_rate = int(args[3].strip(), 10)
    ev.out_channel_id = int(args[4].strip(), 10)
    ev.channel_id = ev.out_channel_id


def _parse_read_version(ev):
    args = split_top_level_commas(ev.args_str)
    if len(args) < 4:
        return
    ev.device_id = int(args[0].strip(), 10)
    ev.firmware_version = args[1].strip().strip('"')
    ev.dll_version = args[2].strip().strip('"')
    ev.api_version = args[3].strip().strip('"')


def _parse_ioctl(ev):
    args = split_top_level_commas(ev.args_str)
    if len(args) < 2:
        return
    ev.device_id = int(args[0].strip(), 10)  # may be channel or device

    ioctl_str = args[1].strip()
    if ioctl_str.startswith("UNK("):
        # UNK(0xHEX) or UNK(HEX) or UNK(DEC)
        inner = ioctl_str[4:ioctl_str.rfind(")")]
        if inner.startswith("0x") or inner.startswith("0X"):
            ev.ioctl_id = int(inner, 16)
        else:
            # Try hex first (vendor IOCTLs like 10ECB), fall back to decimal
            try:
                ev.ioctl_id = int(inner, 16)
            except ValueError:
                ev.ioctl_id = int(inner, 10)
        ev.ioctl_id_known = False
        ev.ioctl_name = f"0x{ev.ioctl_id:X}"
    else:
        ev.ioctl_name = ioctl_str
        ev.ioctl_id = lookup(ioctl_str, IOCTLS) or 0

    # Third arg: input (NULL or [[param,val],...])
    if len(args) > 2:
        input_str = args[2].strip()
        # We don't need to parse SCONFIG_LIST for the scenario

    # Fourth arg: output (NULL or a number)
    if len(args) > 3:
        output_str = args[3].strip()
        if output_str != "NULL" and output_str:
            try:
                val = int(output_str, 10)
                ev.ioctl_output_val = val
                if ev.ioctl_name == "READ_VBATT" and 5000 <= val <= 24000:
                    ev.vbatt_value = val
            except ValueError:
                pass


def _parse_read_write(ev):
    args = split_top_level_commas(ev.args_str)
    if len(args) < 2:
        return
    ev.channel_id = int(args[0].strip(), 10)

    # Second arg: message array [...]
    arr_str = args[1].strip()
    if arr_str.startswith("["):
        ev.msgs = parse_messages_array(arr_str)

    # Third arg: numIn=>numOut
    if len(args) > 2:
        num_str = args[2].strip()
        if "=>" in num_str:
            parts = num_str.split("=>")
            try:
                ev.num_msgs_in = int(parts[0].strip(), 10)
                ev.num_msgs_out = int(parts[1].strip(), 10)
            except ValueError:
                pass

    # Fourth arg: timeout
    if len(args) > 3:
        try:
            ev.timeout = int(args[3].strip(), 10)
        except ValueError:
            pass


def _parse_filter_start(ev):
    args = split_top_level_commas(ev.args_str)
    if len(args) < 2:
        return
    ev.channel_id = int(args[0].strip(), 10)
    ev.filter_type = args[1].strip()
    # Remaining args: mask{...}, pattern{...}, flow{...}, filterId
    # We don't need filter details for scenario generation
    if len(args) >= 6:
        try:
            ev.filter_id = int(args[-1].strip(), 10)
        except ValueError:
            pass


def _parse_filter_stop(ev):
    args = split_top_level_commas(ev.args_str)
    if len(args) < 2:
        return
    ev.channel_id = int(args[0].strip(), 10)
    ev.filter_id = int(args[1].strip(), 10)


# ─── Scenario builder ────────────────────────────────────────────────────────

class ScenarioBuilder:
    """Accumulates parsed events and builds the scenario JSON."""

    def __init__(self):
        self.device = {
            "firmwareVersion": "",
            "dllVersion": "",
            "apiVersion": "",
            "vbatt_mV": 12000,
        }
        self.ioctls = {}
        self.targets = {}  # key: (proto, flags, baud) -> target dict
        self.connect_params = []  # list of (proto, flags, baud, channel_id)
        self.write_read_pairs = []  # list of (channel_id, write_msg, reply_msgs, delay_ms)
        self.periodic_candidates = []  # list of (channel_id, msg, interval_ms)
        self.state_machine = {
            "initial": "CLOSED",
            "transitions": [
                {"event": "PassThruOpen", "from": "CLOSED", "to": "OPENED"},
                {"event": "PassThruConnect", "from": "OPENED", "to": "OPENED"},
                {"event": "PassThruDisconnect", "from": "OPENED", "to": "OPENED"},
                {"event": "PassThruClose", "from": "OPENED", "to": "CLOSED"},
            ],
        }
        self._channel_target = {}  # channel_id -> (proto, flags, baud)
        self._last_write = {}  # channel_id -> (write_msg, timestamp)
        self._read_seen = defaultdict(list)  # channel_id -> [(msg_data, timestamp)]
        self._all_events = []

    def process_event(self, ev):
        self._all_events.append(ev)

        if ev.func == "PassThruReadVersion":
            self.device["firmwareVersion"] = ev.firmware_version
            self.device["dllVersion"] = ev.dll_version
            self.device["apiVersion"] = ev.api_version

        elif ev.func == "PassThruIoctl":
            self._process_ioctl(ev)

        elif ev.func == "PassThruConnect":
            key = (ev.protocol_id, ev.connect_flags, ev.baud_rate)
            self.connect_params.append(key)
            self._channel_target[ev.channel_id] = key
            if key not in self.targets:
                self.targets[key] = self._new_target(ev)

        elif ev.func == "PassThruWriteMsgs":
            self._process_write(ev)

        elif ev.func == "PassThruReadMsgs":
            self._process_read(ev)

        elif ev.func == "PassThruDisconnect":
            target_key = self._channel_target.pop(ev.channel_id, None)
            self._last_write.pop(ev.channel_id, None)

            # Finalize periodic detection for this channel
            reads = self._read_seen.pop(ev.channel_id, [])
            self._detect_periodic(ev.channel_id, reads, target_key)

    def _process_ioctl(self, ev):
        if not ev.ioctl_name:
            return
        if ev.ioctl_name in self.ioctls:
            return  # already recorded

        scope = "any"
        if ev.ioctl_name == "READ_VBATT":
            scope = "device"
        elif ev.ioctl_name in ("SET_CONFIG", "GET_CONFIG", "CLEAR_RX_BUFFER",
                                "CLEAR_TX_BUFFER", "CLEAR_MSG_FILTERS",
                                "CLEAR_PERIODIC_MSGS"):
            scope = "channel"

        rule = {"return": ev.return_code, "scope": scope}
        if ev.ioctl_name == "READ_VBATT":
            rule["output"] = "auto"
            if ev.vbatt_value:
                self.device["vbatt_mV"] = ev.vbatt_value
        elif ev.ioctl_name == "SET_CONFIG":
            rule["consumeInput"] = True

        self.ioctls[ev.ioctl_name] = rule

    def _new_target(self, ev):
        proto_name = lookup_name(ev.protocol_id, PROTOCOL_NAMES) or ev.protocol_id
        flags_name = lookup_name(ev.connect_flags, CONNECT_FLAG_NAMES) or ev.connect_flags
        return {
            "name": f"ECU_{ev.channel_id}",
            "match": {
                "protocolId": proto_name,
                "flags": flags_name,
                "baud": ev.baud_rate,
            },
            "preferredChannelId": ev.channel_id,
            "replies": [],
            "periodic": [],
        }

    def _process_write(self, ev):
        if not ev.msgs:
            return
        write_msg = ev.msgs[0]
        # Store with the target key so we can find it later even after disconnect
        target_key = self._channel_target.get(ev.channel_id)
        self._last_write[ev.channel_id] = (write_msg, ev.timestamp, target_key)

    def _process_read(self, ev):
        if not ev.msgs:
            return  # empty read (ERR_BUFFER_EMPTY) — don't clear last_write
        for msg in ev.msgs:
            self._read_seen[ev.channel_id].append((msg, ev.timestamp))

        # Try to pair with last write on this channel
        last = self._last_write.get(ev.channel_id)
        if last and last[0] is not None:
            write_msg, write_ts, target_key = last
            for read_msg in ev.msgs:
                delay_ms = 0
                if write_ts and ev.timestamp:
                    delta = (ev.timestamp - write_ts).total_seconds() * 1000
                    delay_ms = max(0, int(delta))
                self.write_read_pairs.append((
                    ev.channel_id, write_msg, read_msg, delay_ms, target_key
                ))
            # Clear so we don't pair the same write with later reads
            self._last_write[ev.channel_id] = None

    def _detect_periodic(self, channel_id, reads, target_key=None):
        """Detect periodic messages: same data appearing multiple times at
        regular intervals."""
        if len(reads) < 3:
            return

        # Group by data content
        by_data = defaultdict(list)
        for msg, ts in reads:
            by_data[msg.data].append(ts)

        for data, timestamps in by_data.items():
            if len(timestamps) < 3:
                continue
            # Compute intervals
            intervals = []
            for i in range(1, len(timestamps)):
                if timestamps[i] and timestamps[i - 1]:
                    delta = (timestamps[i] - timestamps[i - 1]).total_seconds() * 1000
                    intervals.append(delta)
            if len(intervals) < 2:
                continue
            avg = sum(intervals) / len(intervals)
            # Check regularity (all within 30% of avg)
            if avg < 10:
                continue
            if all(abs(x - avg) < avg * 0.3 for x in intervals):
                self.periodic_candidates.append(
                    (channel_id, data, int(avg), target_key)
                )

    def build(self):
        """Build the final scenario JSON dict."""
        # Ensure default IOCTLs are present
        default_ioctls = {
            "SET_CONFIG": {"return": "STATUS_NOERROR", "scope": "channel", "consumeInput": True},
            "GET_CONFIG": {"return": "STATUS_NOERROR", "scope": "channel", "output": "auto"},
            "CLEAR_RX_BUFFER": {"return": "STATUS_NOERROR", "scope": "channel"},
            "CLEAR_TX_BUFFER": {"return": "STATUS_NOERROR", "scope": "channel"},
            "CLEAR_MSG_FILTERS": {"return": "STATUS_NOERROR", "scope": "channel"},
            "CLEAR_PERIODIC_MSGS": {"return": "STATUS_NOERROR", "scope": "channel"},
        }
        for name, rule in default_ioctls.items():
            if name not in self.ioctls:
                self.ioctls[name] = rule

        # Ensure READ_VBATT is present
        if "READ_VBATT" not in self.ioctls:
            self.ioctls["READ_VBATT"] = {
                "return": "STATUS_NOERROR", "output": "auto", "scope": "device"
            }

        # Assign reply rules to targets
        for channel_id, write_msg, reply_msg, delay_ms, target_key in self.write_read_pairs:
            if not target_key or target_key not in self.targets:
                continue
            target = self.targets[target_key]

            # Check if this request already has a reply
            match_data = write_msg.data_hex
            existing = any(
                r["match"]["data"] == match_data
                for r in target["replies"]
            )
            if existing:
                continue

            reply_rule = {
                "match": {"data": match_data, "mode": "prefix"},
                "response": {
                    "data": reply_msg.data_hex,
                    "delayMs": delay_ms,
                    "protocolId": lookup_name(reply_msg.protocol_id, PROTOCOL_NAMES) or reply_msg.protocol_id,
                },
            }
            if reply_msg.rx_status:
                reply_rule["response"]["rxStatus"] = reply_msg.rx_status
            if reply_msg.tx_flags:
                tx_name = lookup_name(reply_msg.tx_flags, TX_FLAG_NAMES)
                reply_rule["response"]["txFlags"] = tx_name if tx_name else reply_msg.tx_flags
            target["replies"].append(reply_rule)

        # Assign periodic generators to targets
        for channel_id, data, interval_ms, target_key in self.periodic_candidates:
            if not target_key or target_key not in self.targets:
                continue
            target = self.targets[target_key]

            data_hex = "-".join(f"{b:02X}" for b in data)
            # Avoid duplicates
            existing = any(
                p["msg"]["data"] == data_hex
                for p in target["periodic"]
            )
            if existing:
                continue

            target["periodic"].append({
                "intervalMs": interval_ms,
                "msg": {
                    "protocolId": "ISO15765",
                    "data": data_hex,
                },
                "startOn": "connect",
                "stopOn": "disconnect",
            })

        # Build targets list, filtering out empty targets
        targets_list = []
        for key in self.targets:
            t = self.targets[key]
            if t["replies"] or t["periodic"]:
                targets_list.append(t)
            else:
                # Keep targets that had connect calls even if no replies
                targets_list.append(t)

        # Sort targets by preferredChannelId for deterministic output
        targets_list.sort(key=lambda t: t.get("preferredChannelId", 0))

        return {
            "device": self.device,
            "ioctls": self.ioctls,
            "targets": targets_list,
            "states": self.state_machine,
        }


# ─── Main ────────────────────────────────────────────────────────────────────

def load_log(path):
    """Load the JSONL log file and return a list of LogEvent."""
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)

    events = []
    for entry in data.get("entries", []):
        text = entry.get("text", "")
        ts_str = entry.get("timestamp", "")
        ts = parse_iso_timestamp(ts_str) if ts_str else None
        index = entry.get("index", 0)
        count = entry.get("count", 1)

        ev = parse_log_entry(text, ts, index, count)
        if ev:
            events.append(ev)

    return events


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <input.jsonl> [output.json]")
        print(f"  Converts a PassThruLogger capture to a ReplayJ2534 scenario.json")
        sys.exit(1)

    input_path = sys.argv[1]
    if len(sys.argv) >= 3:
        output_path = sys.argv[2]
    else:
        # Default: replace .jsonl with .scenario.json
        if input_path.endswith(".jsonl"):
            output_path = input_path[:-6] + ".scenario.json"
        else:
            base = input_path.rsplit(".", 1)[0] if "." in input_path else input_path
            output_path = base + ".scenario.json"

    events = load_log(input_path)
    print(f"Loaded {len(events)} events from {input_path}")

    builder = ScenarioBuilder()
    for ev in events:
        builder.process_event(ev)

    scenario = builder.build()

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(scenario, f, indent=2)
        f.write("\n")

    # Summary
    n_ioctls = len(scenario["ioctls"])
    n_targets = len(scenario["targets"])
    n_replies = sum(len(t["replies"]) for t in scenario["targets"])
    n_periodic = sum(len(t["periodic"]) for t in scenario["targets"])

    print(f"Written: {output_path}")
    print(f"  Device: fw={scenario['device']['firmwareVersion']}, "
          f"dll={scenario['device']['dllVersion']}, "
          f"api={scenario['device']['apiVersion']}, "
          f"vbatt={scenario['device']['vbatt_mV']}mV")
    print(f"  IOCTLs: {n_ioctls}")
    print(f"  Targets: {n_targets}")
    print(f"  Reply rules: {n_replies}")
    print(f"  Periodic generators: {n_periodic}")


if __name__ == "__main__":
    main()
