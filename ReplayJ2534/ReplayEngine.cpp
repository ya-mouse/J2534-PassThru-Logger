#include "ReplayEngine.h"
#include "Logger.h"
#include "Config.h"
#include <string.h>
#include <stdio.h>

ReplayEngine::ReplayEngine()
    : cursor_(0), initialized_(false), dedupIndex_(-1), dedupRemaining_(0)
{
    InitializeCriticalSection(&lock_);
    lastError_[0] = '\0';
}

ReplayEngine::~ReplayEngine() {
    shutdown();
    DeleteCriticalSection(&lock_);
}

bool ReplayEngine::init(const char *logFilePath) {
    EnterCriticalSection(&lock_);
    cursor_ = 0;
    dedupIndex_ = -1;
    dedupRemaining_ = 0;

    bool ok = parser_.load(logFilePath);
    initialized_ = ok;

    if (ok) {
        g_logger.verbose("ReplayEngine: initialized with %d events", parser_.eventCount());
    } else {
        snprintf(lastError_, sizeof(lastError_), "Failed to load log file: %s",
                 logFilePath ? logFilePath : "(null)");
        g_logger.verbose("ReplayEngine: %s", lastError_);
    }

    LeaveCriticalSection(&lock_);
    return ok;
}

void ReplayEngine::shutdown() {
    EnterCriticalSection(&lock_);
    parser_.clear();
    cursor_ = 0;
    initialized_ = false;
    dedupIndex_ = -1;
    dedupRemaining_ = 0;
    LeaveCriticalSection(&lock_);
}

void ReplayEngine::copyMessage(const ReplayMsg &src, PASSTHRU_MSG &dst) {
    memset(&dst, 0, sizeof(dst));
    dst.ProtocolID = src.protocolId;
    dst.RxStatus = src.rxStatus;
    dst.TxFlags = src.txFlags;
    dst.Timestamp = src.timestamp;
    dst.ExtraDataIndex = src.extraDataIndex;
    dst.DataSize = src.dataSize;
    if (src.dataSize > 0 && src.dataSize <= sizeof(dst.Data)) {
        memcpy(dst.Data, src.data, src.dataSize);
    }
}

//
// === Event matching ===
//

const ReplayEvent* ReplayEngine::findNextMatch(ReplayCallType type, unsigned long handle) {
    // If we're in a dedup run, check if current dedup event matches
    if (dedupIndex_ >= 0 && dedupRemaining_ > 0) {
        const ReplayEvent *ev = parser_.getEvent(dedupIndex_);
        if (ev && ev->type == type && ev->handle == handle) {
            dedupRemaining_--;
            g_logger.debug("  Matched dedup event %d (remaining=%lu)", dedupIndex_, dedupRemaining_);
            return ev;
        }
        // Dedup exhausted or mismatch — clear dedup
        dedupRemaining_ = 0;
        dedupIndex_ = -1;
    }

    // Search forward from cursor
    for (int i = cursor_; i < parser_.eventCount(); i++) {
        const ReplayEvent *ev = parser_.getEvent(i);
        if (!ev) continue;
        if (ev->type != type) continue;
        if (ev->handle != handle) continue;

        // Found match
        cursor_ = i + 1;
        if (ev->count > 1) {
            dedupIndex_ = i;
            dedupRemaining_ = ev->count - 1;
        }
        g_logger.debug("  Matched event %d (type=%d handle=%lu count=%lu)",
                       i, (int)ev->type, ev->handle, ev->count);
        return ev;
    }

    g_logger.debug("  No match found for type=%d handle=%lu (cursor=%d)",
                   (int)type, handle, cursor_);
    return NULL;
}

const ReplayEvent* ReplayEngine::findNextRead(unsigned long channelId) {
    // Same as findNextMatch but specifically for READ
    return findNextMatch(CALL_READ, channelId);
}

const ReplayEvent* ReplayEngine::findNextWrite(unsigned long channelId) {
    return findNextMatch(CALL_WRITE, channelId);
}

//
// === API implementations ===
//

long ReplayEngine::openDevice(void *pName, unsigned long *pDeviceId) {
    EnterCriticalSection(&lock_);

    const ReplayEvent *ev = findNextMatch(CALL_OPEN, 0);
    long ret;

    if (ev) {
        ret = (long)ev->returnCode;
        if (pDeviceId) *pDeviceId = ev->handle;
        g_logger.verbose("  Replay: Open -> DeviceId=%lu, ret=%s",
                         ev->handle, rjRetCodeName(ret));
    } else {
        // No matching open event — check if we're past end of log
        // For robustness, allow it but return a warning
        g_logger.verbose("  Replay: Open not found in log, auto-assigning DeviceId=1");
        if (pDeviceId) *pDeviceId = 1;
        ret = STATUS_NOERROR;
    }

    LeaveCriticalSection(&lock_);
    return ret;
}

long ReplayEngine::closeDevice(unsigned long deviceId) {
    EnterCriticalSection(&lock_);
    const ReplayEvent *ev = findNextMatch(CALL_CLOSE, deviceId);
    long ret = ev ? (long)ev->returnCode : STATUS_NOERROR;
    g_logger.verbose("  Replay: Close(%lu) -> %s", deviceId, rjRetCodeName(ret));
    LeaveCriticalSection(&lock_);
    return ret;
}

long ReplayEngine::connect(unsigned long deviceId, unsigned long protocolId,
                           unsigned long flags, unsigned long baudRate,
                           unsigned long *pChannelId) {
    EnterCriticalSection(&lock_);
    const ReplayEvent *ev = findNextMatch(CALL_CONNECT, deviceId);
    long ret;

    if (ev) {
        ret = (long)ev->returnCode;
        if (pChannelId) *pChannelId = ev->outChannelId;
        g_logger.verbose("  Replay: Connect -> ChannelId=%lu, ret=%s",
                         ev->outChannelId, rjRetCodeName(ret));
    } else {
        g_logger.verbose("  Replay: Connect not found in log");
        if (pChannelId) *pChannelId = 2; // fallback
        ret = STATUS_NOERROR;
    }

    LeaveCriticalSection(&lock_);
    return ret;
}

long ReplayEngine::disconnect(unsigned long channelId) {
    EnterCriticalSection(&lock_);
    const ReplayEvent *ev = findNextMatch(CALL_DISCONNECT, channelId);
    long ret = ev ? (long)ev->returnCode : STATUS_NOERROR;
    g_logger.verbose("  Replay: Disconnect(%lu) -> %s", channelId, rjRetCodeName(ret));
    LeaveCriticalSection(&lock_);
    return ret;
}

long ReplayEngine::readMsgs(unsigned long channelId, PASSTHRU_MSG *pMsg,
                            unsigned long *pNumMsgs, unsigned long timeout) {
    EnterCriticalSection(&lock_);

    // Check dedup first
    if (dedupIndex_ >= 0 && dedupRemaining_ > 0) {
        const ReplayEvent *ev = parser_.getEvent(dedupIndex_);
        if (ev && ev->type == CALL_READ && ev->handle == channelId) {
            dedupRemaining_--;
            *pNumMsgs = ev->numMsgsOut;
            if (ev->numMsgsOut > 0 && pMsg) {
                unsigned long toCopy = ev->numMsgsOut;
                if (toCopy > ev->numMsgs) toCopy = ev->numMsgs;
                for (unsigned long i = 0; i < toCopy; i++)
                    copyMessage(ev->msgs[i], pMsg[i]);
            }
            g_logger.debug("  Replay: ReadMsgs dedup (idx=%d rem=%lu out=%lu)",
                           dedupIndex_, dedupRemaining_, ev->numMsgsOut);
            long ret = (long)ev->returnCode;
            LeaveCriticalSection(&lock_);
            return ret;
        }
        dedupRemaining_ = 0;
        dedupIndex_ = -1;
    }

    // Search for next read event
    const ReplayEvent *ev = findNextRead(channelId);
    long ret;
    if (ev) {
        ret = (long)ev->returnCode;
        *pNumMsgs = ev->numMsgsOut;
        if (ev->numMsgsOut > 0 && pMsg) {
            unsigned long toCopy = ev->numMsgsOut;
            if (toCopy > ev->numMsgs) toCopy = ev->numMsgs;
            for (unsigned long i = 0; i < toCopy; i++) {
                copyMessage(ev->msgs[i], pMsg[i]);
                if (g_logger.isDebug()) {
                    g_logger.hexDump("  Replay RX", pMsg[i].Data, pMsg[i].DataSize);
                }
            }
        }
        g_logger.verbose("  Replay: ReadMsgs ch=%lu out=%lu ret=%s",
                         channelId, ev->numMsgsOut, rjRetCodeName(ret));
    } else {
        // No read event available
        *pNumMsgs = 0;
        ret = ERR_BUFFER_EMPTY;
        g_logger.verbose("  Replay: ReadMsgs ch=%lu -> no event, returning BUFFER_EMPTY",
                         channelId);
    }

    LeaveCriticalSection(&lock_);
    return ret;
}

long ReplayEngine::writeMsgs(unsigned long channelId, PASSTHRU_MSG *pMsg,
                             unsigned long *pNumMsgs, unsigned long timeout) {
    EnterCriticalSection(&lock_);
    const ReplayEvent *ev = findNextWrite(channelId);
    long ret;

    if (ev) {
        ret = (long)ev->returnCode;
        *pNumMsgs = ev->numMsgsOut;
        g_logger.verbose("  Replay: WriteMsgs ch=%lu num=%lu ret=%s",
                         channelId, ev->numMsgsOut, rjRetCodeName(ret));
    } else {
        // No matching write event — accept the write anyway
        g_logger.verbose("  Replay: WriteMsgs ch=%lu not in log, accepting",
                         channelId);
        *pNumMsgs = *pNumMsgs;  // accept all
        ret = STATUS_NOERROR;
    }

    LeaveCriticalSection(&lock_);
    return ret;
}

long ReplayEngine::ioctl(unsigned long handle, unsigned long ioctlId,
                         void *pInput, void *pOutput) {
    EnterCriticalSection(&lock_);

    // For IOCTL matching, we need to also match the ioctlId
    // If we're in dedup mode, check
    if (dedupIndex_ >= 0 && dedupRemaining_ > 0) {
        const ReplayEvent *ev = parser_.getEvent(dedupIndex_);
        if (ev && ev->type == CALL_IOCTL && ev->handle == handle) {
            dedupRemaining_--;
            if (pOutput && ev->hasOutput && ev->ioctlOutSize > 0) {
                memcpy(pOutput, ev->ioctlOut, ev->ioctlOutSize);
            }
            g_logger.debug("  Replay: Ioctl dedup handle=%lu ioctl=0x%lX",
                           handle, ioctlId);
            long ret = (long)ev->returnCode;
            LeaveCriticalSection(&lock_);
            return ret;
        }
        dedupRemaining_ = 0;
        dedupIndex_ = -1;
    }

    // Search for next matching ioctl
    for (int i = cursor_; i < parser_.eventCount(); i++) {
        const ReplayEvent *ev = parser_.getEvent(i);
        if (!ev || ev->type != CALL_IOCTL) continue;
        if (ev->handle != handle) continue;

        // Match ioctl ID if known
        // For READ_VBATT on device handle, always match
        // For SET_CONFIG/GET_CONFIG, always match
        // For unknown ioctls, match any unknown
        bool idMatch = (ev->ioctlId == ioctlId) ||
                       (!ev->ioctlIdKnown && ioctlId != 0);

        if (!idMatch) continue;

        cursor_ = i + 1;
        if (ev->count > 1) {
            dedupIndex_ = i;
            dedupRemaining_ = ev->count - 1;
        }

        if (pOutput && ev->hasOutput && ev->ioctlOutSize > 0) {
            memcpy(pOutput, ev->ioctlOut, ev->ioctlOutSize);
        }

        // For SET_CONFIG with SCONFIG_LIST input, if the caller passes an
        // SCONFIG_LIST, we don't need to do anything — the caller already set
        // their local state. But we return the logged return code.

        long ret = (long)ev->returnCode;
        g_logger.verbose("  Replay: Ioctl(%lu, 0x%lX) -> %s",
                         handle, ioctlId, rjRetCodeName(ret));
        LeaveCriticalSection(&lock_);
        return ret;
    }

    // No matching ioctl event found
    g_logger.verbose("  Replay: Ioctl(%lu, 0x%lX) not in log, returning NOERROR",
                     handle, ioctlId);
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

long ReplayEngine::startMsgFilter(unsigned long channelId, unsigned long filterType,
                                  PASSTHRU_MSG *pMask, PASSTHRU_MSG *pPattern,
                                  PASSTHRU_MSG *pFlowControl, unsigned long *pFilterId) {
    EnterCriticalSection(&lock_);
    const ReplayEvent *ev = findNextMatch(CALL_FILTER_START, channelId);
    long ret;

    if (ev) {
        ret = (long)ev->returnCode;
        if (pFilterId) *pFilterId = ev->filterId;
        g_logger.verbose("  Replay: StartMsgFilter ch=%lu filterId=%lu ret=%s",
                         channelId, ev->filterId, rjRetCodeName(ret));
    } else {
        g_logger.verbose("  Replay: StartMsgFilter ch=%lu not in log, auto-assigning filterId=1");
        if (pFilterId) *pFilterId = 1;
        ret = STATUS_NOERROR;
    }

    LeaveCriticalSection(&lock_);
    return ret;
}

long ReplayEngine::stopMsgFilter(unsigned long channelId, unsigned long filterId) {
    EnterCriticalSection(&lock_);
    const ReplayEvent *ev = findNextMatch(CALL_FILTER_STOP, channelId);
    long ret = ev ? (long)ev->returnCode : STATUS_NOERROR;
    g_logger.verbose("  Replay: StopMsgFilter(%lu, %lu) -> %s",
                     channelId, filterId, rjRetCodeName(ret));
    LeaveCriticalSection(&lock_);
    return ret;
}

long ReplayEngine::readVersion(unsigned long deviceId, char *pFw, char *pDll, char *pApi) {
    EnterCriticalSection(&lock_);

    // Check dedup
    if (dedupIndex_ >= 0 && dedupRemaining_ > 0) {
        const ReplayEvent *ev = parser_.getEvent(dedupIndex_);
        if (ev && ev->type == CALL_READ_VERSION && ev->handle == deviceId) {
            dedupRemaining_--;
            if (pFw && ev->firmwareVersion[0])
                strncpy(pFw, ev->firmwareVersion, 80);
            if (pDll && ev->dllVersion[0])
                strncpy(pDll, ev->dllVersion, 80);
            if (pApi && ev->apiVersion[0])
                strncpy(pApi, ev->apiVersion, 80);
            long ret = (long)ev->returnCode;
            g_logger.debug("  Replay: ReadVersion dedup (idx=%d rem=%lu)",
                           dedupIndex_, dedupRemaining_);
            LeaveCriticalSection(&lock_);
            return ret;
        }
        dedupRemaining_ = 0;
        dedupIndex_ = -1;
    }

    const ReplayEvent *ev = findNextMatch(CALL_READ_VERSION, deviceId);
    long ret;

    if (ev) {
        ret = (long)ev->returnCode;
        if (pFw) {
            if (ev->firmwareVersion[0])
                strncpy(pFw, ev->firmwareVersion, 80);
            else
                strncpy(pFw, "0.0.0", 80);
        }
        if (pDll) {
            if (ev->dllVersion[0])
                strncpy(pDll, ev->dllVersion, 80);
            else
                strncpy(pDll, "1.0.0", 80);
        }
        if (pApi) {
            if (ev->apiVersion[0])
                strncpy(pApi, ev->apiVersion, 80);
            else
                strncpy(pApi, "04.04", 80);
        }
        g_logger.verbose("  Replay: ReadVersion fw=%s dll=%s api=%s",
                         pFw ? pFw : "(null)", pDll ? pDll : "(null)",
                         pApi ? pApi : "(null)");
    } else {
        // Default versions
        if (pFw) strncpy(pFw, "0.0.0", 80);
        if (pDll) strncpy(pDll, "1.0.0", 80);
        if (pApi) strncpy(pApi, "04.04", 80);
        ret = STATUS_NOERROR;
        g_logger.verbose("  Replay: ReadVersion not in log, using defaults");
    }

    LeaveCriticalSection(&lock_);
    return ret;
}

long ReplayEngine::getLastError(char *pBuf) {
    if (!pBuf) return ERR_NULL_PARAMETER;
    EnterCriticalSection(&lock_);
    strncpy(pBuf, lastError_, 80);
    pBuf[79] = '\0';
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}
