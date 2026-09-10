// ElmJ2534 Unit Tests — Native build (macOS/Linux)
// Tests the sans-IO ELM327 core (parser / classifier / assembler / builders)
// without any Win32 or link dependency:
//   make -f ElmJ2534/tests/Makefile.native test
//
// Captures are from the live bench (ELM327 v1.5 clone on COM3, obd2.json
// scenario replayed through a Feather M4): 0100 single frame and the 0902
// multi-frame VIN.

#include "../ElmProto.h"
#include <stdio.h>
#include <string.h>

// ═══════════════════════════════════════════════════════════════════════════
// Test framework (same shape as ReplayJ2534/tests/test_configstore.cpp)
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

// ═══════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════

static std::vector<std::string> feedAll(const char *s) {
    ElmLineParser p;
    std::vector<std::string> lines;
    p.feed(s, (int)strlen(s), lines);
    return lines;
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

// Bench VIN capture, ATH1 shape: id 0x7E8, first frame declares 0x14 bytes.
static const char *VIN_FF  = "7E81014490201574444";
static const char *VIN_CF1 = "7E82132303430303131";
static const char *VIN_CF2 = "7E82241313233343536";
static const char *VIN_HEX = "4902015744443230343030313141313233343536";

// ═══════════════════════════════════════════════════════════════════════════
// ElmLineParser
// ═══════════════════════════════════════════════════════════════════════════

TEST(parser_cr_split) {
    std::vector<std::string> l = feedAll("OK\rNODATA\rSEARCHING...\r");
    ASSERT_EQ(3, (int)l.size());
    ASSERT_STR_EQ("OK", l[0]);
    ASSERT_STR_EQ("NODATA", l[1]);
    ASSERT_STR_EQ("SEARCHING...", l[2]);
}

TEST(parser_bare_prompt) {
    // The prompt can arrive with no CR before it — this is the exact wire
    // shape of the bench VIN read.
    std::vector<std::string> l = feedAll("7E81014490201574444\r>");
    ASSERT_EQ(2, (int)l.size());
    ASSERT_STR_EQ("7E81014490201574444", l[0]);
    ASSERT_STR_EQ(">", l[1]);
}

TEST(parser_prompt_splits_pending) {
    // '>' terminates even a mid-line chunk: data line first, then prompt.
    std::vector<std::string> l = feedAll("4100183B0011>");
    ASSERT_EQ(2, (int)l.size());
    ASSERT_STR_EQ("4100183B0011", l[0]);
    ASSERT_STR_EQ(">", l[1]);
}

TEST(parser_crlf_variants) {
    std::vector<std::string> l = feedAll("A\r\nB\nC\r");
    ASSERT_EQ(3, (int)l.size());
    ASSERT_STR_EQ("A", l[0]);
    ASSERT_STR_EQ("B", l[1]);
    ASSERT_STR_EQ("C", l[2]);
}

TEST(parser_junk_drop) {
    // Clones emit 0xFF/0x00 junk on reset; drop at byte level, not char level.
    const char junk[] = { (char)0xFF, (char)0xFE, 'O', 'K', (char)0x00,
                          '\r', (char)0x80, '>' };
    ElmLineParser p;
    std::vector<std::string> l;
    p.feed(junk, (int)sizeof(junk), l);
    ASSERT_EQ(2, (int)l.size());
    ASSERT_STR_EQ("OK", l[0]);
    ASSERT_STR_EQ(">", l[1]);
}

TEST(parser_space_strip) {
    // ATS0 makes spaces cosmetic; clones that ignore ATS0 still send them.
    std::vector<std::string> l = feedAll("NO DATA\r12 .3V\r7E8 06 41 00\r");
    ASSERT_EQ(3, (int)l.size());
    ASSERT_STR_EQ("NODATA", l[0]);
    ASSERT_STR_EQ("12.3V", l[1]);
    ASSERT_STR_EQ("7E8064100", l[2]);
}

TEST(parser_chunk_boundary) {
    // SPP splits replies mid-line at arbitrary points; state must persist.
    ElmLineParser p;
    std::vector<std::string> l;
    p.feed("7E8 1014", 8, l);
    ASSERT_EQ(0, (int)l.size());
    p.feed("490201574444\r>", 14, l);
    ASSERT_EQ(2, (int)l.size());
    ASSERT_STR_EQ("7E81014490201574444", l[0]);
    ASSERT_STR_EQ(">", l[1]);
}

TEST(parser_reset) {
    ElmLineParser p;
    std::vector<std::string> l;
    p.feed("PARTIAL", 7, l);
    p.reset();   // (re)connect must not leak stale bytes into first reply
    p.feed("\r>", 2, l);
    ASSERT_EQ(1, (int)l.size());
    ASSERT_STR_EQ(">", l[0]);
}

// ═══════════════════════════════════════════════════════════════════════════
// elmClassifyLine
// ═══════════════════════════════════════════════════════════════════════════

TEST(classify_ok_prompt) {
    ElmResponse r;
    ASSERT_TRUE(elmClassifyLine("OK", "", r));
    ASSERT_EQ((int)ElmResponseKind::Ok, (int)r.kind);
    ASSERT_EQ((int)ElmRecovery::None, (int)r.recovery);
    ASSERT_TRUE(elmClassifyLine(">", "", r));
    ASSERT_EQ((int)ElmResponseKind::Prompt, (int)r.kind);
}

TEST(classify_drops) {
    ElmResponse r;
    ASSERT_TRUE(!elmClassifyLine("", "", r));            // empty
    ASSERT_TRUE(!elmClassifyLine("+CONNECTED", "", r));  // clone chatter
    ASSERT_TRUE(!elmClassifyLine("+IPD: 1", "", r));
}

TEST(classify_echo_drop) {
    // Software echo suppression: clones disagree about ATE0.
    ElmResponse r;
    ASSERT_TRUE(!elmClassifyLine("ATE0", "ATE0", r));
    ASSERT_TRUE(!elmClassifyLine("ate0", "ATE0", r));       // case-insensitive
    ASSERT_TRUE(!elmClassifyLine("AT SH 7DF", "ATSH7DF", r)); // spaces removed
    ASSERT_TRUE(elmClassifyLine("OK", "ATE0", r));          // not the echo
    ASSERT_EQ((int)ElmResponseKind::Ok, (int)r.kind);
    ASSERT_TRUE(elmClassifyLine("ATE0", "", r));            // no echoOf → kept
}

TEST(classify_failures) {
    struct { const char *line; ElmRecovery rec; } cases[] = {
        { "NODATA",          ElmRecovery::Retry },
        { "STOPPED",         ElmRecovery::Retry },
        { "CANERROR",        ElmRecovery::ResetProtocol },
        { "BUSERROR",        ElmRecovery::ResetProtocol },
        { "UNABLETOCONNECT", ElmRecovery::ResetProtocol },
        { "NABLETO",         ElmRecovery::ResetProtocol },  // clone spelling
        { "DATAERROR",       ElmRecovery::WarmStart },
        { "BUFFERFULL",      ElmRecovery::WarmStart },
        { "<RXERROR",        ElmRecovery::WarmStart },
        { "<RX ERROR",       ElmRecovery::WarmStart },      // spaces tolerated
        { "LVRESET",         ElmRecovery::Reinitialise },
        { "?",               ElmRecovery::Unsupported },
    };
    ElmResponse r;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ASSERT_TRUE(elmClassifyLine(cases[i].line, "", r));
        ASSERT_EQ((int)ElmResponseKind::Failure, (int)r.kind);
        ASSERT_EQ((int)cases[i].rec, (int)r.recovery);
    }
}

TEST(classify_waiting) {
    // SEARCHING is not a failure: treating it as one discards the first
    // good read after every protocol negotiation.
    ElmResponse r;
    ASSERT_TRUE(elmClassifyLine("SEARCHING...", "", r));
    ASSERT_EQ((int)ElmResponseKind::Waiting, (int)r.kind);
    ASSERT_EQ((int)ElmRecovery::Wait, (int)r.recovery);
    ASSERT_TRUE(elmClassifyLine("SEARCHING", "", r));
    ASSERT_EQ((int)ElmResponseKind::Waiting, (int)r.kind);
}

TEST(classify_data) {
    ElmResponse r;
    ASSERT_TRUE(elmClassifyLine("7E8064100183B0011", "", r));   // ATH1 frame
    ASSERT_EQ((int)ElmResponseKind::Data, (int)r.kind);
    ASSERT_TRUE(elmClassifyLine("7e8064100183b0011", "", r));   // lowercase
    ASSERT_EQ((int)ElmResponseKind::Data, (int)r.kind);
    ASSERT_TRUE(elmClassifyLine("4100183B0011", "", r));        // even pure hex
    ASSERT_EQ((int)ElmResponseKind::Data, (int)r.kind);
    ASSERT_TRUE(elmClassifyLine("0:49020157", "", r));          // ATH0 shape
    ASSERT_EQ((int)ElmResponseKind::Data, (int)r.kind);
}

TEST(classify_banner) {
    ElmResponse r;
    ASSERT_TRUE(elmClassifyLine("ELM327V1.5", "", r));
    ASSERT_EQ((int)ElmResponseKind::Banner, (int)r.kind);
    ASSERT_STR_EQ("ELM327V1.5", r.raw);
    ASSERT_TRUE(elmClassifyLine("Elm327 v1.5", "", r));   // raw preserves case
    ASSERT_EQ((int)ElmResponseKind::Banner, (int)r.kind);
    ASSERT_STR_EQ("Elm327 v1.5", r.raw);
    ASSERT_TRUE(elmClassifyLine("OBDLINK MX", "", r));
    ASSERT_EQ((int)ElmResponseKind::Banner, (int)r.kind);
    ASSERT_TRUE(elmClassifyLine("STN1234", "", r));
    ASSERT_EQ((int)ElmResponseKind::Banner, (int)r.kind);
    ASSERT_TRUE(elmClassifyLine("SCANTOOL.NET", "", r));
    ASSERT_EQ((int)ElmResponseKind::Banner, (int)r.kind);
    ASSERT_TRUE(elmClassifyLine("V2.1", "", r));         // bare version shape
    ASSERT_EQ((int)ElmResponseKind::Banner, (int)r.kind);
}

TEST(classify_text) {
    ElmResponse r;
    ASSERT_TRUE(elmClassifyLine("12.3V", "", r));        // ATRV
    ASSERT_EQ((int)ElmResponseKind::Text, (int)r.kind);
    ASSERT_STR_EQ("12.3V", r.raw);
    ASSERT_TRUE(elmClassifyLine("ISO 15765-4 CAN 11-BIT/500 KBIT", "", r));
    ASSERT_EQ((int)ElmResponseKind::Text, (int)r.kind);  // ATDP description
    ASSERT_TRUE(elmClassifyLine("BUS INIT: OK", "", r));
    ASSERT_EQ((int)ElmResponseKind::Text, (int)r.kind);  // progress notice
}

// ═══════════════════════════════════════════════════════════════════════════
// elmAssembleAll
// ═══════════════════════════════════════════════════════════════════════════

TEST(assemble_single_frame) {
    // Bench 0100 read: 7E8 06 41 00 18 3B 00 11 → six payload bytes.
    std::vector<std::string> lines;
    lines.push_back("7E8064100183B0011");
    std::vector<ElmAssembly> a = elmAssembleAll(lines, 0);
    ASSERT_EQ(1, (int)a.size());
    ASSERT_TRUE(a[0].ok);
    ASSERT_TRUE(!a[0].truncated);
    ASSERT_EQ(0x7E8, (int)a[0].canId);
    ASSERT_TRUE(hexEq(a[0].payload, "4100183B0011"));
}

TEST(assemble_vin_bench) {
    // Real bench capture: 0902 → three ATH1 lines, 20-byte ISO-TP payload
    // carrying 49 02 01 + "WDD2040011A123456".
    std::vector<std::string> lines;
    lines.push_back(VIN_FF);
    lines.push_back(VIN_CF1);
    lines.push_back(VIN_CF2);
    std::vector<ElmAssembly> a = elmAssembleAll(lines, 0);
    ASSERT_EQ(1, (int)a.size());
    ASSERT_TRUE(a[0].ok);
    ASSERT_TRUE(!a[0].truncated);
    ASSERT_EQ(0x7E8, (int)a[0].canId);
    ASSERT_EQ(20, (int)a[0].payload.size());
    ASSERT_TRUE(hexEq(a[0].payload, VIN_HEX));
    // 49 02 <count> <ascii VIN> — spot-check the decoded string.
    ASSERT_EQ(0x49, a[0].payload[0]);
    ASSERT_EQ(0x02, a[0].payload[1]);
    ASSERT_TRUE(memcmp(&a[0].payload[3], "WDD2040011A123456", 17) == 0);
}

TEST(assemble_truncated) {
    // CF2 never arrived: the ATFCSM1-ignored signature. Report, never return
    // the partial as a message.
    std::vector<std::string> lines;
    lines.push_back(VIN_FF);
    lines.push_back(VIN_CF1);
    std::vector<ElmAssembly> a = elmAssembleAll(lines, 0);
    ASSERT_EQ(1, (int)a.size());
    ASSERT_TRUE(!a[0].ok);
    ASSERT_TRUE(a[0].truncated);
    ASSERT_EQ(0x7E8, (int)a[0].canId);
}

TEST(assemble_out_of_order_cf_dropped) {
    // SN2 arrives before SN1: the misnumbered frame is dropped rather than
    // corrupting the payload; the in-order CF still completes the message
    // (declared 0x0D = FF 6 bytes + CF 7 bytes).
    std::vector<std::string> lines;
    lines.push_back("7E8100D490201574444");
    lines.push_back("7E82232303430303131");   // SN2, want 1 → dropped
    lines.push_back("7E82132303430303131");   // SN1 → accepted
    std::vector<ElmAssembly> a = elmAssembleAll(lines, 0);
    ASSERT_EQ(1, (int)a.size());
    ASSERT_TRUE(a[0].ok);
    ASSERT_TRUE(!a[0].truncated);
    ASSERT_TRUE(hexEq(a[0].payload, "49020157444432303430303131"));
}

TEST(assemble_two_ecus_functional) {
    // A functional 0100 gets an answer from every ECU; both groups assemble
    // independently, singles in line order.
    std::vector<std::string> lines;
    lines.push_back("7E8064100183B0011");
    lines.push_back("7E9064100BE3B0022");
    std::vector<ElmAssembly> a = elmAssembleAll(lines, 0);
    ASSERT_EQ(2, (int)a.size());
    ASSERT_TRUE(a[0].ok && a[1].ok);
    ASSERT_EQ(0x7E8, (int)a[0].canId);
    ASSERT_TRUE(hexEq(a[0].payload, "4100183B0011"));
    ASSERT_EQ(0x7E9, (int)a[1].canId);
    ASSERT_TRUE(hexEq(a[1].payload, "4100BE3B0022"));
}

TEST(assemble_two_ecus_mixed_frames) {
    // One ECU answers multi-frame, the other single, interleaved on the wire.
    std::vector<std::string> lines;
    lines.push_back(VIN_FF);
    lines.push_back("7E9064100BE3B0022");
    lines.push_back(VIN_CF1);
    lines.push_back(VIN_CF2);
    std::vector<ElmAssembly> a = elmAssembleAll(lines, 0x7E8);
    ASSERT_EQ(2, (int)a.size());
    // Singles first, then multi-frame groups.
    ASSERT_EQ(0x7E9, (int)a[0].canId);
    ASSERT_TRUE(a[0].ok);
    ASSERT_EQ(0x7E8, (int)a[1].canId);
    ASSERT_TRUE(a[1].ok);
    ASSERT_TRUE(hexEq(a[1].payload, VIN_HEX));
}

TEST(assemble_ath0_fallback) {
    // Clone ignoring ATH1: chip pre-assembled "N:hex" lines, source lost —
    // attributed to defaultCanId and accumulated across lines.
    std::vector<std::string> lines;
    lines.push_back("0:49020157");
    lines.push_back("1:444432303430303131");
    lines.push_back("2:41313233343536");
    std::vector<ElmAssembly> a = elmAssembleAll(lines, 0x7E8);
    ASSERT_EQ(1, (int)a.size());
    ASSERT_TRUE(a[0].ok);
    ASSERT_TRUE(!a[0].truncated);
    ASSERT_EQ(0x7E8, (int)a[0].canId);
    ASSERT_TRUE(hexEq(a[0].payload, VIN_HEX));
}

TEST(assemble_skips_nonframes) {
    // Defensive: error tokens and runt lines that reach the assembler anyway
    // must not fabricate messages.
    std::vector<std::string> lines;
    lines.push_back("NODATA");
    lines.push_back("SEARCHING...");
    lines.push_back("4902");      // > 3 chars but body < 2 after id strip
    lines.push_back("XYZ12");     // not hex
    lines.push_back("");
    std::vector<ElmAssembly> a = elmAssembleAll(lines, 0x7E8);
    ASSERT_EQ(0, (int)a.size());
}

TEST(assemble_single_frame_trims_to_declared_len) {
    // PCI declares 7 bytes; the printed frame carries padding beyond it.
    std::vector<std::string> lines;
    lines.push_back("7E8074100183B00112233");
    std::vector<ElmAssembly> a = elmAssembleAll(lines, 0);
    ASSERT_EQ(1, (int)a.size());
    ASSERT_TRUE(a[0].ok);
    ASSERT_EQ(7, (int)a[0].payload.size());
    ASSERT_TRUE(hexEq(a[0].payload, "4100183B001122"));
}

TEST(assemble_single_frame_truncated) {
    // SF declares 6 bytes but carries 3: same contract as multi-frame —
    // reported truncated, never returned ok with a silently shortened payload.
    std::vector<std::string> lines;
    lines.push_back("7E806410018");
    std::vector<ElmAssembly> a = elmAssembleAll(lines, 0);
    ASSERT_EQ(1, (int)a.size());
    ASSERT_TRUE(!a[0].ok);
    ASSERT_TRUE(a[0].truncated);
    ASSERT_EQ(0x7E8, (int)a[0].canId);
    ASSERT_EQ(3, (int)a[0].payload.size());   // received bytes for diagnostics
    ASSERT_TRUE(hexEq(a[0].payload, "410018"));

    // SF_DL == 0 is malformed ISO-TP and lands in the same bucket.
    lines.clear();
    lines.push_back("7E80041");
    a = elmAssembleAll(lines, 0);
    ASSERT_EQ(1, (int)a.size());
    ASSERT_TRUE(!a[0].ok);
    ASSERT_TRUE(a[0].truncated);
}

TEST(assemble_sn_wrap) {
    // 0x76 = 118 bytes: FF carries 6, sixteen CFs carry 7 each, and the last
    // CF's sequence number wraps F → 0. Payload = 0x01..0x76.
    std::vector<std::string> lines;
    lines.push_back("7E81076010203040506");
    for (int sn = 1; sn <= 16; sn++) {
        char line[32];
        sprintf(line, "7E82%X", sn & 0x0F);
        for (int b = 0; b < 7; b++) {
            char hex[4];
            sprintf(hex, "%02X", 6 + (sn - 1) * 7 + b + 1);
            strcat(line, hex);
        }
        lines.push_back(line);
    }
    std::vector<ElmAssembly> a = elmAssembleAll(lines, 0);
    ASSERT_EQ(1, (int)a.size());
    ASSERT_TRUE(a[0].ok);
    ASSERT_TRUE(!a[0].truncated);
    ASSERT_EQ(0x7E8, (int)a[0].canId);
    ASSERT_EQ(118, (int)a[0].payload.size());
    for (int i = 0; i < 118; i++)
        ASSERT_EQ(i + 1, a[0].payload[i]);
}

TEST(assemble_stray_cf_ignored) {
    // A CF whose CAN ID never saw a first frame emits no message — and does
    // not disturb the healthy group that follows it on the wire.
    std::vector<std::string> lines;
    lines.push_back("7E92132303430303131");   // stray CF, no 7E9 FF
    lines.push_back(VIN_FF);
    lines.push_back(VIN_CF1);
    lines.push_back(VIN_CF2);
    std::vector<ElmAssembly> a = elmAssembleAll(lines, 0);
    ASSERT_EQ(1, (int)a.size());
    ASSERT_EQ(0x7E8, (int)a[0].canId);
    ASSERT_TRUE(a[0].ok);
    ASSERT_TRUE(!a[0].truncated);
    ASSERT_TRUE(hexEq(a[0].payload, VIN_HEX));
}

TEST(classify_even_hex_word_dropped) {
    // Document-via-test: a 4-letter hex word is legitimately Data (the
    // classifier cannot know better), and the assembler harmlessly drops it —
    // after the 3-char id strip the body is odd-length, so it is not a frame.
    ElmResponse r;
    ASSERT_TRUE(elmClassifyLine("CAFE", "", r));
    ASSERT_EQ((int)ElmResponseKind::Data, (int)r.kind);
    std::vector<std::string> lines;
    lines.push_back("CAFE");
    std::vector<ElmAssembly> a = elmAssembleAll(lines, 0);
    ASSERT_EQ(0, (int)a.size());
}

// ═══════════════════════════════════════════════════════════════════════════
// Command builders + voltage
// ═══════════════════════════════════════════════════════════════════════════

TEST(builder_hex_request) {
    const uint8_t vin[] = { 0x09, 0x02 };
    ASSERT_STR_EQ("0902", elmHexRequest(vin, 2));
    const uint8_t pids[] = { 0x01, 0x00 };
    ASSERT_STR_EQ("0100", elmHexRequest(pids, 2));
    const uint8_t ab[] = { 0xAB, 0xCD, 0xEF };
    ASSERT_STR_EQ("ABCDEF", elmHexRequest(ab, 3));   // uppercase, no spaces
    ASSERT_STR_EQ("", elmHexRequest(NULL, 0));
}

TEST(builder_can_id_commands) {
    ASSERT_STR_EQ("ATSH7DF", elmCmdSetHeader(0x7DF));
    ASSERT_STR_EQ("ATSH7E0", elmCmdSetHeader(0x7E0));
    ASSERT_STR_EQ("ATSH7FF", elmCmdSetHeader(0x7FF));       // max 11-bit
    ASSERT_STR_EQ("ATSH18DAF110", elmCmdSetHeader(0x18DAF110)); // 29-bit → 8 digits
    ASSERT_STR_EQ("ATCRA7E8", elmCmdSetFilter(0x7E8));
    ASSERT_STR_EQ("ATCRA7DF", elmCmdSetFilter(0x7DF));
    ASSERT_STR_EQ("ATFCSH7DF", elmCmdFcHeader(0x7DF));
    ASSERT_STR_EQ("ATFCSH7E0", elmCmdFcHeader(0x7E0));
}

TEST(voltage_parse) {
    int mv = -1;
    ASSERT_TRUE(elmParseVoltage("12.3V", mv));  ASSERT_EQ(12300, mv);
    ASSERT_TRUE(elmParseVoltage("12.30V", mv)); ASSERT_EQ(12300, mv);
    ASSERT_TRUE(elmParseVoltage("12.3", mv));   ASSERT_EQ(12300, mv);
    ASSERT_TRUE(elmParseVoltage("14.75V", mv)); ASSERT_EQ(14750, mv);
    ASSERT_TRUE(elmParseVoltage("0.0V", mv));   ASSERT_EQ(0, mv);
    ASSERT_TRUE(elmParseVoltage("ATRV 12.6V", mv)); ASSERT_EQ(12600, mv);
    ASSERT_TRUE(!elmParseVoltage("NODATA", mv));
    ASSERT_TRUE(!elmParseVoltage("", mv));
}

// ═══════════════════════════════════════════════════════════════════════════
// Init plan
// ═══════════════════════════════════════════════════════════════════════════

TEST(init_sequence) {
    const std::vector<ElmStep> &plan = elmInitSequence();
    ASSERT_EQ(8, (int)plan.size());
    // Order is load-bearing: ATSP clears header/filter state.
    const char *order[] = { "ATE0", "ATL0", "ATS0", "ATH1",
                            "ATCAF1", "ATSP6", "ATAT1", "ATST32" };
    const bool req[] = { true, false, false, true, true, true, false, false };
    for (size_t i = 0; i < plan.size(); i++) {
        ASSERT_STR_EQ(order[i], plan[i].command);
        ASSERT_EQ((int)req[i], (int)plan[i].required);
        ASSERT_TRUE(!plan[i].purpose.empty());
    }
    // Static plan: heap-stable across calls, safe to iterate while held.
    ASSERT_TRUE(&elmInitSequence() == &plan);
}

TEST(addressing_sequence) {
    std::vector<ElmStep> s = elmAddressingSequence(0x7DF, 0x7E8);
    ASSERT_EQ(5, (int)s.size());
    // Required = CRITICAL set only {ATFCSH, ATFCSD}; ATSH is deliberately
    // not critical — a clone answering it oddly must not hard-fail init.
    ASSERT_STR_EQ("ATSH7DF", s[0].command);   ASSERT_TRUE(!s[0].required);
    ASSERT_STR_EQ("ATCRA7E8", s[1].command);  ASSERT_TRUE(!s[1].required);
    ASSERT_STR_EQ("ATFCSH7DF", s[2].command); ASSERT_TRUE(s[2].required);
    ASSERT_STR_EQ("ATFCSD300000", s[3].command); ASSERT_TRUE(s[3].required);
    ASSERT_STR_EQ("ATFCSM1", s[4].command);   ASSERT_TRUE(!s[4].required);

    // resp == 0 (functional broadcast): ATCRA omitted — filtering to one
    // responder would hide the other ECUs answering 0x7DF.
    s = elmAddressingSequence(0x7DF, 0);
    ASSERT_EQ(4, (int)s.size());
    ASSERT_STR_EQ("ATSH7DF", s[0].command);      ASSERT_TRUE(!s[0].required);
    ASSERT_STR_EQ("ATFCSH7DF", s[1].command);    ASSERT_TRUE(s[1].required);
    ASSERT_STR_EQ("ATFCSD300000", s[2].command); ASSERT_TRUE(s[2].required);
    ASSERT_STR_EQ("ATFCSM1", s[3].command);      ASSERT_TRUE(!s[3].required);

    // Physical target: every id tracks the request/response pair.
    s = elmAddressingSequence(0x7E0, 0x7E8);
    ASSERT_EQ(5, (int)s.size());
    ASSERT_STR_EQ("ATSH7E0", s[0].command);   ASSERT_TRUE(!s[0].required);
    ASSERT_STR_EQ("ATCRA7E8", s[1].command);  ASSERT_TRUE(!s[1].required);
    ASSERT_STR_EQ("ATFCSH7E0", s[2].command); ASSERT_TRUE(s[2].required);
    ASSERT_STR_EQ("ATFCSD300000", s[3].command); ASSERT_TRUE(s[3].required);
    ASSERT_STR_EQ("ATFCSM1", s[4].command);   ASSERT_TRUE(!s[4].required);
}

// ═══════════════════════════════════════════════════════════════════════════

int main() {
    printf("\n=== ElmJ2534 ElmProto Tests (native) ===\n\n");

    RUN_TEST(parser_cr_split);
    RUN_TEST(parser_bare_prompt);
    RUN_TEST(parser_prompt_splits_pending);
    RUN_TEST(parser_crlf_variants);
    RUN_TEST(parser_junk_drop);
    RUN_TEST(parser_space_strip);
    RUN_TEST(parser_chunk_boundary);
    RUN_TEST(parser_reset);

    RUN_TEST(classify_ok_prompt);
    RUN_TEST(classify_drops);
    RUN_TEST(classify_echo_drop);
    RUN_TEST(classify_failures);
    RUN_TEST(classify_waiting);
    RUN_TEST(classify_data);
    RUN_TEST(classify_even_hex_word_dropped);
    RUN_TEST(classify_banner);
    RUN_TEST(classify_text);

    RUN_TEST(assemble_single_frame);
    RUN_TEST(assemble_vin_bench);
    RUN_TEST(assemble_truncated);
    RUN_TEST(assemble_out_of_order_cf_dropped);
    RUN_TEST(assemble_two_ecus_functional);
    RUN_TEST(assemble_two_ecus_mixed_frames);
    RUN_TEST(assemble_ath0_fallback);
    RUN_TEST(assemble_skips_nonframes);
    RUN_TEST(assemble_single_frame_trims_to_declared_len);
    RUN_TEST(assemble_single_frame_truncated);
    RUN_TEST(assemble_sn_wrap);
    RUN_TEST(assemble_stray_cf_ignored);

    RUN_TEST(builder_hex_request);
    RUN_TEST(builder_can_id_commands);
    RUN_TEST(voltage_parse);

    RUN_TEST(init_sequence);
    RUN_TEST(addressing_sequence);

    printf("\n%d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0) printf(", %d FAILED", g_tests_failed);
    printf("\n\n");
    return g_tests_failed > 0 ? 1 : 0;
}
