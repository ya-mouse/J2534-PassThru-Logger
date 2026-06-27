// j2534_test.cpp — Minimal J2534 test client
// Directly calls ReplayJ2534.dll via LoadLibrary
// Compile: i686-w64-mingw32-g++ -static -O2 -o j2534_test.exe j2534_test.cpp
#include <windows.h>
#include <stdio.h>
#include <string.h>

// J2534 v04.04 function pointer types
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

#define J2534_ISO15765 0x06
#define CAN_ID_BOTH 0x00000800
#define ISO15765_FRAME_PAD 0x0040
#define READ_VBATT 0x03
#define FLOW_CONTROL_FILTER 0x00000003

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

int main(int argc, char **argv) {
    const char *dllPath = "ReplayJ2534.dll";
    if (argc > 1) dllPath = argv[1];

    printf("Loading %s ...\n", dllPath);
    HMODULE h = LoadLibraryA(dllPath);
    if (!h) {
        printf("FAIL: LoadLibrary failed, error=%lu\n", GetLastError());
        return 1;
    }
    printf("OK: DLL loaded at %p\n\n", (void*)h);

    PFN_PassThruOpen pOpen = (PFN_PassThruOpen)GetProcAddress(h, "PassThruOpen");
    PFN_PassThruClose pClose = (PFN_PassThruClose)GetProcAddress(h, "PassThruClose");
    PFN_PassThruConnect pConnect = (PFN_PassThruConnect)GetProcAddress(h, "PassThruConnect");
    PFN_PassThruDisconnect pDisconnect = (PFN_PassThruDisconnect)GetProcAddress(h, "PassThruDisconnect");
    PFN_PassThruReadMsgs pRead = (PFN_PassThruReadMsgs)GetProcAddress(h, "PassThruReadMsgs");
    PFN_PassThruWriteMsgs pWrite = (PFN_PassThruWriteMsgs)GetProcAddress(h, "PassThruWriteMsgs");
    PFN_PassThruStartMsgFilter pFilter = (PFN_PassThruStartMsgFilter)GetProcAddress(h, "PassThruStartMsgFilter");
    PFN_PassThruStopMsgFilter pStopFilter = (PFN_PassThruStopMsgFilter)GetProcAddress(h, "PassThruStopMsgFilter");
    PFN_PassThruReadVersion pVersion = (PFN_PassThruReadVersion)GetProcAddress(h, "PassThruReadVersion");
    PFN_PassThruGetLastError pGetErr = (PFN_PassThruGetLastError)GetProcAddress(h, "PassThruGetLastError");
    PFN_PassThruIoctl pIoctl = (PFN_PassThruIoctl)GetProcAddress(h, "PassThruIoctl");

    if (!pOpen || !pClose || !pConnect || !pDisconnect || !pRead || !pWrite
        || !pFilter || !pStopFilter || !pVersion || !pGetErr || !pIoctl) {
        printf("FAIL: Missing exports\n");
        FreeLibrary(h);
        return 1;
    }
    printf("OK: All 11 exports resolved\n\n");

    // --- PassThruOpen ---
    unsigned long devId = 0;
    long ret = pOpen(NULL, &devId);
    printf("PassThruOpen(NULL) => %ld (devId=%lu)\n", ret, devId);
    if (ret != 0) {
        char err[256] = {0};
        pGetErr(err);
        printf("  Error: %s\n", err);
        FreeLibrary(h);
        return 1;
    }

    // --- PassThruReadVersion ---
    char fw[80] = {0}, dll[80] = {0}, api[80] = {0};
    ret = pVersion(devId, fw, dll, api);
    printf("PassThruReadVersion => %ld\n  firmware: '%s'\n  dll:      '%s'\n  api:      '%s'\n",
           ret, fw, dll, api);

    // --- PassThruIoctl READ_VBATT (before connect) ---
    unsigned long vbatt = 0;
    ret = pIoctl(devId, READ_VBATT, NULL, &vbatt); // READ_VBATT = 0x03
    printf("PassThruIoctl(READ_VBATT) => %ld (vbatt=%lu mV)\n", ret, vbatt);

    // --- PassThruConnect ---
    unsigned long chId = 0;
    ret = pConnect(devId, J2534_ISO15765, CAN_ID_BOTH, 500000, &chId);
    printf("PassThruConnect(ISO15765, 500000) => %ld (chId=%lu)\n", ret, chId);
    if (ret != 0) {
        char err[256] = {0};
        pGetErr(err);
        printf("  Error: %s\n", err);
        pClose(devId);
        FreeLibrary(h);
        return 1;
    }

    // --- StartMsgFilter (FLOW_CONTROL_FILTER) ---
    // Mask: FF-FF-FF-FF, Pattern: 00-00-04-80, Flow: 00-00-06-02
    PASSTHRU_MSG maskMsg, patMsg, flowMsg;
    memset(&maskMsg, 0, sizeof(maskMsg));
    memset(&patMsg, 0, sizeof(patMsg));
    memset(&flowMsg, 0, sizeof(flowMsg));
    maskMsg.ProtocolID = J2534_ISO15765;
    maskMsg.TxFlags = ISO15765_FRAME_PAD;
    maskMsg.DataSize = 4;
    unsigned char mask[] = {0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(maskMsg.Data, mask, 4);

    patMsg.ProtocolID = J2534_ISO15765;
    patMsg.TxFlags = ISO15765_FRAME_PAD;
    patMsg.DataSize = 4;
    unsigned char pat[] = {0x00, 0x00, 0x04, 0x80};
    memcpy(patMsg.Data, pat, 4);

    flowMsg.ProtocolID = J2534_ISO15765;
    flowMsg.TxFlags = ISO15765_FRAME_PAD;
    flowMsg.DataSize = 4;
    unsigned char flow[] = {0x00, 0x00, 0x06, 0x02};
    memcpy(flowMsg.Data, flow, 4);

    unsigned long filterId = 0;
    ret = pFilter(chId, FLOW_CONTROL_FILTER, &maskMsg, &patMsg, &flowMsg, &filterId);
    printf("PassThruStartMsgFilter(FLOW_CONTROL) => %ld (filterId=%lu)\n", ret, filterId);

    // --- WriteMsgs: UDS Diagnostic Session Control (10 03) ---
    // Full ISO15765 frame: 00-00-06-02-10-03 (functional addressing, 6 bytes)
    PASSTHRU_MSG wmsg;
    memset(&wmsg, 0, sizeof(wmsg));
    wmsg.ProtocolID = J2534_ISO15765;
    wmsg.TxFlags = ISO15765_FRAME_PAD;
    wmsg.DataSize = 6;
    wmsg.ExtraDataIndex = 6;
    unsigned char req[] = {0x00, 0x00, 0x06, 0x02, 0x10, 0x03};
    memcpy(wmsg.Data, req, 6);

    unsigned long numWrite = 1;
    ret = pWrite(chId, &wmsg, &numWrite, 1000);
    printf("PassThruWriteMsgs(10 03) => %ld (numWritten=%lu)\n", ret, numWrite);

    // --- ReadMsgs: expect ECU reply ---
    PASSTHRU_MSG rmsg;
    memset(&rmsg, 0, sizeof(rmsg));
    unsigned long numRead = 1;
    ret = pRead(chId, &rmsg, &numRead, 500);
    printf("PassThruReadMsgs(500ms) => %ld (numRead=%lu)\n", ret, numRead);
    if (ret == 0 && numRead > 0) {
        printf("  Reply: proto=%lu datasize=%lu data=", rmsg.ProtocolID, rmsg.DataSize);
        for (unsigned long i = 0; i < rmsg.DataSize && i < 16; i++)
            printf("%02X ", rmsg.Data[i]);
        printf("\n");
    }

    // --- WriteMsgs: Tester Present (3E 00) ---
    memset(&wmsg, 0, sizeof(wmsg));
    wmsg.ProtocolID = J2534_ISO15765;
    wmsg.TxFlags = ISO15765_FRAME_PAD;
    wmsg.DataSize = 6;
    wmsg.ExtraDataIndex = 6;
    unsigned char req2[] = {0x00, 0x00, 0x06, 0x02, 0x3E, 0x00};
    memcpy(wmsg.Data, req2, 6);
    numWrite = 1;
    ret = pWrite(chId, &wmsg, &numWrite, 1000);
    printf("PassThruWriteMsgs(3E 00) => %ld (numWritten=%lu)\n", ret, numWrite);

    // --- ReadMsgs: expect reply ---
    memset(&rmsg, 0, sizeof(rmsg));
    numRead = 1;
    ret = pRead(chId, &rmsg, &numRead, 500);
    printf("PassThruReadMsgs(500ms) => %ld (numRead=%lu)\n", ret, numRead);
    if (ret == 0 && numRead > 0) {
        printf("  Reply: proto=%lu datasize=%lu data=", rmsg.ProtocolID, rmsg.DataSize);
        for (unsigned long i = 0; i < rmsg.DataSize && i < 16; i++)
            printf("%02X ", rmsg.Data[i]);
        printf("\n");
    }

    // --- ReadMsgs: wait for periodic (tester present from ECU) ---
    printf("\nWaiting 200ms for periodic message...\n");
    memset(&rmsg, 0, sizeof(rmsg));
    numRead = 1;
    ret = pRead(chId, &rmsg, &numRead, 200);
    printf("PassThruReadMsgs(200ms) => %ld (numRead=%lu)\n", ret, numRead);
    if (ret == 0 && numRead > 0) {
        printf("  Periodic: proto=%lu datasize=%lu data=", rmsg.ProtocolID, rmsg.DataSize);
        for (unsigned long i = 0; i < rmsg.DataSize && i < 16; i++)
            printf("%02X ", rmsg.Data[i]);
        printf("\n");
    }

    // --- Disconnect ---
    ret = pDisconnect(chId);
    printf("\nPassThruDisconnect(%lu) => %ld\n", chId, ret);

    // --- Close ---
    ret = pClose(devId);
    printf("PassThruClose(%lu) => %ld\n", devId, ret);

    FreeLibrary(h);
    printf("\n=== Test complete ===\n");
    return 0;
}
