// j2534_elm_test.cpp — ElmJ2534 E2E bench client
// Drives ElmJ2534.dll (ELM327 over BT SPP) against a live CAN scenario:
//   01 00 → 41 00 …            single frame
//   09 02 → 49 02 01 <VIN>     multi-frame (real ISO-TP flow control)
// Usage: j2534_elm_test.exe [dll] [reqIdHex] [respIdHex]
//   defaults: ElmJ2534.dll 7DF 7E8   (W204 bench later: ElmJ2534.dll 602 480)
// Compile: i686-w64-mingw32-g++ -Wall -static -O2 -o j2534_elm_test.exe j2534_elm_test.cpp
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// J2534 v04.04 function pointer types (mirrors ReplayJ2534/tests/j2534_test.cpp)
#define PTAPI __stdcall
typedef long (PTAPI *PFN_PassThruOpen)(void *pName, unsigned long *pDeviceID);
typedef long (PTAPI *PFN_PassThruClose)(unsigned long DeviceID);
typedef long (PTAPI *PFN_PassThruConnect)(unsigned long DeviceID, unsigned long ProtocolID,
    unsigned long Flags, unsigned long BaudRate, unsigned long *pChannelID);
typedef long (PTAPI *PFN_PassThruDisconnect)(unsigned long ChannelID);
typedef long (PTAPI *PFN_PassThruReadMsgs)(unsigned long ChannelID, void *pMsg,
    unsigned long *pNumMsgs, unsigned long Timeout);
typedef long (PTAPI *PFN_PassThruWriteMsgs)(unsigned long ChannelID, void *pMsg,
    unsigned long *pNumMsgs, unsigned long Timeout);
typedef long (PTAPI *PFN_PassThruStartMsgFilter)(unsigned long ChannelID, unsigned long FilterType,
    const void *pMaskMsg, const void *pPatternMsg, const void *pFlowControlMsg, unsigned long *pFilterID);
typedef long (PTAPI *PFN_PassThruStopMsgFilter)(unsigned long ChannelID, unsigned long FilterID);
typedef long (PTAPI *PFN_PassThruReadVersion)(unsigned long DeviceID,
    char *pFirmwareVersion, char *pDllVersion, char *pApiVersion);
typedef long (PTAPI *PFN_PassThruGetLastError)(char *pErrorDescription);
typedef long (PTAPI *PFN_PassThruIoctl)(unsigned long ChannelID, unsigned long IoctlID,
    void *pInput, void *pOutput);
typedef long (PTAPI *PFN_PassThruStartPeriodicMsg)(unsigned long ChannelID, void *pMsg,
    unsigned long *pMsgID, unsigned long TimeInterval);
typedef long (PTAPI *PFN_PassThruStopPeriodicMsg)(unsigned long ChannelID, unsigned long MsgID);
typedef long (PTAPI *PFN_PassThruSetProgrammingVoltage)(unsigned long DeviceID,
    unsigned long PinNumber, unsigned long Voltage);

// Spec constants — exact values per P-LIVE-002 (never guess these)
#define J2534_ISO15765        0x06
#define CAN_ID_BOTH           0x00000800
#define ISO15765_FRAME_PAD    0x0040
#define READ_VBATT            0x03
#define FLOW_CONTROL_FILTER   0x00000003
#define STATUS_NOERROR        0x00
#define ERR_NOT_SUPPORTED     0x01
#define ERR_TIMEOUT           0x09
#define TX_MSG_TYPE           0x0001
#define START_OF_MESSAGE      0x0002

#pragma pack(push, 4)
typedef struct {
    unsigned long ProtocolID;
    unsigned long RxStatus;
    unsigned long TxFlags;
    unsigned long Timestamp;
    unsigned long DataSize;
    unsigned long ExtraDataIndex;
    unsigned char Data[4128];
} PASSTHRU_MSG;
#pragma pack(pop)

struct Exports {
    PFN_PassThruOpen           Open;
    PFN_PassThruClose          Close;
    PFN_PassThruConnect        Connect;
    PFN_PassThruDisconnect     Disconnect;
    PFN_PassThruReadMsgs       ReadMsgs;
    PFN_PassThruWriteMsgs      WriteMsgs;
    PFN_PassThruStartMsgFilter StartFilter;
    PFN_PassThruStopMsgFilter  StopFilter;
    PFN_PassThruReadVersion    ReadVersion;
    PFN_PassThruGetLastError   GetErr;
    PFN_PassThruIoctl          Ioctl;
    PFN_PassThruStartPeriodicMsg    StartPeriodic;
    PFN_PassThruStopPeriodicMsg     StopPeriodic;
    PFN_PassThruSetProgrammingVoltage SetProgVoltage;
};

// Tracks what was opened so every exit path can tear down in order.
struct Session {
    Exports fn;
    HMODULE dll;
    unsigned long devId;
    unsigned long chId;
    bool devOpen;
    bool chOpen;
    Session() : dll(NULL), devId(0), chId(0), devOpen(false), chOpen(false) {
        memset(&fn, 0, sizeof(fn));
    }
};

static int g_missingExports = 0;

// private helpers

static FARPROC mustResolve(HMODULE h, const char *name) {
    FARPROC p = GetProcAddress(h, name);
    if (!p) {
        printf("  MISSING export: %s\n", name);
        g_missingExports++;
    }
    return p;
}

static bool resolveExports(Session &s) {
    s.fn.Open        = (PFN_PassThruOpen)mustResolve(s.dll, "PassThruOpen");
    s.fn.Close       = (PFN_PassThruClose)mustResolve(s.dll, "PassThruClose");
    s.fn.Connect     = (PFN_PassThruConnect)mustResolve(s.dll, "PassThruConnect");
    s.fn.Disconnect  = (PFN_PassThruDisconnect)mustResolve(s.dll, "PassThruDisconnect");
    s.fn.ReadMsgs    = (PFN_PassThruReadMsgs)mustResolve(s.dll, "PassThruReadMsgs");
    s.fn.WriteMsgs   = (PFN_PassThruWriteMsgs)mustResolve(s.dll, "PassThruWriteMsgs");
    s.fn.StartFilter = (PFN_PassThruStartMsgFilter)mustResolve(s.dll, "PassThruStartMsgFilter");
    s.fn.StopFilter  = (PFN_PassThruStopMsgFilter)mustResolve(s.dll, "PassThruStopMsgFilter");
    s.fn.ReadVersion = (PFN_PassThruReadVersion)mustResolve(s.dll, "PassThruReadVersion");
    s.fn.GetErr      = (PFN_PassThruGetLastError)mustResolve(s.dll, "PassThruGetLastError");
    s.fn.Ioctl       = (PFN_PassThruIoctl)mustResolve(s.dll, "PassThruIoctl");
    s.fn.StartPeriodic  = (PFN_PassThruStartPeriodicMsg)mustResolve(s.dll, "PassThruStartPeriodicMsg");
    s.fn.StopPeriodic   = (PFN_PassThruStopPeriodicMsg)mustResolve(s.dll, "PassThruStopPeriodicMsg");
    s.fn.SetProgVoltage = (PFN_PassThruSetProgrammingVoltage)mustResolve(s.dll, "PassThruSetProgrammingVoltage");
    return g_missingExports == 0;
}

static void printLastErr(Session &s) {
    char buf[256] = {0};
    if (s.fn.GetErr) {
        long rc = s.fn.GetErr(buf);
        printf("  PassThruGetLastError => %ld: '%s'\n", rc, buf);
    } else {
        printf("  Win32 GetLastError=%lu\n", GetLastError());
    }
}

// Single teardown for ALL exit paths (success and failure).
static void sessionClose(Session &s) {
    if (s.chOpen) {
        long rc = s.fn.Disconnect(s.chId);
        printf("PassThruDisconnect(%lu) => %ld\n", s.chId, rc);
        s.chOpen = false;
    }
    if (s.devOpen) {
        long rc = s.fn.Close(s.devId);
        printf("PassThruClose(%lu) => %ld\n", s.devId, rc);
        s.devOpen = false;
    }
    if (s.dll) {
        FreeLibrary(s.dll);
        s.dll = NULL;
    }
}

// J2534 ISO15765 frames carry the CAN id in Data[0..3], big-endian.
static void putBeId(unsigned char *dst, unsigned long id) {
    dst[0] = (unsigned char)((id >> 24) & 0xFF);
    dst[1] = (unsigned char)((id >> 16) & 0xFF);
    dst[2] = (unsigned char)((id >> 8) & 0xFF);
    dst[3] = (unsigned char)(id & 0xFF);
}

static unsigned long getBeId(const unsigned char *src) {
    return ((unsigned long)src[0] << 24) | ((unsigned long)src[1] << 16)
         | ((unsigned long)src[2] << 8) | (unsigned long)src[3];
}

static void printHex(const char *label, const unsigned char *data, unsigned long len) {
    printf("%s", label);
    for (unsigned long i = 0; i < len; i++)
        printf("%02X ", data[i]);
    printf("\n");
}

static unsigned long parseHexArg(const char *arg, unsigned long dflt, const char *what) {
    if (!arg) return dflt;
    char *end = NULL;
    unsigned long v = strtoul(arg, &end, 16);
    if (end == arg) {
        printf("WARNING: bad %s '%s' — using default %lX\n", what, arg, dflt);
        return dflt;
    }
    return v;
}

// Synchronous in ElmJ2534: the whole ELM exchange (and thus the response
// queueing) completes before WriteMsgs returns — 10 s covers the BT round trip.
static bool writeRequest(Session &s, unsigned long canId,
                         const unsigned char *payload, unsigned long len) {
    PASSTHRU_MSG msg;
    memset(&msg, 0, sizeof(msg));
    msg.ProtocolID = J2534_ISO15765;
    msg.TxFlags = ISO15765_FRAME_PAD;
    msg.DataSize = 4 + len;
    msg.ExtraDataIndex = msg.DataSize;
    putBeId(msg.Data, canId);
    memcpy(msg.Data + 4, payload, len);

    unsigned long num = 1;
    long rc = s.fn.WriteMsgs(s.chId, &msg, &num, 10000);
    printf("PassThruWriteMsgs(id=%03lX, DataSize=%lu, timeout=10000) => %ld (numWritten=%lu)\n",
           canId, msg.DataSize, rc, num);
    if (rc != STATUS_NOERROR)
        printLastErr(s);
    return rc == STATUS_NOERROR && num == 1;
}

// Drain until a payload-bearing frame (DataSize>4) shows up. The TX echo and
// SOM notifications are 4-byte-id-only messages and must be skipped, not
// mistaken for responses.
static bool readResponse(Session &s, unsigned long expectId, PASSTHRU_MSG &out,
                         unsigned long timeoutMs = 8000) {
    for (int attempt = 1; attempt <= 10; attempt++) {
        memset(&out, 0, sizeof(out));
        unsigned long num = 1;
        long rc = s.fn.ReadMsgs(s.chId, &out, &num, timeoutMs);
        printf("PassThruReadMsgs(attempt %d/10, timeout=%lu) => %ld (numRead=%lu)\n",
               attempt, timeoutMs, rc, num);
        if (rc == ERR_TIMEOUT)
            continue;
        if (rc != STATUS_NOERROR || num == 0) {
            printf("  read failed (rc=%ld) — giving up\n", rc);
            if (rc != STATUS_NOERROR)
                printLastErr(s);
            return false;
        }
        if (out.DataSize == 4 && (out.RxStatus & (TX_MSG_TYPE | START_OF_MESSAGE))) {
            printf("  %s: id=%03lX RxStatus=0x%lX — skipped\n",
                   (out.RxStatus & TX_MSG_TYPE) ? "echo" : "som",
                   getBeId(out.Data), out.RxStatus);
            continue;
        }
        if (out.DataSize > 4) {
            unsigned long gotId = getBeId(out.Data);
            if (gotId != expectId)
                printf("  WARNING: response id %03lX != expected %03lX (multi-ECU bus?) — accepting\n",
                       gotId, expectId);
            return true;
        }
        printf("  stray: DataSize=%lu RxStatus=0x%lX — skipped\n",
               out.DataSize, out.RxStatus);
    }
    printf("  no usable response after 10 attempts\n");
    return false;
}

static void startFlowFilter(Session &s, unsigned long reqId, unsigned long respId) {
    // mask FF.FF.FF.FF = exact-id match; pattern = response id to flow-control
    // for; flow = request id the FC frame is sent to. Non-fatal: ElmJ2534
    // already programs ATCRA/ATFCSH during init and works without filters.
    PASSTHRU_MSG mask, pat, flow;
    memset(&mask, 0, sizeof(mask));
    memset(&pat, 0, sizeof(pat));
    memset(&flow, 0, sizeof(flow));
    const unsigned char all[4] = {0xFF, 0xFF, 0xFF, 0xFF};

    mask.ProtocolID = J2534_ISO15765;
    mask.TxFlags = ISO15765_FRAME_PAD;
    mask.DataSize = 4;
    mask.ExtraDataIndex = 4;
    memcpy(mask.Data, all, 4);

    pat.ProtocolID = J2534_ISO15765;
    pat.TxFlags = ISO15765_FRAME_PAD;
    pat.DataSize = 4;
    pat.ExtraDataIndex = 4;
    putBeId(pat.Data, respId);

    flow.ProtocolID = J2534_ISO15765;
    flow.TxFlags = ISO15765_FRAME_PAD;
    flow.DataSize = 4;
    flow.ExtraDataIndex = 4;
    putBeId(flow.Data, reqId);

    unsigned long filterId = 0;
    long rc = s.fn.StartFilter(s.chId, FLOW_CONTROL_FILTER, &mask, &pat, &flow, &filterId);
    printf("PassThruStartMsgFilter(FLOW_CONTROL, pattern=%03lX, flow=%03lX) => %ld (filterId=%lu)\n",
           respId, reqId, rc, filterId);
    if (rc != STATUS_NOERROR) {
        printf("  WARNING: filter failed — continuing without it\n");
        printLastErr(s);
    }
}

static bool testMode0100(Session &s, unsigned long reqId, unsigned long respId) {
    printf("\n--- Test 1: 01 00 supported PIDs (single frame) ---\n");
    const unsigned char req[2] = {0x01, 0x00};
    if (!writeRequest(s, reqId, req, 2)) return false;
    PASSTHRU_MSG resp;
    if (!readResponse(s, respId, resp)) return false;
    printHex("  rx frame: ", resp.Data, resp.DataSize);
    // positive response 0x41 for mode 0x01 + 4 PID-bitmap bytes = payload >= 6
    bool ok = resp.DataSize >= 4 + 6 && resp.Data[4] == 0x41 && resp.Data[5] == 0x00;
    printf("  DataSize=%lu (expected >= %d), Data[4]=%02X Data[5]=%02X (expected 41 00)\n",
           resp.DataSize, 4 + 6, resp.Data[4], resp.Data[5]);
    printf("Test 1: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static bool testMode0902(Session &s, unsigned long reqId, unsigned long respId) {
    printf("\n--- Test 2: 09 02 VIN (multi-frame ISO-TP, real flow control) ---\n");
    const unsigned char req[2] = {0x09, 0x02};
    if (!writeRequest(s, reqId, req, 2)) return false;
    PASSTHRU_MSG resp;
    if (!readResponse(s, respId, resp)) return false;
    printHex("  rx frame: ", resp.Data, resp.DataSize);
    // 49 02 01 + 17 VIN chars = exactly 20 payload bytes, reassembled by the
    // DLL from first+consecutive frames — anything short means FC is broken.
    bool ok = resp.DataSize == 4 + 20 && resp.Data[4] == 0x49 && resp.Data[5] == 0x02;
    printf("  DataSize=%lu (expected %d), Data[4]=%02X Data[5]=%02X (expected 49 02)\n",
           resp.DataSize, 4 + 20, resp.Data[4], resp.Data[5]);
    if (ok) {
        char vin[18];
        memcpy(vin, resp.Data + 7, 17); // Data[6] = VIN count byte (0x01)
        vin[17] = '\0';
        printf("  count byte=%02X (expected 01), VIN='%s'\n", resp.Data[6], vin);
    }
    printf("Test 2: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static bool testVbattAfter(Session &s) {
    printf("\n--- Test 3: READ_VBATT after traffic (warn-only) ---\n");
    unsigned long mv = 0;
    long rc = s.fn.Ioctl(s.devId, READ_VBATT, NULL, &mv);
    printf("PassThruIoctl(READ_VBATT) => %ld (%lu mV)\n", rc, mv);
    if (rc != STATUS_NOERROR) {
        printf("  WARNING: VBATT re-read failed — does not affect exit code\n");
        printLastErr(s);
    }
    printf("Test 3: %s\n", rc == STATUS_NOERROR ? "PASS" : "FAIL (warn-only)");
    return rc == STATUS_NOERROR;
}

// The exports that must exist but do nothing useful on an ELM327: periodic
// messages are accepted as no-ops (the chip has no free-running TX — every
// frame costs one host exchange) and programming voltage is unsupported. Apps
// probe both, so a missing export or one that returns the wrong code is a real
// defect — but it is not an OBD failure, so it stays warn-only.
static bool testNoOpExports(Session &s) {
    printf("\n--- Test 4: periodic + programming-voltage exports (warn-only) ---\n");
    bool ok = true;

    PASSTHRU_MSG msg;
    memset(&msg, 0, sizeof(msg));
    msg.ProtocolID = J2534_ISO15765;
    msg.TxFlags = ISO15765_FRAME_PAD;
    msg.DataSize = 6;
    msg.ExtraDataIndex = 6;
    putBeId(msg.Data, 0x7DF);
    msg.Data[4] = 0x01;
    msg.Data[5] = 0x00;

    unsigned long msgId = 0;
    long rc = s.fn.StartPeriodic(s.chId, &msg, &msgId, 100);
    printf("PassThruStartPeriodicMsg(interval=100) => %ld (MsgID=%lu)\n", rc, msgId);
    if (rc != STATUS_NOERROR || msgId == 0) {
        printf("  WARNING: expected STATUS_NOERROR and a non-zero MsgID\n");
        printLastErr(s);
        ok = false;
    }

    rc = s.fn.StopPeriodic(s.chId, msgId);
    printf("PassThruStopPeriodicMsg(MsgID=%lu) => %ld\n", msgId, rc);
    if (rc != STATUS_NOERROR) {
        printf("  WARNING: expected STATUS_NOERROR\n");
        printLastErr(s);
        ok = false;
    }

    rc = s.fn.SetProgVoltage(s.devId, 0, 12000);
    printf("PassThruSetProgrammingVoltage(pin=0, 12000) => %ld\n", rc);
    if (rc != ERR_NOT_SUPPORTED) {
        printf("  WARNING: expected ERR_NOT_SUPPORTED (0x%02X)\n", ERR_NOT_SUPPORTED);
        ok = false;
    }

    printf("Test 4: %s\n", ok ? "PASS" : "FAIL (warn-only)");
    return ok;
}

int main(int argc, char **argv) {
    const char *dllPath = (argc > 1) ? argv[1] : "ElmJ2534.dll";
    unsigned long reqId = parseHexArg(argc > 2 ? argv[2] : NULL, 0x7DF, "request id");
    unsigned long respId = parseHexArg(argc > 3 ? argv[3] : NULL, 0x7E8, "response id");

    printf("ElmJ2534 E2E bench client — dll=%s requestId=%03lX responseId=%03lX\n\n",
           dllPath, reqId, respId);

    Session s;
    printf("LoadLibraryA(%s) ...\n", dllPath);
    s.dll = LoadLibraryA(dllPath);
    if (!s.dll) {
        printf("FAIL: LoadLibrary failed, GetLastError=%lu\n", GetLastError());
        return 1;
    }
    printf("OK: DLL loaded at %p\n", (void*)s.dll);

    if (!resolveExports(s)) {
        printf("FAIL: %d missing export(s)\n", g_missingExports);
        sessionClose(s);
        return 1;
    }
    printf("OK: all 14 exports resolved\n\n");

    // Open does the BT connect + full ELM init sequence on the bench adapter.
    printf("opening (BT connect + ELM init, may take 20s)...\n");
    long rc = s.fn.Open(NULL, &s.devId);
    printf("PassThruOpen(NULL) => %ld (devId=%lu)\n", rc, s.devId);
    if (rc != STATUS_NOERROR) {
        printLastErr(s);
        sessionClose(s);
        return 1;
    }
    s.devOpen = true;

    char fw[80] = {0}, dllv[80] = {0}, api[80] = {0};
    rc = s.fn.ReadVersion(s.devId, fw, dllv, api);
    printf("PassThruReadVersion => %ld\n  firmware: '%s'\n  dll:      '%s'\n  api:      '%s'\n",
           rc, fw, dllv, api);
    if (rc != STATUS_NOERROR)
        printLastErr(s);

    unsigned long mv = 0;
    rc = s.fn.Ioctl(s.devId, READ_VBATT, NULL, &mv);
    printf("PassThruIoctl(READ_VBATT) => %ld (%lu mV)\n", rc, mv);
    if (rc != STATUS_NOERROR)
        printf("  WARNING: VBATT read failed (bench supply should be ~12000 mV) — continuing\n");

    rc = s.fn.Connect(s.devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &s.chId);
    printf("PassThruConnect(ISO15765, CAN_ID_BOTH, 500000) => %ld (chId=%lu)\n", rc, s.chId);
    if (rc != STATUS_NOERROR) {
        printLastErr(s);
        sessionClose(s);
        return 1;
    }
    s.chOpen = true;

    startFlowFilter(s, reqId, respId);

    bool t1 = testMode0100(s, reqId, respId);
    bool t2 = testMode0902(s, reqId, respId);
    bool t3 = testVbattAfter(s);
    bool t4 = testNoOpExports(s);

    int passed = (t1 ? 1 : 0) + (t2 ? 1 : 0) + (t3 ? 1 : 0) + (t4 ? 1 : 0);
    printf("\n=== %d/4 PASS ===\n", passed);

    sessionClose(s);
    // Exit code gates the bench run: the two OBD tests must pass; VBATT and the
    // no-op exports are advisory.
    return (t1 && t2) ? 0 : 1;
}
