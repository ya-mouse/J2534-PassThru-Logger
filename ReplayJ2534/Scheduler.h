#pragma once
// ReplayJ2534 — Scheduler: background thread for periodic generators
// and delayed replies. Posts messages into Simulator RX queues.

#include "J2534Defs.h"
#include <vector>

class Simulator;
struct Target;

class Scheduler {
public:
    Scheduler();
    ~Scheduler();

    void init(Simulator *sim, bool instantMode);
    void start();
    void shutdown();

    // Schedule a delayed reply for a channel
    void scheduleReply(unsigned long channelId, const PASSTHRU_MSG &msg, unsigned long delayMs);

    // Start periodic generators for a channel (called on Connect)
    void startPeriodic(unsigned long channelId, const Target *target);

    // Stop all generators for a channel (called on Disconnect)
    void stopChannel(unsigned long channelId);

    // Cancel all pending replies for a channel
    void cancelReplies(unsigned long channelId);

private:
    struct PendingReply {
        unsigned long channelId;
        PASSTHRU_MSG msg;
        unsigned long fireAtMs;
    };

    struct PeriodicGen {
        unsigned long channelId;
        PASSTHRU_MSG msg;
        unsigned long intervalMs;
        unsigned long nextFireMs;
        bool active;
    };

    Simulator *sim_;
    bool instant_;
    bool running_;
    HANDLE thread_;
    HANDLE stopEvent_;
    CRITICAL_SECTION lock_;
    std::vector<PendingReply> pendingReplies_;
    std::vector<PeriodicGen> periodics_;

    static DWORD WINAPI threadFuncStub_(LPVOID param);
    void threadLoop();
    unsigned long nowMs();
};
