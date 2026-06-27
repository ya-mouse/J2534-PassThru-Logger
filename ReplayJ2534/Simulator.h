#pragma once
// ReplayJ2534 — Simulator: state machine + API dispatch
// Owns device/channel state, validates transitions, routes calls,
// manages the Scheduler thread.

#include "J2534Defs.h"
#include "ConfigStore.h"
#include "Scheduler.h"
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

class Simulator {
public:
    Simulator();
    ~Simulator();

    bool init(const char *configPath, bool instantMode);
    void shutdown();
    bool isInitialized() const { return initialized_; }

    // ── J2534 API handlers (same signatures as old ReplayEngine) ──
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

    // ── Called by Scheduler to deliver a message to a channel's RX queue ──
    void deliverRx(unsigned long channelId, const PASSTHRU_MSG &msg);

    // ── Test accessors (take lock) ──
    bool hasChannel(unsigned long channelId);
    int channelCount();
    int rxQueueSize(unsigned long channelId);

private:
    struct Channel {
        unsigned long id;
        unsigned long protocolId;
        unsigned long flags;
        unsigned long baud;
        const Target *target;
        std::deque<PASSTHRU_MSG> rxQueue;
        std::vector<unsigned long> filters;
    };

    bool initialized_;
    ConfigStore config_;
    Scheduler scheduler_;

    // Device state
    unsigned long deviceId_;
    std::string deviceState_;
    std::unordered_map<unsigned long, Channel> channels_;
    std::unordered_map<unsigned long, unsigned long> filters_; // filterId -> channelId

    // ID pools
    unsigned long nextChannelId_;
    unsigned long nextFilterId_;

    CRITICAL_SECTION lock_;
    CONDITION_VARIABLE rxCond_;  // signaled when new RX msg arrives

    // State machine helpers
    bool checkTransition(const char *event, std::string &outErr);
    unsigned long allocateChannelId(unsigned long preferred);
    Channel* findChannel(unsigned long id);
    long applyIoctlOutput(const IoctlRule &rule, void *pOutput);
    void msgSpecToPassthru(const MsgSpec &spec, PASSTHRU_MSG &msg);
    bool matchReply(const ReplyRule &rule, const PASSTHRU_MSG &written);
};
