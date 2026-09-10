// ElmJ2534 — ElmProto implementation
//
// Implements: docs/elm-j2534-design.md → "ElmProto interfaces (T1 contract)"
// Semantics ported from candroid-fw/tools/elm327.py (_clean, classify,
// _reassemble, INIT/ADDRESSING/CRITICAL tables) and CanDroid :core:elm327
// (ElmLineParser framing, ElmResponseClassifier recovery classes,
// ElmIsoTpAssembler per-CAN-ID grouping, ElmCommand id formatting).

#include "ElmProto.h"
#include <stdio.h>
#include <stdlib.h>

// ═══════════════════════════════════════════════════════════════════════════
// Byte framing
// ═══════════════════════════════════════════════════════════════════════════

void ElmLineParser::reset() {
    pending_.clear();
}

void ElmLineParser::feed(const char *data, int len, std::vector<std::string> &lines) {
    for (int i = 0; i < len; i++) {
        int b = (unsigned char)data[i];
        // Junk is filtered at byte level, not character level: decoding 0xFF
        // as text is what kills Windows consoles mid-run on clone resets.
        if (b == '\r' || b == '\n') {
            if (!pending_.empty()) {
                lines.push_back(pending_);
                pending_.clear();
            }
            continue;
        }
        if (b == '>') {
            // The prompt can arrive with no CR before it, and it means idle —
            // it ends a command whether or not anything preceded it.
            if (!pending_.empty()) {
                lines.push_back(pending_);
                pending_.clear();
            }
            lines.push_back(">");
            continue;
        }
        if (b < 0x20 || b > 0x7E) continue;
        // ATS0 makes spaces cosmetic, but clones ignore ATS0; every token
        // comparison downstream is space-free because they die here.
        if (b == ' ') continue;
        pending_ += (char)b;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Line classification
// ═══════════════════════════════════════════════════════════════════════════

// private helpers

static std::string elmUpperNoSpace(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == ' ') continue;
        out += (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    return out;
}

static int hexDigitVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool isHexChar(char c) { return hexDigitVal(c) >= 0; }

static bool hexToBytes(const std::string &hex, std::vector<uint8_t> &out) {
    if (hex.empty() || (hex.size() % 2) != 0) return false;
    out.clear();
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = hexDigitVal(hex[i]), lo = hexDigitVal(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back((uint8_t)((hi << 4) | lo));
    }
    return true;
}

// Pure even-length hex → an ISO-TP frame line or a pre-assembled payload.
// Lines with ':' or odd length are Data too but are resolved by the
// assembler, which is the only stage that knows the header mode.
static bool isPureHex(const std::string &s) {
    if (s.empty() || (s.size() % 2) != 0) return false;
    for (size_t i = 0; i < s.size(); i++)
        if (!isHexChar(s[i])) return false;
    return true;
}

struct ElmErrorEntry { const char *token; ElmRecovery recovery; };

// Space-free spellings (ATS0). Matching is exact (token ==), not prefix-based,
// so table order is irrelevant — no token can shadow another.
static const ElmErrorEntry ERROR_TOKENS[] = {
    { "NODATA",          ElmRecovery::Retry },
    { "STOPPED",         ElmRecovery::Retry },
    { "CANERROR",        ElmRecovery::ResetProtocol },
    { "BUSERROR",        ElmRecovery::ResetProtocol },
    { "UNABLETOCONNECT", ElmRecovery::ResetProtocol },
    { "NABLETO",         ElmRecovery::ResetProtocol },  // clone drops the 'U'
    { "DATAERROR",       ElmRecovery::WarmStart },
    { "BUFFERFULL",      ElmRecovery::WarmStart },
    { "<RXERROR",        ElmRecovery::WarmStart },
    { "LVRESET",         ElmRecovery::Reinitialise },
    { "?",               ElmRecovery::Unsupported },
};

// Firmware banners and version strings. Never trusted for capability
// decisions — a clone lying "ELM327 v2.1" is the norm — but the session
// reports them as the J2534 firmware string.
static const char *BANNER_TOKENS[] = {
    "ELM327", "ELM32", "STN", "OBDLINK", "SCANTOOL",
};

static bool containsToken(const std::string &upper, const char *token) {
    return upper.find(token) != std::string::npos;
}

static bool isBanner(const std::string &upper) {
    for (size_t i = 0; i < sizeof(BANNER_TOKENS) / sizeof(BANNER_TOKENS[0]); i++)
        if (containsToken(upper, BANNER_TOKENS[i])) return true;
    // V[0-9].[0-9] anywhere in the line: clones spell banners freely
    // ("V1.5", "OBDII/EOBD V2.1"), the shape is the reliable part.
    for (size_t i = 0; i + 3 < upper.size(); i++)
        if (upper[i] == 'V' && upper[i + 1] >= '0' && upper[i + 1] <= '9' &&
            upper[i + 2] == '.' && upper[i + 3] >= '0' && upper[i + 3] <= '9')
            return true;
    return false;
}

// public API

bool elmClassifyLine(const std::string &line, const std::string &echoOf,
                     ElmResponse &out) {
    out.raw = line;
    out.kind = ElmResponseKind::Text;
    out.recovery = ElmRecovery::None;

    if (line.empty()) return false;
    // '+' lines are unsolicited connection chatter from clones, not answers.
    if (line[0] == '+') return false;

    const std::string token = elmUpperNoSpace(line);
    if (token.empty()) return false;

    // Software echo suppression: clones disagree about ATE0, so the echo is
    // dropped here and no downstream stage needs to be echo-aware.
    if (!echoOf.empty() && token == elmUpperNoSpace(echoOf)) return false;

    if (token == ">") {
        out.kind = ElmResponseKind::Prompt;
        return true;
    }
    if (token == "OK") {
        out.kind = ElmResponseKind::Ok;
        return true;
    }
    if (token == "SEARCHING..." || token == "SEARCHING") {
        out.kind = ElmResponseKind::Waiting;
        out.recovery = ElmRecovery::Wait;
        return true;
    }

    for (size_t i = 0; i < sizeof(ERROR_TOKENS) / sizeof(ERROR_TOKENS[0]); i++) {
        if (token == ERROR_TOKENS[i].token) {
            out.kind = ElmResponseKind::Failure;
            out.recovery = ERROR_TOKENS[i].recovery;
            return true;
        }
    }

    if (isPureHex(token)) {
        out.kind = ElmResponseKind::Data;
        return true;
    }
    // ATH1 "<3-hex-id><PCI><data>": odd-length hex with an id prefix.
    if (token.size() >= 5 && (token.size() % 2) == 1 &&
        isHexChar(token[0]) && isHexChar(token[1]) && isHexChar(token[2])) {
        bool restHex = true;
        for (size_t i = 3; i < token.size(); i++)
            if (!isHexChar(token[i])) { restHex = false; break; }
        if (restHex) {
            out.kind = ElmResponseKind::Data;
            return true;
        }
    }
    // ATH0 "<index>:<hex>": the chip's own pre-assembled shape.
    {
        size_t colon = token.find(':');
        if (colon != std::string::npos && colon > 0 && colon <= 2) {
            bool prefixOk = true;
            for (size_t i = 0; i < colon; i++)
                if (!isHexChar(token[i])) { prefixOk = false; break; }
            bool bodyOk = !token.empty() && colon + 1 < token.size();
            for (size_t i = colon + 1; bodyOk && i < token.size(); i++)
                if (!isHexChar(token[i])) { bodyOk = false; break; }
            if (prefixOk && bodyOk) {
                out.kind = ElmResponseKind::Data;
                return true;
            }
        }
    }

    if (isBanner(token)) {
        out.kind = ElmResponseKind::Banner;
        return true;
    }

    // ATRV's "12.3V", ATDP's protocol descriptions, "BUS INIT: OK" progress.
    out.kind = ElmResponseKind::Text;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// ISO-TP reassembly
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// Per-CAN-ID multi-frame state (ATH1 shape). One builder per responding ECU:
// a functional request can have 0x7E8 and 0x7E9 interleaved on the wire.
struct GroupBuilder {
    uint32_t canId;
    bool hasFirst;
    bool complete;              // single frame seen → finished, ignore the rest
    int total;                  // declared length from the first frame
    int wantSn;                 // next expected consecutive-frame SN
    std::vector<uint8_t> buf;

    explicit GroupBuilder(uint32_t id)
        : canId(id), hasFirst(false), complete(false), total(0), wantSn(1) {}
};

// The header is THREE hex characters, not three bytes: stripping a
// byte-aligned prefix leaves an odd-length string, which is what makes a
// naive whole-line hex decode fail at the end rather than the beginning.
const size_t ID_HEX_DIGITS = 3;
const int PCI_SINGLE = 0;
const int PCI_FIRST = 1;
const int PCI_CONSECUTIVE = 2;

} // namespace

std::vector<ElmAssembly> elmAssembleAll(const std::vector<std::string> &dataLines,
                                        uint32_t defaultCanId) {
    std::vector<ElmAssembly> singles;     // line order (ok or truncated)
    std::vector<GroupBuilder> groups;     // first-appearance order
    std::vector<uint8_t> ath0;            // chip-assembled fallback accumulator
    bool ath0Seen = false;

    for (size_t li = 0; li < dataLines.size(); li++) {
        const std::string s = elmUpperNoSpace(dataLines[li]);
        if (s.empty()) continue;
        bool shapeOk = true;
        for (size_t i = 0; i < s.size(); i++)
            if (!isHexChar(s[i]) && s[i] != ':') { shapeOk = false; break; }
        if (!shapeOk) continue;   // NODATA etc. never reach here, but be sure

        const size_t colon = s.find(':');
        if (colon != std::string::npos) {
            // ATH0 shape "<index>:<payload>": source lost, bytes accumulate
            // across lines into one message attributed to defaultCanId.
            std::vector<uint8_t> body;
            if (!hexToBytes(s.substr(colon + 1), body)) continue;
            ath0.insert(ath0.end(), body.begin(), body.end());
            ath0Seen = true;
            continue;
        }

        if (s.size() <= ID_HEX_DIGITS) continue;   // no room for id + PCI
        std::string body = s.substr(ID_HEX_DIGITS);
        if (body.size() < 2 || (body.size() % 2) != 0) continue;
        int idVal = 0;
        for (size_t i = 0; i < ID_HEX_DIGITS; i++) {
            int d = hexDigitVal(s[i]);
            if (d < 0) { idVal = -1; break; }
            idVal = (idVal << 4) | d;
        }
        if (idVal < 0) continue;
        std::vector<uint8_t> raw;
        if (!hexToBytes(body, raw)) continue;

        GroupBuilder *g = NULL;
        for (size_t i = 0; i < groups.size(); i++)
            if (groups[i].canId == (uint32_t)idVal) { g = &groups[i]; break; }
        if (!g) {
            groups.push_back(GroupBuilder((uint32_t)idVal));
            g = &groups.back();
        }
        if (g->complete) continue;   // a finished group ignores later frames

        const int pciType = (raw[0] >> 4) & 0x0F;
        if (pciType == PCI_SINGLE) {
            const int len = raw[0] & 0x0F;
            const int carried = (int)raw.size() - 1;
            ElmAssembly a;
            a.canId = g->canId;
            // Same contract as the multi-frame path: declared > received is
            // reported truncated, never returned as a message. len == 0 is
            // malformed ISO-TP (SF_DL must be ≥ 1) and lands in the same bucket.
            a.truncated = (len == 0 || len > carried);
            a.ok = !a.truncated;
            const int take = a.ok ? len : carried;
            for (int i = 1; i <= take; i++)
                a.payload.push_back(raw[i]);
            singles.push_back(a);
            g->complete = true;
        } else if (pciType == PCI_FIRST) {
            if (raw.size() < 2) continue;
            g->hasFirst = true;
            g->total = ((raw[0] & 0x0F) << 8) | raw[1];
            g->buf.assign(raw.begin() + 2, raw.end());
            g->wantSn = 1;
        } else if (pciType == PCI_CONSECUTIVE) {
            if (!g->hasFirst) continue;   // stray CF with no first frame
            if ((raw[0] & 0x0F) != (g->wantSn & 0x0F))
                continue;   // out of order: drop rather than corrupt payload
            g->buf.insert(g->buf.end(), raw.begin() + 1, raw.end());
            g->wantSn++;
        }
        // PCI 3 (flow control) is addressed to the adapter, which answers it
        // itself — nothing to reassemble.
    }

    std::vector<ElmAssembly> out(singles);
    for (size_t i = 0; i < groups.size(); i++) {
        const GroupBuilder &g = groups[i];
        if (!g.hasFirst || g.complete) continue;
        ElmAssembly a;
        a.canId = g.canId;
        a.truncated = (int)g.buf.size() < g.total;
        a.ok = !a.truncated;
        // Truncation is reported, never returned half-assembled: a
        // half-decoded parameter is worse than a missing one, and this is
        // the exact signature of a clone that ignored ATFCSM1.
        const size_t take = a.ok ? (size_t)g.total : g.buf.size();
        a.payload.assign(g.buf.begin(), g.buf.begin() + take);
        out.push_back(a);
    }
    if (ath0Seen && !ath0.empty()) {
        ElmAssembly a;
        a.ok = true;
        a.truncated = false;
        a.canId = defaultCanId;
        a.payload = ath0;
        out.push_back(a);
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// Command builders
// ═══════════════════════════════════════════════════════════════════════════

std::string elmHexRequest(const uint8_t *payload, int len) {
    static const char HEX[] = "0123456789ABCDEF";
    std::string out;
    if (!payload || len <= 0) return out;
    out.reserve((size_t)len * 2);
    for (int i = 0; i < len; i++) {
        out += HEX[(payload[i] >> 4) & 0x0F];
        out += HEX[payload[i] & 0x0F];
    }
    return out;
}

// 11-bit ids print as exactly three hex digits; 29-bit ids as eight, which
// ELM327 v1.4b+ accepts directly in ATSH/ATCRA/ATFCSH.
static std::string formatCanId(uint32_t canId) {
    char buf[16];
    if (canId <= 0x7FF)
        snprintf(buf, sizeof(buf), "%03X", canId);
    else
        snprintf(buf, sizeof(buf), "%08X", canId);
    return std::string(buf);
}

std::string elmCmdSetHeader(uint32_t canId) { return "ATSH" + formatCanId(canId); }
std::string elmCmdSetFilter(uint32_t canId) { return "ATCRA" + formatCanId(canId); }
std::string elmCmdFcHeader(uint32_t canId)  { return "ATFCSH" + formatCanId(canId); }

bool elmParseVoltage(const std::string &text, int &millivolts) {
    size_t i = 0;
    while (i < text.size() && !(text[i] >= '0' && text[i] <= '9')) i++;
    if (i >= text.size()) return false;
    size_t start = i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') i++;
    const std::string whole = text.substr(start, i - start);
    std::string frac;
    if (i < text.size() && text[i] == '.') {
        i++;
        size_t fs = i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') i++;
        frac = text.substr(fs, i - fs);
    }
    while (frac.size() < 3) frac += '0';
    long mv = atol(whole.c_str()) * 1000 + atol(frac.substr(0, 3).c_str());
    millivolts = (int)mv;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Init plan
// ═══════════════════════════════════════════════════════════════════════════

// Required set mirrors elm327.py CRITICAL as intersected with the init plan:
// {ATE0, ATH1, ATCAF1, ATSP6}. CRITICAL's ATFCSH/ATFCSD live in the addressing
// sequence; ATSH is deliberately NOT critical. Continuing past a rejected
// ATSP6 produces a session that answers nothing and blames the wiring.
const std::vector<ElmStep> &elmInitSequence() {
    static const ElmStep steps[] = {
        { "ATE0",   true,  "echo off" },
        { "ATL0",   false, "line feeds off" },
        { "ATS0",   false, "spaces off" },
        { "ATH1",   true,  "headers on" },
        { "ATCAF1", true,  "ISO-TP auto-format on" },
        { "ATSP6",  true,  "ISO15765-4 11-bit 500k" },
        { "ATAT1",  false, "adaptive timing" },
        { "ATST32", false, "response timeout ~200ms" },
    };
    static const std::vector<ElmStep> plan(steps, steps + sizeof(steps) / sizeof(steps[0]));
    return plan;
}

std::vector<ElmStep> elmAddressingSequence(uint32_t requestCanId,
                                           uint32_t responseCanId) {
    std::vector<ElmStep> seq;
    seq.push_back(ElmStep());
    seq.back().command = elmCmdSetHeader(requestCanId);
    seq.back().required = false;   // not in CRITICAL: a clone answering ATSH
                                   // oddly must not hard-fail init
    seq.back().purpose = "TX header = request id";

    if (responseCanId != 0) {
        // Omitted for functional (broadcast) targets: filtering to a single
        // responder would hide the other ECUs that answer 0x7DF.
        seq.push_back(ElmStep());
        seq.back().command = elmCmdSetFilter(responseCanId);
        seq.back().required = false;
        seq.back().purpose = "RX filter = response id";
    }

    seq.push_back(ElmStep());
    seq.back().command = elmCmdFcHeader(requestCanId);
    seq.back().required = true;
    seq.back().purpose = "flow-control header = request id";

    seq.push_back(ElmStep());
    seq.back().command = "ATFCSD300000";
    seq.back().required = true;
    seq.back().purpose = "FC data: CTS, no limit";

    seq.push_back(ElmStep());
    seq.back().command = "ATFCSM1";
    seq.back().required = false;   // clones answer OK then ignore it; warn only
    seq.back().purpose = "FC mode: use configured";
    return seq;
}
