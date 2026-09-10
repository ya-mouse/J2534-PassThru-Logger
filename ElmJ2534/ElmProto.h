#pragma once
// ElmJ2534 — sans-IO ELM327 protocol core
//
// Implements: docs/elm-j2534-design.md → "ElmProto interfaces (T1 contract)"
//
// Byte framing, line classification, ISO-TP reassembly, command builders and
// the init plan. Pure C++11 — no windows.h — so every rule here is testable
// natively (tests/test_elmproto.cpp) and on the target (mingw) from one source.
// Ported semantics: candroid-fw/tools/elm327.py + CanDroid :core:elm327.

#include <string>
#include <vector>
#include <stdint.h>

// ── Response classification ─────────────────────────────────────────────────

enum class ElmResponseKind { Data, Text, Ok, Prompt, Banner, Waiting, Failure };

// What clears a Failure. SEARCHING is Waiting/Wait, not a failure: treating it
// as one discards the first good read after every protocol negotiation.
enum class ElmRecovery { None, Retry, Wait, ResetProtocol, WarmStart,
                         Unsupported, Reinitialise };

struct ElmResponse {
    ElmResponseKind kind;
    std::string raw;
    ElmRecovery recovery;
};

// One cleaned line → response. echoOf = command sent (spaces removed); a
// matching line is the adapter's own echo and is dropped. Returns false when
// the line is chatter ('+' lines, empty, echo) → ignore it entirely.
bool elmClassifyLine(const std::string &line, const std::string &echoOf,
                     ElmResponse &out);

// ── Byte framing ────────────────────────────────────────────────────────────

// Incremental framer: feed raw link bytes → complete lines. Bytes arrive in
// whatever chunks SPP hands over, so state survives across feed() calls.
// Drops non-printables at BYTE level (clones emit 0xFF junk on reset),
// strips cosmetic spaces, splits on CR or LF, and surfaces '>' as its own
// line even with no CR before it — the prompt is the pacing signal for the
// whole half-duplex conversation.
class ElmLineParser {
public:
    ElmLineParser() {}
    void reset();   // discard partial state on (re)connect
    void feed(const char *data, int len, std::vector<std::string> &lines);

private:
    std::string pending_;
};

// ── ISO-TP reassembly ───────────────────────────────────────────────────────

struct ElmAssembly {
    bool ok;                    // complete message assembled
    bool truncated;             // declared len > received → NEVER return partial
    uint32_t canId;             // response CAN id (ATH1 header; 0 for ATH0 shape)
    std::vector<uint8_t> payload;
};

// Groups ATH1-shaped frames ("<3-hex-id><PCI><data>") by CAN ID and assembles
// each group: single / first / consecutive frames, SN checked, out-of-order
// dropped. ATH0 "N:hex" lines accumulate into one assembly with
// canId = defaultCanId. Truncated messages are returned with ok=false so the
// caller can report them — that is the ATFCSM1-ignored signature. Their
// payload carries the received bytes for diagnostics ONLY; never forward a
// partial message (ok=false ⇒ not a message).
// Result order: single-frame results first (line order), then multi-frame
// groups (first-appearance order), then the ATH0 accumulator if any.
std::vector<ElmAssembly> elmAssembleAll(const std::vector<std::string> &dataLines,
                                        uint32_t defaultCanId);

// ── Command builders ────────────────────────────────────────────────────────

std::string elmHexRequest(const uint8_t *payload, int len); // → "0902"
std::string elmCmdSetHeader(uint32_t canId);                // → "ATSH7DF"
std::string elmCmdSetFilter(uint32_t canId);                // → "ATCRA7E8"
std::string elmCmdFcHeader(uint32_t canId);                 // → "ATFCSH7DF"
bool elmParseVoltage(const std::string &text, int &millivolts); // "12.3V" → 12300

// ── Init plan ───────────────────────────────────────────────────────────────

// Spec deviation (per T1): command/purpose are std::string, not const char* —
// addressing commands are built dynamically and must stay heap-stable.
// Field names/order otherwise as spec'd.
struct ElmStep {
    std::string command;
    bool required;              // true → '?' or non-OK fails init (CRITICAL set)
    std::string purpose;
};

// ATE0 ATL0 ATS0 ATH1 ATCAF1 ATSP6 ATAT1 ATST32 — order is load-bearing:
// ATSP clears header/filter state, so addressing must come after. ATZ is
// handled by the session (6 s timeout, ATI banner fallback), not here.
const std::vector<ElmStep> &elmInitSequence();

// ATSH<req> ATCRA<resp> ATFCSH<req> ATFCSD300000 ATFCSM1 — re-sent whenever
// the target changes. Required = the CRITICAL set only: {ATFCSH, ATFCSD}.
// ATSH is NOT required — a clone answering it oddly must not hard-fail init.
// ATCRA is omitted when responseCanId == 0 (functional broadcasts must not
// filter replies away). ATFCSM1 is not required: clones answer OK then ignore
// it — the session should warn, not fail.
std::vector<ElmStep> elmAddressingSequence(uint32_t requestCanId,
                                           uint32_t responseCanId);
