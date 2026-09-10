// ElmJ2534 Device Tests — FakeLink-driven J2534 state machine, native build
//   make -f ElmJ2534/tests/Makefile.native test
//
// Compiles ElmDevice.cpp against tests/stubs/windows.h (CRITICAL_SECTION /
// condvar / GetTickCount are no-ops or sleeps there — the suites are
// single-threaded, and WriteMsgs is synchronous, so the RX queue is always
// filled before ReadMsgs runs). Every adapter conversation is scripted through
// the shared FakeElmLink, so these assert on J2534 return codes, RX queue
// contents AND the exact AT command order that reached the wire.

#include "fake_link.h"
#include "../ElmDevice.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// ═══════════════════════════════════════════════════════════════════════════
// Config.h globals: dllmain.cpp owns them in the DLL, the suite owns them here
// ═══════════════════════════════════════════════════════════════════════════

static char g_tlsLastError[256] = {0};

void setLastError(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_tlsLastError, sizeof(g_tlsLastError), fmt, args);
    va_end(args);
}

const char *getLastError() { return g_tlsLastError; }

// ═══════════════════════════════════════════════════════════════════════════
// Test framework (same shape as test_session.cpp)
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

// WriteMsgs/ReadMsgs take in/out parameters and consume link script, so their
// result is captured before it is compared: ASSERT_EQ re-evaluates its argument
// when the comparison fails, and a second call would report the state the first
// one left behind (*pNumMsgs already zeroed) instead of the real return code.
#define ASSERT_RC(expected, call) do { \
    const long rc_ = (call); \
    ASSERT_EQ(expected, rc_); \
} while(0)

// ═══════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════

// Bench captures (same as test_session.cpp), ATH1 shape.
static const char *PIDS_REPLY = "7E8064100183B0011\r>";
static const char *VIN_REPLY =
    "SEARCHING...\r7E81014490201574444\r7E82132303430303131\r"
    "7E82241313233343536\r>";
// A functional request both ECUs answer: two single frames, different ids.
static const char *TWO_ECU_REPLY =
    "7E8064100183B0011\r7E9064100183B0012\r>";
// First frame declares 0x14 bytes; CF2 never arrives (ATFCSM1 ignored).
static const char *TRUNCATED_VIN_REPLY =
    "SEARCHING...\r7E81014490201574444\r7E82132303430303131\r>";

static const uint8_t PIDS_REQ[] = { 0x01, 0x00 };   // → "0100"
static const uint8_t VIN_REQ[] = { 0x09, 0x02 };     // → "0902"

// What a write to 7DF with no flow-control filter puts on the wire: the
// session is addressed to 7DF/7E8 after init, so resp==0 is a change — and
// because a restrictive ATCRA7E8 is loaded, the session must clear it with
// ATCRA000 (accept-all) rather than omit ATCRA and leave 7E8 filtered.
static const char *RETARGET_7DF_OPEN =
    ",ATSH7DF,ATCRA000,ATFCSH7DF,ATFCSD300000,ATFCSM1";
// After a ResetProtocol recovery (ATPC+ATSP6) the chip holds no filter at all,
// so the same retarget omits ATCRA entirely.
static const char *RETARGET_7DF_CLEAN = ",ATSH7DF,ATFCSH7DF,ATFCSD300000,ATFCSM1";

static void putBe(unsigned char *dst, unsigned long id) {
    dst[0] = (unsigned char)((id >> 24) & 0xFF);
    dst[1] = (unsigned char)((id >> 16) & 0xFF);
    dst[2] = (unsigned char)((id >> 8) & 0xFF);
    dst[3] = (unsigned char)(id & 0xFF);
}

static unsigned long beId(const unsigned char *src) {
    return ((unsigned long)src[0] << 24) | ((unsigned long)src[1] << 16) |
           ((unsigned long)src[2] << 8) | (unsigned long)src[3];
}

static std::string hexOf(const unsigned char *data, unsigned long len) {
    static const char H[] = "0123456789ABCDEF";
    std::string s;
    for (unsigned long i = 0; i < len; i++) {
        s += H[data[i] >> 4];
        s += H[data[i] & 0x0F];
    }
    return s;
}

// J2534 ISO15765 write: Data = 4-byte big-endian CAN id + payload.
static PASSTHRU_MSG makeTx(unsigned long canId, const uint8_t *payload,
                           unsigned long len) {
    PASSTHRU_MSG m;
    memset(&m, 0, sizeof(m));
    m.ProtocolID = J2534_ISO15765;
    m.TxFlags = ISO15765_FRAME_PAD;
    m.DataSize = ELM_CAN_ID_BYTES + len;
    m.ExtraDataIndex = m.DataSize;
    putBe(m.Data, canId);
    if (payload && len) memcpy(m.Data + ELM_CAN_ID_BYTES, payload, len);
    return m;
}

// Filter messages carry only the id.
static PASSTHRU_MSG makeIdMsg(unsigned long canId) {
    return makeTx(canId, NULL, 0);
}

static void makeConfig(ElmConfig &cfg, const char *port) {
    memset(&cfg, 0, sizeof(cfg));
    if (port) strncpy(cfg.comPort, port, sizeof(cfg.comPort) - 1);
    cfg.baudRate = 38400;
    cfg.logLevel = 0;
    cfg.openTimeoutSec = 1;
}

// A device with its adapter up: init scripted, fake link injected, config
// pointing at a port. Mirrors what DllMain + PassThruOpen do in the DLL.
struct Bench {
    FakeElmLink link;
    ElmDevice dev;
    unsigned long devId;
    unsigned long chId;

    Bench() : devId(0), chId(0) {}

    long open(const char *port = "COM9") {
        scriptFullInit(link);
        ElmConfig cfg;
        makeConfig(cfg, port);
        dev.attachLinkForTesting(&link);
        dev.init(cfg);
        return dev.openDevice(NULL, &devId);
    }
    long connectIso(unsigned long flags = CAN_ID_BOTH) {
        return dev.connect(devId, J2534_ISO15765, flags, ELM_CAN_BAUD_500K, &chId);
    }
    long write(unsigned long canId, const uint8_t *payload, unsigned long len,
               unsigned long *pNum, unsigned long timeout = 5000) {
        PASSTHRU_MSG tx = makeTx(canId, payload, len);
        return dev.writeMsgs(chId, &tx, pNum, timeout);
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// 1-2. Open / close lifecycle
// ═══════════════════════════════════════════════════════════════════════════

TEST(open_probes_banner_and_version) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(ELM_DEVICE_ID, b.devId);
    ASSERT_TRUE(b.link.opened);
    ASSERT_STR_EQ(FULL_INIT_ORDER, b.link.written());
    ASSERT_TRUE(b.link.orderOk);
    // The banner is the firmware string, space-free by construction.
    ASSERT_STR_EQ("ELM327v1.5", b.dev.firmware());

    char fw[80] = {0}, dll[80] = {0}, api[80] = {0};
    ASSERT_EQ(STATUS_NOERROR, b.dev.readVersion(b.devId, fw, dll, api));
    ASSERT_STR_EQ("ELM327v1.5", fw);
    ASSERT_STR_EQ("ElmJ2534 1.0.0", dll);
    ASSERT_STR_EQ("04.04", api);
    ASSERT_EQ(ERR_INVALID_DEVICE_ID, b.dev.readVersion(42, fw, dll, api));

    // Single-device DLL: a second open reuses the id and touches nothing.
    unsigned long again = 0;
    ASSERT_EQ(STATUS_NOERROR, b.dev.openDevice(NULL, &again));
    ASSERT_EQ((long)b.devId, (long)again);
    ASSERT_STR_EQ(FULL_INIT_ORDER, b.link.written());

    ASSERT_EQ(ERR_INVALID_DEVICE_ID, b.dev.closeDevice(99));
    b.link.scriptOk("ATLP");
    ASSERT_EQ(STATUS_NOERROR, b.dev.closeDevice(b.devId));
    ASSERT_STR_EQ(std::string(FULL_INIT_ORDER) + ",ATLP", b.link.written());
    ASSERT_TRUE(b.link.closed);
    ASSERT_EQ(0, b.dev.channelCount());
    // Closed: the handle no longer validates.
    ASSERT_EQ(ERR_INVALID_DEVICE_ID, b.dev.closeDevice(b.devId));
}

TEST(open_failure_paths) {
    // No port configured and no scan available → the documented message.
    FakeElmLink link;
    ElmDevice dev;
    ElmConfig cfg;
    makeConfig(cfg, "");
    dev.attachLinkForTesting(&link);
    dev.init(cfg);
    unsigned long id = 0;
    ASSERT_EQ(ERR_DEVICE_NOT_CONNECTED, dev.openDevice(NULL, &id));
    ASSERT_HAS(std::string(getLastError()), "no COM port configured");
    ASSERT_HAS(std::string(getLastError()), "ELM_J2534_PORT");
    ASSERT_TRUE(!link.opened);

    // A port that will not open (unpowered dongle / stale link key).
    FakeElmLink dead;
    dead.openShouldFail = true;
    ElmDevice d2;
    makeConfig(cfg, "COM9");
    d2.attachLinkForTesting(&dead);
    d2.init(cfg);
    ASSERT_EQ(ERR_DEVICE_NOT_CONNECTED, d2.openDevice(NULL, &id));
    ASSERT_HAS(std::string(getLastError()), "fake open failed");

    // A rejected REQUIRED init step fails the handshake and releases the port:
    // continuing with flow control unset would leave every multi-frame read
    // timing out with nothing to point at.
    FakeElmLink picky;
    picky.script("ATZ", "ELM327v1.5\r>");
    picky.scriptOk("ATE0");
    picky.scriptOk("ATL0");
    picky.scriptOk("ATS0");
    picky.script("ATH1", "?\r>");
    ElmDevice d3;
    d3.attachLinkForTesting(&picky);
    d3.init(cfg);
    ASSERT_EQ(ERR_DEVICE_NOT_CONNECTED, d3.openDevice(NULL, &id));
    ASSERT_HAS(std::string(getLastError()), "ATH1");
    ASSERT_TRUE(picky.closed);
    ASSERT_EQ(ERR_INVALID_DEVICE_ID, d3.closeDevice(id));
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Connect validation and channel id allocation
// ═══════════════════════════════════════════════════════════════════════════

TEST(connect_validation_and_channel_ids) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(STATUS_NOERROR, b.connectIso());
    ASSERT_EQ(ELM_PREFERRED_CHANNEL_ID, b.chId);

    unsigned long ch = 0;
    ASSERT_EQ(ERR_INVALID_PROTOCOL_ID,
              b.dev.connect(b.devId, J2534_CAN, CAN_ID_BOTH, ELM_CAN_BAUD_500K, &ch));
    ASSERT_EQ(ERR_INVALID_PROTOCOL_ID,
              b.dev.connect(b.devId, J2534_ISO9141, 0, ELM_CAN_BAUD_500K, &ch));
    ASSERT_EQ(ERR_INVALID_BAUDRATE,
              b.dev.connect(b.devId, J2534_ISO15765, CAN_ID_BOTH, 250000, &ch));
    // ATSP6 is 11-bit: 29-bit ids cannot be honoured.
    ASSERT_EQ(ERR_INVALID_FLAGS,
              b.dev.connect(b.devId, J2534_ISO15765, CAN_29BIT_ID, ELM_CAN_BAUD_500K, &ch));
    ASSERT_EQ(ERR_INVALID_FLAGS,
              b.dev.connect(b.devId, J2534_ISO15765, ISO9141_K_LINE_ONLY,
                            ELM_CAN_BAUD_500K, &ch));
    ASSERT_EQ(ERR_INVALID_DEVICE_ID,
              b.dev.connect(42, J2534_ISO15765, 0, ELM_CAN_BAUD_500K, &ch));
    ASSERT_EQ(ERR_NULL_PARAMETER,
              b.dev.connect(b.devId, J2534_ISO15765, 0, ELM_CAN_BAUD_500K, NULL));
    ASSERT_EQ(1, b.dev.channelCount());   // rejected connects allocate nothing

    // P-LIVE-003: a taken preferred id continues ABOVE it (2, 3, 4 — never a
    // restart below the preferred range, and never 1, which is the device id).
    unsigned long ch2 = 0, ch3 = 0;
    ASSERT_EQ(STATUS_NOERROR,
              b.dev.connect(b.devId, J2534_ISO15765, 0, ELM_CAN_BAUD_500K, &ch2));
    ASSERT_EQ(STATUS_NOERROR,
              b.dev.connect(b.devId, J2534_ISO15765, 0, ELM_CAN_BAUD_500K, &ch3));
    ASSERT_EQ(3, (int)ch2);
    ASSERT_EQ(4, (int)ch3);
    ASSERT_EQ(3, b.dev.channelCount());
    ASSERT_TRUE(ch2 != b.devId && ch3 != b.devId);   // no handle collision

    ASSERT_EQ(STATUS_NOERROR, b.dev.disconnect(ch2));
    ASSERT_EQ(ERR_INVALID_CHANNEL_ID, b.dev.disconnect(ch2));
    ASSERT_TRUE(!b.dev.hasChannel(ch2));
    ASSERT_EQ(2, b.dev.channelCount());
    // Connect writes nothing to the adapter: addressing rides the first write.
    ASSERT_STR_EQ(FULL_INIT_ORDER, b.link.written());
}

// ═══════════════════════════════════════════════════════════════════════════
// 4-6. WriteMsgs / ReadMsgs happy paths
// ═══════════════════════════════════════════════════════════════════════════

TEST(write_single_frame_echo_then_response) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(STATUS_NOERROR, b.connectIso());
    scriptAddressing(b.link, 0x7DF, 0, true);
    b.link.script("0100", PIDS_REPLY);

    unsigned long n = 1;
    ASSERT_RC(STATUS_NOERROR, b.write(0x7DF, PIDS_REQ, 2, &n));
    ASSERT_EQ(1, (int)n);
    ASSERT_TRUE(b.link.orderOk);
    ASSERT_STR_EQ(std::string(FULL_INIT_ORDER) + RETARGET_7DF_OPEN + ",0100",
                  b.link.written());

    ASSERT_EQ(2, b.dev.rxQueueSize(b.chId));
    // ReadMsgs never exceeds the caller's capacity: one at a time here.
    PASSTHRU_MSG rx;
    memset(&rx, 0, sizeof(rx));
    unsigned long got = 1;
    ASSERT_RC(STATUS_NOERROR, b.dev.readMsgs(b.chId, &rx, &got, 100));
    ASSERT_EQ(1, (int)got);
    ASSERT_EQ(J2534_ISO15765, (int)rx.ProtocolID);
    ASSERT_EQ(4, (int)rx.DataSize);
    ASSERT_EQ(4, (int)rx.ExtraDataIndex);
    ASSERT_TRUE((rx.RxStatus & TX_MSG_TYPE) != 0);
    ASSERT_TRUE((rx.RxStatus & TX_INDICATION) != 0);
    ASSERT_EQ(0x7DF, (int)beId(rx.Data));

    memset(&rx, 0, sizeof(rx));
    got = 1;
    ASSERT_RC(STATUS_NOERROR, b.dev.readMsgs(b.chId, &rx, &got, 100));
    ASSERT_EQ(1, (int)got);
    ASSERT_EQ(0, (int)rx.RxStatus);
    ASSERT_EQ(0, (int)rx.TxFlags);
    ASSERT_EQ(10, (int)rx.DataSize);
    ASSERT_EQ(10, (int)rx.ExtraDataIndex);
    ASSERT_EQ(0x7E8, (int)beId(rx.Data));
    ASSERT_STR_EQ("4100183B0011", hexOf(rx.Data + 4, rx.DataSize - 4));
    // Timestamp is ms since the port opened, not an epoch.
    ASSERT_TRUE(rx.Timestamp < 60000);
    ASSERT_EQ(0, b.dev.rxQueueSize(b.chId));
}

TEST(write_nodata_is_not_a_write_failure) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(STATUS_NOERROR, b.connectIso());
    scriptAddressing(b.link, 0x7DF, 0, true);
    b.link.script("0100", "NODATA\r>");

    // The frame went out and no ECU answered: a valid OBD outcome. The app
    // learns "no reply" from the empty queue, not from a write error.
    unsigned long n = 1;
    ASSERT_RC(STATUS_NOERROR, b.write(0x7DF, PIDS_REQ, 2, &n));
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ(1, b.dev.rxQueueSize(b.chId));   // TX echo only

    PASSTHRU_MSG rx;
    unsigned long got = 1;
    ASSERT_RC(STATUS_NOERROR, b.dev.readMsgs(b.chId, &rx, &got, 100));
    ASSERT_EQ(4, (int)rx.DataSize);
    // Drained: the deadline expires with nothing to hand over.
    got = 1;
    ASSERT_RC(ERR_TIMEOUT, b.dev.readMsgs(b.chId, &rx, &got, 20));
    ASSERT_EQ(0, (int)got);
    // A zero timeout never waits.
    got = 1;
    ASSERT_RC(ERR_TIMEOUT, b.dev.readMsgs(b.chId, &rx, &got, 0));
    ASSERT_EQ(0, (int)got);
}

TEST(write_multiframe_vin) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(STATUS_NOERROR, b.connectIso());
    scriptAddressing(b.link, 0x7DF, 0, true);
    b.link.script("0902", VIN_REPLY);

    unsigned long n = 1;
    ASSERT_RC(STATUS_NOERROR, b.write(0x7DF, VIN_REQ, 2, &n));
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ(2, b.dev.rxQueueSize(b.chId));

    PASSTHRU_MSG rx[2];
    memset(rx, 0, sizeof(rx));
    unsigned long got = 2;
    ASSERT_RC(STATUS_NOERROR, b.dev.readMsgs(b.chId, rx, &got, 100));
    ASSERT_EQ(2, (int)got);
    // One reassembled message: first + two consecutive frames, 20 payload
    // bytes (49 02 01 + the 17-character VIN).
    ASSERT_EQ(24, (int)rx[1].DataSize);
    ASSERT_EQ(24, (int)rx[1].ExtraDataIndex);
    ASSERT_EQ(0x7E8, (int)beId(rx[1].Data));
    ASSERT_EQ(0x49, rx[1].Data[4]);
    ASSERT_EQ(0x02, rx[1].Data[5]);
    ASSERT_EQ(0x01, rx[1].Data[6]);
    ASSERT_STR_EQ("WDD2040011A123456",
                  std::string((const char *)rx[1].Data + 7, 17));
    ASSERT_STR_EQ("4902015744443230343030313141313233343536",
                  hexOf(rx[1].Data + 4, rx[1].DataSize - 4));
}

// ═══════════════════════════════════════════════════════════════════════════
// 7-11. Failure taxonomy and recovery
// ═══════════════════════════════════════════════════════════════════════════

TEST(canerror_recovers_then_succeeds) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(STATUS_NOERROR, b.connectIso());
    scriptAddressing(b.link, 0x7DF, 0, true);
    b.link.script("0100", "CANERROR\r>");
    // ResetProtocol = ATPC + re-assert ATSP6, then the addressing block again
    // (ATSP6 cleared it) and ONE retry.
    b.link.scriptOk("ATPC");
    b.link.scriptOk("ATSP6");
    scriptAddressing(b.link, 0x7DF, 0);   // chip reset: no filter to clear
    b.link.script("0100", PIDS_REPLY);

    unsigned long n = 1;
    ASSERT_RC(STATUS_NOERROR, b.write(0x7DF, PIDS_REQ, 2, &n));
    ASSERT_EQ(1, (int)n);
    ASSERT_TRUE(b.link.orderOk);
    ASSERT_STR_EQ(std::string(FULL_INIT_ORDER) + RETARGET_7DF_OPEN + ",0100" +
                  ",ATPC,ATSP6" + RETARGET_7DF_CLEAN + ",0100",
                  b.link.written());
    ASSERT_EQ(2, b.dev.rxQueueSize(b.chId));
}

TEST(canerror_persistent_fails) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(STATUS_NOERROR, b.connectIso());
    scriptAddressing(b.link, 0x7DF, 0, true);
    b.link.script("0100", "CANERROR\r>");
    b.link.scriptOk("ATPC");
    b.link.scriptOk("ATSP6");
    scriptAddressing(b.link, 0x7DF, 0);   // chip reset: no filter to clear
    b.link.script("0100", "CANERROR\r>");

    unsigned long n = 1;
    ASSERT_RC(ERR_FAILED, b.write(0x7DF, PIDS_REQ, 2, &n));
    ASSERT_EQ(0, (int)n);          // nothing was transmitted successfully
    ASSERT_EQ(0, b.dev.rxQueueSize(b.chId));
    ASSERT_HAS(std::string(getLastError()), "CANERROR");
}

TEST(unsupported_request) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(STATUS_NOERROR, b.connectIso());
    scriptAddressing(b.link, 0x7DF, 0, true);
    b.link.script("0100", "?\r>");

    unsigned long n = 1;
    ASSERT_RC(ERR_NOT_SUPPORTED, b.write(0x7DF, PIDS_REQ, 2, &n));
    ASSERT_EQ(0, (int)n);
    ASSERT_EQ(0, b.dev.rxQueueSize(b.chId));
}

TEST(wedged_link_reinitialises_and_retries) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(STATUS_NOERROR, b.connectIso());
    scriptAddressing(b.link, 0x7DF, 0, true);
    // Data but no '>' prompt: the adapter is mid-something or the link wedged.
    b.link.script("0100", "SEARCHING...\r7E8064100183B0011\r");
    scriptFullInit(b.link);            // the re-handshake
    scriptAddressing(b.link, 0x7DF, 0, true);
    b.link.script("0100", PIDS_REPLY);

    unsigned long n = 1;
    long rc = b.write(0x7DF, PIDS_REQ, 2, &n, 200);
    ASSERT_EQ(STATUS_NOERROR, rc);
    ASSERT_EQ(1, (int)n);
    ASSERT_TRUE(b.link.orderOk);
    // The retry is a full re-handshake (ATZ + init plan + default addressing)
    // followed by the re-address this write needs.
    ASSERT_STR_EQ(std::string(FULL_INIT_ORDER) + RETARGET_7DF_OPEN + ",0100," +
                  FULL_INIT_ORDER + RETARGET_7DF_OPEN + ",0100",
                  b.link.written());
    ASSERT_EQ(2, b.dev.rxQueueSize(b.chId));
}

TEST(dead_link_reports_device_not_connected) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(STATUS_NOERROR, b.connectIso());
    // One good write first, so the target is already addressed and the next
    // failure happens INSIDE the request exchange (a setTarget failure is
    // reported as ERR_FAILED — the addressing block is what broke).
    scriptAddressing(b.link, 0x7DF, 0, true);
    b.link.script("0100", PIDS_REPLY);
    unsigned long n = 1;
    long rc = b.write(0x7DF, PIDS_REQ, 2, &n);
    ASSERT_EQ(STATUS_NOERROR, rc);
    ASSERT_EQ(2, b.dev.rxQueueSize(b.chId));

    // RFCOMM is gone: every read fails, so neither the re-handshake nor the
    // port re-open can bring the session back.
    b.link.readShouldFail = true;
    b.link.script("0100", PIDS_REPLY);
    n = 1;
    rc = b.write(0x7DF, PIDS_REQ, 2, &n, 200);
    ASSERT_EQ(ERR_DEVICE_NOT_CONNECTED, rc);
    ASSERT_EQ(0, (int)n);
    ASSERT_EQ(2, b.dev.rxQueueSize(b.chId));   // nothing new was queued
    ASSERT_HAS(std::string(getLastError()), "ELM link lost");
    // The device stays "open" so the caller can still Close it cleanly.
    b.link.readShouldFail = false;
    b.link.scriptOk("ATLP");
    ASSERT_EQ(STATUS_NOERROR, b.dev.closeDevice(b.devId));
    ASSERT_TRUE(b.link.closed);
}

// ═══════════════════════════════════════════════════════════════════════════
// 12. Truncated multi-frame: reported, never forwarded
// ═══════════════════════════════════════════════════════════════════════════

TEST(truncated_reply_not_forwarded) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(STATUS_NOERROR, b.connectIso());
    scriptAddressing(b.link, 0x7DF, 0, true);
    b.link.script("0902", TRUNCATED_VIN_REPLY);

    unsigned long n = 1;
    ASSERT_RC(STATUS_NOERROR, b.write(0x7DF, VIN_REQ, 2, &n));
    ASSERT_EQ(1, (int)n);
    ASSERT_EQ(1, b.dev.rxQueueSize(b.chId));   // echo only — no partial payload

    char buf[80] = {0};
    ASSERT_EQ(STATUS_NOERROR, b.dev.getLastError(buf));
    ASSERT_HAS(std::string(buf), "truncated multi-frame reply from 7E8");
    ASSERT_HAS(std::string(getLastError()), "truncated");
}

// ═══════════════════════════════════════════════════════════════════════════
// 13-15. Filters
// ═══════════════════════════════════════════════════════════════════════════

TEST(flow_control_filter_programs_atcra) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(STATUS_NOERROR, b.connectIso());

    PASSTHRU_MSG mask = makeIdMsg(0xFFFFFFFF);
    PASSTHRU_MSG pattern = makeIdMsg(0x7E8);
    PASSTHRU_MSG flow = makeIdMsg(0x7DF);
    unsigned long fid = 0;
    ASSERT_EQ(STATUS_NOERROR,
              b.dev.startMsgFilter(b.chId, FLOW_CONTROL_FILTER, &mask,
                                   &pattern, &flow, &fid));
    ASSERT_EQ(1, (int)fid);
    // No AT command goes out for the filter itself — ATCRA rides the next
    // setTarget, which the write below triggers.
    ASSERT_STR_EQ(FULL_INIT_ORDER, b.link.written());

    scriptAddressing(b.link, 0x7E0, 0x7E8);
    b.link.script("0100", PIDS_REPLY);
    unsigned long n = 1;
    ASSERT_RC(STATUS_NOERROR, b.write(0x7E0, PIDS_REQ, 2, &n));
    ASSERT_TRUE(b.link.orderOk);
    ASSERT_HAS(b.link.written(), ",ATSH7E0,ATCRA7E8,ATFCSH7E0,ATFCSD300000,ATFCSM1,0100");

    // Removing the filter must CLEAR the chip's ATCRA7E8, not merely omit
    // ATCRA: an omitted command leaves the old filter loaded and every
    // response silently dropped by the adapter (review finding 2).
    ASSERT_EQ(STATUS_NOERROR, b.dev.stopMsgFilter(b.chId, fid));
    ASSERT_EQ(ERR_INVALID_FILTER_ID, b.dev.stopMsgFilter(b.chId, fid));
    scriptAddressing(b.link, 0x7E0, 0, true);
    b.link.script("0100", "NODATA\r>");
    n = 1;
    ASSERT_RC(STATUS_NOERROR, b.write(0x7E0, PIDS_REQ, 2, &n));
    ASSERT_TRUE(b.link.orderOk);
    ASSERT_HAS(b.link.written(),
               ",ATSH7E0,ATCRA000,ATFCSH7E0,ATFCSD300000,ATFCSM1,0100");

    // Accept-all is now in force, so a further unfiltered write spends no
    // round trip on ATCRA000 again.
    b.link.script("0100", "NODATA\r>");
    n = 1;
    ASSERT_RC(STATUS_NOERROR, b.write(0x7E0, PIDS_REQ, 2, &n));
    ASSERT_HAS(b.link.written(), ",ATFCSM1,0100,0100");
}

TEST(pass_and_block_filters_apply_at_rx) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(STATUS_NOERROR, b.connectIso());

    PASSTHRU_MSG mask = makeIdMsg(0xFFFFFFFF);
    PASSTHRU_MSG pattern = makeIdMsg(0x7E8);
    unsigned long passId = 0, blockId = 0;
    ASSERT_EQ(STATUS_NOERROR,
              b.dev.startMsgFilter(b.chId, PASS_FILTER, &mask, &pattern, NULL, &passId));
    ASSERT_EQ(1, (int)passId);

    scriptAddressing(b.link, 0x7DF, 0, true);
    b.link.script("0100", TWO_ECU_REPLY);
    unsigned long n = 1;
    ASSERT_RC(STATUS_NOERROR, b.write(0x7DF, PIDS_REQ, 2, &n));
    ASSERT_EQ(2, b.dev.rxQueueSize(b.chId));   // echo + 7E8; 7E9 filtered out

    PASSTHRU_MSG rx[2];
    memset(rx, 0, sizeof(rx));
    unsigned long got = 2;
    ASSERT_RC(STATUS_NOERROR, b.dev.readMsgs(b.chId, rx, &got, 100));
    ASSERT_EQ(2, (int)got);
    ASSERT_EQ(0x7E8, (int)beId(rx[1].Data));

    // BLOCK wins over PASS. Target is unchanged, so no addressing is re-sent.
    ASSERT_EQ(STATUS_NOERROR,
              b.dev.startMsgFilter(b.chId, BLOCK_FILTER, &mask, &pattern,
                                   NULL, &blockId));
    ASSERT_EQ(2, (int)blockId);
    b.link.script("0100", TWO_ECU_REPLY);
    n = 1;
    ASSERT_RC(STATUS_NOERROR, b.write(0x7DF, PIDS_REQ, 2, &n));
    ASSERT_EQ(1, b.dev.rxQueueSize(b.chId));   // echo only
    // Drain it, so the queue counted below belongs to the next write alone.
    PASSTHRU_MSG echo;
    memset(&echo, 0, sizeof(echo));
    unsigned long drained = 1;
    ASSERT_RC(STATUS_NOERROR, b.dev.readMsgs(b.chId, &echo, &drained, 100));
    ASSERT_EQ(4, (int)echo.DataSize);

    // CLEAR_MSG_FILTERS drops both: everything the adapter prints comes back.
    ASSERT_EQ(STATUS_NOERROR, b.dev.ioctl(b.chId, CLEAR_MSG_FILTERS, NULL, NULL));
    b.link.script("0100", TWO_ECU_REPLY);
    n = 1;
    ASSERT_RC(STATUS_NOERROR, b.write(0x7DF, PIDS_REQ, 2, &n));
    ASSERT_EQ(3, b.dev.rxQueueSize(b.chId));
    PASSTHRU_MSG rx3[3];
    memset(rx3, 0, sizeof(rx3));
    unsigned long got3 = 3;
    ASSERT_RC(STATUS_NOERROR, b.dev.readMsgs(b.chId, rx3, &got3, 100));
    ASSERT_EQ(3, (int)got3);
    ASSERT_EQ(0x7E8, (int)beId(rx3[1].Data));
    ASSERT_EQ(0x7E9, (int)beId(rx3[2].Data));
}

TEST(filter_validation) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(STATUS_NOERROR, b.connectIso());

    PASSTHRU_MSG mask = makeIdMsg(0xFFFFFFFF);
    PASSTHRU_MSG pattern = makeIdMsg(0x7E8);
    unsigned long fid = 0;
    // P-LIVE-002 project convention: a missing mask/pattern is ERR_NULL_PARAMETER.
    ASSERT_EQ(ERR_NULL_PARAMETER,
              b.dev.startMsgFilter(b.chId, PASS_FILTER, NULL, &pattern, NULL, &fid));
    ASSERT_EQ(ERR_NULL_PARAMETER,
              b.dev.startMsgFilter(b.chId, PASS_FILTER, &mask, NULL, NULL, &fid));
    ASSERT_EQ(ERR_NULL_PARAMETER,
              b.dev.startMsgFilter(b.chId, FLOW_CONTROL_FILTER, &mask, &pattern,
                                   NULL, &fid));
    ASSERT_EQ(ERR_NULL_PARAMETER,
              b.dev.startMsgFilter(b.chId, PASS_FILTER, &mask, &pattern, NULL, NULL));
    ASSERT_EQ(ERR_INVALID_FLAGS,
              b.dev.startMsgFilter(b.chId, 7, &mask, &pattern, NULL, &fid));
    ASSERT_EQ(ERR_INVALID_CHANNEL_ID,
              b.dev.startMsgFilter(99, PASS_FILTER, &mask, &pattern, NULL, &fid));

    // A filter message must carry the 4-byte id.
    PASSTHRU_MSG tooSmall = makeIdMsg(0x7E8);
    tooSmall.DataSize = 2;
    ASSERT_EQ(ERR_INVALID_MSG,
              b.dev.startMsgFilter(b.chId, PASS_FILTER, &tooSmall, &pattern,
                                   NULL, &fid));
    ASSERT_EQ(ERR_INVALID_CHANNEL_ID, b.dev.stopMsgFilter(99, 1));
    ASSERT_EQ(ERR_INVALID_FILTER_ID, b.dev.stopMsgFilter(b.chId, 42));
    ASSERT_STR_EQ(FULL_INIT_ORDER, b.link.written());
}

// ═══════════════════════════════════════════════════════════════════════════
// 16. Write validation — all of it before anything reaches the adapter
// ═══════════════════════════════════════════════════════════════════════════

TEST(write_validation) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(STATUS_NOERROR, b.connectIso());

    // Id with no payload: the adapter would have nothing to send.
    PASSTHRU_MSG noPayload = makeTx(0x7DF, NULL, 0);
    unsigned long n = 1;
    ASSERT_RC(ERR_INVALID_MSG, b.dev.writeMsgs(b.chId, &noPayload, &n, 1000));
    ASSERT_EQ(0, (int)n);

    PASSTHRU_MSG wrongProto = makeTx(0x7DF, PIDS_REQ, 2);
    wrongProto.ProtocolID = J2534_CAN;
    n = 1;
    ASSERT_RC(ERR_MSG_PROTOCOL_ID, b.dev.writeMsgs(b.chId, &wrongProto, &n, 1000));
    ASSERT_EQ(0, (int)n);

    // DataSize past the end of the caller's own buffer must not be read.
    PASSTHRU_MSG oversized = makeTx(0x7DF, PIDS_REQ, 2);
    oversized.DataSize = sizeof(oversized.Data) + 1;
    n = 1;
    ASSERT_RC(ERR_INVALID_MSG, b.dev.writeMsgs(b.chId, &oversized, &n, 1000));

    n = 1;
    ASSERT_RC(ERR_INVALID_CHANNEL_ID, b.dev.writeMsgs(99, &wrongProto, &n, 1000));
    ASSERT_RC(ERR_NULL_PARAMETER, b.dev.writeMsgs(b.chId, NULL, &n, 1000));
    ASSERT_RC(ERR_NULL_PARAMETER, b.dev.writeMsgs(b.chId, &wrongProto, NULL, 1000));
    ASSERT_STR_EQ(FULL_INIT_ORDER, b.link.written());

    // A multi-message write reports how far it got: the second message fails,
    // the first really did go out.
    scriptAddressing(b.link, 0x7DF, 0, true);
    b.link.script("0100", PIDS_REPLY);
    PASSTHRU_MSG two[2];
    two[0] = makeTx(0x7DF, PIDS_REQ, 2);
    two[1] = makeTx(0x7DF, NULL, 0);
    n = 2;
    ASSERT_RC(ERR_INVALID_MSG, b.dev.writeMsgs(b.chId, two, &n, 5000));
    ASSERT_EQ(1, (int)n);
}

// ═══════════════════════════════════════════════════════════════════════════
// 17-19. Ioctl
// ═══════════════════════════════════════════════════════════════════════════

TEST(ioctl_vbatt_and_handle_scoping) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());

    // Device-level handle, before any channel exists (what Xentry does).
    b.link.script("ATRV", "12.3V\r>");
    unsigned long mv = 0;
    long rc = b.dev.ioctl(b.devId, READ_VBATT, NULL, &mv);
    ASSERT_EQ(STATUS_NOERROR, rc);
    ASSERT_EQ(12300, (int)mv);

    // Channel-scoped ioctls reject a device handle (Simulator's scoping rule).
    ASSERT_EQ(ERR_INVALID_CHANNEL_ID,
              b.dev.ioctl(b.devId, CLEAR_RX_BUFFER, NULL, NULL));
    ASSERT_EQ(ERR_INVALID_CHANNEL_ID,
              b.dev.ioctl(b.devId, CLEAR_MSG_FILTERS, NULL, NULL));
    ASSERT_EQ(ERR_INVALID_CHANNEL_ID, b.dev.ioctl(77, READ_VBATT, NULL, &mv));
    ASSERT_EQ(ERR_NULL_PARAMETER, b.dev.ioctl(b.devId, READ_VBATT, NULL, NULL));
    // An unparseable ATRV reply is a failure, not a zero reading.
    b.link.script("ATRV", "NODATA\r>");
    rc = b.dev.ioctl(b.devId, READ_VBATT, NULL, &mv);
    ASSERT_EQ(ERR_FAILED, rc);
    // K-line init belongs to protocols this adapter is not addressed as.
    ASSERT_EQ(ERR_NOT_SUPPORTED, b.dev.ioctl(b.devId, FIVE_BAUD_INIT, NULL, NULL));
    ASSERT_EQ(ERR_INVALID_IOCTL_ID,
              b.dev.ioctl(b.devId, READ_PROG_VOLTAGE, NULL, NULL));

    ASSERT_EQ(STATUS_NOERROR, b.connectIso());
    b.link.script("ATRV", "12.3V\r>");
    mv = 0;
    rc = b.dev.ioctl(b.chId, READ_VBATT, NULL, &mv);
    ASSERT_EQ(STATUS_NOERROR, rc);
    ASSERT_EQ(12300, (int)mv);
    ASSERT_EQ(ERR_NOT_SUPPORTED, b.dev.ioctl(b.chId, FAST_INIT, NULL, NULL));
    // Channels start at 2 precisely so a device handle can never be mistaken
    // for a channel handle: a channel-scoped ioctl on the device id is still
    // rejected now that channel 2 exists (it used to hit channel 1).
    ASSERT_EQ(2, (int)b.chId);
    ASSERT_TRUE(b.chId != b.devId);
    ASSERT_EQ(ERR_INVALID_CHANNEL_ID,
              b.dev.ioctl(b.devId, CLEAR_RX_BUFFER, NULL, NULL));
    ASSERT_HAS(b.link.written(), ",ATRV,ATRV,ATRV");
}

TEST(ioctl_config_lists) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(STATUS_NOERROR, b.connectIso());

    SCONFIG params[2];
    memset(params, 0, sizeof(params));
    SCONFIG_LIST list;
    list.NumOfParams = 2;
    list.ConfigPtr = params;

    params[0].Parameter = DATA_RATE;
    params[1].Parameter = LOOPBACK;
    ASSERT_EQ(STATUS_NOERROR, b.dev.ioctl(b.chId, GET_CONFIG, &list, NULL));
    ASSERT_EQ(ELM_CAN_BAUD_500K, params[0].Value);
    ASSERT_EQ(0, (int)params[1].Value);

    params[0].Parameter = NODE_ADDRESS;
    list.NumOfParams = 1;
    ASSERT_EQ(ERR_INVALID_IOCTL_VALUE, b.dev.ioctl(b.chId, GET_CONFIG, &list, NULL));
    ASSERT_EQ(ERR_NULL_PARAMETER, b.dev.ioctl(b.chId, GET_CONFIG, NULL, NULL));

    // SET_CONFIG: validated no-ops — the ELM chip owns flow-control timing.
    list.NumOfParams = 2;
    params[0].Parameter = ISO15765_BS;
    params[0].Value = 8;
    params[1].Parameter = ISO15765_STMIN;
    params[1].Value = 20;
    ASSERT_EQ(STATUS_NOERROR, b.dev.ioctl(b.chId, SET_CONFIG, &list, NULL));

    list.NumOfParams = 1;
    params[0].Parameter = DATA_RATE;
    params[0].Value = ELM_CAN_BAUD_500K;
    ASSERT_EQ(STATUS_NOERROR, b.dev.ioctl(b.chId, SET_CONFIG, &list, NULL));
    params[0].Value = 250000;
    ASSERT_EQ(ERR_INVALID_IOCTL_VALUE, b.dev.ioctl(b.chId, SET_CONFIG, &list, NULL));
    params[0].Parameter = ISO15765_BS;
    params[0].Value = 0x1000;
    ASSERT_EQ(ERR_INVALID_IOCTL_VALUE, b.dev.ioctl(b.chId, SET_CONFIG, &list, NULL));
    params[0].Parameter = CAN_MIXED_FORMAT;
    params[0].Value = 1;
    // The IOCTL id was valid; the parameter inside it was not — same code
    // GET_CONFIG uses, and the KvaserDirect precedent.
    ASSERT_EQ(ERR_INVALID_IOCTL_VALUE, b.dev.ioctl(b.chId, SET_CONFIG, &list, NULL));
    ASSERT_EQ(ERR_NULL_PARAMETER, b.dev.ioctl(b.chId, SET_CONFIG, NULL, NULL));
    // Config ioctls never reach the adapter.
    ASSERT_STR_EQ(FULL_INIT_ORDER, b.link.written());
}

TEST(ioctl_clear_rx_buffer_and_disconnect) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(STATUS_NOERROR, b.connectIso());
    scriptAddressing(b.link, 0x7DF, 0, true);
    b.link.script("0100", PIDS_REPLY);
    unsigned long n = 1;
    ASSERT_RC(STATUS_NOERROR, b.write(0x7DF, PIDS_REQ, 2, &n));
    ASSERT_EQ(2, b.dev.rxQueueSize(b.chId));

    ASSERT_EQ(STATUS_NOERROR, b.dev.ioctl(b.chId, CLEAR_RX_BUFFER, NULL, NULL));
    ASSERT_EQ(0, b.dev.rxQueueSize(b.chId));

    // Disconnect drops the queue and the filters with the channel.
    b.link.script("0100", PIDS_REPLY);
    n = 1;
    ASSERT_RC(STATUS_NOERROR, b.write(0x7DF, PIDS_REQ, 2, &n));
    ASSERT_EQ(2, b.dev.rxQueueSize(b.chId));
    ASSERT_EQ(STATUS_NOERROR, b.dev.disconnect(b.chId));
    ASSERT_EQ(-1, b.dev.rxQueueSize(b.chId));
    PASSTHRU_MSG rx;
    unsigned long got = 1;
    ASSERT_RC(ERR_INVALID_CHANNEL_ID, b.dev.readMsgs(b.chId, &rx, &got, 0));
}

// ═══════════════════════════════════════════════════════════════════════════
// 20. Cancel path: parked reader vs. concurrent disconnect (Win32 only)
// ═══════════════════════════════════════════════════════════════════════════

#ifdef _WIN32
// Regression for the use-after-free a Channel* cached across the condvar wait
// would cause: Disconnect erases the channel (destroying its deque) and wakes
// every waiter, so the reader must re-resolve the id and report
// ERR_INVALID_CHANNEL_ID instead of popping from freed memory.
//
// Win32-only by necessity — the native stubs make the lock a no-op and
// SleepConditionVariableCS a plain sleep, so a second thread would not
// exercise a real park/wake at all. Runs in build/tests/test_device.exe.
struct ReaderCtx {
    ElmDevice *dev;
    unsigned long chId;
    long rc;
    unsigned long got;
};

static DWORD WINAPI readerThread(LPVOID param) {
    ReaderCtx *ctx = static_cast<ReaderCtx *>(param);
    PASSTHRU_MSG rx;
    memset(&rx, 0, sizeof(rx));
    ctx->got = 1;
    ctx->rc = ctx->dev->readMsgs(ctx->chId, &rx, &ctx->got, 2000);
    return 0;
}

TEST(readmsgs_cancelled_by_disconnect) {
    Bench b;
    ASSERT_EQ(STATUS_NOERROR, b.open());
    ASSERT_EQ(STATUS_NOERROR, b.connectIso());

    ReaderCtx ctx;
    ctx.dev = &b.dev;
    ctx.chId = b.chId;
    ctx.rc = -1;
    ctx.got = 1;
    HANDLE reader = CreateThread(NULL, 0, readerThread, &ctx, 0, NULL);
    ASSERT_TRUE(reader != NULL);

    // Join BEFORE asserting anything: ctx and the Bench live on this stack
    // frame, so an early return while the reader runs would free them under it.
    Sleep(100);   // let the reader park on rxCond_ with an empty queue
    const long disconnectRc = b.dev.disconnect(b.chId);
    const DWORD joinRc = WaitForSingleObject(reader, 5000);
    CloseHandle(reader);

    ASSERT_EQ(STATUS_NOERROR, disconnectRc);
    ASSERT_EQ(WAIT_OBJECT_0, (long)joinRc);   // no hang: the wake really came
    ASSERT_EQ(ERR_INVALID_CHANNEL_ID, ctx.rc);
    ASSERT_EQ(0, (int)ctx.got);
}
#endif // _WIN32

// ═══════════════════════════════════════════════════════════════════════════

int main() {
    printf("\n=== ElmJ2534 Device Tests (FakeLink) ===\n\n");

    RUN_TEST(open_probes_banner_and_version);
    RUN_TEST(open_failure_paths);
    RUN_TEST(connect_validation_and_channel_ids);
    RUN_TEST(write_single_frame_echo_then_response);
    RUN_TEST(write_nodata_is_not_a_write_failure);
    RUN_TEST(write_multiframe_vin);
    RUN_TEST(canerror_recovers_then_succeeds);
    RUN_TEST(canerror_persistent_fails);
    RUN_TEST(unsupported_request);
    RUN_TEST(wedged_link_reinitialises_and_retries);
    RUN_TEST(dead_link_reports_device_not_connected);
    RUN_TEST(truncated_reply_not_forwarded);
    RUN_TEST(flow_control_filter_programs_atcra);
    RUN_TEST(pass_and_block_filters_apply_at_rx);
    RUN_TEST(filter_validation);
    RUN_TEST(write_validation);
    RUN_TEST(ioctl_vbatt_and_handle_scoping);
    RUN_TEST(ioctl_config_lists);
    RUN_TEST(ioctl_clear_rx_buffer_and_disconnect);
#ifdef _WIN32
    RUN_TEST(readmsgs_cancelled_by_disconnect);
#endif

    printf("\n%d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0) printf(", %d FAILED", g_tests_failed);
    printf("\n\n");
    return g_tests_failed > 0 ? 1 : 0;
}
