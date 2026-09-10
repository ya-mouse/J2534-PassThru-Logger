// ElmJ2534 — ElmSession implementation
//
// Implements: docs/elm-j2534-design.md → "ElmSession interfaces (T3 contract)"
// Semantics ported from CanDroid :core:elm327 Elm327Session.kt (exchange /
// applySteps / recover / setTarget) and candroid-fw tools/elm327.py (reset
// timeout, ATI banner fallback, error-class handling).

#include "ElmSession.h"

#include <chrono>
#include <stdio.h>

#ifdef _WIN32
#include "Logger.h"
// Warnings about non-required init steps belong in the log, not in err: a
// clone that rejects ATFCSM1 still does single frames, and failing init over
// it would lock out usable hardware.
#define ELM_LOGV(...) g_logger.verbose(__VA_ARGS__)
#else
// Native builds compile without logging — the session core stays sans-IO,
// and sans-win32 (Logger.h pulls in windows.h).
#define ELM_LOGV(...) ((void)0)
#endif

// Per-read slice inside the exchange deadline: bounds how long a wedged link
// can oversleep the deadline, while staying far below prompt cadence.
static const int ELM_READ_SLICE_MS = 100;
static const int ELM_READ_BUF = 512;

namespace {

// First genuine failure in a reply. Waiting (SEARCHING...) is excluded: it
// precedes a perfectly good reply, and counting it as a failure would
// discard the first read after every protocol negotiation.
const ElmResponse *firstFailure(const std::vector<ElmResponse> &responses) {
    for (size_t i = 0; i < responses.size(); i++) {
        const ElmResponse &r = responses[i];
        if (r.kind == ElmResponseKind::Failure && r.recovery != ElmRecovery::Wait)
            return &responses[i];
    }
    return NULL;
}

// Banner preference: a recognised banner line wins; clones that answer ATZ
// with plain text still get their text reported. Empty when neither showed.
std::string pickBanner(const std::vector<ElmResponse> &responses) {
    for (size_t i = 0; i < responses.size(); i++)
        if (responses[i].kind == ElmResponseKind::Banner) return responses[i].raw;
    for (size_t i = 0; i < responses.size(); i++)
        if (responses[i].kind == ElmResponseKind::Text) return responses[i].raw;
    return std::string();
}

} // namespace

ElmSession::ElmSession(IElmLink &link)
    : link_(link), addressed_(false), addressedReq_(0), addressedResp_(0),
      craSet_(false) {}

// ── Exchange loop ───────────────────────────────────────────────────────────

bool ElmSession::exchange(const std::string &cmd, int timeoutMs,
                          std::vector<ElmResponse> &responses,
                          std::string &err) {
    responses.clear();
    // Stale bytes from an abandoned exchange must not leak into this one:
    // half-duplex means whatever is still in flight belongs to a dead command.
    parser_.reset();
    link_.discardInput();

    const std::string wire = cmd + "\r";
    if (!link_.write(wire.c_str(), (int)wire.size())) {
        err = "write to adapter failed during '" + cmd + "' (link down)";
        return false;
    }

    using std::chrono::steady_clock;
    using std::chrono::milliseconds;
    const steady_clock::time_point deadline =
        steady_clock::now() + milliseconds(timeoutMs);

    bool sawPrompt = false;
    while (!sawPrompt) {
        const steady_clock::time_point now = steady_clock::now();
        if (now >= deadline) break;
        const long long remaining =
            std::chrono::duration_cast<milliseconds>(deadline - now).count();
        int slice = remaining < ELM_READ_SLICE_MS ? (int)remaining
                                                  : ELM_READ_SLICE_MS;
        // Never a zero/negative wait: read() would return immediately and this
        // loop would hot-spin (burning a core and starving the adapter) until
        // the deadline check above caught up.
        if (slice < 1) slice = 1;
        uint8_t buf[ELM_READ_BUF];
        const int n = link_.read(buf, (int)sizeof(buf), slice);
        if (n < 0) {
            err = "read from adapter failed during '" + cmd + "' (link down)";
            return false;
        }
        if (n == 0) continue;   // slice timed out; the deadline still governs

        std::vector<std::string> lines;
        parser_.feed((const char *)buf, n, lines);
        for (size_t i = 0; i < lines.size(); i++) {
            ElmResponse resp;
            // false = echo of cmd, '+' chatter or empty → not a response.
            if (!elmClassifyLine(lines[i], cmd, resp)) continue;
            if (resp.kind == ElmResponseKind::Prompt) {
                sawPrompt = true;   // '>' ends the command; later bytes in the
                break;              // same chunk belong to nobody — drop them
            }
            responses.push_back(resp);
        }
    }

    if (!sawPrompt) {
        // The adapter prints '>' even for errors: silence within the deadline
        // means it is wedged or gone, not merely unhappy.
        char msg[96];
        snprintf(msg, sizeof(msg),
                 "no '>' prompt within %dms (link wedged or adapter gone)",
                 timeoutMs);
        err = msg;
        return false;
    }
    return true;
}

// ── Init / addressing ───────────────────────────────────────────────────────

bool ElmSession::applySteps(const std::vector<ElmStep> &steps,
                            std::string &err) {
    std::vector<ElmResponse> responses;
    for (size_t i = 0; i < steps.size(); i++) {
        const ElmStep &step = steps[i];
        if (!exchange(step.command, ELM_EXCHANGE_TIMEOUT_MS, responses, err)) {
            // A half-applied plan leaves the chip in an unknown state; keeping
            // the remembered ids would make the next setTarget early-return and
            // send frames with the WRONG CAN id.
            addressed_ = false;
            return false;
        }
        const ElmResponse *failure = firstFailure(responses);
        if (!failure) continue;
        if (step.required) {
            addressed_ = false;   // same reasoning as above
            err = "adapter rejected '" + step.command + "' (" + step.purpose +
                  "): " + failure->raw;
            return false;
        }
        ELM_LOGV("ElmSession: '%s' (%s) not accepted: %s — continuing",
                 step.command.c_str(), step.purpose.c_str(),
                 failure->raw.c_str());
    }
    return true;
}

bool ElmSession::open(const char *port, int baud, int openTimeoutSec,
                      std::string &err) {
    if (!link_.open(port, baud, openTimeoutSec, err))
        return false;
    parser_.reset();   // RFCOMM can hand over pre-open junk; start from silence
    banner_.clear();   // a fresh link may lead to a different adapter
    addressed_ = false;
    addressedReq_ = 0;
    addressedResp_ = 0;
    // The handshake that follows starts with ATZ, which wipes the chip's
    // filter — until then nothing is known about the receive side.
    craSet_ = false;
    return true;
}

bool ElmSession::initialise(std::string &banner, std::string &err) {
    banner.clear();
    parser_.reset();
    addressed_ = false;
    addressedReq_ = 0;
    addressedResp_ = 0;
    craSet_ = false;   // ATZ below clears every filter the chip held

    std::vector<ElmResponse> responses;
    // Be generous exactly once: a reset takes up to a second on clones
    // (measured worst ~1.5 s), and mistaking a slow ATZ for a dead adapter
    // is a support ticket.
    if (!exchange("ATZ", ELM_RESET_TIMEOUT_MS, responses, err))
        return false;
    banner_ = pickBanner(responses);
    for (int probe = 0; probe < 2 && banner_.empty(); probe++) {
        // Some adapters answer ATZ with nothing recognisable; ATI is the
        // documented fallback, probed twice before giving up on identity.
        if (!exchange("ATI", ELM_EXCHANGE_TIMEOUT_MS, responses, err))
            return false;
        banner_ = pickBanner(responses);
    }
    if (banner_.empty()) banner_ = "unknown adapter";
    banner = banner_;

    if (!applySteps(elmInitSequence(), err))
        return false;
    // Addressing comes last and only after ATSP6: ATSP clears header/filter
    // state, so anything set before it would be silently dropped.
    return applyAddressing(ELM_DEFAULT_REQUEST_CAN_ID,
                           ELM_DEFAULT_RESPONSE_CAN_ID, err);
}

bool ElmSession::setTarget(uint32_t reqCanId, uint32_t respCanId,
                           std::string &err) {
    if (addressed_ && addressedReq_ == reqCanId && addressedResp_ == respCanId)
        return true;   // already loaded in the adapter — zero round trips
    return applyAddressing(reqCanId, respCanId, err);
}

bool ElmSession::applyAddressing(uint32_t reqCanId, uint32_t respCanId,
                                 std::string &err) {
    std::vector<ElmStep> steps = elmAddressingSequence(reqCanId, respCanId);
    if (respCanId == 0 && craSet_) {
        // elmAddressingSequence omits ATCRA for resp==0, but OMITTING it leaves
        // the chip's previous filter in force — after a FLOW_CONTROL filter is
        // removed (or a second channel writes elsewhere) every response would
        // be dropped by the adapter with nothing on the wire to explain it.
        // ATCRA000 is the accept-all spelling; it goes where ATCRA would have.
        ElmStep clearFilter;
        clearFilter.command = elmCmdSetFilter(0);
        // Not required: a clone that rejects ATCRA000 with '?' still answers
        // single-frame reads, and failing the write over it would lock out
        // usable hardware — the rejection is logged by applySteps.
        clearFilter.required = false;
        clearFilter.purpose = "clear the receive filter (accept all ids)";
        steps.insert(steps.begin() + 1, clearFilter);
    }

    // Invalidate BEFORE applying: a partially applied block must never leave
    // the remembered ids looking valid, or the next setTarget early-returns
    // and frames go out with the wrong CAN id. (applySteps' failure returns
    // invalidate too — the redundancy is deliberate belt-and-braces; neither
    // guard alone is covered by a failing test, so do not "simplify" one away.)
    addressed_ = false;
    // Conservative: the ATCRA may land even if a later step fails, so assume
    // the filter is set from the moment we ask for one.
    if (respCanId != 0) craSet_ = true;

    if (!applySteps(steps, err))
        return false;

    if (respCanId == 0) craSet_ = false;   // accept-all is positively in force
    addressed_ = true;
    addressedReq_ = reqCanId;
    addressedResp_ = respCanId;
    return true;
}

// ── Requests ────────────────────────────────────────────────────────────────

bool ElmSession::request(const uint8_t *payload, int len, int timeoutMs,
                         std::vector<ElmAssembly> &out, std::string &err,
                         ElmRecovery *recoveryOut) {
    out.clear();
    if (recoveryOut) *recoveryOut = ElmRecovery::None;

    const std::string cmd = elmHexRequest(payload, len);
    if (cmd.empty()) {
        err = "empty request payload";
        return false;
    }

    std::vector<ElmResponse> responses;
    if (!exchange(cmd, timeoutMs > 0 ? timeoutMs : ELM_EXCHANGE_TIMEOUT_MS,
                  responses, err)) {
        // No prompt = the adapter's state is unknowable: it may have reset
        // itself (settings lost — LVRESET semantics without the token) or the
        // RFCOMM link may be gone. Reinitialise is the only recovery that
        // covers both, which is why this is not None.
        if (recoveryOut) *recoveryOut = ElmRecovery::Reinitialise;
        return false;
    }

    std::vector<std::string> dataLines;
    for (size_t i = 0; i < responses.size(); i++) {
        const ElmResponse &r = responses[i];
        if (r.kind == ElmResponseKind::Data) {
            dataLines.push_back(r.raw);
            continue;
        }
        if (r.kind == ElmResponseKind::Failure &&
            r.recovery != ElmRecovery::Wait) {
            // Latch the FIRST failure: it is the one the adapter meant, and
            // its own wording ("NODATA" vs "CANERROR") is what tells the
            // caller silence from a broken bus.
            err = r.raw;
            if (recoveryOut) *recoveryOut = r.recovery;
            ELM_LOGV("ElmSession: request '%s' failed: %s",
                     cmd.c_str(), r.raw.c_str());
            return false;
        }
        // Waiting (SEARCHING...) is not a failure: keep what came after it.
    }

    // Success with zero parseable lines returns true + empty out: the adapter
    // answered nothing usable, which is "no reply", not an error. Truncated
    // assemblies come back ok=false for the caller to report — never hidden,
    // never forwarded as messages.
    //
    // defaultCanId only matters to the ATH0 fallback shape ("N:hex"), where the
    // chip dropped the source id: attribute those to the expected responder.
    // With no filter installed that id is 0, which would surface as a message
    // from CAN id 0 — fall back to the OBD default instead.
    const uint32_t defaultCanId =
        (addressed_ && addressedResp_ != 0) ? addressedResp_
                                            : ELM_DEFAULT_RESPONSE_CAN_ID;
    out = elmAssembleAll(dataLines, defaultCanId);
    return true;
}

bool ElmSession::readVoltageMillivolts(int &mv, std::string &err) {
    std::vector<ElmResponse> responses;
    if (!exchange("ATRV", ELM_EXCHANGE_TIMEOUT_MS, responses, err))
        return false;
    // The adapter's own failure token first: "no voltage text" would hide a
    // CANERROR/BUSERROR (a real bench condition) behind a shrug.
    const ElmResponse *failure = firstFailure(responses);
    if (failure) {
        err = "ATRV: " + failure->raw;
        return false;
    }
    for (size_t i = 0; i < responses.size(); i++) {
        if (responses[i].kind != ElmResponseKind::Text) continue;
        if (elmParseVoltage(responses[i].raw, mv))
            return true;
        err = "unparseable ATRV reply '" + responses[i].raw + "'";
        return false;
    }
    err = "ATRV returned no voltage text";
    return false;
}

// ── Recovery / shutdown ─────────────────────────────────────────────────────

bool ElmSession::recover(ElmRecovery kind, std::string &err) {
    std::vector<ElmResponse> responses;
    switch (kind) {
    case ElmRecovery::ResetProtocol:
        if (!exchange("ATPC", ELM_EXCHANGE_TIMEOUT_MS, responses, err))
            return false;
        // The protocol reset already dropped the chip's header/filter state:
        // invalidate now, so a failing ATSP6 below cannot leave the remembered
        // ids looking valid. craSet_ is cleared only AFTER ATSP6 succeeds —
        // until then the chip's filter state is unknown, and believing it
        // clear would skip the ATCRA000 a later accept-all needs.
        addressed_ = false;
        if (!exchange("ATSP6", ELM_EXCHANGE_TIMEOUT_MS, responses, err))
            return false;
        craSet_ = false;
        // ATSP clears header/filter state on most firmware: force the
        // addressing block to be re-sent on the next setTarget.
        return true;

    case ElmRecovery::WarmStart:
        if (!exchange("ATWS", ELM_RESET_TIMEOUT_MS, responses, err))
            return false;
        // ATWS reset the chip — everything it remembered, including the
        // receive filter, is gone whether or not the re-init below completes.
        addressed_ = false;
        craSet_ = false;
        // Re-run the init plan but NOT ATZ: a warm start already reset the
        // chip, and another ATZ would cost the full clone-reset timeout.
        if (!applySteps(elmInitSequence(), err))
            return false;
        return true;

    case ElmRecovery::Reinitialise: {
        std::string banner;
        return initialise(banner, err);
    }

    case ElmRecovery::Retry:
    case ElmRecovery::Wait:
    case ElmRecovery::Unsupported:
    case ElmRecovery::None:
    default:
        return true;   // nothing to undo — the caller retries or skips itself
    }
}

void ElmSession::shutdown() {
    std::vector<ElmResponse> responses;
    std::string err;
    // ATLP sleeps the adapter so it does not drain the vehicle battery; a
    // dying link must not stop us releasing the port, so failures are ignored.
    exchange("ATLP", ELM_SHUTDOWN_TIMEOUT_MS, responses, err);
    link_.close();
}
