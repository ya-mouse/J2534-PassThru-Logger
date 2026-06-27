#include "Simulator.h"
#include "Logger.h"
#include <string.h>
#include <stdio.h>

Simulator::Simulator()
    : initialized_(false), deviceId_(0), nextChannelId_(1), nextFilterId_(1) {
    InitializeCriticalSection(&lock_);
    InitializeConditionVariable(&rxCond_);
}

Simulator::~Simulator() {
    shutdown();
    DeleteCriticalSection(&lock_);
}

bool Simulator::init(const char *configPath, bool instantMode) {
    EnterCriticalSection(&lock_);
    shutdown();

    if (!config_.load(configPath)) {
        g_logger.verbose("Simulator: config load failed: %s", config_.lastError());
        LeaveCriticalSection(&lock_);
        return false;
    }

    deviceState_ = config_.stateMachine().initial;
    if (deviceState_.empty()) deviceState_ = "CLOSED";
    deviceId_ = 0;
    channels_.clear();
    filters_.clear();
    nextChannelId_ = 1;
    nextFilterId_ = 1;

    scheduler_.init(this, instantMode);
    scheduler_.start();
    initialized_ = true;

    g_logger.verbose("Simulator: initialized (state=%s, %d targets)",
                     deviceState_.c_str(), (int)config_.targets().size());
    LeaveCriticalSection(&lock_);
    return true;
}

void Simulator::shutdown() {
    EnterCriticalSection(&lock_);
    if (initialized_) {
        scheduler_.shutdown();
        channels_.clear();
        filters_.clear();
        initialized_ = false;
    }
    LeaveCriticalSection(&lock_);
}

// ═══════════════════════════════════════════════════════════════════════════
// State machine
// ═══════════════════════════════════════════════════════════════════════════

bool Simulator::checkTransition(const char *event, std::string &outErr) {
    const auto &transitions = config_.stateMachine().transitions;
    // Default transitions if none configured
    if (transitions.empty()) {
        outErr = "";
        return true; // accept all if no state machine defined
    }
    for (const auto &t : transitions) {
        if (t.event == event && t.from == deviceState_) {
            deviceState_ = t.to;
            g_logger.verbose("  State: %s -> %s (event=%s)", t.from.c_str(), t.to.c_str(), event);
            return true;
        }
    }
    outErr = "invalid transition for event ";
    outErr += event;
    outErr += " from state ";
    outErr += deviceState_;
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// Channel management
// ═══════════════════════════════════════════════════════════════════════════

unsigned long Simulator::allocateChannelId(unsigned long preferred) {
    if (preferred > 0 && channels_.find(preferred) == channels_.end())
        return preferred;
    // Start searching from preferred+1 (or nextChannelId_ if higher)
    unsigned long start = (preferred > 0) ? preferred + 1 : nextChannelId_;
    if (start < nextChannelId_) start = nextChannelId_;
    for (unsigned long id = start; ; id++) {
        if (channels_.find(id) == channels_.end()) {
            if (id >= nextChannelId_) nextChannelId_ = id + 1;
            return id;
        }
    }
}

Simulator::Channel* Simulator::findChannel(unsigned long id) {
    auto it = channels_.find(id);
    return it != channels_.end() ? &it->second : NULL;
}

// ═══════════════════════════════════════════════════════════════════════════
// API: Open / Close
// ═══════════════════════════════════════════════════════════════════════════

long Simulator::openDevice(void *pName, unsigned long *pDeviceId) {
    EnterCriticalSection(&lock_);
    std::string err;
    if (!checkTransition("PassThruOpen", err)) {
        LeaveCriticalSection(&lock_);
        return ERR_DEVICE_IN_USE;
    }
    // Assign device ID — use a fixed value (1) or from config
    deviceId_ = 1;
    if (pDeviceId) *pDeviceId = deviceId_;
    g_logger.verbose("  Simulator: Open -> DeviceId=%lu", deviceId_);
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

long Simulator::closeDevice(unsigned long deviceId) {
    EnterCriticalSection(&lock_);
    if (deviceId != deviceId_) {
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_DEVICE_ID;
    }
    if (!channels_.empty()) {
        g_logger.verbose("  Simulator: Close rejected — %d channels open",
                         (int)channels_.size());
        LeaveCriticalSection(&lock_);
        return ERR_DEVICE_IN_USE;
    }
    std::string err;
    if (!checkTransition("PassThruClose", err)) {
        LeaveCriticalSection(&lock_);
        return ERR_FAILED;
    }
    deviceId_ = 0;
    g_logger.verbose("  Simulator: Close(%lu)", deviceId);
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

// ═══════════════════════════════════════════════════════════════════════════
// API: Connect / Disconnect
// ═══════════════════════════════════════════════════════════════════════════

long Simulator::connect(unsigned long deviceId, unsigned long protocolId,
                        unsigned long flags, unsigned long baudRate,
                        unsigned long *pChannelId) {
    EnterCriticalSection(&lock_);
    if (deviceId != deviceId_) {
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_DEVICE_ID;
    }
    // Connect doesn't change device state (OPENED stays OPENED)
    // but we validate the transition exists
    std::string err;
    checkTransition("PassThruConnect", err);

    const Target *target = config_.findTarget(protocolId, flags, baudRate);
    unsigned long chId = allocateChannelId(target ? target->preferredChannelId : 0);

    Channel ch;
    ch.id = chId;
    ch.protocolId = protocolId;
    ch.flags = flags;
    ch.baud = baudRate;
    ch.target = target;
    channels_[chId] = ch;

    if (target) {
        scheduler_.startPeriodic(chId, target);
    }

    if (pChannelId) *pChannelId = chId;
    g_logger.verbose("  Simulator: Connect -> ChannelId=%lu (target=%s)",
                     chId, target ? target->name.c_str() : "(none)");
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

long Simulator::disconnect(unsigned long channelId) {
    EnterCriticalSection(&lock_);
    Channel *ch = findChannel(channelId);
    if (!ch) {
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_CHANNEL_ID;
    }

    scheduler_.stopChannel(channelId);
    scheduler_.cancelReplies(channelId);

    // Remove filters for this channel
    for (auto it = filters_.begin(); it != filters_.end(); ) {
        if (it->second == channelId) it = filters_.erase(it);
        else ++it;
    }

    std::string err;
    checkTransition("PassThruDisconnect", err);

    channels_.erase(channelId);
    WakeAllConditionVariable(&rxCond_);
    g_logger.verbose("  Simulator: Disconnect(%lu)", channelId);
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

// ═══════════════════════════════════════════════════════════════════════════
// API: ReadMsgs / WriteMsgs
// ═══════════════════════════════════════════════════════════════════════════

long Simulator::readMsgs(unsigned long channelId, PASSTHRU_MSG *pMsg,
                         unsigned long *pNumMsgs, unsigned long timeout) {
    EnterCriticalSection(&lock_);
    Channel *ch = findChannel(channelId);
    if (!ch) {
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_CHANNEL_ID;
    }

    unsigned long requested = pNumMsgs ? *pNumMsgs : 0;

    // Wait for messages if queue is empty and timeout > 0
    if (ch->rxQueue.empty() && timeout > 0) {
        DWORD deadline = GetTickCount() + timeout;
        while (ch->rxQueue.empty() && initialized_) {
            DWORD remaining = (deadline > GetTickCount()) ? (deadline - GetTickCount()) : 0;
            if (remaining == 0) break;
            if (!SleepConditionVariableCS(&rxCond_, &lock_, remaining)) break;
        }
    }

    unsigned long delivered = 0;
    while (!ch->rxQueue.empty() && delivered < requested && pMsg) {
        pMsg[delivered] = ch->rxQueue.front();
        ch->rxQueue.pop_front();
        delivered++;
    }

    if (pNumMsgs) *pNumMsgs = delivered;
    long ret = (delivered > 0) ? STATUS_NOERROR : ERR_BUFFER_EMPTY;

    g_logger.verbose("  Simulator: ReadMsgs ch=%lu delivered=%lu ret=%s",
                     channelId, delivered, rjRetCodeName(ret));
    LeaveCriticalSection(&lock_);
    return ret;
}

long Simulator::writeMsgs(unsigned long channelId, PASSTHRU_MSG *pMsg,
                          unsigned long *pNumMsgs, unsigned long timeout) {
    EnterCriticalSection(&lock_);
    Channel *ch = findChannel(channelId);
    if (!ch) {
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_CHANNEL_ID;
    }

    unsigned long count = pNumMsgs ? *pNumMsgs : 0;
    unsigned long accepted = 0;

    for (unsigned long i = 0; i < count; i++) {
        accepted++;
        if (!ch->target) continue;
        // Match reply rules
        for (const auto &rule : ch->target->replies) {
            if (matchReply(rule, pMsg[i])) {
                PASSTHRU_MSG reply;
                memset(&reply, 0, sizeof(reply));
                msgSpecToPassthru(rule.response, reply);
                scheduler_.scheduleReply(channelId, reply, rule.delayMs);
                g_logger.verbose("  Simulator: reply scheduled ch=%lu delay=%lums",
                                 channelId, rule.delayMs);
            }
        }
    }

    if (pNumMsgs) *pNumMsgs = accepted;
    g_logger.verbose("  Simulator: WriteMsgs ch=%lu accepted=%lu", channelId, accepted);
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

// ═══════════════════════════════════════════════════════════════════════════
// API: Ioctl
// ═══════════════════════════════════════════════════════════════════════════

long Simulator::ioctl(unsigned long handle, unsigned long ioctlId,
                      void *pInput, void *pOutput) {
    EnterCriticalSection(&lock_);
    const IoctlRule *rule = config_.findIoctl(ioctlId);

    if (!rule) {
        g_logger.verbose("  Simulator: Ioctl(0x%lX) not in config -> NOT_SUPPORTED", ioctlId);
        LeaveCriticalSection(&lock_);
        return ERR_NOT_SUPPORTED;
    }

    // Enforce scope
    bool isChannel = (channels_.find(handle) != channels_.end());
    bool isDevice = (handle == deviceId_);

    if (rule->scope == SCOPE_DEVICE && !isDevice) {
        g_logger.verbose("  Simulator: Ioctl(0x%lX) requires device scope", ioctlId);
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_CHANNEL_ID;
    }
    if (rule->scope == SCOPE_CHANNEL && !isChannel) {
        g_logger.verbose("  Simulator: Ioctl(0x%lX) requires channel scope", ioctlId);
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_CHANNEL_ID;
    }
    if (rule->scope == SCOPE_ANY && !isDevice && !isChannel) {
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_CHANNEL_ID;
    }

    // Consume input (e.g. SET_CONFIG SCONFIG_LIST) — just log it
    if (rule->consumeInput && pInput && ioctlId == SET_CONFIG) {
        SCONFIG_LIST *list = (SCONFIG_LIST *)pInput;
        for (unsigned long i = 0; i < list->NumOfParams; i++)
            g_logger.verbose("  SET_CONFIG param=0x%lX val=%lu",
                             list->ConfigPtr[i].Parameter, list->ConfigPtr[i].Value);
    }

    long ret = rule->returnCode;
    if (rule->hasOutput && pOutput)
        applyIoctlOutput(*rule, pOutput);

    // Log special cases
    if (ioctlId == READ_VBATT && ret == STATUS_NOERROR && pOutput)
        g_logger.verbose("  VBATT=%lu mV", *(unsigned long*)pOutput);

    g_logger.verbose("  Simulator: Ioctl(%lu, 0x%lX) -> %s",
                     handle, ioctlId, rjRetCodeName(ret));
    LeaveCriticalSection(&lock_);
    return ret;
}

long Simulator::applyIoctlOutput(const IoctlRule &rule, void *pOutput) {
    if (rule.outputAuto) {
        // Auto-generate output based on IOCTL type
        if (rule.ioctlId == READ_VBATT) {
            unsigned long vbatt = config_.device().vbatt_mV;
            memcpy(pOutput, &vbatt, sizeof(vbatt));
            return STATUS_NOERROR;
        }
        return STATUS_NOERROR; // no auto for this IOCTL
    }
    if (rule.outputBytes.len > 0) {
        memcpy(pOutput, rule.outputBytes.data, rule.outputBytes.len);
    }
    return STATUS_NOERROR;
}

// ═══════════════════════════════════════════════════════════════════════════
// API: Filters
// ═══════════════════════════════════════════════════════════════════════════

long Simulator::startMsgFilter(unsigned long channelId, unsigned long filterType,
                               PASSTHRU_MSG *pMask, PASSTHRU_MSG *pPattern,
                               PASSTHRU_MSG *pFlowControl, unsigned long *pFilterId) {
    EnterCriticalSection(&lock_);
    Channel *ch = findChannel(channelId);
    if (!ch) {
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_CHANNEL_ID;
    }
    unsigned long fid = nextFilterId_++;
    filters_[fid] = channelId;
    ch->filters.push_back(fid);
    if (pFilterId) *pFilterId = fid;
    g_logger.verbose("  Simulator: StartFilter ch=%lu -> FilterId=%lu", channelId, fid);
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

long Simulator::stopMsgFilter(unsigned long channelId, unsigned long filterId) {
    EnterCriticalSection(&lock_);
    Channel *ch = findChannel(channelId);
    if (!ch) {
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_CHANNEL_ID;
    }
    auto it = filters_.find(filterId);
    if (it == filters_.end() || it->second != channelId) {
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_FILTER_ID;
    }
    filters_.erase(it);
    for (auto f = ch->filters.begin(); f != ch->filters.end(); ++f) {
        if (*f == filterId) { ch->filters.erase(f); break; }
    }
    g_logger.verbose("  Simulator: StopFilter ch=%lu FilterId=%lu", channelId, filterId);
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

// ═══════════════════════════════════════════════════════════════════════════
// API: ReadVersion / GetLastError
// ═══════════════════════════════════════════════════════════════════════════

long Simulator::readVersion(unsigned long deviceId, char *pFw, char *pDll, char *pApi) {
    EnterCriticalSection(&lock_);
    if (deviceId != deviceId_) {
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_DEVICE_ID;
    }
    const DeviceConfig &dev = config_.device();
    if (pFw) { strncpy(pFw, dev.firmwareVersion, 79); pFw[79] = '\0'; }
    if (pDll) { strncpy(pDll, dev.dllVersion, 79); pDll[79] = '\0'; }
    if (pApi) { strncpy(pApi, dev.apiVersion, 79); pApi[79] = '\0'; }
    g_logger.verbose("  Simulator: ReadVersion fw=%s dll=%s api=%s",
                     dev.firmwareVersion, dev.dllVersion, dev.apiVersion);
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

long Simulator::getLastError(char *pBuf) {
    if (!pBuf) return ERR_NULL_PARAMETER;
    pBuf[0] = '\0';
    return STATUS_NOERROR;
}

// ═══════════════════════════════════════════════════════════════════════════
// Scheduler callback
// ═══════════════════════════════════════════════════════════════════════════

void Simulator::deliverRx(unsigned long channelId, const PASSTHRU_MSG &msg) {
    EnterCriticalSection(&lock_);
    Channel *ch = findChannel(channelId);
    if (ch) {
        ch->rxQueue.push_back(msg);
        WakeConditionVariable(&rxCond_);
    }
    LeaveCriticalSection(&lock_);
}

// ═══════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════

void Simulator::msgSpecToPassthru(const MsgSpec &spec, PASSTHRU_MSG &msg) {
    msg.ProtocolID = spec.protocolId;
    msg.RxStatus = spec.rxStatus;
    msg.TxFlags = spec.txFlags;
    msg.Timestamp = 0;
    msg.DataSize = spec.data.len;
    msg.ExtraDataIndex = spec.data.len; // typical for ISO15765
    if (spec.data.len > 0 && spec.data.len <= (int)sizeof(msg.Data))
        memcpy(msg.Data, spec.data.data, spec.data.len);
}

bool Simulator::matchReply(const ReplyRule &rule, const PASSTHRU_MSG &written) {
    int matchLen = rule.matchData.len;
    if (matchLen == 0) return true; // empty match = match all
    if (rule.mode == MATCH_PREFIX) {
        if ((int)written.DataSize < matchLen) return false;
        return memcmp(written.Data, rule.matchData.data, matchLen) == 0;
    }
    // MATCH_EXACT
    if ((int)written.DataSize != matchLen) return false;
    return memcmp(written.Data, rule.matchData.data, matchLen) == 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Test accessors
// ═══════════════════════════════════════════════════════════════════════════

bool Simulator::hasChannel(unsigned long channelId) {
    EnterCriticalSection(&lock_);
    bool r = channels_.find(channelId) != channels_.end();
    LeaveCriticalSection(&lock_);
    return r;
}

int Simulator::channelCount() {
    EnterCriticalSection(&lock_);
    int n = (int)channels_.size();
    LeaveCriticalSection(&lock_);
    return n;
}

int Simulator::rxQueueSize(unsigned long channelId) {
    EnterCriticalSection(&lock_);
    auto it = channels_.find(channelId);
    int n = it != channels_.end() ? (int)it->second.rxQueue.size() : -1;
    LeaveCriticalSection(&lock_);
    return n;
}
