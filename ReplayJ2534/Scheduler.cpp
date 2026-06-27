#include "Scheduler.h"
#include "Simulator.h"
#include "ConfigStore.h"
#include "Logger.h"

Scheduler::Scheduler()
    : sim_(NULL), instant_(false), running_(false), thread_(NULL), stopEvent_(NULL) {
    InitializeCriticalSection(&lock_);
}

Scheduler::~Scheduler() {
    shutdown();
    DeleteCriticalSection(&lock_);
}

void Scheduler::init(Simulator *sim, bool instantMode) {
    sim_ = sim;
    instant_ = instantMode;
}

unsigned long Scheduler::nowMs() {
    return GetTickCount();
}

void Scheduler::start() {
    if (running_) return;
    stopEvent_ = CreateEventA(NULL, TRUE, FALSE, NULL);
    running_ = true;
    thread_ = CreateThread(NULL, 0, threadFuncStub_, this, 0, NULL);
    g_logger.verbose("Scheduler: started (instant=%d)", instant_);
}

void Scheduler::shutdown() {
    if (!running_) return;
    running_ = false;
    if (stopEvent_) SetEvent(stopEvent_);
    if (thread_) {
        WaitForSingleObject(thread_, 2000);
        CloseHandle(thread_);
        thread_ = NULL;
    }
    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = NULL;
    }
    EnterCriticalSection(&lock_);
    pendingReplies_.clear();
    periodics_.clear();
    LeaveCriticalSection(&lock_);
}

void Scheduler::scheduleReply(unsigned long channelId, const PASSTHRU_MSG &msg, unsigned long delayMs) {
    PendingReply pr;
    pr.channelId = channelId;
    pr.msg = msg;
    pr.fireAtMs = nowMs() + (instant_ ? 0 : delayMs);
    EnterCriticalSection(&lock_);
    pendingReplies_.push_back(pr);
    LeaveCriticalSection(&lock_);
}

void Scheduler::startPeriodic(unsigned long channelId, const Target *target) {
    if (!target) return;
    EnterCriticalSection(&lock_);
    unsigned long t = nowMs();
    for (size_t i = 0; i < target->periodic.size(); i++) {
        const PeriodicRule &rule = target->periodic[i];
        if (!rule.startOnConnect) continue;
        PeriodicGen gen;
        memset(&gen, 0, sizeof(gen));
        gen.channelId = channelId;
        gen.msg.ProtocolID = rule.msg.protocolId;
        gen.msg.RxStatus = rule.msg.rxStatus;
        gen.msg.TxFlags = rule.msg.txFlags;
        gen.msg.DataSize = rule.msg.data.len;
        gen.msg.ExtraDataIndex = 0;
        if (rule.msg.data.len > 0 && rule.msg.data.len <= (int)sizeof(gen.msg.Data))
            memcpy(gen.msg.Data, rule.msg.data.data, rule.msg.data.len);
        gen.intervalMs = instant_ ? rule.intervalMs : rule.intervalMs;
        // First fire: immediate in instant mode, else after one interval
        gen.nextFireMs = t + (instant_ ? 0 : rule.intervalMs);
        gen.active = true;
        periodics_.push_back(gen);
        g_logger.verbose("Scheduler: periodic started ch=%lu interval=%lums",
                         channelId, gen.intervalMs);
    }
    LeaveCriticalSection(&lock_);
}

void Scheduler::stopChannel(unsigned long channelId) {
    EnterCriticalSection(&lock_);
    for (size_t i = 0; i < periodics_.size(); i++) {
        if (periodics_[i].channelId == channelId)
            periodics_[i].active = false;
    }
    // Remove inactive generators
    size_t w = 0;
    for (size_t r = 0; r < periodics_.size(); r++) {
        if (periodics_[r].active)
            periodics_[w++] = periodics_[r];
    }
    periodics_.resize(w);
    LeaveCriticalSection(&lock_);
}

void Scheduler::cancelReplies(unsigned long channelId) {
    EnterCriticalSection(&lock_);
    size_t w = 0;
    for (size_t r = 0; r < pendingReplies_.size(); r++) {
        if (pendingReplies_[r].channelId != channelId)
            pendingReplies_[w++] = pendingReplies_[r];
    }
    pendingReplies_.resize(w);
    LeaveCriticalSection(&lock_);
}

DWORD WINAPI Scheduler::threadFuncStub_(LPVOID param) {
    ((Scheduler *)param)->threadLoop();
    return 0;
}

void Scheduler::threadLoop() {
    while (running_) {
        unsigned long t = nowMs();

        EnterCriticalSection(&lock_);

        // Process pending replies
        std::vector<PendingReply> due;
        size_t w = 0;
        for (size_t r = 0; r < pendingReplies_.size(); r++) {
            if (pendingReplies_[r].fireAtMs <= t) {
                due.push_back(pendingReplies_[r]);
            } else {
                pendingReplies_[w++] = pendingReplies_[r];
            }
        }
        pendingReplies_.resize(w);

        // Process periodic generators
        for (size_t i = 0; i < periodics_.size(); i++) {
            if (!periodics_[i].active) continue;
            if (periodics_[i].nextFireMs <= t) {
                due.push_back(PendingReply());
                due.back().channelId = periodics_[i].channelId;
                due.back().msg = periodics_[i].msg;
                due.back().fireAtMs = t;
                periodics_[i].nextFireMs = t + periodics_[i].intervalMs;
            }
        }

        LeaveCriticalSection(&lock_);

        // Deliver due messages to Simulator (outside our lock)
        for (size_t i = 0; i < due.size(); i++) {
            if (sim_) sim_->deliverRx(due[i].channelId, due[i].msg);
        }

        // Sleep 1ms or until stop event
        if (running_ && stopEvent_)
            WaitForSingleObject(stopEvent_, 1);
    }
}
