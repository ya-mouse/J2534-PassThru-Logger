#pragma once
// ReplayJ2534 — Replay engine that matches API calls to logged events

#include "LogParser.h"
#include "J2534Defs.h"
#include <windows.h>

class ReplayEngine {
public:
    ReplayEngine();
    ~ReplayEngine();

    bool init(const char *logFilePath);
    void shutdown();
    bool isInitialized() const { return initialized_; }

    // API implementations
    long openDevice(void *pName, unsigned long *pDeviceId);
    long closeDevice(unsigned long deviceId);
    long connect(unsigned long deviceId, unsigned long protocolId,
                 unsigned long flags, unsigned long baudRate,
                 unsigned long *pChannelId);
    long disconnect(unsigned long channelId);
    long readMsgs(unsigned long channelId, PASSTHRU_MSG *pMsg,
                  unsigned long *pNumMsgs, unsigned long timeout);
    long writeMsgs(unsigned long channelId, PASSTHRU_MSG *pMsg,
                   unsigned long *pNumMsgs, unsigned long timeout);
    long ioctl(unsigned long handle, unsigned long ioctlId,
               void *pInput, void *pOutput);
    long startMsgFilter(unsigned long channelId, unsigned long filterType,
                        PASSTHRU_MSG *pMask, PASSTHRU_MSG *pPattern,
                        PASSTHRU_MSG *pFlowControl, unsigned long *pFilterId);
    long stopMsgFilter(unsigned long channelId, unsigned long filterId);
    long readVersion(unsigned long deviceId, char *pFw, char *pDll, char *pApi);
    long getLastError(char *pBuf);

private:
    // Find next event matching type and handle, advancing cursor
    const ReplayEvent* findNextMatch(ReplayCallType type, unsigned long handle);

    // Find next READ event for channel
    const ReplayEvent* findNextRead(unsigned long channelId);

    // Find next WRITE event for channel
    const ReplayEvent* findNextWrite(unsigned long channelId);

    // Copy ReplayMsg to PASSTHRU_MSG
    void copyMessage(const ReplayMsg &src, PASSTHRU_MSG &dst);

    // Track dedup counters per event index
    struct DedupCounter {
        int eventIndex;
        unsigned long remaining;
    };

    LogParser parser_;
    int cursor_;              // next event index to search from
    bool initialized_;
    char lastError_[256];
    CRITICAL_SECTION lock_;

    // Dedup state for repeated events
    int dedupIndex_;          // current dedup event index
    unsigned long dedupRemaining_;  // remaining repeats
};
