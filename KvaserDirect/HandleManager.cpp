#include "HandleManager.h"
#include "CanlibLoader.h"
#include "Config.h"
#include <string.h>

extern CanlibApi g_canlib;

// RX thread for ISO15765 channels — reads raw CAN and feeds to ISO-TP engine
static DWORD WINAPI isoTpRxThreadFunc(LPVOID param) {
    ChannelState *ch = (ChannelState *)param;
    IsoTpEngine *engine = ch->isoTpEngine;

    while (WaitForSingleObject(ch->rxStopEvent, 0) == WAIT_TIMEOUT) {
        long id = 0;
        unsigned char data[64];
        unsigned int dlc = 0, flags = 0;
        unsigned long timestamp = 0;

        canStatus st = g_canlib.canReadWait(ch->canHandle, &id, data, &dlc,
                                            &flags, &timestamp, 10);
        if (st == canOK) {
            if (!(flags & canMSG_ERROR_FRAME)) {
                engine->processFrame(id, flags, data, dlc, timestamp);
            }
        }
    }
    return 0;
}

HandleManager::HandleManager() {
    InitializeCriticalSection(&lock_);
    memset(devices_, 0, sizeof(devices_));
    memset(channels_, 0, sizeof(channels_));
    nextDeviceId_ = 1232;
    nextChannelId_ = 1;
}

HandleManager::~HandleManager() {
    // Close any remaining channels
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (channels_[i].active) {
            // Stop RX thread if running
            if (channels_[i].rxStopEvent) {
                SetEvent(channels_[i].rxStopEvent);
                if (channels_[i].rxThread) {
                    WaitForSingleObject(channels_[i].rxThread, 2000);
                    CloseHandle(channels_[i].rxThread);
                }
                CloseHandle(channels_[i].rxStopEvent);
            }
            if (channels_[i].isoTpEngine) {
                channels_[i].isoTpEngine->shutdown();
                delete channels_[i].isoTpEngine;
            }
            g_canlib.canBusOff(channels_[i].canHandle);
            g_canlib.canClose(channels_[i].canHandle);
            DeleteCriticalSection(&channels_[i].rxLock);
        }
    }
    DeleteCriticalSection(&lock_);
}

// Maps J2534 baud rate to CANlib predefined constants or raw value
static long mapBaudRate(unsigned long baudRate) {
    switch (baudRate) {
    case 1000000: return canBITRATE_1M;
    case 500000:  return canBITRATE_500K;
    case 250000:  return canBITRATE_250K;
    case 125000:  return canBITRATE_125K;
    case 100000:  return canBITRATE_100K;
    case 83333:   return canBITRATE_83K;
    case 62500:   return canBITRATE_62K;
    case 50000:   return canBITRATE_50K;
    case 10000:   return canBITRATE_10K;
    default:      return (long)baudRate;  // Pass raw value
    }
}

long HandleManager::openDevice(const char *name, unsigned long *pDeviceId) {
    EnterCriticalSection(&lock_);

    // Find a free device slot
    int slot = -1;
    for (int i = 0; i < 4; i++) {
        if (!devices_[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        LeaveCriticalSection(&lock_);
        setLastError("Maximum number of devices reached");
        return ERR_DEVICE_IN_USE;
    }

    // Verify CANlib has channels available
    int channelCount = 0;
    canStatus st = g_canlib.canGetNumberOfChannels(&channelCount);
    if (st != canOK || channelCount == 0) {
        LeaveCriticalSection(&lock_);
        setLastError("No Kvaser CAN channels found");
        return ERR_DEVICE_NOT_CONNECTED;
    }

    if (g_config.canlibChannel >= channelCount) {
        LeaveCriticalSection(&lock_);
        setLastError("Configured CanlibChannel %d exceeds available %d",
                     g_config.canlibChannel, channelCount);
        return ERR_DEVICE_NOT_CONNECTED;
    }

    devices_[slot].active = true;
    devices_[slot].deviceId = nextDeviceId_++;
    devices_[slot].canlibChannel = g_config.canlibChannel;

    if (name) {
        strncpy(devices_[slot].deviceName, name, sizeof(devices_[slot].deviceName) - 1);
    } else {
        // Get device description from CANlib
        g_canlib.canGetChannelData(g_config.canlibChannel, canCHANNELDATA_DEVDESCR_ASCII,
                                   devices_[slot].deviceName, sizeof(devices_[slot].deviceName));
    }

    *pDeviceId = devices_[slot].deviceId;
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

long HandleManager::closeDevice(unsigned long deviceId) {
    EnterCriticalSection(&lock_);

    DeviceState *dev = NULL;
    for (int i = 0; i < 4; i++) {
        if (devices_[i].active && devices_[i].deviceId == deviceId) {
            dev = &devices_[i];
            break;
        }
    }
    if (!dev) {
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_DEVICE_ID;
    }

    // Close all channels belonging to this device
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (channels_[i].active && channels_[i].deviceId == deviceId) {
            g_canlib.canBusOff(channels_[i].canHandle);
            g_canlib.canClose(channels_[i].canHandle);
            DeleteCriticalSection(&channels_[i].rxLock);
            channels_[i].active = false;
        }
    }

    dev->active = false;
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

DeviceState* HandleManager::getDevice(unsigned long deviceId) {
    for (int i = 0; i < 4; i++) {
        if (devices_[i].active && devices_[i].deviceId == deviceId)
            return &devices_[i];
    }
    return NULL;
}

long HandleManager::openChannel(unsigned long deviceId, unsigned long protocolId,
                                unsigned long flags, unsigned long baudRate,
                                unsigned long *pChannelId) {
    EnterCriticalSection(&lock_);

    DeviceState *dev = getDevice(deviceId);
    if (!dev) {
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_DEVICE_ID;
    }

    // Find free channel slot
    int slot = -1;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (!channels_[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        LeaveCriticalSection(&lock_);
        setLastError("Maximum channels reached");
        return ERR_EXCEEDED_LIMIT;
    }

    // Determine CANlib channel number
    // J2534-2 CAN_CH1..128 maps to CANlib channel 0..127
    int canlibCh = dev->canlibChannel;
    if (protocolId >= J2534_FD_CAN_CH1 && protocolId <= J2534_FD_CAN_CH1 + 127) {
        canlibCh = (int)(protocolId - J2534_FD_CAN_CH1);
    } else if (protocolId >= J2534_FD_ISO15765_CH1 && protocolId <= J2534_FD_ISO15765_CH1 + 127) {
        canlibCh = (int)(protocolId - J2534_FD_ISO15765_CH1);
    }

    // Build canOpenChannel flags
    int openFlags = 0;
    if (flags & CAN_29BIT_ID)
        openFlags |= canOPEN_REQUIRE_EXTENDED;
    if (!g_config.shareCanlibChannels)
        openFlags |= canOPEN_REQUIRE_INIT_ACCESS;
    if (g_config.acceptVirtualChannels)
        openFlags |= canOPEN_ACCEPT_VIRTUAL;

    // Open the CANlib channel
    CanHandle hnd = g_canlib.canOpenChannel(canlibCh, openFlags);
    if (hnd < 0) {
        LeaveCriticalSection(&lock_);
        setLastError("canOpenChannel(%d) failed: %d", canlibCh, hnd);
        return ERR_DEVICE_NOT_CONNECTED;
    }

    // Set bus parameters
    long freq = mapBaudRate(baudRate);
    canStatus st = g_canlib.canSetBusParams(hnd, freq, 0, 0, 0, 0, 0);
    if (st != canOK) {
        g_canlib.canClose(hnd);
        LeaveCriticalSection(&lock_);
        setLastError("canSetBusParams(%ld) failed: %d", freq, st);
        return ERR_INVALID_BAUDRATE;
    }

    // Disable local TX echo (we don't want to receive our own transmissions)
    unsigned int noEcho = 0;
    g_canlib.canIoCtl(hnd, canIOCTL_SET_LOCAL_TXECHO, &noEcho, sizeof(noEcho));

    // Go bus-on
    st = g_canlib.canBusOn(hnd);
    if (st != canOK) {
        g_canlib.canClose(hnd);
        LeaveCriticalSection(&lock_);
        setLastError("canBusOn failed: %d", st);
        return ERR_DEVICE_NOT_CONNECTED;
    }

    // Initialize channel state
    ChannelState *ch = &channels_[slot];
    memset(ch, 0, sizeof(ChannelState));
    ch->active = true;
    ch->channelId = nextChannelId_++;
    ch->deviceId = deviceId;
    ch->protocolId = protocolId;
    ch->flags = flags;
    ch->baudRate = baudRate;
    ch->canHandle = hnd;
    ch->dataRate = baudRate;
    ch->iso15765_bs = 0;
    ch->iso15765_stmin = 0;
    ch->iso15765_bs_tx = 0;
    ch->iso15765_stmin_tx = 0;
    ch->iso15765_wft_max = 0;
    ch->iso15765_pad_value = 0xCC;
    ch->nextFilterId = 1;
    ch->nextPeriodicId = 1;
    ch->isoTpEngine = NULL;
    ch->rxThread = NULL;
    ch->rxStopEvent = NULL;
    InitializeCriticalSection(&ch->rxLock);

    // For ISO15765 channels, create the ISO-TP engine and RX thread
    bool isIsoTp = (protocolId == J2534_ISO15765 ||
                    (protocolId >= J2534_FD_ISO15765_CH1 &&
                     protocolId <= J2534_FD_ISO15765_CH1 + 127));
    if (isIsoTp) {
        ch->isoTpEngine = new IsoTpEngine();
        ch->isoTpEngine->init(hnd, &g_canlib,
                              ch->iso15765_bs_tx, ch->iso15765_stmin_tx,
                              ch->iso15765_pad_value, ch->iso15765_wft_max);

        // Start RX polling thread
        ch->rxStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        ch->rxThread = CreateThread(NULL, 0, isoTpRxThreadFunc, ch, 0, NULL);
    }

    *pChannelId = ch->channelId;
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

long HandleManager::closeChannel(unsigned long channelId) {
    EnterCriticalSection(&lock_);

    ChannelState *ch = NULL;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (channels_[i].active && channels_[i].channelId == channelId) {
            ch = &channels_[i];
            break;
        }
    }
    if (!ch) {
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_CHANNEL_ID;
    }

    // Stop periodic messages
    for (int i = 0; i < MAX_PERIODIC; i++) {
        if (ch->periodic[i].active && ch->periodic[i].timerHandle) {
            DeleteTimerQueueTimer(NULL, ch->periodic[i].timerHandle, NULL);
        }
    }

    // Stop ISO-TP RX thread
    if (ch->rxStopEvent) {
        SetEvent(ch->rxStopEvent);
        if (ch->rxThread) {
            WaitForSingleObject(ch->rxThread, 2000);
            CloseHandle(ch->rxThread);
        }
        CloseHandle(ch->rxStopEvent);
        ch->rxThread = NULL;
        ch->rxStopEvent = NULL;
    }

    // Destroy ISO-TP engine
    if (ch->isoTpEngine) {
        ch->isoTpEngine->shutdown();
        delete ch->isoTpEngine;
        ch->isoTpEngine = NULL;
    }

    g_canlib.canBusOff(ch->canHandle);
    g_canlib.canClose(ch->canHandle);
    DeleteCriticalSection(&ch->rxLock);
    ch->active = false;

    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

ChannelState* HandleManager::getChannel(unsigned long channelId) {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (channels_[i].active && channels_[i].channelId == channelId)
            return &channels_[i];
    }
    return NULL;
}

long HandleManager::addFilter(unsigned long channelId, unsigned long filterType,
                              PASSTHRU_MSG *pMask, PASSTHRU_MSG *pPattern,
                              PASSTHRU_MSG *pFlowControl, unsigned long *pFilterId) {
    ChannelState *ch = getChannel(channelId);
    if (!ch) return ERR_INVALID_CHANNEL_ID;

    // Find free filter slot
    int slot = -1;
    for (int i = 0; i < MAX_FILTERS; i++) {
        if (!ch->filters[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        setLastError("Maximum filters reached for channel");
        return ERR_EXCEEDED_LIMIT;
    }

    FilterEntry *f = &ch->filters[slot];
    f->active = true;
    f->filterType = filterType;

    // Extract CAN IDs from mask/pattern messages (first 4 bytes, big-endian)
    if (pMask->DataSize >= 4) {
        f->maskId = ((unsigned long)pMask->Data[0] << 24) |
                    ((unsigned long)pMask->Data[1] << 16) |
                    ((unsigned long)pMask->Data[2] << 8) |
                    (unsigned long)pMask->Data[3];
        f->maskFlags = pMask->TxFlags;
    }
    if (pPattern->DataSize >= 4) {
        f->patternId = ((unsigned long)pPattern->Data[0] << 24) |
                       ((unsigned long)pPattern->Data[1] << 16) |
                       ((unsigned long)pPattern->Data[2] << 8) |
                       (unsigned long)pPattern->Data[3];
        f->patternFlags = pPattern->TxFlags;
    }
    if (filterType == FLOW_CONTROL_FILTER && pFlowControl && pFlowControl->DataSize >= 4) {
        f->flowControlId = ((unsigned long)pFlowControl->Data[0] << 24) |
                           ((unsigned long)pFlowControl->Data[1] << 16) |
                           ((unsigned long)pFlowControl->Data[2] << 8) |
                           (unsigned long)pFlowControl->Data[3];
    }

    unsigned long filterId = ch->nextFilterId++;
    *pFilterId = filterId;

    // For ISO15765 FLOW_CONTROL filters, register an ISO-TP session
    if (filterType == FLOW_CONTROL_FILTER && ch->isoTpEngine) {
        // txId = FlowControl CAN ID (our outgoing FC address = pattern matched ID)
        // rxId = Pattern CAN ID (ECU responses we want to receive)
        // Per J2534 spec: FlowControl msg contains the ID we SEND FC on,
        //                 Pattern msg contains the ID we RECEIVE data from
        unsigned int txFlags = (pFlowControl->TxFlags & CAN_29BIT_ID) ? canMSG_EXT : 0;
        unsigned int rxFlags = (pPattern->TxFlags & CAN_29BIT_ID) ? canMSG_EXT : 0;
        ch->isoTpEngine->addSession(f->flowControlId, f->patternId,
                                     txFlags, rxFlags, filterId);
    }

    return STATUS_NOERROR;
}

long HandleManager::removeFilter(unsigned long channelId, unsigned long filterId) {
    ChannelState *ch = getChannel(channelId);
    if (!ch) return ERR_INVALID_CHANNEL_ID;

    // Find the filter by ID (filterId is 1-based sequential)
    // We store filterId implicitly by order, so scan for the matching one
    // The filterId tracks which filter was assigned (addFilter sets nextFilterId++)
    // Since we need the actual filterId to remove the ISO-TP session, track it properly
    for (int i = 0; i < MAX_FILTERS; i++) {
        if (ch->filters[i].active) {
            // Simple approach: remove first active filter matching
            // TODO: store filterId in FilterEntry for proper lookup
            if (ch->isoTpEngine && ch->filters[i].filterType == FLOW_CONTROL_FILTER) {
                ch->isoTpEngine->removeSession(filterId);
            }
            ch->filters[i].active = false;
            return STATUS_NOERROR;
        }
    }
    return ERR_INVALID_FILTER_ID;
}

// Periodic message timer callback
static VOID CALLBACK periodicTimerCallback(PVOID param, BOOLEAN timerFired) {
    (void)timerFired;
    // param encodes channel slot << 16 | periodic slot
    // TODO: implement actual CAN write in callback
}

long HandleManager::startPeriodic(unsigned long channelId, PASSTHRU_MSG *pMsg,
                                  unsigned long *pMsgId, unsigned long interval) {
    ChannelState *ch = getChannel(channelId);
    if (!ch) return ERR_INVALID_CHANNEL_ID;

    int slot = -1;
    for (int i = 0; i < MAX_PERIODIC; i++) {
        if (!ch->periodic[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        setLastError("Maximum periodic messages reached");
        return ERR_EXCEEDED_LIMIT;
    }

    ch->periodic[slot].active = true;
    ch->periodic[slot].intervalMs = interval;
    memcpy(&ch->periodic[slot].msg, pMsg, sizeof(PASSTHRU_MSG));

    // Create a timer
    HANDLE timer = NULL;
    CreateTimerQueueTimer(&timer, NULL, periodicTimerCallback,
                          (PVOID)(ULONG_PTR)((channelId << 16) | slot),
                          interval, interval, WT_EXECUTEDEFAULT);
    ch->periodic[slot].timerHandle = timer;

    *pMsgId = ch->nextPeriodicId++;
    return STATUS_NOERROR;
}

long HandleManager::stopPeriodic(unsigned long channelId, unsigned long msgId) {
    ChannelState *ch = getChannel(channelId);
    if (!ch) return ERR_INVALID_CHANNEL_ID;

    // Find by sequential search (msgIds are small sequential numbers)
    for (int i = 0; i < MAX_PERIODIC; i++) {
        if (ch->periodic[i].active) {
            if (ch->periodic[i].timerHandle)
                DeleteTimerQueueTimer(NULL, ch->periodic[i].timerHandle, NULL);
            ch->periodic[i].active = false;
            return STATUS_NOERROR;
        }
    }
    return ERR_INVALID_MSG_ID;
}
