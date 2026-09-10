// ElmJ2534 — channel configuration surface: message filters + ioctl
//
// Implements: docs/elm-j2534-design.md → "J2534 mapping" rows for
// PassThruStartMsgFilter and PassThruIoctl. Split out of ElmDeviceMsgs.cpp so
// each TU stays inside the project's LOC budget; the three ElmDevice*.cpp
// files implement one class and share ElmDevicePriv.h.
//
// Filters and ioctl belong together: both configure per-channel state rather
// than move messages, and a FLOW_CONTROL filter is what the next write's
// ATCRA/ATFCSH addressing rides on.

#include "ElmDevicePriv.h"

// ═══════════════════════════════════════════════════════════════════════════
// Filters
// ═══════════════════════════════════════════════════════════════════════════

// The last FLOW_CONTROL filter installed owns the adapter's single hardware
// receive filter; removing it falls back to whatever remains (or to none).
void ElmDevice::applyFlowIds(Channel &ch) {
    ch.flowRespId = 0;
    ch.flowReqId = 0;
    for (size_t i = 0; i < ch.filters.size(); i++) {
        if (ch.filters[i].type != FLOW_CONTROL_FILTER) continue;
        ch.flowRespId = ch.filters[i].pattern;
        ch.flowReqId = ch.filters[i].flow;
    }
}

// PASS/BLOCK are host-side post-filters: ATCRA is already spent on the
// flow-control target, so anything finer has to be applied here. A PASS
// filter present means "only these ids", BLOCK always wins first.
bool ElmDevice::rxFilterAllows(const Channel &ch, uint32_t respId) {
    bool hasPass = false;
    for (size_t i = 0; i < ch.filters.size(); i++) {
        const Filter &f = ch.filters[i];
        const bool hit = ((respId & f.mask) == (f.pattern & f.mask));
        if (f.type == BLOCK_FILTER && hit) return false;
        if (f.type == PASS_FILTER) hasPass = true;
    }
    if (!hasPass) return true;
    for (size_t i = 0; i < ch.filters.size(); i++) {
        const Filter &f = ch.filters[i];
        if (f.type == PASS_FILTER && (respId & f.mask) == (f.pattern & f.mask))
            return true;
    }
    return false;
}

long ElmDevice::startMsgFilter(unsigned long channelId, unsigned long filterType,
                               PASSTHRU_MSG *pMask, PASSTHRU_MSG *pPattern,
                               PASSTHRU_MSG *pFlow, unsigned long *pFilterId) {
    if (!pFilterId) return ERR_NULL_PARAMETER;
    // Project convention (P-LIVE-002): a missing mask/pattern is a NULL
    // parameter, not "match everything".
    if (!pMask || !pPattern) return ERR_NULL_PARAMETER;
    if (filterType == FLOW_CONTROL_FILTER && !pFlow) return ERR_NULL_PARAMETER;

    EnterCriticalSection(&lock_);
    if (filterType != PASS_FILTER && filterType != BLOCK_FILTER &&
        filterType != FLOW_CONTROL_FILTER) {
        noteError("invalid filter type %lu", filterType);
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_FLAGS;
    }
    Channel *ch = findChannel(channelId);
    if (!ch) {
        noteError("invalid channel id %lu", channelId);
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_CHANNEL_ID;
    }

    Filter f;
    f.id = 0;
    f.type = filterType;
    f.mask = 0;
    f.pattern = 0;
    f.flow = 0;
    if (!beIdFromMsg(*pMask, f.mask) || !beIdFromMsg(*pPattern, f.pattern)) {
        noteError("filter mask/pattern needs a 4-byte CAN id in Data[0..3]");
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_MSG;
    }
    if (filterType == FLOW_CONTROL_FILTER && !beIdFromMsg(*pFlow, f.flow)) {
        noteError("flow-control message needs a 4-byte CAN id in Data[0..3]");
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_MSG;
    }
    f.id = nextFilterId_++;
    ch->filters.push_back(f);
    applyFlowIds(*ch);
    *pFilterId = f.id;

    // No AT command goes out here: ATCRA/ATFCSH ride the next setTarget. The
    // adapter is half-duplex, and re-addressing per filter would cost five
    // round trips to reach a state the next write re-asserts anyway.
    ELM_LOGV("ElmDevice: StartFilter ch=%lu type=%lu mask=%08X pattern=%08X "
             "-> FilterId=%lu (flowResp=%s)",
             channelId, filterType, (unsigned)f.mask, (unsigned)f.pattern,
             f.id, idText(ch->flowRespId).c_str());
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

long ElmDevice::stopMsgFilter(unsigned long channelId, unsigned long filterId) {
    EnterCriticalSection(&lock_);
    Channel *ch = findChannel(channelId);
    if (!ch) {
        noteError("invalid channel id %lu", channelId);
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_CHANNEL_ID;
    }
    for (size_t i = 0; i < ch->filters.size(); i++) {
        if (ch->filters[i].id != filterId) continue;
        ch->filters.erase(ch->filters.begin() + (long)i);
        applyFlowIds(*ch);
        ELM_LOGV("ElmDevice: StopFilter ch=%lu FilterId=%lu", channelId, filterId);
        LeaveCriticalSection(&lock_);
        return STATUS_NOERROR;
    }
    noteError("invalid filter id %lu on channel %lu", filterId, channelId);
    LeaveCriticalSection(&lock_);
    return ERR_INVALID_FILTER_ID;
}

// ═══════════════════════════════════════════════════════════════════════════
// Ioctl
// ═══════════════════════════════════════════════════════════════════════════

long ElmDevice::ioctl(unsigned long handle, unsigned long ioctlId,
                      void *pInput, void *pOutput) {
    EnterCriticalSection(&lock_);
    if (!deviceOpen_ || !session_) {
        noteError("device is not open");
        LeaveCriticalSection(&lock_);
        return ERR_DEVICE_NOT_CONNECTED;
    }
    // Handle scoping as in Simulator::ioctl: a handle is either the device or
    // one of its channels, and each ioctl declares which it accepts.
    Channel *ch = findChannel(handle);
    const bool isDevice = (handle == deviceId_);
    if (!ch && !isDevice) {
        noteError("invalid handle %lu", handle);
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_CHANNEL_ID;
    }

    long ret = STATUS_NOERROR;
    switch (ioctlId) {
    case READ_VBATT: {
        // Device OR channel handle: apps probe the supply before connecting.
        if (!pOutput) { ret = ERR_NULL_PARAMETER; break; }
        int mv = 0;
        std::string err;
        if (!session_->readVoltageMillivolts(mv, err)) {
            noteError("ATRV failed: %s", err.c_str());
            ret = ERR_FAILED;
            break;
        }
        *(unsigned long *)pOutput = (unsigned long)mv;
        ELM_LOGV("ElmDevice: VBATT=%d mV", mv);
        break;
    }
    case CLEAR_RX_BUFFER:
        if (!ch) { ret = ERR_INVALID_CHANNEL_ID; break; }
        ELM_LOGV("ElmDevice: CLEAR_RX_BUFFER ch=%lu (%u dropped)",
                 handle, (unsigned)ch->rxQueue.size());
        ch->rxQueue.clear();
        break;

    case CLEAR_MSG_FILTERS:
        if (!ch) { ret = ERR_INVALID_CHANNEL_ID; break; }
        ch->filters.clear();
        applyFlowIds(*ch);
        ELM_LOGV("ElmDevice: CLEAR_MSG_FILTERS ch=%lu", handle);
        break;

    case GET_CONFIG: {
        if (!ch) { ret = ERR_INVALID_CHANNEL_ID; break; }
        // v04.04 documents GET_CONFIG as pInput=list / pOutput=NULL (see
        // PassThruLogger/J2534_v0404.h); accept either so a caller that passes
        // a separate output list is not left with garbage in it.
        SCONFIG_LIST *list = (SCONFIG_LIST *)(pInput ? pInput : pOutput);
        if (!list || (!list->ConfigPtr && list->NumOfParams > 0)) {
            ret = ERR_NULL_PARAMETER;
            break;
        }
        for (unsigned long i = 0; i < list->NumOfParams; i++) {
            switch (list->ConfigPtr[i].Parameter) {
            case DATA_RATE: list->ConfigPtr[i].Value = ELM_CAN_BAUD_500K; break;
            case LOOPBACK:  list->ConfigPtr[i].Value = 0; break;
            default:
                noteError("config parameter 0x%lX is not available on this device",
                          list->ConfigPtr[i].Parameter);
                ret = ERR_INVALID_IOCTL_VALUE;
                break;
            }
            if (ret != STATUS_NOERROR) break;
        }
        break;
    }
    case SET_CONFIG: {
        if (!ch) { ret = ERR_INVALID_CHANNEL_ID; break; }
        SCONFIG_LIST *list = (SCONFIG_LIST *)pInput;
        if (!list || (!list->ConfigPtr && list->NumOfParams > 0)) {
            ret = ERR_NULL_PARAMETER;
            break;
        }
        for (unsigned long i = 0; i < list->NumOfParams; i++) {
            const unsigned long param = list->ConfigPtr[i].Parameter;
            const unsigned long value = list->ConfigPtr[i].Value;
            switch (param) {
            case DATA_RATE:
                // ATSP6 fixes the rate: accepting anything else would be a lie
                // the bus would expose on the first frame.
                if (value != ELM_CAN_BAUD_500K) {
                    noteError("DATA_RATE %lu not supported: the adapter is at %lu",
                              value, ELM_CAN_BAUD_500K);
                    ret = ERR_INVALID_IOCTL_VALUE;
                }
                break;
            case LOOPBACK:
                // No loopback switch in the ELM327; the TX echo an app wants
                // is CAN_ID_BOTH at Connect time.
                ELM_LOGV("ElmDevice: SET_CONFIG LOOPBACK=%lu accepted (no-op)",
                         value);
                break;
            case ISO15765_BS:
            case ISO15765_STMIN:
                // Validated no-ops: flow-control timing belongs to the chip
                // (ATFCSD300000 = CTS, BS 0, STmin 0) and is re-asserted by
                // every addressing block, so a per-channel value could not
                // survive the next write even if we stored it.
                if (value > ELM_FC_PARAM_MAX) {
                    noteError("ISO15765 parameter 0x%lX value %lu out of range",
                              param, value);
                    ret = ERR_INVALID_IOCTL_VALUE;
                } else {
                    ELM_LOGV("ElmDevice: SET_CONFIG param=0x%lX value=%lu "
                             "accepted (ELM owns flow-control timing)",
                             param, value);
                }
                break;
            default:
                // Same code GET_CONFIG uses for an unknown parameter, and the
                // KvaserDirect precedent: the IOCTL id was valid, the
                // parameter inside it was not.
                noteError("config parameter 0x%lX is not supported", param);
                ret = ERR_INVALID_IOCTL_VALUE;
                break;
            }
            if (ret != STATUS_NOERROR) break;
        }
        break;
    }
    case FIVE_BAUD_INIT:
    case FAST_INIT:
        // K-line initialisation belongs to ISO9141/ISO14230, which this
        // adapter is not addressed as (ATSP6 only).
        noteError("ioctl 0x%lX not supported: no K-line init over ISO15765",
                  ioctlId);
        ret = ERR_NOT_SUPPORTED;
        break;

    default:
        // CLEAR_TX_BUFFER (writes are synchronous — there is no TX buffer),
        // CLEAR_PERIODIC_MSGS (accepted as a no-op at the API layer), the
        // functional-message lookup table and READ_PROG_VOLTAGE have no
        // meaning on a half-duplex ELM327.
        noteError("ioctl 0x%lX not supported", ioctlId);
        ret = ERR_INVALID_IOCTL_ID;
        break;
    }

    ELM_LOGV("ElmDevice: Ioctl(handle=%lu, 0x%lX) -> %ld", handle, ioctlId, ret);
    LeaveCriticalSection(&lock_);
    return ret;
}