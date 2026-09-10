#pragma once
// ElmJ2534 — FakeElmLink, shared by tests/test_session.cpp and
// tests/test_device.cpp
//
// Scripts the wire conversation captured from the bench (ELM327 v1.5 clone on
// COM3, obd2.json scenario): replies are served in small 3-7 byte chunks so
// the line parser's cross-read accumulation is exercised on every test, and
// every written command is recorded so tests assert both protocol content AND
// command order — order is load-bearing (ATSP6 clears addressing state).
//
// Extracted verbatim from test_session.cpp; the session suite's 20 cases and
// the device suite drive the same fake.

#include "../ElmSession.h"

#include <string>
#include <vector>
#include <string.h>

class FakeElmLink : public IElmLink {
public:
    FakeElmLink()
        : orderOk(true), discardCount(0), openShouldFail(false),
          writeShouldFail(false), readShouldFail(false),
          opened(false), closed(false), scriptPos_(0), replyPos_(0) {}

    // Queue one expected command and the raw bytes the "adapter" answers
    // with. Order matters: write() checks against the next expected command.
    void script(const std::string &cmd, const std::string &reply) {
        expectedCmds_.push_back(cmd);
        expectedReplies_.push_back(reply);
    }
    void scriptOk(const std::string &cmd) { script(cmd, "OK\r>"); }

    virtual bool open(const char *port, int baud, int openTimeoutSec,
                      std::string &err) {
        (void)port; (void)baud; (void)openTimeoutSec;
        if (openShouldFail) { err = "fake open failed"; return false; }
        opened = true;
        return true;
    }
    virtual void close() { closed = true; }

    virtual bool write(const char *data, int len) {
        if (writeShouldFail) return false;
        std::string cmd(data, (size_t)len);
        if (!cmd.empty() && cmd[cmd.size() - 1] == '\r')
            cmd.erase(cmd.size() - 1);
        writtenCmds.push_back(cmd);
        if (scriptPos_ < expectedCmds_.size() && expectedCmds_[scriptPos_] == cmd) {
            currentReply_ = expectedReplies_[scriptPos_];
            scriptPos_++;
        } else {
            // Off-script command: flag it, but answer a bare prompt so the
            // exchange ends fast — the order assertion is what fails the test.
            orderOk = false;
            currentReply_ = ">";
        }
        replyPos_ = 0;
        return true;
    }

    virtual int read(uint8_t *buf, int maxLen, int timeoutMs) {
        (void)timeoutMs;
        if (readShouldFail) return -1;
        if (replyPos_ >= currentReply_.size())
            return 0;   // script exhausted → silence until the deadline
        // Small varying chunks (3-7 bytes): SPP respects no line boundaries,
        // and the CR and '>' must survive being split across reads.
        int chunk = 3 + (int)(replyPos_ % 5);
        size_t avail = currentReply_.size() - replyPos_;
        int n = (int)((size_t)chunk < avail ? (size_t)chunk : avail);
        if (n > maxLen) n = maxLen;
        memcpy(buf, currentReply_.data() + replyPos_, (size_t)n);
        replyPos_ += (size_t)n;
        return n;
    }

    virtual void discardInput() { discardCount++; }

    // Comma-joined record of every command written, for order assertions.
    std::string written() const {
        std::string s;
        for (size_t i = 0; i < writtenCmds.size(); i++) {
            if (i) s += ",";
            s += writtenCmds[i];
        }
        return s;
    }

    std::vector<std::string> writtenCmds;
    bool orderOk;             // every write matched the next scripted command
    int discardCount;         // one per exchange (input purged before write)
    bool openShouldFail;
    bool writeShouldFail;
    bool readShouldFail;
    bool opened, closed;

private:
    std::vector<std::string> expectedCmds_;
    std::vector<std::string> expectedReplies_;
    size_t scriptPos_;
    std::string currentReply_;
    size_t replyPos_;
};

// ── Init scripting helpers (shared) ────────────────────────────────────────

static const char *const INIT_CMDS[] = { "ATE0", "ATL0", "ATS0", "ATH1",
                                         "ATCAF1", "ATSP6", "ATAT1", "ATST32" };
static const int INIT_CMD_COUNT = 8;

// The addressing block, exactly as elmAddressingSequence builds it
// (ATCRA only when a response id filters the RX side).
//
// craClear mirrors the SESSION's extra rule (ElmSession::applyAddressing):
// with resp == 0 while a restrictive ATCRA is still loaded in the chip, an
// ATCRA000 (accept-all) is inserted where ATCRA would have been — omitting it
// would leave the old filter in force and silently shut the response side.
static inline void scriptAddressing(FakeElmLink &f, uint32_t req, uint32_t resp,
                                    bool craClear = false) {
    f.scriptOk(elmCmdSetHeader(req));
    if (resp != 0) f.scriptOk(elmCmdSetFilter(resp));
    else if (craClear) f.scriptOk(elmCmdSetFilter(0));
    f.scriptOk(elmCmdFcHeader(req));
    f.scriptOk("ATFCSD300000");
    f.scriptOk("ATFCSM1");
}

// ATZ banner + 8 init steps + default 7DF/7E8 addressing — the happy path.
static inline void scriptFullInit(FakeElmLink &f) {
    f.script("ATZ", "ELM327v1.5\r>");
    for (int i = 0; i < INIT_CMD_COUNT; i++) f.scriptOk(INIT_CMDS[i]);
    scriptAddressing(f, ELM_DEFAULT_REQUEST_CAN_ID, ELM_DEFAULT_RESPONSE_CAN_ID);
}

static const char *const FULL_INIT_ORDER =
    "ATZ,ATE0,ATL0,ATS0,ATH1,ATCAF1,ATSP6,ATAT1,ATST32,"
    "ATSH7DF,ATCRA7E8,ATFCSH7DF,ATFCSD300000,ATFCSM1";
