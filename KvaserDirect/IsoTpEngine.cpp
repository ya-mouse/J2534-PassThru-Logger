#include "IsoTpEngine.h"
#include <string.h>

// ────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ────────────────────────────────────────────────────────────────────

IsoTpEngine::IsoTpEngine()
    : hnd_(-1), api_(NULL), bsTx_(0), stminTx_(0),
      padValue_(0xCC), wftMax_(ISOTP_MAX_WFT),
      rxHead_(0), rxTail_(0), rxCount_(0), rxEvent_(NULL)
{
    memset(sessions_, 0, sizeof(sessions_));
    memset(rxQueue_, 0, sizeof(rxQueue_));
    InitializeCriticalSection(&lock_);
    rxEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
}

IsoTpEngine::~IsoTpEngine() {
    shutdown();
    DeleteCriticalSection(&lock_);
    if (rxEvent_) CloseHandle(rxEvent_);
}

void IsoTpEngine::init(CanHandle hnd, CanlibApi *api,
                       unsigned long bs_tx, unsigned long stmin_tx,
                       unsigned long pad_value, unsigned long wft_max) {
    hnd_ = hnd;
    api_ = api;
    bsTx_ = bs_tx;
    stminTx_ = stmin_tx;
    padValue_ = pad_value;
    wftMax_ = wft_max;
}

void IsoTpEngine::shutdown() {
    EnterCriticalSection(&lock_);
    for (int i = 0; i < ISOTP_MAX_SESSIONS; i++)
        sessions_[i].active = false;
    rxHead_ = rxTail_ = rxCount_ = 0;
    LeaveCriticalSection(&lock_);
}

void IsoTpEngine::setConfig(unsigned long bs_tx, unsigned long stmin_tx,
                            unsigned long pad_value, unsigned long wft_max) {
    EnterCriticalSection(&lock_);
    bsTx_ = bs_tx;
    stminTx_ = stmin_tx;
    padValue_ = pad_value;
    wftMax_ = wft_max;
    LeaveCriticalSection(&lock_);
}

// ────────────────────────────────────────────────────────────────────
// Session Management
// ────────────────────────────────────────────────────────────────────

int IsoTpEngine::addSession(unsigned long txId, unsigned long rxId,
                            unsigned int txFlags, unsigned int rxFlags,
                            unsigned long filterId) {
    EnterCriticalSection(&lock_);
    for (int i = 0; i < ISOTP_MAX_SESSIONS; i++) {
        if (!sessions_[i].active) {
            memset(&sessions_[i], 0, sizeof(IsoTpSession));
            sessions_[i].active = true;
            sessions_[i].txId = txId;
            sessions_[i].rxId = rxId;
            sessions_[i].txFlags = txFlags;
            sessions_[i].rxFlags = rxFlags;
            sessions_[i].filterId = filterId;
            sessions_[i].txState = TX_IDLE;
            sessions_[i].rxState = RX_IDLE;
            LeaveCriticalSection(&lock_);
            return i;
        }
    }
    LeaveCriticalSection(&lock_);
    return -1;
}

void IsoTpEngine::removeSession(unsigned long filterId) {
    EnterCriticalSection(&lock_);
    for (int i = 0; i < ISOTP_MAX_SESSIONS; i++) {
        if (sessions_[i].active && sessions_[i].filterId == filterId) {
            sessions_[i].active = false;
            break;
        }
    }
    LeaveCriticalSection(&lock_);
}

// ────────────────────────────────────────────────────────────────────
// TX: Transmit a complete ISO-TP message
// ────────────────────────────────────────────────────────────────────

long IsoTpEngine::transmit(unsigned long txId, unsigned int txFlags,
                           const unsigned char *data, unsigned long len,
                           unsigned long timeout) {
    if (len == 0 || len > ISOTP_MAX_MSG_LEN || !data)
        return ERR_INVALID_MSG;

    // Find session for this TX ID
    IsoTpSession *s = findSessionByTxId(txId, txFlags);
    if (!s) return ERR_INVALID_MSG;

    EnterCriticalSection(&lock_);
    if (s->txState != TX_IDLE) {
        LeaveCriticalSection(&lock_);
        return ERR_BUFFER_FULL;
    }

    // Single Frame: payload ≤ 7 bytes for classic CAN
    if (len <= 7) {
        unsigned char frame[8];
        memset(frame, padValue_, 8);
        frame[0] = (unsigned char)(ISOTP_PCI_SF | (len & 0x0F));
        memcpy(&frame[1], data, len);
        padFrame(frame, 1 + (int)len, 8);

        LeaveCriticalSection(&lock_);

        canStatus st = api_->canWrite(hnd_, (long)txId, frame, 8, txFlags);
        if (st != canOK) return ERR_FAILED;
        if (timeout > 0) api_->canWriteSync(hnd_, timeout);
        return STATUS_NOERROR;
    }

    // Multi-frame: First Frame + Consecutive Frames
    memcpy(s->txBuf, data, len);
    s->txLen = len;
    s->txOffset = 6;    // FF carries first 6 bytes
    s->txSN = 1;
    s->txBsSent = 0;
    s->txBS = 0;
    s->txSTmin = 0;

    // Send First Frame
    unsigned char ff[8];
    ff[0] = (unsigned char)(ISOTP_PCI_FF | ((len >> 8) & 0x0F));
    ff[1] = (unsigned char)(len & 0xFF);
    memcpy(&ff[2], data, 6);

    canStatus st = api_->canWrite(hnd_, (long)txId, ff, 8, txFlags);
    if (st != canOK) {
        s->txState = TX_IDLE;
        LeaveCriticalSection(&lock_);
        return ERR_FAILED;
    }

    // Now wait for Flow Control
    s->txState = TX_WAIT_FC;
    DWORD deadline = GetTickCount() + (timeout > 0 ? timeout : ISOTP_N_BS_DEFAULT);
    s->txFcDeadline = deadline;
    LeaveCriticalSection(&lock_);

    // Poll for FC response (processFrame will update state)
    while (GetTickCount() < deadline) {
        EnterCriticalSection(&lock_);
        IsoTpTxState state = s->txState;
        LeaveCriticalSection(&lock_);

        if (state == TX_IDLE) {
            // TX completed (all CFs sent in processFrame FC handler)
            return STATUS_NOERROR;
        }
        if (state == TX_SENDING_CF) {
            // FC received, now send CF burst
            EnterCriticalSection(&lock_);
            while (s->txOffset < s->txLen) {
                // Check BS limit
                if (s->txBS > 0 && s->txBsSent >= s->txBS) {
                    // Wait for next FC
                    s->txState = TX_WAIT_FC;
                    s->txBsSent = 0;
                    s->txFcDeadline = GetTickCount() + ISOTP_N_BS_DEFAULT;
                    break;
                }

                // Build CF
                unsigned char cf[8];
                memset(cf, padValue_, 8);
                cf[0] = (unsigned char)(ISOTP_PCI_CF | (s->txSN & 0x0F));
                unsigned long remaining = s->txLen - s->txOffset;
                unsigned long cfLen = (remaining > 7) ? 7 : remaining;
                memcpy(&cf[1], &s->txBuf[s->txOffset], cfLen);
                padFrame(cf, 1 + (int)cfLen, 8);

                LeaveCriticalSection(&lock_);

                st = api_->canWrite(hnd_, (long)s->txId, cf, 8, s->txFlags);
                if (st != canOK) {
                    EnterCriticalSection(&lock_);
                    s->txState = TX_IDLE;
                    LeaveCriticalSection(&lock_);
                    return ERR_FAILED;
                }

                // STmin delay between CFs
                if (s->txSTmin > 0) Sleep(s->txSTmin);

                EnterCriticalSection(&lock_);
                s->txOffset += cfLen;
                s->txSN = (s->txSN + 1) & 0x0F;
                s->txBsSent++;
            }

            if (s->txOffset >= s->txLen) {
                s->txState = TX_IDLE;
                LeaveCriticalSection(&lock_);
                if (timeout > 0) api_->canWriteSync(hnd_, timeout);
                return STATUS_NOERROR;
            }
            LeaveCriticalSection(&lock_);
            // Continue loop waiting for next FC
            deadline = s->txFcDeadline;
        }

        Sleep(1);
    }

    // Timeout waiting for FC
    EnterCriticalSection(&lock_);
    s->txState = TX_IDLE;
    LeaveCriticalSection(&lock_);
    return ERR_TIMEOUT;
}

// ────────────────────────────────────────────────────────────────────
// RX: Retrieve completed reassembled messages
// ────────────────────────────────────────────────────────────────────

long IsoTpEngine::receive(PASSTHRU_MSG *msgs, unsigned long *count, unsigned long timeout) {
    unsigned long requested = *count;
    unsigned long received = 0;

    DWORD deadline = GetTickCount() + timeout;

    while (received < requested) {
        EnterCriticalSection(&lock_);
        if (rxCount_ > 0) {
            IsoTpRxMsg &rx = rxQueue_[rxTail_];
            memset(&msgs[received], 0, sizeof(PASSTHRU_MSG));
            msgs[received].ProtocolID = J2534_ISO15765;
            msgs[received].Timestamp = rx.timestamp;
            // J2534 ISO15765: Data[0..3] = CAN ID (big-endian), Data[4..] = payload
            msgs[received].Data[0] = (unsigned char)((rx.canId >> 24) & 0xFF);
            msgs[received].Data[1] = (unsigned char)((rx.canId >> 16) & 0xFF);
            msgs[received].Data[2] = (unsigned char)((rx.canId >> 8) & 0xFF);
            msgs[received].Data[3] = (unsigned char)(rx.canId & 0xFF);
            unsigned long copyLen = rx.dataLen;
            if (copyLen > sizeof(msgs[received].Data) - 4)
                copyLen = sizeof(msgs[received].Data) - 4;
            memcpy(&msgs[received].Data[4], rx.data, copyLen);
            msgs[received].DataSize = 4 + copyLen;
            msgs[received].ExtraDataIndex = msgs[received].DataSize;
            if (rx.rxFlags & canMSG_EXT)
                msgs[received].RxStatus |= CAN_29BIT_ID;

            rxTail_ = (rxTail_ + 1) % ISOTP_RX_QUEUE_SIZE;
            rxCount_--;
            LeaveCriticalSection(&lock_);
            received++;
        } else {
            LeaveCriticalSection(&lock_);
            if (received > 0) break;  // Return what we have
            if (GetTickCount() >= deadline) break;
            // Wait for signal or short poll
            WaitForSingleObject(rxEvent_, 10);
        }
    }

    *count = received;
    if (received == 0) return ERR_BUFFER_EMPTY;
    if (received < requested) return ERR_TIMEOUT;
    return STATUS_NOERROR;
}

// ────────────────────────────────────────────────────────────────────
// Process incoming CAN frame (called from RX polling thread)
// ────────────────────────────────────────────────────────────────────

void IsoTpEngine::processFrame(long canId, unsigned int canFlags,
                               const unsigned char *data, unsigned int dlc,
                               unsigned long timestamp) {
    if (dlc < 1) return;

    unsigned char pciType = data[0] & 0xF0;

    EnterCriticalSection(&lock_);

    // Flow Control frames are responses to OUR transmissions
    if (pciType == ISOTP_PCI_FC) {
        // FC is from the ECU, matched by rxId (ECU's response address)
        IsoTpSession *s = findSessionByRxId(canId, canFlags);
        if (s && s->txState == TX_WAIT_FC) {
            unsigned char fs = data[0] & 0x0F;
            if (fs == ISOTP_FS_CTS) {
                s->txBS = (dlc > 1) ? data[1] : 0;
                s->txSTmin = (dlc > 2) ? decodeSTmin(data[2]) : 0;
                s->txBsSent = 0;
                s->txState = TX_SENDING_CF;
            } else if (fs == ISOTP_FS_WAIT) {
                // Extend deadline
                s->txFcDeadline = GetTickCount() + ISOTP_N_BS_DEFAULT;
            } else {
                // Overflow or invalid — abort TX
                s->txState = TX_IDLE;
            }
        }
        LeaveCriticalSection(&lock_);
        return;
    }

    // Data frames (SF, FF, CF) are from the ECU
    IsoTpSession *s = findSessionByRxId(canId, canFlags);
    if (!s) {
        LeaveCriticalSection(&lock_);
        return;
    }

    switch (pciType) {
    case ISOTP_PCI_SF: {
        unsigned long sfLen = data[0] & 0x0F;
        if (sfLen == 0 || sfLen > 7 || sfLen > dlc - 1) break;
        // Complete message in single frame
        IsoTpRxMsg msg;
        msg.canId = (unsigned long)canId;
        msg.rxFlags = canFlags;
        msg.dataLen = sfLen;
        msg.timestamp = timestamp;
        memcpy(msg.data, &data[1], sfLen);
        enqueueRx(msg);
        break;
    }

    case ISOTP_PCI_FF: {
        if (dlc < 2) break;
        unsigned long ffLen = ((unsigned long)(data[0] & 0x0F) << 8) | data[1];
        if (ffLen == 0 || ffLen > ISOTP_MAX_MSG_LEN) break;

        // Start RX assembly
        s->rxLen = ffLen;
        s->rxOffset = 0;
        unsigned long firstBytes = (dlc > 2) ? dlc - 2 : 0;
        if (firstBytes > ffLen) firstBytes = ffLen;
        if (firstBytes > 6) firstBytes = 6;
        memcpy(s->rxBuf, &data[2], firstBytes);
        s->rxOffset = firstBytes;
        s->rxSN = 1;
        s->rxState = RX_RECEIVING;
        s->rxBsCount = 0;
        s->rxWftCount = 0;
        s->rxCfDeadline = GetTickCount() + ISOTP_N_CR_DEFAULT;

        // Send Flow Control (CTS)
        sendFC(s, ISOTP_FS_CTS);
        break;
    }

    case ISOTP_PCI_CF: {
        if (s->rxState != RX_RECEIVING) break;
        unsigned char sn = data[0] & 0x0F;
        if (sn != s->rxSN) {
            // Wrong sequence — abort
            s->rxState = RX_IDLE;
            break;
        }

        unsigned long remaining = s->rxLen - s->rxOffset;
        unsigned long cfPayload = (dlc > 1) ? dlc - 1 : 0;
        if (cfPayload > 7) cfPayload = 7;
        if (cfPayload > remaining) cfPayload = remaining;
        memcpy(&s->rxBuf[s->rxOffset], &data[1], cfPayload);
        s->rxOffset += cfPayload;
        s->rxSN = (s->rxSN + 1) & 0x0F;
        s->rxBsCount++;
        s->rxCfDeadline = GetTickCount() + ISOTP_N_CR_DEFAULT;

        if (s->rxOffset >= s->rxLen) {
            // Complete! Deliver to app
            deliverMessage(s, timestamp);
            s->rxState = RX_IDLE;
        } else if (bsTx_ > 0 && s->rxBsCount >= bsTx_) {
            // Block received, send FC for next block
            s->rxBsCount = 0;
            sendFC(s, ISOTP_FS_CTS);
        }
        break;
    }

    default:
        break;
    }

    LeaveCriticalSection(&lock_);
}

// ────────────────────────────────────────────────────────────────────
// Private Helpers
// ────────────────────────────────────────────────────────────────────

IsoTpSession* IsoTpEngine::findSessionByRxId(long canId, unsigned int flags) {
    // Match by rxId — this is the CAN ID from the ECU
    unsigned int extBit = flags & canMSG_EXT;
    for (int i = 0; i < ISOTP_MAX_SESSIONS; i++) {
        if (sessions_[i].active &&
            sessions_[i].rxId == (unsigned long)canId &&
            (sessions_[i].rxFlags & canMSG_EXT) == extBit) {
            return &sessions_[i];
        }
    }
    return NULL;
}

IsoTpSession* IsoTpEngine::findSessionByTxId(unsigned long txId, unsigned int flags) {
    unsigned int extBit = flags & canMSG_EXT;
    for (int i = 0; i < ISOTP_MAX_SESSIONS; i++) {
        if (sessions_[i].active &&
            sessions_[i].txId == txId &&
            (sessions_[i].txFlags & canMSG_EXT) == extBit) {
            return &sessions_[i];
        }
    }
    return NULL;
}

void IsoTpEngine::sendFC(IsoTpSession *s, unsigned char flowStatus) {
    unsigned char fc[8];
    memset(fc, padValue_, 8);
    fc[0] = (unsigned char)(ISOTP_PCI_FC | (flowStatus & 0x0F));
    fc[1] = (unsigned char)(bsTx_ & 0xFF);       // Block Size
    fc[2] = encodeSTmin(stminTx_);                 // STmin
    padFrame(fc, 3, 8);

    // Send FC on our TX ID (we are the receiver responding)
    api_->canWrite(hnd_, (long)s->txId, fc, 8, s->txFlags);
}

void IsoTpEngine::deliverMessage(IsoTpSession *s, unsigned long timestamp) {
    IsoTpRxMsg msg;
    msg.canId = s->rxId;
    msg.rxFlags = s->rxFlags;
    msg.dataLen = s->rxLen;
    msg.timestamp = timestamp;
    unsigned long copyLen = s->rxLen;
    if (copyLen > ISOTP_MAX_MSG_LEN) copyLen = ISOTP_MAX_MSG_LEN;
    memcpy(msg.data, s->rxBuf, copyLen);
    enqueueRx(msg);
}

void IsoTpEngine::enqueueRx(const IsoTpRxMsg &msg) {
    if (rxCount_ >= ISOTP_RX_QUEUE_SIZE) {
        // Overflow — drop oldest
        rxTail_ = (rxTail_ + 1) % ISOTP_RX_QUEUE_SIZE;
        rxCount_--;
    }
    rxQueue_[rxHead_] = msg;
    rxHead_ = (rxHead_ + 1) % ISOTP_RX_QUEUE_SIZE;
    rxCount_++;
    SetEvent(rxEvent_);
}

void IsoTpEngine::padFrame(unsigned char *frame, int usedBytes, int totalBytes) {
    for (int i = usedBytes; i < totalBytes; i++)
        frame[i] = (unsigned char)padValue_;
}

unsigned long IsoTpEngine::decodeSTmin(unsigned char raw) {
    if (raw <= 0x7F) return (unsigned long)raw;          // 0-127 ms
    if (raw >= 0xF1 && raw <= 0xF9) return 1;           // 100-900 µs → round to 1ms
    return 0;  // Reserved values → no delay
}

unsigned char IsoTpEngine::encodeSTmin(unsigned long ms) {
    if (ms <= 127) return (unsigned char)ms;
    return 0x7F;  // Cap at 127ms
}
