#pragma once
// Mock CANlib API for unit testing the ISO-TP engine.
// Captures canWrite calls and provides scripted canReadWait responses.

#include "../CanlibLoader.h"
#include <string.h>

#define MOCK_MAX_FRAMES 128

// A captured CAN frame (from canWrite)
struct MockTxFrame {
    long id;
    unsigned char data[8];
    unsigned int dlc;
    unsigned int flags;
};

// A scripted CAN frame (for canReadWait to return)
struct MockRxFrame {
    long id;
    unsigned char data[8];
    unsigned int dlc;
    unsigned int flags;
    unsigned long timestamp;
};

// Global mock state
struct MockState {
    // TX capture
    MockTxFrame txFrames[MOCK_MAX_FRAMES];
    int txCount;

    // RX script
    MockRxFrame rxFrames[MOCK_MAX_FRAMES];
    int rxCount;
    int rxIndex;    // Next frame to deliver

    // canWriteSync calls
    int writeSyncCount;

    void reset() {
        txCount = 0;
        rxCount = 0;
        rxIndex = 0;
        writeSyncCount = 0;
        memset(txFrames, 0, sizeof(txFrames));
        memset(rxFrames, 0, sizeof(rxFrames));
    }

    void pushRx(long id, const unsigned char *data, unsigned int dlc,
                unsigned int flags, unsigned long ts) {
        if (rxCount < MOCK_MAX_FRAMES) {
            rxFrames[rxCount].id = id;
            memcpy(rxFrames[rxCount].data, data, dlc > 8 ? 8 : dlc);
            rxFrames[rxCount].dlc = dlc;
            rxFrames[rxCount].flags = flags;
            rxFrames[rxCount].timestamp = ts;
            rxCount++;
        }
    }
};

extern MockState g_mock;

// Mock function implementations
static canStatus __stdcall mock_canWrite(CanHandle hnd, long id, void *msg,
                               unsigned int dlc, unsigned int flag) {
    (void)hnd;
    if (g_mock.txCount < MOCK_MAX_FRAMES) {
        g_mock.txFrames[g_mock.txCount].id = id;
        if (msg && dlc <= 8)
            memcpy(g_mock.txFrames[g_mock.txCount].data, msg, dlc);
        g_mock.txFrames[g_mock.txCount].dlc = dlc;
        g_mock.txFrames[g_mock.txCount].flags = flag;
        g_mock.txCount++;
    }
    return canOK;
}

static canStatus __stdcall mock_canRead(CanHandle hnd, long *id, void *msg,
                              unsigned int *dlc, unsigned int *flag,
                              unsigned long *time) {
    (void)hnd;
    if (g_mock.rxIndex < g_mock.rxCount) {
        MockRxFrame &f = g_mock.rxFrames[g_mock.rxIndex++];
        *id = f.id;
        if (msg) memcpy(msg, f.data, f.dlc > 8 ? 8 : f.dlc);
        *dlc = f.dlc;
        *flag = f.flags;
        *time = f.timestamp;
        return canOK;
    }
    return (canStatus)(-2); // canERR_NOMSG
}

static canStatus __stdcall mock_canReadWait(CanHandle hnd, long *id, void *msg,
                                  unsigned int *dlc, unsigned int *flag,
                                  unsigned long *time, unsigned long timeout) {
    (void)timeout;
    return mock_canRead(hnd, id, msg, dlc, flag, time);
}

static canStatus __stdcall mock_canWriteSync(CanHandle hnd, unsigned long timeout) {
    (void)hnd; (void)timeout;
    g_mock.writeSyncCount++;
    return canOK;
}

static canStatus __stdcall mock_canIoCtl(CanHandle hnd, unsigned int func,
                               void *buf, unsigned int buflen) {
    (void)hnd; (void)func; (void)buf; (void)buflen;
    return canOK;
}

// Initialize mock CanlibApi struct
static void mockCanlibInit(CanlibApi *api) {
    memset(api, 0, sizeof(CanlibApi));
    api->canWrite = mock_canWrite;
    api->canRead = mock_canRead;
    api->canReadWait = mock_canReadWait;
    api->canWriteSync = mock_canWriteSync;
    api->canIoCtl = mock_canIoCtl;
}
