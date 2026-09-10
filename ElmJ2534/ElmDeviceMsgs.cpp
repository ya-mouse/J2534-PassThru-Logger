// ElmJ2534 — J2534 message path: WriteMsgs / ReadMsgs and the RX queue
//
// Implements: docs/elm-j2534-design.md → "J2534 mapping" + "RX PASSTHRU_MSG
// convention". Patterns mirrored from ReplayJ2534/Simulator.cpp: condvar-based
// readMsgs and TX echo semantics. Lifecycle, channel management and versioning
// live in ElmDevice.cpp, filters and ioctl in ElmDeviceFilters.cpp; the shared
// plumbing (id byte order, log shim) is in ElmDevicePriv.h.
//
// One J2534 write == one ELM exchange, synchronously: the adapter is
// half-duplex with one command per '>' prompt, so there is no send window to
// pipeline into and the RX queue is filled before WriteMsgs returns.

#include "ElmDevicePriv.h"

// ═══════════════════════════════════════════════════════════════════════════
// WriteMsgs — one J2534 message == one ELM exchange
// ═══════════════════════════════════════════════════════════════════════════

// Drives one request through the adapter's failure taxonomy. The adapter is
// the authority on what clears a failure, so the retry shape follows its
// recovery token; every recovery clears ATSH/ATCRA/ATFCSH, so the retry is
// always re-addressed first (T3 handoff).
ElmDevice::ExchangeOutcome
ElmDevice::runExchange(uint32_t reqId, uint32_t respId,
                       const uint8_t *payload, int payloadLen, int exchangeMs,
                       std::vector<ElmAssembly> &assemblies) {
    std::string err, rerr;
    if (!session_->setTarget(reqId, respId, err)) {
        noteError("addressing %s failed: %s", idText(reqId).c_str(), err.c_str());
        return ExchangeFailed;
    }

    for (int attempt = 0; attempt < 2; attempt++) {
        ElmRecovery recovery = ElmRecovery::None;
        assemblies.clear();
        if (session_->request(payload, payloadLen, exchangeMs, assemblies,
                              err, &recovery))
            return ExchangeOk;

        if (recovery == ElmRecovery::Retry || recovery == ElmRecovery::Wait) {
            // NODATA / STOPPED / SEARCHING: the frame went out and no ECU
            // answered. That is an OBD outcome, not a write failure — J2534
            // callers learn "no reply" from an empty RX queue and ERR_TIMEOUT.
            ELM_LOGV("ElmDevice: %s -> %s (no reply)", idText(reqId).c_str(),
                     err.c_str());
            return ExchangeSilent;
        }
        if (recovery == ElmRecovery::Unsupported) {
            noteError("adapter rejected the request to %s ('?'): %s",
                      idText(reqId).c_str(), err.c_str());
            return ExchangeUnsupported;
        }

        // Link-class faults (no token at all, or an explicit Reinitialise)
        // mean the adapter's state is unknowable — RFCOMM may be gone — so the
        // recovery may have to re-open the port. Bus-class faults keep the
        // link and only reset protocol state.
        const bool linkFault = (recovery == ElmRecovery::None ||
                                recovery == ElmRecovery::Reinitialise);
        if (attempt == 1) {
            noteError("%s to %s: %s",
                      linkFault ? "ELM link lost" : "ELM request failed after recovery",
                      idText(reqId).c_str(), err.c_str());
            return linkFault ? ExchangeLinkGone : ExchangeFailed;
        }

        const bool recovered = linkFault ? reinitialiseLink(rerr)
                                         : session_->recover(recovery, rerr);
        if (!recovered) {
            noteError("%s on %s: recovery %s failed: %s / %s",
                      linkFault ? "ELM link lost" : "ELM request failed",
                      idText(reqId).c_str(), recoveryName(recovery),
                      err.c_str(), rerr.c_str());
            return linkFault ? ExchangeLinkGone : ExchangeFailed;
        }
        if (!session_->setTarget(reqId, respId, rerr)) {
            noteError("re-addressing %s after %s recovery failed: %s",
                      idText(reqId).c_str(), recoveryName(recovery),
                      rerr.c_str());
            return linkFault ? ExchangeLinkGone : ExchangeFailed;
        }
        ELM_LOGV("ElmDevice: retrying %s after %s recovery",
                 idText(reqId).c_str(), recoveryName(recovery));
    }
    return ExchangeFailed;   // unreachable: attempt 1 always returns
}

long ElmDevice::writeOne(Channel &ch, const PASSTHRU_MSG &msg,
                         unsigned long timeout) {
    if (msg.ProtocolID != ch.protocolId) {
        noteError("message protocol 0x%lX does not match channel protocol 0x%lX",
                  msg.ProtocolID, ch.protocolId);
        return ERR_MSG_PROTOCOL_ID;
    }
    if (msg.DataSize < ELM_MIN_WRITE_DATA_SIZE) {
        noteError("DataSize %lu too small: 4-byte CAN id + at least one "
                  "payload byte", msg.DataSize);
        return ERR_INVALID_MSG;
    }
    if (msg.DataSize > sizeof(msg.Data)) {
        noteError("DataSize %lu exceeds the %u-byte PASSTHRU_MSG data buffer",
                  msg.DataSize, (unsigned)sizeof(msg.Data));
        return ERR_INVALID_MSG;
    }

    const uint32_t reqId = beIdFromData(msg.Data);
    const int payloadLen = (int)msg.DataSize - (int)ELM_CAN_ID_BYTES;
    const int exchangeMs = timeout ? (int)timeout : ELM_EXCHANGE_TIMEOUT_MS;
    // msg.TxFlags is deliberately ignored: with ATH1+ATCAF1 the chip adds the
    // ISO-TP PCI byte and pads the frame itself, so there is nothing here the
    // host could honour (ISO15765_FRAME_PAD is already the chip's behaviour).

    ELM_LOGD("ElmDevice: TX %s [%d bytes]", idText(reqId).c_str(), payloadLen);
    if (ch.flowReqId != 0 && ch.flowReqId != reqId) {
        // ATFCSH follows the id being written, not the one the filter named:
        // worth a line in the log when an app's two disagree, because the
        // flow-control frames then go somewhere the app did not ask for.
        ELM_LOGV("ElmDevice: FLOW_CONTROL filter names %s but this write targets "
                 "%s — flow control follows the write",
                 idText(ch.flowReqId).c_str(), idText(reqId).c_str());
    }

    std::vector<ElmAssembly> assemblies;
    const ExchangeOutcome outcome =
        runExchange(reqId, ch.flowRespId, msg.Data + ELM_CAN_ID_BYTES,
                    payloadLen, exchangeMs, assemblies);
    switch (outcome) {
    case ExchangeSilent:
        enqueueEcho(ch, reqId);   // the transmission itself is confirmed
        return STATUS_NOERROR;
    case ExchangeUnsupported:
        return ERR_NOT_SUPPORTED;
    case ExchangeLinkGone:
        // The device stays "open" so the caller can still Close it cleanly;
        // later exchanges fail the same way until it does.
        return ERR_DEVICE_NOT_CONNECTED;
    case ExchangeFailed:
        return ERR_FAILED;
    case ExchangeOk:
        break;
    }

    enqueueEcho(ch, reqId);
    for (size_t i = 0; i < assemblies.size(); i++) {
        const ElmAssembly &a = assemblies[i];
        if (!a.ok) {
            // Never forward a partial message: a half-decoded PID is worse
            // than a missing one, and truncation is exactly the signature of
            // a clone that ignored ATFCSM1 — the caller must be able to see it.
            if (a.truncated)
                noteError("truncated multi-frame reply from %s (received %u bytes)",
                          idText(a.canId).c_str(), (unsigned)a.payload.size());
            else
                noteError("incomplete reply from %s dropped",
                          idText(a.canId).c_str());
            continue;
        }
        if (!rxFilterAllows(ch, a.canId)) {
            ELM_LOGV("ElmDevice: RX from %s dropped by filter",
                     idText(a.canId).c_str());
            continue;
        }
        enqueueResponse(ch, a.canId, a.payload);
    }
    return STATUS_NOERROR;
}

long ElmDevice::writeMsgs(unsigned long channelId, PASSTHRU_MSG *pMsg,
                          unsigned long *pNumMsgs, unsigned long timeout) {
    if (!pMsg || !pNumMsgs) return ERR_NULL_PARAMETER;

    EnterCriticalSection(&lock_);
    if (!deviceOpen_ || !session_) {
        noteError("device is not open");
        LeaveCriticalSection(&lock_);
        return ERR_DEVICE_NOT_CONNECTED;
    }
    Channel *ch = findChannel(channelId);
    if (!ch) {
        noteError("invalid channel id %lu", channelId);
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_CHANNEL_ID;
    }

    const unsigned long count = *pNumMsgs;
    unsigned long written = 0;
    for (unsigned long i = 0; i < count; i++) {
        const long rc = writeOne(*ch, pMsg[i], timeout);
        if (rc != STATUS_NOERROR) {
            // Report how far we got: the adapter is half-duplex, so everything
            // before the failure really did go out on the bus.
            *pNumMsgs = written;
            ELM_LOGV("ElmDevice: WriteMsgs ch=%lu written=%lu/%lu rc=%ld",
                     channelId, written, count, rc);
            LeaveCriticalSection(&lock_);
            return rc;
        }
        written++;
    }
    *pNumMsgs = written;
    ELM_LOGV("ElmDevice: WriteMsgs ch=%lu written=%lu", channelId, written);
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

// ═══════════════════════════════════════════════════════════════════════════
// ReadMsgs + RX queue
// ═══════════════════════════════════════════════════════════════════════════

// No cap on the queue: writes are synchronous and serialised under lock_, so
// it can only grow as fast as the caller writes, and a cap would silently drop
// diagnostic data the app is entitled to.
void ElmDevice::enqueueRx(Channel &ch, const PASSTHRU_MSG &msg) {
    ch.rxQueue.push_back(msg);
    WakeConditionVariable(&rxCond_);
}

// TX confirmation, only when the channel asked for it. TX_INDICATION alongside
// TX_MSG_TYPE is what Simulator.cpp emits and what Xentry needs to recognise
// the write as sent; the id-only 4-byte shape is the J2534 ISO15765 echo.
void ElmDevice::enqueueEcho(Channel &ch, uint32_t reqId) {
    if (!(ch.flags & CAN_ID_BOTH)) return;
    PASSTHRU_MSG echo;
    memset(&echo, 0, sizeof(echo));
    echo.ProtocolID = ch.protocolId;
    echo.RxStatus = TX_MSG_TYPE | TX_INDICATION;
    echo.TxFlags = 0;
    echo.Timestamp = elapsedMs();
    echo.DataSize = ELM_CAN_ID_BYTES;
    echo.ExtraDataIndex = ELM_CAN_ID_BYTES;
    putBeId(echo.Data, reqId);
    enqueueRx(ch, echo);
}

// Design spec → "RX PASSTHRU_MSG convention": 4-byte big-endian response id +
// reassembled payload, DataSize == ExtraDataIndex, RxStatus 0.
void ElmDevice::enqueueResponse(Channel &ch, uint32_t respId,
                                const std::vector<uint8_t> &payload) {
    PASSTHRU_MSG msg;
    memset(&msg, 0, sizeof(msg));
    const size_t capacity = sizeof(msg.Data) - ELM_CAN_ID_BYTES;
    if (payload.size() > capacity) {
        noteError("reply from %s carries %u bytes, over the %u-byte "
                  "PASSTHRU_MSG limit — dropped",
                  idText(respId).c_str(), (unsigned)payload.size(),
                  (unsigned)capacity);
        return;
    }
    msg.ProtocolID = ch.protocolId;
    msg.RxStatus = 0;
    msg.TxFlags = 0;
    msg.Timestamp = elapsedMs();
    msg.DataSize = (unsigned long)(ELM_CAN_ID_BYTES + payload.size());
    msg.ExtraDataIndex = msg.DataSize;
    putBeId(msg.Data, respId);
    if (!payload.empty())
        memcpy(msg.Data + ELM_CAN_ID_BYTES, &payload[0], payload.size());
    enqueueRx(ch, msg);
}

unsigned long ElmDevice::elapsedMs() const {
    // Unsigned subtraction is correct across a GetTickCount wrap (~49.7 days).
    return (unsigned long)(GetTickCount() - startTick_);
}

long ElmDevice::readMsgs(unsigned long channelId, PASSTHRU_MSG *pMsg,
                         unsigned long *pNumMsgs, unsigned long timeout) {
    if (!pMsg || !pNumMsgs) return ERR_NULL_PARAMETER;

    EnterCriticalSection(&lock_);
    if (!findChannel(channelId)) {
        noteError("invalid channel id %lu", channelId);
        *pNumMsgs = 0;
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_CHANNEL_ID;
    }

    const unsigned long requested = *pNumMsgs;
    // Wait while the queue is empty. SleepConditionVariableCS RELEASES lock_,
    // which is what lets a concurrent WriteMsgs in to fill the queue — and
    // what lets Disconnect/Close erase the channel underneath this reader.
    // The channel is therefore re-resolved by id after every wake and again
    // before delivery; a pointer cached across the wait would be dereferenced
    // into a freed deque (P-LIVE-005).
    if (timeout > 0) {
        const DWORD deadline = GetTickCount() + timeout;
        for (;;) {
            Channel *ch = findChannel(channelId);
            if (!ch) {
                noteError("channel %lu went away while waiting for messages",
                          channelId);
                *pNumMsgs = 0;
                LeaveCriticalSection(&lock_);
                return ERR_INVALID_CHANNEL_ID;
            }
            if (!ch->rxQueue.empty() || !deviceOpen_) break;
            const DWORD now = GetTickCount();
            // Signed 32-bit difference: correct across the 49.7-day
            // GetTickCount wrap, unlike a plain `deadline > now` comparison.
            // int32_t, not long — long is 64-bit on the native test host and
            // would read a wrapped difference as a huge positive (P-LIVE-004).
            if ((int32_t)(deadline - now) <= 0) break;
            waiters_++;
            const BOOL waited = SleepConditionVariableCS(&rxCond_, &lock_,
                                                         deadline - now);
            waiters_--;
            if (!waited) break;
        }
    }

    // Final re-resolution: every break path above can leave the channel erased
    // (Disconnect/Close/shutdown all wake waiters after clearing the map).
    Channel *ch = findChannel(channelId);
    if (!ch) {
        noteError("channel %lu went away while waiting for messages", channelId);
        *pNumMsgs = 0;
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_CHANNEL_ID;
    }

    unsigned long delivered = 0;
    while (!ch->rxQueue.empty() && delivered < requested) {
        pMsg[delivered] = ch->rxQueue.front();
        ch->rxQueue.pop_front();
        delivered++;
    }
    *pNumMsgs = delivered;
    // Design spec: an empty queue at the deadline is ERR_TIMEOUT (Simulator
    // answers ERR_BUFFER_EMPTY because a replay can always be re-run; here a
    // silent bus is the normal case and callers branch on ERR_TIMEOUT).
    const long ret = (delivered > 0) ? STATUS_NOERROR : ERR_TIMEOUT;
    ELM_LOGV("ElmDevice: ReadMsgs ch=%lu delivered=%lu/%lu ret=%ld",
             channelId, delivered, requested, ret);
    LeaveCriticalSection(&lock_);
    return ret;
}
