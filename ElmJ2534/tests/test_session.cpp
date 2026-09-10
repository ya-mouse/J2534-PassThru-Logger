// ElmJ2534 Session Tests — FakeLink-driven, native AND mingw builds
//   make -f ElmJ2534/tests/Makefile.native test   (macOS/Linux, no windows.h)
//   make -f ElmJ2534/tests/Makefile.test          (Windows exes via mingw)
//
// FakeElmLink (tests/fake_link.h, shared with test_device.cpp) scripts the
// wire conversation captured from the bench, in small chunks, and records every
// command so tests assert both protocol content AND command order — order is
// load-bearing (ATSP6 clears addressing state).

#include "fake_link.h"
#include <stdio.h>
#include <string.h>

// ═══════════════════════════════════════════════════════════════════════════
// Test framework (same shape as test_elmproto.cpp)
// ═══════════════════════════════════════════════════════════════════════════

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    printf("  %-50s ", #name); \
    g_tests_run++; \
    test_##name(); \
    g_tests_passed++; \
    printf("[PASS]\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((long)(expected) != (long)(actual)) { \
        printf("[FAIL]\n    %s:%d: expected %ld, got %ld\n", \
               __FILE__, __LINE__, (long)(expected), (long)(actual)); \
        g_tests_failed++; g_tests_passed--; return; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("[FAIL]\n    %s:%d: assertion failed: %s\n", \
               __FILE__, __LINE__, #cond); \
        g_tests_failed++; g_tests_passed--; return; \
    } \
} while(0)

#define ASSERT_STR_EQ(expected, actual) do { \
    std::string e_(expected), a_(actual); \
    if (e_ != a_) { \
        printf("[FAIL]\n    %s:%d: expected \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, e_.c_str(), a_.c_str()); \
        g_tests_failed++; g_tests_passed--; return; \
    } \
} while(0)

#define ASSERT_HAS(haystack, needle) do { \
    const std::string h_ = (haystack); \
    const std::string n_ = (needle); \
    if (h_.find(n_) == std::string::npos) { \
        printf("[FAIL]\n    %s:%d: \"%s\" not found in \"%s\"\n", \
                __FILE__, __LINE__, n_.c_str(), h_.c_str()); \
        g_tests_failed++; g_tests_passed--; return; \
    } \
} while(0)

// ═══════════════════════════════════════════════════════════════════════════
// Helpers (FakeElmLink and the init scripting come from fake_link.h)
// ═══════════════════════════════════════════════════════════════════════════

// open() + initialise() on a fully scripted happy-path init.
static bool initSession(FakeElmLink &f, ElmSession &s, std::string &err) {
    if (!s.open("COM9", 38400, 1, err)) return false;
    std::string banner;
    return s.initialise(banner, err);
}

static bool hexEq(const std::vector<uint8_t> &payload, const char *hex) {
    static const char H[] = "0123456789ABCDEF";
    std::string s;
    for (size_t i = 0; i < payload.size(); i++) {
        s += H[payload[i] >> 4];
        s += H[payload[i] & 0x0F];
    }
    return s == std::string(hex);
}

// Bench captures, ATH1 shape (same as test_elmproto.cpp).
static const char *VIN_REPLY =
    "SEARCHING...\r7E81014490201574444\r7E82132303430303131\r"
    "7E82241313233343536\r>";
static const char *VIN_HEX = "4902015744443230343030313141313233343536";
static const char *PIDS_REPLY = "7E8064100183B0011\r>";

static const uint8_t PIDS_REQ[] = { 0x01, 0x00 };   // → "0100"
static const uint8_t VIN_REQ[] = { 0x09, 0x02 };     // → "0902"

// ═══════════════════════════════════════════════════════════════════════════
// 1. Full init happy path
// ═══════════════════════════════════════════════════════════════════════════

TEST(init_happy_path) {
    FakeElmLink f;
    scriptFullInit(f);
    ElmSession s(f);
    std::string err, banner;

    ASSERT_TRUE(s.open("COM9", 38400, 1, err));
    ASSERT_TRUE(f.opened);
    ASSERT_TRUE(s.initialise(banner, err));
    ASSERT_STR_EQ("ELM327v1.5", banner);
    // Space-free by contract: the parser strips spaces before classification.
    ASSERT_STR_EQ("ELM327v1.5", s.banner());
    ASSERT_STR_EQ(FULL_INIT_ORDER, f.written());
    ASSERT_TRUE(f.orderOk);
    ASSERT_EQ(14, f.discardCount);   // one discardInput per exchange
}

// ═══════════════════════════════════════════════════════════════════════════
// 2-4. Requests: single frame, multi-frame VIN, echo suppression
// ═══════════════════════════════════════════════════════════════════════════

TEST(request_single_frame) {
    FakeElmLink f;
    scriptFullInit(f);
    ElmSession s(f);
    std::string err;
    ASSERT_TRUE(initSession(f, s, err));

    f.script("0100", PIDS_REPLY);
    std::vector<ElmAssembly> out;
    ElmRecovery rec = ElmRecovery::None;
    ASSERT_TRUE(s.request(PIDS_REQ, 2, 5000, out, err, &rec));
    ASSERT_EQ(1, (int)out.size());
    ASSERT_TRUE(out[0].ok);
    ASSERT_TRUE(!out[0].truncated);
    ASSERT_EQ(0x7E8, (int)out[0].canId);
    ASSERT_TRUE(hexEq(out[0].payload, "4100183B0011"));
    ASSERT_STR_EQ(std::string(FULL_INIT_ORDER) + ",0100", f.written());
}

TEST(request_multiframe_vin) {
    FakeElmLink f;
    scriptFullInit(f);
    ElmSession s(f);
    std::string err;
    ASSERT_TRUE(initSession(f, s, err));

    // SEARCHING... is Waiting, not a failure: it precedes the good reply.
    f.script("0902", VIN_REPLY);
    std::vector<ElmAssembly> out;
    ElmRecovery rec = ElmRecovery::None;
    ASSERT_TRUE(s.request(VIN_REQ, 2, 5000, out, err, &rec));
    ASSERT_EQ(1, (int)out.size());
    ASSERT_TRUE(out[0].ok);
    ASSERT_TRUE(!out[0].truncated);
    ASSERT_EQ(0x7E8, (int)out[0].canId);
    ASSERT_EQ(20, (int)out[0].payload.size());
    ASSERT_TRUE(hexEq(out[0].payload, VIN_HEX));
    ASSERT_TRUE(memcmp(&out[0].payload[3], "WDD2040011A123456", 17) == 0);
}

TEST(request_echo_despite_ate0) {
    FakeElmLink f;
    scriptFullInit(f);
    ElmSession s(f);
    std::string err;
    ASSERT_TRUE(initSession(f, s, err));

    // A clone that ignored ATE0 echoes the command back; the classifier
    // drops it and the assembly must come out identical.
    f.script("0100", "0100\r" + std::string(PIDS_REPLY));
    std::vector<ElmAssembly> out;
    ElmRecovery rec = ElmRecovery::None;
    ASSERT_TRUE(s.request(PIDS_REQ, 2, 5000, out, err, &rec));
    ASSERT_EQ(1, (int)out.size());
    ASSERT_TRUE(out[0].ok);
    ASSERT_EQ(0x7E8, (int)out[0].canId);
    ASSERT_TRUE(hexEq(out[0].payload, "4100183B0011"));
}

// ═══════════════════════════════════════════════════════════════════════════
// 5-6. Failure tokens and their recoveries
// ═══════════════════════════════════════════════════════════════════════════

TEST(request_nodata) {
    FakeElmLink f;
    scriptFullInit(f);
    ElmSession s(f);
    std::string err;
    ASSERT_TRUE(initSession(f, s, err));

    f.script("0100", "NODATA\r>");
    std::vector<ElmAssembly> out;
    ElmRecovery rec = ElmRecovery::None;
    ASSERT_TRUE(!s.request(PIDS_REQ, 2, 5000, out, err, &rec));
    ASSERT_EQ((int)ElmRecovery::Retry, (int)rec);
    ASSERT_TRUE(err.find("NODATA") != std::string::npos);
    ASSERT_EQ(0, (int)out.size());
}

TEST(request_canerror_reset_recovery) {
    FakeElmLink f;
    scriptFullInit(f);
    ElmSession s(f);
    std::string err;
    ASSERT_TRUE(initSession(f, s, err));

    f.script("0100", "CANERROR\r>");
    std::vector<ElmAssembly> out;
    ElmRecovery rec = ElmRecovery::None;
    ASSERT_TRUE(!s.request(PIDS_REQ, 2, 5000, out, err, &rec));
    ASSERT_EQ((int)ElmRecovery::ResetProtocol, (int)rec);
    ASSERT_STR_EQ("CANERROR", err);

    // ResetProtocol recovery = ATPC + ATSP6, nothing else.
    f.scriptOk("ATPC");
    f.scriptOk("ATSP6");
    ASSERT_TRUE(s.recover(ElmRecovery::ResetProtocol, err));

    // ATSP6 cleared the adapter's addressing state, so even the UNCHANGED
    // target must be re-sent in full — the session cannot know the chip
    // still remembers.
    scriptAddressing(f, 0x7DF, 0x7E8);
    ASSERT_TRUE(s.setTarget(0x7DF, 0x7E8, err));
    ASSERT_STR_EQ(std::string(FULL_INIT_ORDER) + ",0100,ATPC,ATSP6,"
                  "ATSH7DF,ATCRA7E8,ATFCSH7DF,ATFCSD300000,ATFCSM1",
                  f.written());
    ASSERT_TRUE(f.orderOk);
}

// ═══════════════════════════════════════════════════════════════════════════
// 7-8. Required vs non-required init steps
// ═══════════════════════════════════════════════════════════════════════════

TEST(init_required_step_rejected) {
    FakeElmLink f;
    f.script("ATZ", "ELM327v1.5\r>");
    f.scriptOk("ATE0");
    f.scriptOk("ATL0");
    f.scriptOk("ATS0");
    f.script("ATH1", "?\r>");   // required: without headers the assembler is blind
    ElmSession s(f);
    std::string err, banner;
    ASSERT_TRUE(s.open("COM9", 38400, 1, err));
    ASSERT_TRUE(!s.initialise(banner, err));
    ASSERT_TRUE(err.find("ATH1") != std::string::npos);
    ASSERT_TRUE(err.find("?") != std::string::npos);
    // Init stops at the rejection: ATCAF1 and beyond are never sent.
    ASSERT_STR_EQ("ATZ,ATE0,ATL0,ATS0,ATH1", f.written());
}

TEST(init_nonrequired_fcsvm_unsupported) {
    FakeElmLink f;
    f.script("ATZ", "ELM327v1.5\r>");
    for (int i = 0; i < INIT_CMD_COUNT; i++) f.scriptOk(INIT_CMDS[i]);
    f.scriptOk("ATSH7DF");
    f.scriptOk("ATCRA7E8");
    f.scriptOk("ATFCSH7DF");
    f.scriptOk("ATFCSD300000");
    // The fragile one: clones answer '?' (or OK and then ignore it). Warn,
    // don't fail — single-frame reads still work.
    f.script("ATFCSM1", "?\r>");
    ElmSession s(f);
    std::string err, banner;
    ASSERT_TRUE(s.open("COM9", 38400, 1, err));
    ASSERT_TRUE(s.initialise(banner, err));
    ASSERT_STR_EQ("ELM327v1.5", banner);
    ASSERT_STR_EQ(FULL_INIT_ORDER, f.written());
}

// ═══════════════════════════════════════════════════════════════════════════
// 9. setTarget: no-op when unchanged, full block on change
// ═══════════════════════════════════════════════════════════════════════════

TEST(set_target_noop_and_change) {
    FakeElmLink f;
    scriptFullInit(f);
    ElmSession s(f);
    std::string err;
    ASSERT_TRUE(initSession(f, s, err));

    // Already addressed to 7DF/7E8 by init: ZERO commands written.
    ASSERT_TRUE(s.setTarget(0x7DF, 0x7E8, err));
    ASSERT_STR_EQ(FULL_INIT_ORDER, f.written());

    // Physical target with resp==0: no restrictive ATCRA is installed, but the
    // chip still holds init's ATCRA7E8, so the session must clear it —
    // omitting ATCRA would leave 7E8 filtered and every other ECU invisible.
    f.scriptOk("ATSH7E0");
    f.scriptOk("ATCRA000");
    f.scriptOk("ATFCSH7E0");
    f.scriptOk("ATFCSD300000");
    f.scriptOk("ATFCSM1");
    ASSERT_TRUE(s.setTarget(0x7E0, 0, err));
    ASSERT_STR_EQ(std::string(FULL_INIT_ORDER) +
                  ",ATSH7E0,ATCRA000,ATFCSH7E0,ATFCSD300000,ATFCSM1",
                  f.written());
    ASSERT_TRUE(f.orderOk);

    // Accept-all is now positively in force: a second resp==0 target must NOT
    // spend another round trip on ATCRA000.
    f.scriptOk("ATSH7E2");
    f.scriptOk("ATFCSH7E2");
    f.scriptOk("ATFCSD300000");
    f.scriptOk("ATFCSM1");
    ASSERT_TRUE(s.setTarget(0x7E2, 0, err));
    ASSERT_STR_EQ(std::string(FULL_INIT_ORDER) +
                  ",ATSH7E0,ATCRA000,ATFCSH7E0,ATFCSD300000,ATFCSM1"
                  ",ATSH7E2,ATFCSH7E2,ATFCSD300000,ATFCSM1",
                  f.written());
    ASSERT_TRUE(f.orderOk);
}

// A FAILED addressing block must not leave the remembered ids looking valid.
// The chip may already hold part of the new sequence (ATSH7E0 below), so a
// later setTarget for the OLD ids has to re-send everything — an early return
// there puts frames on the wire with the wrong CAN id.
TEST(set_target_after_failed_addressing) {
    FakeElmLink f;
    scriptFullInit(f);
    ElmSession s(f);
    std::string err;
    ASSERT_TRUE(initSession(f, s, err));

    f.scriptOk("ATSH7E0");
    f.scriptOk("ATCRA000");
    f.script("ATFCSH7E0", "?\r>");   // required step rejected → block fails
    ASSERT_TRUE(!s.setTarget(0x7E0, 0, err));
    ASSERT_TRUE(err.find("ATFCSH7E0") != std::string::npos);

    // Back to the ids init left behind: they must NOT be treated as current.
    scriptAddressing(f, 0x7DF, 0x7E8);
    ASSERT_TRUE(s.setTarget(0x7DF, 0x7E8, err));
    ASSERT_STR_EQ(std::string(FULL_INIT_ORDER) +
                  ",ATSH7E0,ATCRA000,ATFCSH7E0"
                  ",ATSH7DF,ATCRA7E8,ATFCSH7DF,ATFCSD300000,ATFCSM1",
                  f.written());
    ASSERT_TRUE(f.orderOk);
}

// WarmStart resets the chip, so the addressing it held is gone even though the
// ids have not changed — the next setTarget must re-address in full.
TEST(recover_warm_start_forces_readdress) {
    FakeElmLink f;
    scriptFullInit(f);
    ElmSession s(f);
    std::string err;
    ASSERT_TRUE(initSession(f, s, err));

    f.script("ATWS", "ELM327v1.5\r>");
    for (int i = 0; i < INIT_CMD_COUNT; i++) f.scriptOk(INIT_CMDS[i]);
    ASSERT_TRUE(s.recover(ElmRecovery::WarmStart, err));

    // The ids init left behind (7DF/7E8) are requested again — and must be
    // re-sent, because ATWS wiped them.
    scriptAddressing(f, 0x7DF, 0x7E8);
    ASSERT_TRUE(s.setTarget(0x7DF, 0x7E8, err));
    ASSERT_HAS(f.written(),
               std::string(",ATWS,ATE0,ATL0,ATS0,ATH1,ATCAF1,ATSP6,ATAT1,ATST32") +
               ",ATSH7DF,ATCRA7E8,ATFCSH7DF,ATFCSD300000,ATFCSM1");
    ASSERT_TRUE(f.orderOk);
}

// An ATH0-shaped clone ("N:hex") loses the source id; with no response filter
// installed the assembler must fall back to the OBD default, not CAN id 0.
TEST(ath0_reply_uses_default_response_id) {
    FakeElmLink f;
    scriptFullInit(f);
    ElmSession s(f);
    std::string err;
    ASSERT_TRUE(initSession(f, s, err));

    scriptAddressing(f, 0x7DF, 0, true);
    ASSERT_TRUE(s.setTarget(0x7DF, 0, err));
    f.script("0100", "0:4100183B0011\r>");
    std::vector<ElmAssembly> out;
    ElmRecovery rec = ElmRecovery::None;
    ASSERT_TRUE(s.request(PIDS_REQ, 2, 5000, out, err, &rec));
    ASSERT_EQ(1, (int)out.size());
    ASSERT_TRUE(out[0].ok);
    ASSERT_EQ(0x7E8, (int)out[0].canId);
    ASSERT_TRUE(hexEq(out[0].payload, "4100183B0011"));
}

// ═══════════════════════════════════════════════════════════════════════════
// 10. Truncated multi-frame: reported, never hidden
// ═══════════════════════════════════════════════════════════════════════════

TEST(request_truncated_multiframe) {
    FakeElmLink f;
    scriptFullInit(f);
    ElmSession s(f);
    std::string err;
    ASSERT_TRUE(initSession(f, s, err));

    // FF declares 0x14 bytes; CF2 never arrives — the ATFCSM1-ignored
    // signature. request() succeeds; the assembly reports the truncation.
    f.script("0902",
             "SEARCHING...\r7E81014490201574444\r7E82132303430303131\r>");
    std::vector<ElmAssembly> out;
    ElmRecovery rec = ElmRecovery::None;
    ASSERT_TRUE(s.request(VIN_REQ, 2, 5000, out, err, &rec));
    ASSERT_EQ(1, (int)out.size());
    ASSERT_TRUE(!out[0].ok);
    ASSERT_TRUE(out[0].truncated);
    ASSERT_EQ(0x7E8, (int)out[0].canId);
}

// ═══════════════════════════════════════════════════════════════════════════
// 11. ATRV voltage
// ═══════════════════════════════════════════════════════════════════════════

TEST(read_voltage) {
    FakeElmLink f;
    ElmSession s(f);
    std::string err;
    ASSERT_TRUE(s.open("COM9", 38400, 1, err));

    f.script("ATRV", "12.3V\r>");
    int mv = 0;
    ASSERT_TRUE(s.readVoltageMillivolts(mv, err));
    ASSERT_EQ(12300, mv);
    ASSERT_STR_EQ("ATRV", f.written());

    // The adapter's own failure token is propagated, not a generic shrug: a
    // CANERROR here means the bench supply is down, and "no voltage text" would
    // have hidden it.
    f.script("ATRV", "NODATA\r>");
    ASSERT_TRUE(!s.readVoltageMillivolts(mv, err));
    ASSERT_STR_EQ("ATRV: NODATA", err);

    f.script("ATRV", "CANERROR\r>");
    ASSERT_TRUE(!s.readVoltageMillivolts(mv, err));
    ASSERT_STR_EQ("ATRV: CANERROR", err);
}

// ═══════════════════════════════════════════════════════════════════════════
// 12. Wedged link: no prompt within the deadline
// ═══════════════════════════════════════════════════════════════════════════

TEST(request_no_prompt) {
    FakeElmLink f;
    scriptFullInit(f);
    ElmSession s(f);
    std::string err;
    ASSERT_TRUE(initSession(f, s, err));

    // Data but no '>': the adapter is mid-something or the link is wedged.
    // Small timeout keeps the suite fast — the deadline is real time.
    f.script("0100", "SEARCHING...\r7E8064100183B0011\r");
    std::vector<ElmAssembly> out;
    ElmRecovery rec = ElmRecovery::None;
    ASSERT_TRUE(!s.request(PIDS_REQ, 2, 300, out, err, &rec));
    ASSERT_TRUE(err.find("no '>' prompt") != std::string::npos);
    // State is unknowable after a wedged exchange → full reinit is the only
    // recovery that covers both "chip reset itself" and "RFCOMM gone".
    ASSERT_EQ((int)ElmRecovery::Reinitialise, (int)rec);
}

// ═══════════════════════════════════════════════════════════════════════════
// 13-14. WarmStart and Reinitialise recoveries
// ═══════════════════════════════════════════════════════════════════════════

TEST(recover_warm_start) {
    FakeElmLink f;
    scriptFullInit(f);
    ElmSession s(f);
    std::string err;
    ASSERT_TRUE(initSession(f, s, err));

    f.script("0100", "DATAERROR\r>");
    std::vector<ElmAssembly> out;
    ElmRecovery rec = ElmRecovery::None;
    ASSERT_TRUE(!s.request(PIDS_REQ, 2, 5000, out, err, &rec));
    ASSERT_EQ((int)ElmRecovery::WarmStart, (int)rec);

    // ATWS first, then the 8 init steps — NOT ATZ (warm start already reset).
    f.script("ATWS", "ELM327v1.5\r>");
    for (int i = 0; i < INIT_CMD_COUNT; i++) f.scriptOk(INIT_CMDS[i]);
    ASSERT_TRUE(s.recover(ElmRecovery::WarmStart, err));
    ASSERT_STR_EQ(std::string(FULL_INIT_ORDER) +
                  ",0100,ATWS,ATE0,ATL0,ATS0,ATH1,ATCAF1,ATSP6,ATAT1,ATST32",
                  f.written());
    ASSERT_TRUE(f.orderOk);
}

TEST(recover_reinitialise) {
    FakeElmLink f;
    scriptFullInit(f);
    ElmSession s(f);
    std::string err;
    ASSERT_TRUE(initSession(f, s, err));

    f.script("0100", "LVRESET\r>");   // settings were lost — full reset needed
    std::vector<ElmAssembly> out;
    ElmRecovery rec = ElmRecovery::None;
    ASSERT_TRUE(!s.request(PIDS_REQ, 2, 5000, out, err, &rec));
    ASSERT_EQ((int)ElmRecovery::Reinitialise, (int)rec);

    scriptFullInit(f);   // recovery re-runs the COMPLETE init incl. ATZ
    ASSERT_TRUE(s.recover(ElmRecovery::Reinitialise, err));
    ASSERT_STR_EQ(std::string(FULL_INIT_ORDER) + ",0100," + FULL_INIT_ORDER,
                  f.written());
    ASSERT_TRUE(f.orderOk);

    // Addressing was restored by the reinit: setTarget to the same pair is
    // a no-op again.
    ASSERT_TRUE(s.setTarget(0x7DF, 0x7E8, err));
    ASSERT_STR_EQ(std::string(FULL_INIT_ORDER) + ",0100," + FULL_INIT_ORDER,
                  f.written());
}

// ═══════════════════════════════════════════════════════════════════════════
// 15-17. Lifecycle: shutdown, open failure, link IO failures
// ═══════════════════════════════════════════════════════════════════════════

TEST(shutdown_sleeps_and_closes) {
    FakeElmLink f;
    f.scriptOk("ATLP");
    ElmSession s(f);
    std::string err;
    ASSERT_TRUE(s.open("COM9", 38400, 1, err));
    s.shutdown();
    ASSERT_STR_EQ("ATLP", f.written());
    ASSERT_TRUE(f.closed);
}

TEST(open_failure_propagates) {
    FakeElmLink f;
    f.openShouldFail = true;
    ElmSession s(f);
    std::string err;
    ASSERT_TRUE(!s.open("COM9", 38400, 1, err));
    ASSERT_TRUE(!err.empty());
    ASSERT_EQ(0, (int)f.writtenCmds.size());   // nothing is written to a dead link
}

TEST(link_io_failures_propagate) {
    // Write failure: the very first exchange (ATZ) must fail loudly.
    FakeElmLink fw;
    ElmSession sw(fw);
    std::string err, banner;
    ASSERT_TRUE(sw.open("COM9", 38400, 1, err));
    fw.writeShouldFail = true;
    ASSERT_TRUE(!sw.initialise(banner, err));
    ASSERT_TRUE(err.find("write") != std::string::npos);

    // Read failure (-1) mid-exchange: same contract.
    FakeElmLink fr;
    ElmSession sr(fr);
    ASSERT_TRUE(sr.open("COM9", 38400, 1, err));
    fr.readShouldFail = true;
    ASSERT_TRUE(!sr.initialise(banner, err));
    ASSERT_TRUE(err.find("read") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════

int main() {
    printf("\n=== ElmJ2534 Session Tests (FakeLink) ===\n\n");

    RUN_TEST(init_happy_path);
    RUN_TEST(request_single_frame);
    RUN_TEST(request_multiframe_vin);
    RUN_TEST(request_echo_despite_ate0);
    RUN_TEST(request_nodata);
    RUN_TEST(request_canerror_reset_recovery);
    RUN_TEST(init_required_step_rejected);
    RUN_TEST(init_nonrequired_fcsvm_unsupported);
    RUN_TEST(set_target_noop_and_change);
    RUN_TEST(set_target_after_failed_addressing);
    RUN_TEST(recover_warm_start_forces_readdress);
    RUN_TEST(ath0_reply_uses_default_response_id);
    RUN_TEST(request_truncated_multiframe);
    RUN_TEST(read_voltage);
    RUN_TEST(request_no_prompt);
    RUN_TEST(recover_warm_start);
    RUN_TEST(recover_reinitialise);
    RUN_TEST(shutdown_sleeps_and_closes);
    RUN_TEST(open_failure_propagates);
    RUN_TEST(link_io_failures_propagate);

    printf("\n%d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0) printf(", %d FAILED", g_tests_failed);
    printf("\n\n");
    return g_tests_failed > 0 ? 1 : 0;
}
