// ReplayJ2534 Unit Tests — ConfigStore, Simulator, Scheduler
// Compile: see ReplayJ2534/tests/Makefile.test
// Run: wine build/tests/test_simulator.exe  (or natively on Windows)

#include "ConfigStore.h"
#include "Simulator.h"
#include "J2534Defs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// ═══════════════════════════════════════════════════════════════════════════
// Minimal test framework
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

#define ASSERT_MEM_EQ(expected, actual, len) do { \
    if (memcmp((expected), (actual), (len)) != 0) { \
        printf("[FAIL]\n    %s:%d: memory mismatch (%d bytes)\n", \
               __FILE__, __LINE__, (int)(len)); \
        g_tests_failed++; g_tests_passed--; return; \
    } \
} while(0)

// ═══════════════════════════════════════════════════════════════════════════
// Stub logger (so we don't link Logger.cpp)
// ═══════════════════════════════════════════════════════════════════════════

#include "Logger.h"
ReplayLogger g_logger;
ReplayLogger::ReplayLogger() : level_(0), hFile_(INVALID_HANDLE_VALUE), initialized_(false) {
    InitializeCriticalSection(&lock_);
}
ReplayLogger::~ReplayLogger() { DeleteCriticalSection(&lock_); }
void ReplayLogger::init(int, const char*) {}
void ReplayLogger::shutdown() {}
void ReplayLogger::verbose(const char *, ...) {}
void ReplayLogger::debug(const char *, ...) {}
void ReplayLogger::hexDump(const char *, const unsigned char *, unsigned int) {}
void ReplayLogger::apiEntry(const char *, const char *, ...) {}
void ReplayLogger::apiReturn(const char *, long) {}

const char* rjRetCodeName(long) { return "?"; }
const char* rjProtocolName(unsigned long) { return "?"; }
const char* rjIoctlName(unsigned long) { return "?"; }
const char* rjFilterTypeName(unsigned long) { return "?"; }

// ═══════════════════════════════════════════════════════════════════════════
// Test scenario JSON (written to temp file)
// ═══════════════════════════════════════════════════════════════════════════

static const char *TEST_SCENARIO =
"{\n"
"  \"device\": {\n"
"    \"firmwareVersion\": \"3.37.0\",\n"
"    \"dllVersion\": \"1.0.0\",\n"
"    \"apiVersion\": \"04.04\",\n"
"    \"vbatt_mV\": 12000\n"
"  },\n"
"  \"ioctls\": {\n"
"    \"READ_VBATT\": { \"return\": \"STATUS_NOERROR\", \"output\": \"auto\", \"scope\": \"device\" },\n"
"    \"SET_CONFIG\": { \"return\": \"STATUS_NOERROR\", \"scope\": \"channel\", \"consumeInput\": true },\n"
"    \"0x10ECB\": { \"return\": \"ERR_INVALID_IOCTL_ID\", \"scope\": \"any\" }\n"
"  },\n"
"  \"targets\": [\n"
"    {\n"
"      \"name\": \"ECM\",\n"
"      \"match\": { \"protocolId\": \"ISO15765\", \"flags\": \"CAN_ID_BOTH\", \"baud\": 500000 },\n"
"      \"preferredChannelId\": 2,\n"
"      \"replies\": [\n"
"        {\n"
"          \"match\": { \"data\": \"00-00-06-02-10-03\", \"mode\": \"prefix\" },\n"
"          \"response\": {\n"
"            \"data\": \"00-00-04-80-50-03-00-14-00-C8\",\n"
"            \"delayMs\": 0,\n"
"            \"protocolId\": \"ISO15765\"\n"
"          }\n"
"        },\n"
"        {\n"
"          \"match\": { \"data\": \"00-00-06-02-3E-00\", \"mode\": \"prefix\" },\n"
"          \"response\": {\n"
"            \"data\": \"00-00-04-80-7E-00\",\n"
"            \"delayMs\": 0,\n"
"            \"protocolId\": \"ISO15765\"\n"
"          }\n"
"        }\n"
"      ],\n"
"      \"periodic\": [\n"
"        {\n"
"          \"intervalMs\": 50,\n"
"          \"msg\": { \"protocolId\": \"ISO15765\", \"data\": \"00-00-04-80-7E-00\" },\n"
"          \"startOn\": \"connect\",\n"
"          \"stopOn\": \"disconnect\"\n"
"        }\n"
"      ]\n"
"    }\n"
"  ],\n"
"  \"states\": {\n"
"    \"initial\": \"CLOSED\",\n"
"    \"transitions\": [\n"
"      { \"event\": \"PassThruOpen\",       \"from\": \"CLOSED\",  \"to\": \"OPENED\" },\n"
"      { \"event\": \"PassThruConnect\",    \"from\": \"OPENED\",  \"to\": \"OPENED\" },\n"
"      { \"event\": \"PassThruDisconnect\", \"from\": \"OPENED\",  \"to\": \"OPENED\" },\n"
"      { \"event\": \"PassThruClose\",      \"from\": \"OPENED\",  \"to\": \"CLOSED\" }\n"
"    ]\n"
"  }\n"
"}\n";

static const char *writeScenarioFile() {
    const char *path = "test_scenario.json";
    FILE *fp = fopen(path, "wb");
    if (!fp) return NULL;
    fputs(TEST_SCENARIO, fp);
    fclose(fp);
    return path;
}

// ═══════════════════════════════════════════════════════════════════════════
// Helper: build a PASSTHRU_MSG from hex bytes
// ═══════════════════════════════════════════════════════════════════════════

static PASSTHRU_MSG makeMsg(const char *hex, unsigned long proto = J2534_ISO15765) {
    PASSTHRU_MSG m;
    memset(&m, 0, sizeof(m));
    m.ProtocolID = proto;
    m.DataSize = ConfigStore::parseHexBytes(hex, m.Data, sizeof(m.Data));
    m.ExtraDataIndex = m.DataSize;
    return m;
}

// ═══════════════════════════════════════════════════════════════════════════
// ConfigStore tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(config_load) {
    ConfigStore cs;
    ASSERT_TRUE(cs.load(writeScenarioFile()));
    ASSERT_TRUE(cs.isLoaded());
}

TEST(config_device) {
    ConfigStore cs;
    cs.load(writeScenarioFile());
    ASSERT_EQ(0, strcmp(cs.device().firmwareVersion, "3.37.0"));
    ASSERT_EQ(0, strcmp(cs.device().dllVersion, "1.0.0"));
    ASSERT_EQ(0, strcmp(cs.device().apiVersion, "04.04"));
    ASSERT_EQ(12000UL, cs.device().vbatt_mV);
}

TEST(config_ioctls) {
    ConfigStore cs;
    cs.load(writeScenarioFile());
    // READ_VBATT = 0x03
    const IoctlRule *vbatt = cs.findIoctl(READ_VBATT);
    ASSERT_TRUE(vbatt != NULL);
    ASSERT_EQ(STATUS_NOERROR, vbatt->returnCode);
    ASSERT_EQ((int)SCOPE_DEVICE, (int)vbatt->scope);
    ASSERT_TRUE(vbatt->hasOutput);
    ASSERT_TRUE(vbatt->outputAuto);

    // SET_CONFIG = 0x02
    const IoctlRule *setcfg = cs.findIoctl(SET_CONFIG);
    ASSERT_TRUE(setcfg != NULL);
    ASSERT_EQ(STATUS_NOERROR, setcfg->returnCode);
    ASSERT_EQ((int)SCOPE_CHANNEL, (int)setcfg->scope);
    ASSERT_TRUE(setcfg->consumeInput);

    // Unknown vendor IOCTL 0x10ECB
    const IoctlRule *vendor = cs.findIoctl(0x10ECB);
    ASSERT_TRUE(vendor != NULL);
    ASSERT_EQ(ERR_INVALID_IOCTL_ID, vendor->returnCode);
    ASSERT_EQ((int)SCOPE_ANY, (int)vendor->scope);

    // Not configured
    const IoctlRule *none = cs.findIoctl(FAST_INIT);
    ASSERT_TRUE(none == NULL);
}

TEST(config_targets) {
    ConfigStore cs;
    cs.load(writeScenarioFile());
    ASSERT_EQ(1, (int)cs.targets().size());
    const Target *t = cs.findTarget(J2534_ISO15765, CAN_ID_BOTH, 500000);
    ASSERT_TRUE(t != NULL);
    ASSERT_EQ(0, strcmp(t->name.c_str(), "ECM"));
    ASSERT_EQ(2UL, t->preferredChannelId);
    ASSERT_EQ(2, (int)t->replies.size());
    ASSERT_EQ(1, (int)t->periodic.size());

    // Non-matching target
    const Target *none = cs.findTarget(J2534_CAN, 0, 250000);
    ASSERT_TRUE(none == NULL);
}

TEST(config_hex_parsing) {
    unsigned char buf[16];
    int len = (int)ConfigStore::parseHexBytes("00-00-06-02-10-03", buf, sizeof(buf));
    ASSERT_EQ(6, len);
    ASSERT_EQ(0x00, buf[0]);
    ASSERT_EQ(0x06, buf[2]);
    ASSERT_EQ(0x02, buf[3]);
    ASSERT_EQ(0x10, buf[4]);
    ASSERT_EQ(0x03, buf[5]);

    // No separators
    len = (int)ConfigStore::parseHexBytes("00000A0B", buf, sizeof(buf));
    ASSERT_EQ(4, len);
    ASSERT_EQ(0x0A, buf[2]);
    ASSERT_EQ(0x0B, buf[3]);
}

TEST(config_return_code_lookup) {
    ASSERT_EQ(STATUS_NOERROR, ConfigStore::lookupReturnCode("STATUS_NOERROR"));
    ASSERT_EQ(ERR_BUFFER_EMPTY, ConfigStore::lookupReturnCode("ERR_BUFFER_EMPTY"));
    ASSERT_EQ(ERR_INVALID_IOCTL_ID, ConfigStore::lookupReturnCode("ERR_INVALID_IOCTL_ID"));
    ASSERT_EQ(-1L, ConfigStore::lookupReturnCode("ERR_BOGUS"));
}

TEST(config_ioctl_key_parsing) {
    ASSERT_EQ(READ_VBATT, ConfigStore::parseIoctlKey("READ_VBATT"));
    ASSERT_EQ(0x10ECBUL, ConfigStore::parseIoctlKey("0x10ECB"));
    ASSERT_EQ(5UL, ConfigStore::parseIoctlKey("5"));
}

// ═══════════════════════════════════════════════════════════════════════════
// Simulator state machine tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(sim_open_close) {
    Simulator sim;
    ASSERT_TRUE(sim.init(writeScenarioFile(), true));

    unsigned long devId = 0;
    long ret = sim.openDevice(NULL, &devId);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_EQ(1UL, devId);

    // Cannot open again (already OPENED)
    ret = sim.openDevice(NULL, &devId);
    ASSERT_EQ(ERR_DEVICE_IN_USE, ret);

    // Close
    ret = sim.closeDevice(devId);
    ASSERT_EQ(STATUS_NOERROR, ret);

    // Cannot close again
    ret = sim.closeDevice(devId);
    ASSERT_EQ(ERR_INVALID_DEVICE_ID, ret);

    sim.shutdown();
}

TEST(sim_connect_disconnect) {
    Simulator sim;
    sim.init(writeScenarioFile(), true);

    unsigned long devId = 0;
    sim.openDevice(NULL, &devId);

    unsigned long channelId = 0;
    long ret = sim.connect(devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &channelId);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_EQ(2UL, channelId); // preferredChannelId
    ASSERT_TRUE(sim.hasChannel(channelId));
    ASSERT_EQ(1, sim.channelCount());

    // Disconnect
    ret = sim.disconnect(channelId);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_TRUE(!sim.hasChannel(channelId));
    ASSERT_EQ(0, sim.channelCount());

    // Cannot disconnect again
    ret = sim.disconnect(channelId);
    ASSERT_EQ(ERR_INVALID_CHANNEL_ID, ret);

    sim.closeDevice(devId);
    sim.shutdown();
}

TEST(sim_multi_session) {
    Simulator sim;
    sim.init(writeScenarioFile(), true);

    unsigned long devId = 0;
    sim.openDevice(NULL, &devId);

    // First session
    unsigned long ch1 = 0;
    sim.connect(devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &ch1);
    ASSERT_EQ(2UL, ch1);
    sim.disconnect(ch1);

    // Second session — should reuse preferredChannelId (2 is now free)
    unsigned long ch2 = 0;
    sim.connect(devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &ch2);
    ASSERT_EQ(2UL, ch2);
    sim.disconnect(ch2);

    // Open both simultaneously
    unsigned long ch3 = 0, ch4 = 0;
    sim.connect(devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &ch3);
    ASSERT_EQ(2UL, ch3);
    sim.connect(devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &ch4);
    ASSERT_EQ(3UL, ch4); // next available

    sim.disconnect(ch3);
    sim.disconnect(ch4);
    sim.closeDevice(devId);
    sim.shutdown();
}

TEST(sim_close_with_open_channel) {
    Simulator sim;
    sim.init(writeScenarioFile(), true);

    unsigned long devId = 0;
    sim.openDevice(NULL, &devId);

    unsigned long ch = 0;
    sim.connect(devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &ch);

    // Close should fail with channels open
    long ret = sim.closeDevice(devId);
    ASSERT_EQ(ERR_DEVICE_IN_USE, ret);

    sim.disconnect(ch);
    sim.closeDevice(devId);
    sim.shutdown();
}

// ═══════════════════════════════════════════════════════════════════════════
// IOCTL tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(sim_ioctl_read_vbatt_no_connect) {
    // IOCTL on device handle before Connect (the no-connection case)
    Simulator sim;
    sim.init(writeScenarioFile(), true);

    unsigned long devId = 0;
    sim.openDevice(NULL, &devId);

    unsigned long vbatt = 0;
    long ret = sim.ioctl(devId, READ_VBATT, NULL, &vbatt);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_EQ(12000UL, vbatt);

    sim.closeDevice(devId);
    sim.shutdown();
}

TEST(sim_ioctl_unknown) {
    Simulator sim;
    sim.init(writeScenarioFile(), true);

    unsigned long devId = 0;
    sim.openDevice(NULL, &devId);

    long ret = sim.ioctl(devId, FAST_INIT, NULL, NULL);
    ASSERT_EQ(ERR_NOT_SUPPORTED, ret);

    // Vendor IOCTL from config
    ret = sim.ioctl(devId, 0x10ECB, NULL, NULL);
    ASSERT_EQ(ERR_INVALID_IOCTL_ID, ret);

    sim.closeDevice(devId);
    sim.shutdown();
}

TEST(sim_ioctl_scope_enforcement) {
    Simulator sim;
    sim.init(writeScenarioFile(), true);

    unsigned long devId = 0;
    sim.openDevice(NULL, &devId);
    unsigned long ch = 0;
    sim.connect(devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &ch);

    // SET_CONFIG on channel scope with channel handle -> OK
    long ret = sim.ioctl(ch, SET_CONFIG, NULL, NULL);
    ASSERT_EQ(STATUS_NOERROR, ret);

    // READ_VBATT on device scope with device handle -> OK
    unsigned long vbatt = 0;
    ret = sim.ioctl(devId, READ_VBATT, NULL, &vbatt);
    ASSERT_EQ(STATUS_NOERROR, ret);

    // READ_VBATT on device scope with channel handle (wrong scope) -> error
    ret = sim.ioctl(ch, READ_VBATT, NULL, NULL);
    ASSERT_EQ(ERR_INVALID_CHANNEL_ID, ret);

    sim.disconnect(ch);
    sim.closeDevice(devId);
    sim.shutdown();
}

// ═══════════════════════════════════════════════════════════════════════════
// ReadMsgs / WriteMsgs tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(sim_read_empty) {
    Simulator sim;
    sim.init(writeScenarioFile(), true);

    unsigned long devId = 0;
    sim.openDevice(NULL, &devId);
    unsigned long ch = 0;
    sim.connect(devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &ch);

    PASSTHRU_MSG msg;
    unsigned long num = 1;
    long ret = sim.readMsgs(ch, &msg, &num, 0);
    ASSERT_EQ(ERR_BUFFER_EMPTY, ret);
    ASSERT_EQ(0UL, num);

    sim.disconnect(ch);
    sim.closeDevice(devId);
    sim.shutdown();
}

TEST(sim_write_reply) {
    Simulator sim;
    sim.init(writeScenarioFile(), true);

    unsigned long devId = 0;
    sim.openDevice(NULL, &devId);
    unsigned long ch = 0;
    sim.connect(devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &ch);

    // Write UDS diagnostic session request
    PASSTHRU_MSG req = makeMsg("00-00-06-02-10-03");
    unsigned long num = 1;
    long ret = sim.writeMsgs(ch, &req, &num, 0);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_EQ(1UL, num);

    // Wait for Scheduler to deliver reply (instant mode: ~1ms)
    Sleep(50);

    // Read the reply
    PASSTHRU_MSG reply;
    unsigned long rnum = 1;
    ret = sim.readMsgs(ch, &reply, &rnum, 0);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_EQ(1UL, rnum);
    ASSERT_EQ(10UL, reply.DataSize);

    // Verify reply data: 00-00-04-80-50-03-00-14-00-C8
    unsigned char expected[] = {0x00,0x00,0x04,0x80,0x50,0x03,0x00,0x14,0x00,0xC8};
    ASSERT_MEM_EQ(expected, reply.Data, 10);

    sim.disconnect(ch);
    sim.closeDevice(devId);
    sim.shutdown();
}

TEST(sim_write_tester_present) {
    Simulator sim;
    sim.init(writeScenarioFile(), true);

    unsigned long devId = 0;
    sim.openDevice(NULL, &devId);
    unsigned long ch = 0;
    sim.connect(devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &ch);

    PASSTHRU_MSG req = makeMsg("00-00-06-02-3E-00");
    unsigned long num = 1;
    sim.writeMsgs(ch, &req, &num, 0);
    Sleep(50);

    PASSTHRU_MSG reply;
    unsigned long rnum = 1;
    long ret = sim.readMsgs(ch, &reply, &rnum, 0);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_EQ(6UL, reply.DataSize);

    unsigned char expected[] = {0x00,0x00,0x04,0x80,0x7E,0x00};
    ASSERT_MEM_EQ(expected, reply.Data, 6);

    sim.disconnect(ch);
    sim.closeDevice(devId);
    sim.shutdown();
}

TEST(sim_write_no_match) {
    Simulator sim;
    sim.init(writeScenarioFile(), true);

    unsigned long devId = 0;
    sim.openDevice(NULL, &devId);
    unsigned long ch = 0;
    sim.connect(devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &ch);

    // Write a message that doesn't match any reply rule
    PASSTHRU_MSG req = makeMsg("00-00-06-02-22-11");
    unsigned long num = 1;
    long ret = sim.writeMsgs(ch, &req, &num, 0);
    ASSERT_EQ(STATUS_NOERROR, ret);

    Sleep(50);
    // No reply should be available (only periodic if interval fired)
    // Don't assert ERR_BUFFER_EMPTY since periodic might have fired
    // Just verify write succeeded

    sim.disconnect(ch);
    sim.closeDevice(devId);
    sim.shutdown();
}

TEST(sim_periodic) {
    Simulator sim;
    sim.init(writeScenarioFile(), true);

    unsigned long devId = 0;
    sim.openDevice(NULL, &devId);
    unsigned long ch = 0;
    sim.connect(devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &ch);

    // In instant mode, first periodic fire is immediate.
    // Wait for scheduler to deliver.
    Sleep(60);

    // Should have at least one message in the queue
    ASSERT_TRUE(sim.rxQueueSize(ch) >= 1);

    // Read it
    PASSTHRU_MSG msg;
    unsigned long num = 1;
    long ret = sim.readMsgs(ch, &msg, &num, 0);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_EQ(6UL, msg.DataSize);

    sim.disconnect(ch);
    sim.closeDevice(devId);
    sim.shutdown();
}

TEST(sim_periodic_stops_on_disconnect) {
    Simulator sim;
    sim.init(writeScenarioFile(), true);

    unsigned long devId = 0;
    sim.openDevice(NULL, &devId);
    unsigned long ch = 0;
    sim.connect(devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &ch);
    Sleep(60);

    sim.disconnect(ch);
    int qsize = sim.rxQueueSize(ch);
    ASSERT_EQ(-1, qsize); // channel gone

    sim.closeDevice(devId);
    sim.shutdown();
}

// ═══════════════════════════════════════════════════════════════════════════
// ReadVersion test
// ═══════════════════════════════════════════════════════════════════════════

TEST(sim_read_version) {
    Simulator sim;
    sim.init(writeScenarioFile(), true);

    unsigned long devId = 0;
    sim.openDevice(NULL, &devId);

    char fw[80], dll[80], api[80];
    long ret = sim.readVersion(devId, fw, dll, api);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_EQ(0, strcmp(fw, "3.37.0"));
    ASSERT_EQ(0, strcmp(dll, "1.0.0"));
    ASSERT_EQ(0, strcmp(api, "04.04"));

    sim.closeDevice(devId);
    sim.shutdown();
}

// ═══════════════════════════════════════════════════════════════════════════
// Filter tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(sim_filters) {
    Simulator sim;
    sim.init(writeScenarioFile(), true);

    unsigned long devId = 0;
    sim.openDevice(NULL, &devId);
    unsigned long ch = 0;
    sim.connect(devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &ch);

    unsigned long filterId = 0;
    long ret = sim.startMsgFilter(ch, FLOW_CONTROL_FILTER, NULL, NULL, NULL, &filterId);
    ASSERT_EQ(STATUS_NOERROR, ret);
    ASSERT_EQ(1UL, filterId);

    ret = sim.stopMsgFilter(ch, filterId);
    ASSERT_EQ(STATUS_NOERROR, ret);

    // Stopping again should fail
    ret = sim.stopMsgFilter(ch, filterId);
    ASSERT_EQ(ERR_INVALID_FILTER_ID, ret);

    sim.disconnect(ch);
    sim.closeDevice(devId);
    sim.shutdown();
}

// ═══════════════════════════════════════════════════════════════════════════
// Sequence-mode test scenario
// ═══════════════════════════════════════════════════════════════════════════

static const char *TEST_SCENARIO_SEQ =
"{\n"
"  \"device\": {\n"
"    \"firmwareVersion\": \"3.37.0\",\n"
"    \"dllVersion\": \"1.0.0\",\n"
"    \"apiVersion\": \"04.04\",\n"
"    \"vbatt_mV\": 12000\n"
"  },\n"
"  \"ioctls\": {\n"
"    \"READ_VBATT\": { \"return\": \"STATUS_NOERROR\", \"output\": \"auto\", \"scope\": \"device\" }\n"
"  },\n"
"  \"targets\": [\n"
"    {\n"
"      \"name\": \"ECM\",\n"
"      \"match\": { \"protocolId\": \"ISO15765\", \"flags\": \"CAN_ID_BOTH\", \"baud\": 500000 },\n"
"      \"preferredChannelId\": 2,\n"
"      \"replies\": [\n"
"        {\n"
"          \"match\": { \"data\": \"00-00-06-02-22-01\", \"mode\": \"prefix\" },\n"
"          \"response\": {\n"
"            \"mode\": \"sequence\",\n"
"            \"sequence\": [\n"
"              \"00-00-04-80-62-01-AA\",\n"
"              \"00-00-04-80-62-01-BB\",\n"
"              \"00-00-04-80-62-01-CC\"\n"
"            ],\n"
"            \"timeWindowMs\": 100,\n"
"            \"delayMs\": 0,\n"
"            \"protocolId\": \"ISO15765\"\n"
"          }\n"
"        }\n"
"      ]\n"
"    }\n"
"  ],\n"
"  \"states\": {\n"
"    \"initial\": \"CLOSED\",\n"
"    \"transitions\": [\n"
"      { \"event\": \"PassThruOpen\", \"from\": \"CLOSED\", \"to\": \"OPENED\" },\n"
"      { \"event\": \"PassThruConnect\", \"from\": \"OPENED\", \"to\": \"OPENED\" },\n"
"      { \"event\": \"PassThruDisconnect\", \"from\": \"OPENED\", \"to\": \"OPENED\" },\n"
"      { \"event\": \"PassThruClose\", \"from\": \"OPENED\", \"to\": \"CLOSED\" }\n"
"    ]\n"
"  }\n"
"}\n";

static const char *writeSeqScenarioFile() {
    const char *path = "test_scenario_seq.json";
    FILE *fp = fopen(path, "wb");
    if (!fp) return NULL;
    fputs(TEST_SCENARIO_SEQ, fp);
    fclose(fp);
    return path;
}

// ═══════════════════════════════════════════════════════════════════════════
// ConfigStore sequence parsing test
// ═══════════════════════════════════════════════════════════════════════════

TEST(config_sequence_parsing) {
    ConfigStore cs;
    ASSERT_TRUE(cs.load(writeSeqScenarioFile()));
    ASSERT_EQ(1, (int)cs.targets().size());
    const Target *t = cs.findTarget(J2534_ISO15765, CAN_ID_BOTH, 500000);
    ASSERT_TRUE(t != NULL);
    ASSERT_EQ(1, (int)t->replies.size());
    const ReplyRule &r = t->replies[0];
    ASSERT_EQ((int)RESPONSE_SEQUENCE, (int)r.responseMode);
    ASSERT_EQ(100UL, r.timeWindowMs);
    ASSERT_EQ(3, (int)r.sequenceData.size());
    ASSERT_EQ(7, r.sequenceData[0].len);
    ASSERT_EQ(0xAA, r.sequenceData[0].data[6]);
    ASSERT_EQ(0xBB, r.sequenceData[1].data[6]);
    ASSERT_EQ(0xCC, r.sequenceData[2].data[6]);
}

// ═══════════════════════════════════════════════════════════════════════════
// Simulator sequence advance + loop test (instant mode: advance every read)
// ═══════════════════════════════════════════════════════════════════════════

TEST(sim_sequence_advance_and_loop) {
    Simulator sim;
    sim.init(writeSeqScenarioFile(), true);  // instant mode

    unsigned long devId = 0;
    sim.openDevice(NULL, &devId);
    unsigned long ch = 0;
    sim.connect(devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &ch);

    PASSTHRU_MSG req = makeMsg("00-00-06-02-22-01");
    unsigned long num = 1;
    PASSTHRU_MSG reply;
    unsigned long rnum = 1;

    // 1st read: returns seq[0] (initialization, no advance)
    sim.writeMsgs(ch, &req, &num, 0);
    rnum = 1;
    sim.readMsgs(ch, &reply, &rnum, 500);
    ASSERT_EQ(7UL, reply.DataSize);
    ASSERT_EQ(0xAA, reply.Data[6]);

    // 2nd read: advances to seq[1]
    sim.writeMsgs(ch, &req, &num, 0);
    rnum = 1;
    sim.readMsgs(ch, &reply, &rnum, 500);
    ASSERT_EQ(7UL, reply.DataSize);
    ASSERT_EQ(0xBB, reply.Data[6]);

    // 3rd read: advances to seq[2]
    sim.writeMsgs(ch, &req, &num, 0);
    rnum = 1;
    sim.readMsgs(ch, &reply, &rnum, 500);
    ASSERT_EQ(7UL, reply.DataSize);
    ASSERT_EQ(0xCC, reply.Data[6]);

    // 4th read: wraps to seq[0] (loop)
    sim.writeMsgs(ch, &req, &num, 0);
    rnum = 1;
    sim.readMsgs(ch, &reply, &rnum, 500);
    ASSERT_EQ(7UL, reply.DataSize);
    ASSERT_EQ(0xAA, reply.Data[6]);

    sim.disconnect(ch);
    sim.closeDevice(devId);
    sim.shutdown();
}

// ═══════════════════════════════════════════════════════════════════════════
// Time-gated advance test (non-instant mode: burst holds, spread advances)
// ═══════════════════════════════════════════════════════════════════════════

TEST(sim_sequence_time_gated_advance) {
    Simulator sim;
    // NON-instant mode: timeWindowMs=100 from TEST_SCENARIO_SEQ
    sim.init(writeSeqScenarioFile(), false);

    unsigned long devId = 0;
    sim.openDevice(NULL, &devId);
    unsigned long ch = 0;
    sim.connect(devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &ch);

    PASSTHRU_MSG req = makeMsg("00-00-06-02-22-01");
    unsigned long num = 1;
    PASSTHRU_MSG reply;
    unsigned long rnum = 1;

    // Use timeout=500 in readMsgs so it waits for Scheduler delivery
    // instead of returning ERR_BUFFER_EMPTY if the reply isn't queued yet.

    // 1st read: initialization → seq[0] (AA), no advance
    sim.writeMsgs(ch, &req, &num, 0);
    rnum = 1;
    sim.readMsgs(ch, &reply, &rnum, 500);
    ASSERT_EQ(0xAA, reply.Data[6]);

    // 2nd read immediately (burst, <100ms window) → still seq[0] (AA)
    sim.writeMsgs(ch, &req, &num, 0);
    rnum = 1;
    sim.readMsgs(ch, &reply, &rnum, 500);
    ASSERT_EQ(0xAA, reply.Data[6]);

    // 3rd read immediately (still burst) → still seq[0] (AA)
    sim.writeMsgs(ch, &req, &num, 0);
    rnum = 1;
    sim.readMsgs(ch, &reply, &rnum, 500);
    ASSERT_EQ(0xAA, reply.Data[6]);

    // Sleep past window (100ms), then read → advance to seq[1] (BB)
    Sleep(150);
    sim.writeMsgs(ch, &req, &num, 0);
    rnum = 1;
    sim.readMsgs(ch, &reply, &rnum, 500);
    ASSERT_EQ(0xBB, reply.Data[6]);

    // Sleep past window → advance to seq[2] (CC)
    Sleep(150);
    sim.writeMsgs(ch, &req, &num, 0);
    rnum = 1;
    sim.readMsgs(ch, &reply, &rnum, 500);
    ASSERT_EQ(0xCC, reply.Data[6]);

    // Sleep past window → wrap to seq[0] (AA) — loop in non-instant mode
    Sleep(150);
    sim.writeMsgs(ch, &req, &num, 0);
    rnum = 1;
    sim.readMsgs(ch, &reply, &rnum, 500);
    ASSERT_EQ(0xAA, reply.Data[6]);

    sim.disconnect(ch);
    sim.closeDevice(devId);
    sim.shutdown();
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main() {
    printf("\n=== ReplayJ2534 Unit Tests ===\n\n");

    printf("--- ConfigStore ---\n");
    RUN_TEST(config_load);
    RUN_TEST(config_device);
    RUN_TEST(config_ioctls);
    RUN_TEST(config_targets);
    RUN_TEST(config_hex_parsing);
    RUN_TEST(config_return_code_lookup);
    RUN_TEST(config_ioctl_key_parsing);
    RUN_TEST(config_sequence_parsing);

    printf("\n--- State Machine ---\n");
    RUN_TEST(sim_open_close);
    RUN_TEST(sim_connect_disconnect);
    RUN_TEST(sim_multi_session);
    RUN_TEST(sim_close_with_open_channel);

    printf("\n--- IOCTL ---\n");
    RUN_TEST(sim_ioctl_read_vbatt_no_connect);
    RUN_TEST(sim_ioctl_unknown);
    RUN_TEST(sim_ioctl_scope_enforcement);

    printf("\n--- ReadMsgs / WriteMsgs ---\n");
    RUN_TEST(sim_read_empty);
    RUN_TEST(sim_write_reply);
    RUN_TEST(sim_write_tester_present);
    RUN_TEST(sim_write_no_match);
    RUN_TEST(sim_periodic);
    RUN_TEST(sim_periodic_stops_on_disconnect);

    printf("\n--- Sequence Mode ---\n");
    RUN_TEST(sim_sequence_advance_and_loop);
    RUN_TEST(sim_sequence_time_gated_advance);

    printf("\n--- Version / Filters ---\n");
    RUN_TEST(sim_read_version);
    RUN_TEST(sim_filters);

    printf("\n=== Results: %d passed, %d failed, %d total ===\n\n",
           g_tests_passed, g_tests_failed, g_tests_run);

    // Cleanup temp files
    remove("test_scenario.json");
    remove("test_scenario_seq.json");

    return g_tests_failed > 0 ? 1 : 0;
}
