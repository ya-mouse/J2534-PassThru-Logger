// KvaserDirect Unit Tests — ISO-TP Engine
// Compile: i686-w64-mingw32-g++ -std=c++11 -DUNIT_TEST -I.. -I../../vendor/canlib/INC
//          tests/test_isotp.cpp ../IsoTpEngine.cpp -o tests/test_isotp.exe -lws2_32 -ladvapi32
// Run: wine tests/test_isotp.exe  (or natively on Windows)

#include "mock_canlib.h"
#include "../IsoTpEngine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Minimal test framework
static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    printf("  %-50s ", #name); \
    g_mock.reset(); \
    g_tests_run++; \
    test_##name(); \
    g_tests_passed++; \
    printf("[PASS]\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("[FAIL]\n    %s:%d: expected %ld, got %ld\n", \
               __FILE__, __LINE__, (long)(expected), (long)(actual)); \
        g_tests_failed++; g_tests_passed--; return; \
    } \
} while(0)

#define ASSERT_MEM_EQ(expected, actual, len) do { \
    if (memcmp((expected), (actual), (len)) != 0) { \
        printf("[FAIL]\n    %s:%d: memory mismatch (%d bytes)\n", \
               __FILE__, __LINE__, (int)(len)); \
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

// Global mock state
MockState g_mock;

// Provide stub for g_logger (tests don't need real logging)
#include "../Logger.h"
KdLogger g_logger;
// Stub implementations so we don't link Logger.cpp in test mode
KdLogger::KdLogger() : level_(0), hFile_(INVALID_HANDLE_VALUE), initialized_(false) {
    InitializeCriticalSection(&lock_);
}
KdLogger::~KdLogger() { DeleteCriticalSection(&lock_); }
void KdLogger::init(int, const char*) {}
void KdLogger::shutdown() {}
void KdLogger::verbose(const char*, ...) {}
void KdLogger::debug(const char*, ...) {}
void KdLogger::hexDump(const char*, const unsigned char*, unsigned int) {}
void KdLogger::isoTpState(const char*, unsigned long, const char*, const char*) {}
void KdLogger::apiEntry(const char*, const char*, ...) {}
void KdLogger::apiReturn(const char*, long) {}
void KdLogger::writeRaw(const char*, int) {}
void KdLogger::writeTimestamp() {}

// Helper: create and initialize an IsoTpEngine with mock API
static CanlibApi g_mockApi;

static IsoTpEngine* createEngine(unsigned long bs_tx = 0, unsigned long stmin_tx = 0,
                                  unsigned long pad = 0xCC, unsigned long wft_max = 10) {
    mockCanlibInit(&g_mockApi);
    IsoTpEngine *eng = new IsoTpEngine();
    eng->init(0, &g_mockApi, bs_tx, stmin_tx, pad, wft_max);
    return eng;
}

// Thread context for multi-frame TX tests
struct TxTestCtx {
    IsoTpEngine *eng;
    long result;
    unsigned char *data;
    unsigned long dataLen;
};

static DWORD WINAPI txTestThread(LPVOID p) {
    TxTestCtx *c = (TxTestCtx*)p;
    c->result = c->eng->transmit(0x7E0, 0, c->data, c->dataLen, 3000);
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// TX Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(tx_single_frame_1byte) {
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    unsigned char data[] = {0x3E};  // TesterPresent
    long ret = eng->transmit(0x7E0, 0, data, 1, 100);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_EQ(1, g_mock.txCount);
    // SF: PCI=0x01, data=0x3E, padding
    ASSERT_EQ(0x01, g_mock.txFrames[0].data[0]);
    ASSERT_EQ(0x3E, g_mock.txFrames[0].data[1]);
    ASSERT_EQ((long)0x7E0, g_mock.txFrames[0].id);
    ASSERT_EQ(8, (int)g_mock.txFrames[0].dlc);
    delete eng;
}

TEST(tx_single_frame_7bytes) {
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    unsigned char data[] = {0x22, 0xF1, 0x90, 0x01, 0x02, 0x03, 0x04};
    long ret = eng->transmit(0x7E0, 0, data, 7, 100);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_EQ(1, g_mock.txCount);
    ASSERT_EQ(0x07, g_mock.txFrames[0].data[0]);  // SF, len=7
    ASSERT_MEM_EQ(data, &g_mock.txFrames[0].data[1], 7);
    delete eng;
}

TEST(tx_single_frame_padding) {
    IsoTpEngine *eng = createEngine(0, 0, 0xAA);  // pad=0xAA
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    unsigned char data[] = {0x10, 0x01};
    eng->transmit(0x7E0, 0, data, 2, 100);
    ASSERT_EQ(0x02, g_mock.txFrames[0].data[0]);
    ASSERT_EQ(0x10, g_mock.txFrames[0].data[1]);
    ASSERT_EQ(0x01, g_mock.txFrames[0].data[2]);
    // Bytes 3-7 should be 0xAA
    for (int i = 3; i < 8; i++)
        ASSERT_EQ(0xAA, (int)g_mock.txFrames[0].data[i]);
    delete eng;
}

TEST(tx_multi_frame_8bytes) {
    // 8 bytes requires FF+CF (boundary case: SF max is 7)
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    unsigned char data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    // We need FC from ECU after FF — script it
    // After engine sends FF, we simulate receiving FC(CTS, BS=0, STmin=0)
    unsigned char fc[] = {0x30, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    g_mock.pushRx(0x7E8, fc, 8, 0, 1000);

    // transmit will send FF, then poll. But our mock canReadWait is instant,
    // so we need to manually call processFrame with the FC.
    // Actually, transmit() polls by sleeping — it won't call canRead itself.
    // The FC must be delivered via processFrame() from a separate thread or before.
    // For testing, we'll feed FC directly.

    // Start TX in a way that we can inject FC between FF and CFs.
    // Since transmit blocks, we need a different approach for multi-frame:
    // Feed the FC via processFrame during the TX_WAIT_FC state.

    // Approach: Use a thread, or simply test the protocol manually.
    // For unit tests, let's verify the FF is correct and that processFrame
    // correctly transitions state.

    // Direct state verification approach:
    // 1. Call transmit with data > 7 bytes
    // 2. It sends FF and enters TX_WAIT_FC
    // 3. Since there's no background read, it will timeout
    // Let's just verify the FF content

    // Actually, we can use a thread. But simpler: verify FF in mock,
    // then call processFrame with FC, verify CFs appear.

    // The transmit function blocks waiting for FC. Without a real threading
    // setup, let's test timeout behavior and manual state machine.

    // Test that FF is sent correctly even though TX will timeout:
    long ret = eng->transmit(0x7E0, 0, data, 8, 50);  // Short timeout
    // Should timeout since no FC arrives (processFrame not called)
    ASSERT_EQ(ERR_TIMEOUT, ret);
    // But FF should have been sent
    ASSERT_TRUE(g_mock.txCount >= 1);
    ASSERT_EQ(0x10, g_mock.txFrames[0].data[0]);  // FF high nibble
    ASSERT_EQ(0x08, g_mock.txFrames[0].data[1]);  // length = 8
    ASSERT_MEM_EQ(data, &g_mock.txFrames[0].data[2], 6);  // First 6 bytes
    delete eng;
}

TEST(tx_multi_frame_with_fc) {
    // Test full multi-frame TX with FC injection via processFrame
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    // 10 bytes: FF(6) + CF(4)
    unsigned char data[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22, 0x33, 0x44};
    TxTestCtx ctx = {eng, -1, data, 10};

    HANDLE hThread = CreateThread(NULL, 0, txTestThread, &ctx, 0, NULL);

    // Wait a bit for FF to be sent
    Sleep(20);

    // Verify FF was sent
    ASSERT_TRUE(g_mock.txCount >= 1);
    ASSERT_EQ(0x10, g_mock.txFrames[0].data[0]);  // FF
    ASSERT_EQ(0x0A, g_mock.txFrames[0].data[1]);  // len=10

    // Inject FC(CTS, BS=0, STmin=0)
    unsigned char fc[] = {0x30, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    eng->processFrame(0x7E8, 0, fc, 8, 1000);

    // Wait for transmit to complete
    WaitForSingleObject(hThread, 4000);
    CloseHandle(hThread);

    ASSERT_EQ(STATUS_NOERROR, ctx.result);
    // Should have FF + 1 CF (10 bytes: 6 in FF + 4 in CF)
    ASSERT_EQ(2, g_mock.txCount);
    // CF: SN=1
    ASSERT_EQ(0x21, g_mock.txFrames[1].data[0]);
    // CF data: bytes 6-9 of original
    ASSERT_EQ(0x11, (int)g_mock.txFrames[1].data[1]);
    ASSERT_EQ(0x22, (int)g_mock.txFrames[1].data[2]);
    ASSERT_EQ(0x33, (int)g_mock.txFrames[1].data[3]);
    ASSERT_EQ(0x44, (int)g_mock.txFrames[1].data[4]);
    delete eng;
}

TEST(tx_multi_frame_large_sn_wrap) {
    // Test SN wrapping: 100 bytes = FF(6) + 14 CFs(7 each) + 1 CF(2)
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    unsigned char data[100];
    for (int i = 0; i < 100; i++) data[i] = (unsigned char)(i & 0xFF);

    TxTestCtx ctx = {eng, -1, data, 100};

    HANDLE hThread = CreateThread(NULL, 0, txTestThread, &ctx, 0, NULL);

    Sleep(20);
    // Inject FC
    unsigned char fc[] = {0x30, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    eng->processFrame(0x7E8, 0, fc, 8, 1000);

    WaitForSingleObject(hThread, 4000);
    CloseHandle(hThread);

    ASSERT_EQ(STATUS_NOERROR, ctx.result);
    // FF + 14 CFs (6 + 14*7 = 6+98=104 >= 100, actually ceil((100-6)/7)=14 CFs)
    ASSERT_EQ(15, g_mock.txCount);

    // Verify SN wrapping
    for (int i = 1; i < g_mock.txCount; i++) {
        unsigned char expectedSN = (unsigned char)(i & 0x0F);
        unsigned char actualPCI = g_mock.txFrames[i].data[0];
        ASSERT_EQ(0x20 | expectedSN, (int)actualPCI);
    }
    delete eng;
}

TEST(tx_multi_frame_bs_limited) {
    // Test Block Size: FC says BS=2, so after 2 CFs we need another FC
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    // 27 bytes: FF(6) + 3 CFs needed. With BS=2, needs 2 FCs.
    unsigned char data[27];
    for (int i = 0; i < 27; i++) data[i] = (unsigned char)i;

    TxTestCtx ctx = {eng, -1, data, 27};

    HANDLE hThread = CreateThread(NULL, 0, txTestThread, &ctx, 0, NULL);

    Sleep(20);
    // First FC: BS=2, STmin=0
    unsigned char fc1[] = {0x30, 0x02, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    eng->processFrame(0x7E8, 0, fc1, 8, 1000);

    // Wait for first 2 CFs to be sent, then engine will wait for another FC
    Sleep(50);

    // Second FC: BS=0 (unlimited), send remaining
    unsigned char fc2[] = {0x30, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    eng->processFrame(0x7E8, 0, fc2, 8, 2000);

    WaitForSingleObject(hThread, 4000);
    CloseHandle(hThread);

    ASSERT_EQ(STATUS_NOERROR, ctx.result);
    // FF + 3 CFs = 4 frames total (6 + 7 + 7 + 7 = 27)
    ASSERT_EQ(4, g_mock.txCount);
    delete eng;
}

TEST(tx_no_session_returns_error) {
    IsoTpEngine *eng = createEngine();
    // Don't add any session
    unsigned char data[] = {0x01};
    long ret = eng->transmit(0x7E0, 0, data, 1, 100);
    ASSERT_EQ(ERR_INVALID_MSG, ret);
    delete eng;
}

TEST(tx_zero_length_returns_error) {
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);
    long ret = eng->transmit(0x7E0, 0, NULL, 0, 100);
    ASSERT_EQ(ERR_INVALID_MSG, ret);
    delete eng;
}

TEST(tx_max_length_4095) {
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    unsigned char data[4095];
    memset(data, 0x55, 4095);

    TxTestCtx ctx = {eng, -1, data, 4095};

    HANDLE hThread = CreateThread(NULL, 0, txTestThread, &ctx, 0, NULL);

    Sleep(20);
    unsigned char fc[] = {0x30, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    eng->processFrame(0x7E8, 0, fc, 8, 1000);

    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);

    ASSERT_EQ(STATUS_NOERROR, ctx.result);
    // FF(6) + CFs: ceil((4095-6)/7) = ceil(4089/7) = 585 CFs. Total = 586.
    ASSERT_EQ(586, g_mock.txCount);

    // Verify FF length encoding
    unsigned char hi = g_mock.txFrames[0].data[0] & 0x0F;
    unsigned char lo = g_mock.txFrames[0].data[1];
    unsigned long encodedLen = ((unsigned long)hi << 8) | lo;
    ASSERT_EQ(4095, (long)encodedLen);
    delete eng;
}

TEST(tx_over_max_length_returns_error) {
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);
    unsigned char data[1] = {0};
    long ret = eng->transmit(0x7E0, 0, data, 4096, 100);
    ASSERT_EQ(ERR_INVALID_MSG, ret);
    delete eng;
}

// ═══════════════════════════════════════════════════════════════════════════════
// RX Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(rx_single_frame_1byte) {
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    // Simulate receiving SF from ECU (ID 0x7E8)
    unsigned char sf[] = {0x01, 0x7F, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    eng->processFrame(0x7E8, 0, sf, 8, 500);

    PASSTHRU_MSG msg;
    unsigned long count = 1;
    long ret = eng->receive(&msg, &count, 100);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_EQ(1, (int)count);
    ASSERT_EQ(5, (int)msg.DataSize);  // 4 (CAN ID) + 1 (payload)
    // CAN ID in big-endian
    ASSERT_EQ(0x00, (int)msg.Data[0]);
    ASSERT_EQ(0x00, (int)msg.Data[1]);
    ASSERT_EQ(0x07, (int)msg.Data[2]);
    ASSERT_EQ(0xE8, (int)msg.Data[3]);
    // Payload
    ASSERT_EQ(0x7F, (int)msg.Data[4]);
    delete eng;
}

TEST(rx_single_frame_7bytes) {
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    unsigned char sf[] = {0x07, 0x62, 0xF1, 0x90, 0x41, 0x42, 0x43, 0x44};
    eng->processFrame(0x7E8, 0, sf, 8, 500);

    PASSTHRU_MSG msg;
    unsigned long count = 1;
    long ret = eng->receive(&msg, &count, 100);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_EQ(11, (int)msg.DataSize);  // 4 + 7
    ASSERT_MEM_EQ(&sf[1], &msg.Data[4], 7);
    delete eng;
}

TEST(rx_multi_frame_10bytes) {
    // 10 bytes: FF(6) + CF(4)
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    // FF: len=10, first 6 bytes
    unsigned char ff[] = {0x10, 0x0A, 0x62, 0xF1, 0x90, 0x41, 0x42, 0x43};
    eng->processFrame(0x7E8, 0, ff, 8, 1000);

    // Engine should have sent FC
    ASSERT_EQ(1, g_mock.txCount);
    ASSERT_EQ(0x30, g_mock.txFrames[0].data[0] & 0xF0);  // FC PCI
    ASSERT_EQ((long)0x7E0, g_mock.txFrames[0].id);  // Sent on our TX ID

    // CF: SN=1, remaining 4 bytes
    unsigned char cf[] = {0x21, 0x44, 0x45, 0x46, 0x47, 0xCC, 0xCC, 0xCC};
    eng->processFrame(0x7E8, 0, cf, 8, 1001);

    // Should have complete message
    PASSTHRU_MSG msg;
    unsigned long count = 1;
    long ret = eng->receive(&msg, &count, 100);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_EQ(14, (int)msg.DataSize);  // 4 + 10
    // Verify reassembled payload
    unsigned char expected[] = {0x62, 0xF1, 0x90, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47};
    ASSERT_MEM_EQ(expected, &msg.Data[4], 10);
    delete eng;
}

TEST(rx_multi_frame_wrong_sn_aborts) {
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    unsigned char ff[] = {0x10, 0x0E, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    eng->processFrame(0x7E8, 0, ff, 8, 1000);

    // Send CF with wrong SN (expected 1, send 2)
    unsigned char cf_bad[] = {0x22, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D};
    eng->processFrame(0x7E8, 0, cf_bad, 8, 1001);

    // Should NOT deliver a message (reception aborted)
    PASSTHRU_MSG msg;
    unsigned long count = 1;
    long ret = eng->receive(&msg, &count, 50);
    ASSERT_EQ(ERR_BUFFER_EMPTY, ret);
    ASSERT_EQ(0, (int)count);
    delete eng;
}

TEST(rx_fc_overflow_aborts_tx) {
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    unsigned char data[20];
    memset(data, 0xAA, 20);

    TxTestCtx ctx = {eng, -1, data, 20};

    HANDLE hThread = CreateThread(NULL, 0, txTestThread, &ctx, 0, NULL);

    Sleep(20);
    // Send FC with FS=OVERFLOW (0x32)
    unsigned char fc[] = {0x32, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    eng->processFrame(0x7E8, 0, fc, 8, 1000);

    WaitForSingleObject(hThread, 4000);
    CloseHandle(hThread);

    // Should timeout or fail since overflow aborts TX
    ASSERT_TRUE(ctx.result != STATUS_NOERROR);
    delete eng;
}

TEST(rx_fc_wait_extends_deadline) {
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    unsigned char data[10];
    memset(data, 0xBB, 10);

    TxTestCtx ctx = {eng, -1, data, 10};

    HANDLE hThread = CreateThread(NULL, 0, txTestThread, &ctx, 0, NULL);

    Sleep(20);
    // Send WAIT
    unsigned char fc_wait[] = {0x31, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    eng->processFrame(0x7E8, 0, fc_wait, 8, 1000);

    Sleep(100);
    // Now send CTS
    unsigned char fc_cts[] = {0x30, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    eng->processFrame(0x7E8, 0, fc_cts, 8, 1100);

    WaitForSingleObject(hThread, 4000);
    CloseHandle(hThread);

    ASSERT_EQ(STATUS_NOERROR, ctx.result);
    delete eng;
}

TEST(rx_multiple_sessions) {
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);  // Session 1
    eng->addSession(0x7E2, 0x7EA, 0, 0, 2);  // Session 2

    // SF on session 1
    unsigned char sf1[] = {0x02, 0x3E, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    eng->processFrame(0x7E8, 0, sf1, 8, 100);

    // SF on session 2
    unsigned char sf2[] = {0x03, 0x7F, 0x22, 0x31, 0xCC, 0xCC, 0xCC, 0xCC};
    eng->processFrame(0x7EA, 0, sf2, 8, 200);

    PASSTHRU_MSG msgs[2];
    unsigned long count = 2;
    long ret = eng->receive(msgs, &count, 100);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_EQ(2, (int)count);

    // First message from 0x7E8
    ASSERT_EQ(0xE8, (int)msgs[0].Data[3]);
    ASSERT_EQ(6, (int)msgs[0].DataSize);  // 4 + 2

    // Second message from 0x7EA
    ASSERT_EQ(0xEA, (int)msgs[1].Data[3]);
    ASSERT_EQ(7, (int)msgs[1].DataSize);  // 4 + 3
    delete eng;
}

TEST(rx_unmatched_frame_ignored) {
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    // Frame from unknown ID
    unsigned char sf[] = {0x02, 0xFF, 0xFF, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    eng->processFrame(0x123, 0, sf, 8, 100);

    PASSTHRU_MSG msg;
    unsigned long count = 1;
    long ret = eng->receive(&msg, &count, 50);
    ASSERT_EQ(ERR_BUFFER_EMPTY, ret);
    delete eng;
}

TEST(rx_queue_overflow_drops_oldest) {
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    // Fill queue beyond capacity (ISOTP_RX_QUEUE_SIZE = 64)
    for (int i = 0; i < 70; i++) {
        unsigned char sf[] = {0x01, (unsigned char)i, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
        eng->processFrame(0x7E8, 0, sf, 8, (unsigned long)i);
    }

    // Should get 64 messages (the latest ones)
    PASSTHRU_MSG msgs[64];
    unsigned long count = 64;
    long ret = eng->receive(msgs, &count, 100);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_EQ(64, (int)count);
    // Oldest should be dropped — first message should be #6
    ASSERT_EQ(6, (int)msgs[0].Data[4]);
    delete eng;
}

TEST(rx_29bit_id) {
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x18DA00FA, 0x18DAFA00, canMSG_EXT, canMSG_EXT, 1);

    unsigned char sf[] = {0x03, 0x7F, 0x10, 0x12, 0xCC, 0xCC, 0xCC, 0xCC};
    eng->processFrame(0x18DAFA00, canMSG_EXT, sf, 8, 500);

    PASSTHRU_MSG msg;
    unsigned long count = 1;
    long ret = eng->receive(&msg, &count, 100);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_TRUE(msg.RxStatus & CAN_29BIT_ID);
    // CAN ID bytes
    ASSERT_EQ(0x18, (int)msg.Data[0]);
    ASSERT_EQ(0xDA, (int)msg.Data[1]);
    ASSERT_EQ(0xFA, (int)msg.Data[2]);
    ASSERT_EQ(0x00, (int)msg.Data[3]);
    delete eng;
}

TEST(rx_empty_queue_timeout) {
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    PASSTHRU_MSG msg;
    unsigned long count = 1;
    DWORD start = GetTickCount();
    long ret = eng->receive(&msg, &count, 50);
    DWORD elapsed = GetTickCount() - start;
    ASSERT_EQ(ERR_BUFFER_EMPTY, ret);
    ASSERT_EQ(0, (int)count);
    ASSERT_TRUE(elapsed >= 40);  // Should have waited ~50ms
    delete eng;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Session Management Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(session_add_remove) {
    IsoTpEngine *eng = createEngine();
    int idx = eng->addSession(0x7E0, 0x7E8, 0, 0, 42);
    ASSERT_TRUE(idx >= 0);

    // Should work
    unsigned char sf[] = {0x01, 0xAA, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    eng->processFrame(0x7E8, 0, sf, 8, 100);

    PASSTHRU_MSG msg;
    unsigned long count = 1;
    eng->receive(&msg, &count, 50);
    ASSERT_EQ(1, (int)count);

    // Remove session
    eng->removeSession(42);

    // Should no longer match
    eng->processFrame(0x7E8, 0, sf, 8, 200);
    count = 1;
    long ret = eng->receive(&msg, &count, 50);
    ASSERT_EQ(ERR_BUFFER_EMPTY, ret);
    delete eng;
}

TEST(session_max_limit) {
    IsoTpEngine *eng = createEngine();
    // Fill all ISOTP_MAX_SESSIONS (16)
    for (int i = 0; i < ISOTP_MAX_SESSIONS; i++) {
        int idx = eng->addSession(0x700 + i, 0x780 + i, 0, 0, i + 1);
        ASSERT_TRUE(idx >= 0);
    }
    // Next one should fail
    int idx = eng->addSession(0x7FF, 0x7FF, 0, 0, 99);
    ASSERT_EQ(-1, idx);
    delete eng;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Config Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(config_bs_tx_in_fc) {
    // Verify that BS we send in FC matches config
    IsoTpEngine *eng = createEngine(8, 5, 0xCC, 10);  // bs_tx=8, stmin_tx=5
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    // Receive FF → engine sends FC
    unsigned char ff[] = {0x10, 0x20, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    eng->processFrame(0x7E8, 0, ff, 8, 1000);

    ASSERT_EQ(1, g_mock.txCount);
    ASSERT_EQ(0x30, g_mock.txFrames[0].data[0]);  // FC + CTS
    ASSERT_EQ(8, (int)g_mock.txFrames[0].data[1]);    // BS=8
    ASSERT_EQ(5, (int)g_mock.txFrames[0].data[2]);    // STmin=5ms
    delete eng;
}

TEST(config_runtime_update) {
    IsoTpEngine *eng = createEngine(0, 0, 0xCC, 10);
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    // Update config
    eng->setConfig(4, 10, 0xAA, 5);

    // Receive FF → FC should reflect new config
    unsigned char ff[] = {0x10, 0x10, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    eng->processFrame(0x7E8, 0, ff, 8, 1000);

    ASSERT_EQ(4, (int)g_mock.txFrames[0].data[1]);   // BS=4
    ASSERT_EQ(10, (int)g_mock.txFrames[0].data[2]);  // STmin=10ms
    delete eng;
}

TEST(config_stmin_decode) {
    // Verify STmin values received in FC are handled correctly
    IsoTpEngine *eng = createEngine();
    eng->addSession(0x7E0, 0x7E8, 0, 0, 1);

    unsigned char data[15];
    memset(data, 0x11, 15);

    TxTestCtx ctx = {eng, -1, data, 15};

    HANDLE hThread = CreateThread(NULL, 0, txTestThread, &ctx, 0, NULL);

    Sleep(20);
    // FC with STmin=1ms (0x01)
    unsigned char fc[] = {0x30, 0x00, 0x01, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    eng->processFrame(0x7E8, 0, fc, 8, 1000);

    WaitForSingleObject(hThread, 4000);
    CloseHandle(hThread);

    ASSERT_EQ(STATUS_NOERROR, ctx.result);
    delete eng;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════════

int main() {
    printf("KvaserDirect ISO-TP Engine Unit Tests\n");
    printf("═══════════════════════════════════════════════════\n\n");

    printf("TX Tests:\n");
    RUN_TEST(tx_single_frame_1byte);
    RUN_TEST(tx_single_frame_7bytes);
    RUN_TEST(tx_single_frame_padding);
    RUN_TEST(tx_multi_frame_8bytes);
    RUN_TEST(tx_multi_frame_with_fc);
    RUN_TEST(tx_multi_frame_large_sn_wrap);
    RUN_TEST(tx_multi_frame_bs_limited);
    RUN_TEST(tx_no_session_returns_error);
    RUN_TEST(tx_zero_length_returns_error);
    RUN_TEST(tx_max_length_4095);
    RUN_TEST(tx_over_max_length_returns_error);

    printf("\nRX Tests:\n");
    RUN_TEST(rx_single_frame_1byte);
    RUN_TEST(rx_single_frame_7bytes);
    RUN_TEST(rx_multi_frame_10bytes);
    RUN_TEST(rx_multi_frame_wrong_sn_aborts);
    RUN_TEST(rx_fc_overflow_aborts_tx);
    RUN_TEST(rx_fc_wait_extends_deadline);
    RUN_TEST(rx_multiple_sessions);
    RUN_TEST(rx_unmatched_frame_ignored);
    RUN_TEST(rx_queue_overflow_drops_oldest);
    RUN_TEST(rx_29bit_id);
    RUN_TEST(rx_empty_queue_timeout);

    printf("\nSession Tests:\n");
    RUN_TEST(session_add_remove);
    RUN_TEST(session_max_limit);

    printf("\nConfig Tests:\n");
    RUN_TEST(config_bs_tx_in_fc);
    RUN_TEST(config_runtime_update);
    RUN_TEST(config_stmin_decode);

    printf("\n═══════════════════════════════════════════════════\n");
    printf("Results: %d passed, %d failed, %d total\n",
           g_tests_passed, g_tests_failed, g_tests_run);

    return g_tests_failed > 0 ? 1 : 0;
}
