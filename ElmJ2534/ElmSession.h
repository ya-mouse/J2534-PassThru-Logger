#pragma once
// ElmJ2534 — ELM327 session: exchange loop, init, addressing, request, recovery
//
// Implements: docs/elm-j2534-design.md → "ElmSession interfaces (T3 contract)"
//
// Sans-IO apart from the injected IElmLink: no windows.h reaches this header
// (ElmLink.h keeps its Win32 half behind #ifdef _WIN32), so the entire
// conversation logic builds and tests natively (tests/test_session.cpp).
//
// The adapter is strictly half-duplex with one command outstanding; this
// class holds no lock of its own — the caller (T4 ElmDevice) serialises all
// access, mirroring CanDroid's Elm327Session.

#include "ElmProto.h"
#include "ElmLink.h"

#include <string>
#include <vector>
#include <stdint.h>

// Spec timeouts (docs/elm-j2534-design.md → ElmSession contract).
static const int ELM_EXCHANGE_TIMEOUT_MS = 5000;  // slow multi-frame + BT jitter
static const int ELM_RESET_TIMEOUT_MS = 6000;     // clone reset worst ~1.5 s; generous once
static const int ELM_SHUTDOWN_TIMEOUT_MS = 1000;  // ATLP on the way out

// Default addressing applied by initialise(): the OBD-II functional broadcast
// pair. T4's filter handling records its own request id from FLOW_CONTROL_FILTER.
static const uint32_t ELM_DEFAULT_REQUEST_CAN_ID = 0x7DF;
static const uint32_t ELM_DEFAULT_RESPONSE_CAN_ID = 0x7E8;

class ElmSession {
public:
    explicit ElmSession(IElmLink &link);

    // Bring the link up (deadline-guarded inside SerialElmLink) and clear all
    // protocol state. Writes nothing to the adapter.
    bool open(const char *port, int baud, int openTimeoutSec, std::string &err);

    // ATZ (banner, ATI fallback) + init plan + default 7DF/7E8 addressing.
    // A rejected REQUIRED step fails the whole handshake: continuing with,
    // say, flow control unset leaves every multi-frame read timing out with
    // nothing to point at.
    bool initialise(std::string &banner, std::string &err);

    // Re-address only when the target changed — the adapter remembers
    // ATSH/ATCRA/ATFCSH, and re-sending costs five round trips.
    bool setTarget(uint32_t reqCanId, uint32_t respCanId, std::string &err);

    // One OBD request → all reassembled ISO-TP replies.
    // Returns true with EMPTY out when nothing parseable answered (NODATA
    // aside — that is a Failure), and true with out[i].ok==false /
    // truncated==true when a multi-frame message arrived incomplete (the
    // ATFCSM1-ignored signature): the caller must report truncation and
    // never forward a partial payload.
    // Returns false on adapter failure tokens (err = the adapter's own
    // wording, recoveryOut = what clears it) or on a wedged/gone link
    // (recoveryOut = Reinitialise — the adapter's state is unknown).
    bool request(const uint8_t *payload, int len, int timeoutMs,
                 std::vector<ElmAssembly> &out, std::string &err,
                 ElmRecovery *recoveryOut);

    // ATRV → READ_VBATT ("12.3V" → 12300).
    bool readVoltageMillivolts(int &mv, std::string &err);

    // Apply the recovery a failure token asked for. Retry/Wait/Unsupported/
    // None are no-ops (the caller retries or skips on its own).
    bool recover(ElmRecovery kind, std::string &err);

    // ATLP puts the adapter to sleep so it does not drain the battery on the
    // OBD port; errors are ignored — the link closes either way.
    void shutdown();

    // Space-free by construction: the line parser strips spaces before
    // classification, so the stored banner reads like "ELM327v1.5" even when
    // the clone printed "ELM327 v1.5". T4's PassThruReadVersion reports the
    // firmware string straight from here.
    const std::string &banner() const { return banner_; }

private:
    // Write one command, read/parse/classify until the '>' prompt or the
    // deadline. Echo of cmd, '+' chatter and empty lines are dropped inside.
    bool exchange(const std::string &cmd, int timeoutMs,
                  std::vector<ElmResponse> &responses, std::string &err);

    // Run init/addressing steps; a required step the adapter rejects fails.
    bool applySteps(const std::vector<ElmStep> &steps, std::string &err);

    // Apply an addressing block and publish it as the remembered target.
    // Owns the ATCRA bookkeeping: elmAddressingSequence omits ATCRA when the
    // response id is 0, but omission leaves the chip's previous filter in
    // force, so an explicit ATCRA000 (accept-all) is inserted whenever a
    // restrictive filter may still be loaded.
    bool applyAddressing(uint32_t reqCanId, uint32_t respCanId,
                         std::string &err);

    ElmSession(const ElmSession &);            // holds a link reference
    ElmSession &operator=(const ElmSession &);

    IElmLink &link_;
    ElmLineParser parser_;
    std::string banner_;
    bool addressed_;          // the ids below are currently loaded in the adapter
    uint32_t addressedReq_;
    uint32_t addressedResp_;
    // True while the chip may be holding a RESTRICTIVE receive filter (an
    // ATCRA<id> that was not cleared). Conservative: it is set when an
    // addressing block carrying ATCRA is attempted, and cleared only by
    // positive evidence — a successful accept-all addressing, or a chip reset
    // (ATZ/ATSP6/ATWS). A stale filter is invisible on the wire: replies just
    // stop arriving.
    bool craSet_;
};
