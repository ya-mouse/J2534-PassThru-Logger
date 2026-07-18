// ReplayJ2534 Unit Tests — Native build (macOS/Linux)
// Tests ConfigStore logic without Win32 dependencies.
// For full Simulator/Scheduler tests, use the mingw build:
//   make -f ReplayJ2534/tests/Makefile.test run

#include "ConfigStore.h"
#include "J2534Defs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Stub logger — ConfigStore.cpp calls g_logger.verbose()
#include "Logger.h"
ReplayLogger g_logger;
ReplayLogger::ReplayLogger() : level_(0), hFile_(0), initialized_(false) {}
ReplayLogger::~ReplayLogger() {}
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
// Test framework
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
// Test scenario
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
"        { \"match\": { \"data\": \"00-00-06-02-10-03\", \"mode\": \"prefix\" },\n"
"          \"response\": { \"data\": \"00-00-04-80-50-03-00-14-00-C8\", \"delayMs\": 0, \"protocolId\": \"ISO15765\" } },\n"
"        { \"match\": { \"data\": \"00-00-06-02-3E-00\", \"mode\": \"prefix\" },\n"
"          \"response\": { \"data\": \"00-00-04-80-7E-00\", \"delayMs\": 0, \"protocolId\": \"ISO15765\" } }\n"
"      ],\n"
"      \"periodic\": [\n"
"        { \"intervalMs\": 50, \"msg\": { \"protocolId\": \"ISO15765\", \"data\": \"00-00-04-80-7E-00\" },\n"
"          \"startOn\": \"connect\", \"stopOn\": \"disconnect\" }\n"
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
// Tests
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
    const IoctlRule *vbatt = cs.findIoctl(READ_VBATT);
    ASSERT_TRUE(vbatt != NULL);
    ASSERT_EQ(STATUS_NOERROR, vbatt->returnCode);
    ASSERT_EQ((int)SCOPE_DEVICE, (int)vbatt->scope);
    ASSERT_TRUE(vbatt->hasOutput);
    ASSERT_TRUE(vbatt->outputAuto);

    const IoctlRule *setcfg = cs.findIoctl(SET_CONFIG);
    ASSERT_TRUE(setcfg != NULL);
    ASSERT_EQ(STATUS_NOERROR, setcfg->returnCode);
    ASSERT_EQ((int)SCOPE_CHANNEL, (int)setcfg->scope);
    ASSERT_TRUE(setcfg->consumeInput);

    const IoctlRule *vendor = cs.findIoctl(0x10ECB);
    ASSERT_TRUE(vendor != NULL);
    ASSERT_EQ(ERR_INVALID_IOCTL_ID, vendor->returnCode);
    ASSERT_EQ((int)SCOPE_ANY, (int)vendor->scope);

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

TEST(config_reply_data) {
    ConfigStore cs;
    cs.load(writeScenarioFile());
    const Target *t = cs.findTarget(J2534_ISO15765, CAN_ID_BOTH, 500000);
    ASSERT_TRUE(t != NULL);
    // First reply: match 00-00-06-02-10-03, response 00-00-04-80-50-03-00-14-00-C8
    ASSERT_EQ(6, t->replies[0].matchData.len);
    unsigned char expectedMatch[] = {0x00,0x00,0x06,0x02,0x10,0x03};
    ASSERT_MEM_EQ(expectedMatch, t->replies[0].matchData.data, 6);
    ASSERT_EQ(10, t->replies[0].response.data.len);
    unsigned char expectedResp[] = {0x00,0x00,0x04,0x80,0x50,0x03,0x00,0x14,0x00,0xC8};
    ASSERT_MEM_EQ(expectedResp, t->replies[0].response.data.data, 10);
    ASSERT_EQ(J2534_ISO15765, t->replies[0].response.protocolId);
}

TEST(config_state_machine) {
    ConfigStore cs;
    cs.load(writeScenarioFile());
    ASSERT_EQ(0, strcmp(cs.stateMachine().initial.c_str(), "CLOSED"));
    ASSERT_EQ(4, (int)cs.stateMachine().transitions.size());
    ASSERT_EQ(0, strcmp(cs.stateMachine().transitions[0].event.c_str(), "PassThruOpen"));
    ASSERT_EQ(0, strcmp(cs.stateMachine().transitions[0].from.c_str(), "CLOSED"));
    ASSERT_EQ(0, strcmp(cs.stateMachine().transitions[0].to.c_str(), "OPENED"));
}

TEST(config_invalid_json) {
    FILE *fp = fopen("test_bad.json", "wb");
    fputs("{ broken json }}}", fp);
    fclose(fp);

    ConfigStore cs;
    ASSERT_TRUE(!cs.load("test_bad.json"));
    ASSERT_TRUE(!cs.isLoaded());
    ASSERT_TRUE(cs.lastError()[0] != '\0');
    remove("test_bad.json");
}

TEST(config_missing_file) {
    ConfigStore cs;
    ASSERT_TRUE(!cs.load("nonexistent_file.json"));
    ASSERT_TRUE(cs.lastError()[0] != '\0');
}

// ═══════════════════════════════════════════════════════════════════════════
// Sequence-mode parsing test
// ═══════════════════════════════════════════════════════════════════════════

static const char *TEST_SCENARIO_SEQ =
"{\n"
"  \"device\": { \"firmwareVersion\": \"1.0\", \"dllVersion\": \"1.0\", \"apiVersion\": \"04.04\", \"vbatt_mV\": 12000 },\n"
"  \"ioctls\": {},\n"
"  \"targets\": [\n"
"    {\n"
"      \"name\": \"ECU\",\n"
"      \"match\": { \"protocolId\": \"ISO15765\", \"flags\": \"CAN_ID_BOTH\", \"baud\": 500000 },\n"
"      \"preferredChannelId\": 1,\n"
"      \"replies\": [\n"
"        { \"match\": { \"data\": \"00-00-07-E0-21-03\", \"mode\": \"prefix\" },\n"
"          \"response\": {\n"
"            \"mode\": \"sequence\",\n"
"            \"sequence\": [\"00-00-07-E8-61-03-AA\", \"00-00-07-E8-61-03-BB\", \"00-00-07-E8-61-03-CC\"],\n"
"            \"timeWindowMs\": 500,\n"
"            \"delayMs\": 10,\n"
"            \"protocolId\": \"ISO15765\"\n"
"          }\n"
"        },\n"
"        { \"match\": { \"data\": \"00-00-07-E0-3E-00\", \"mode\": \"prefix\" },\n"
"          \"response\": { \"data\": \"00-00-07-E8-7E-00\", \"delayMs\": 0, \"protocolId\": \"ISO15765\" }\n"
"        }\n"
"      ]\n"
"    }\n"
"  ],\n"
"  \"states\": { \"initial\": \"CLOSED\", \"transitions\": [\n"
"    { \"event\": \"PassThruOpen\", \"from\": \"CLOSED\", \"to\": \"OPENED\" },\n"
"    { \"event\": \"PassThruClose\", \"from\": \"OPENED\", \"to\": \"CLOSED\" }\n"
"  ] }\n"
"}\n";

TEST(config_sequence_parsing) {
    const char *path = "test_scenario_seq.json";
    FILE *fp = fopen(path, "wb");
    fputs(TEST_SCENARIO_SEQ, fp);
    fclose(fp);

    ConfigStore cs;
    ASSERT_TRUE(cs.load(path));
    const Target *t = cs.findTarget(J2534_ISO15765, CAN_ID_BOTH, 500000);
    ASSERT_TRUE(t != NULL);
    ASSERT_EQ(2, (int)t->replies.size());

    // First reply: sequence mode
    const ReplyRule &seqRule = t->replies[0];
    ASSERT_EQ((int)RESPONSE_SEQUENCE, (int)seqRule.responseMode);
    ASSERT_EQ(500UL, seqRule.timeWindowMs);
    ASSERT_EQ(10UL, seqRule.delayMs);
    ASSERT_EQ(3, (int)seqRule.sequenceData.size());
    ASSERT_EQ(7, seqRule.sequenceData[0].len);
    ASSERT_EQ(0xAA, seqRule.sequenceData[0].data[6]);
    ASSERT_EQ(0xBB, seqRule.sequenceData[1].data[6]);
    ASSERT_EQ(0xCC, seqRule.sequenceData[2].data[6]);
    ASSERT_EQ(J2534_ISO15765, seqRule.response.protocolId);

    // Second reply: single mode (backward compat)
    const ReplyRule &singleRule = t->replies[1];
    ASSERT_EQ((int)RESPONSE_SINGLE, (int)singleRule.responseMode);
    ASSERT_EQ(0, (int)singleRule.sequenceData.size());
    ASSERT_EQ(6, singleRule.response.data.len);

    remove(path);
}

int main() {
    printf("\n=== ReplayJ2534 ConfigStore Tests (native) ===\n\n");

    RUN_TEST(config_load);
    RUN_TEST(config_device);
    RUN_TEST(config_ioctls);
    RUN_TEST(config_targets);
    RUN_TEST(config_hex_parsing);
    RUN_TEST(config_return_code_lookup);
    RUN_TEST(config_ioctl_key_parsing);
    RUN_TEST(config_reply_data);
    RUN_TEST(config_state_machine);
    RUN_TEST(config_invalid_json);
    RUN_TEST(config_missing_file);
    RUN_TEST(config_sequence_parsing);

    printf("\n=== Results: %d passed, %d failed, %d total ===\n\n",
           g_tests_passed, g_tests_failed, g_tests_run);

    remove("test_scenario.json");
    return g_tests_failed > 0 ? 1 : 0;
}
