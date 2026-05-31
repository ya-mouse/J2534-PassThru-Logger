#include "J2534Defs.h"
#include "CanlibLoader.h"
#include "HandleManager.h"
#include "Config.h"
#include <string.h>
#include <stdio.h>

extern CanlibApi g_canlib;
extern HandleManager g_handleMgr;

// VBATT reading via kvIo or mock
static long readVbatt(ChannelState *ch, unsigned long *pOutput) {
    if (!pOutput) return ERR_NULL_PARAMETER;

    // Try kvIo analog pin if configured
    if (g_config.vbattSource == 1 || g_config.vbattSource == 2) {
        if (g_canlib.kvIoGetNumberOfPins && g_canlib.kvIoPinGetAnalog) {
            unsigned int pinCount = 0;
            canStatus st = g_canlib.kvIoGetNumberOfPins(ch->canHandle, &pinCount);
            if (st == canOK && pinCount > 0 && (unsigned int)g_config.vbattIoPin < pinCount) {
                // Confirm config if not already done (idempotent)
                if (g_canlib.kvIoConfirmConfig)
                    g_canlib.kvIoConfirmConfig(ch->canHandle);

                float voltage = 0.0f;
                st = g_canlib.kvIoPinGetAnalog(ch->canHandle,
                                               (unsigned int)g_config.vbattIoPin, &voltage);
                if (st == canOK) {
                    *pOutput = (unsigned long)(voltage * 1000.0f);
                    return STATUS_NOERROR;
                }
            }
            // If vbattSource==1 (kvIo only) and failed, return error
            if (g_config.vbattSource == 1) {
                setLastError("kvIoPinGetAnalog failed: no I/O module or pin unavailable");
                return ERR_NOT_SUPPORTED;
            }
        } else if (g_config.vbattSource == 1) {
            setLastError("kvIo functions not available in canlib32.dll");
            return ERR_NOT_SUPPORTED;
        }
    }

    // Fall back to mock value
    *pOutput = g_config.mockVbattMv;
    return STATUS_NOERROR;
}

//
// === J2534 API Implementation ===
//

KD_API long PTAPI PassThruOpen(void *pName, unsigned long *pDeviceID) {
    if (!g_initialized) {
        setLastError("canlib32.dll not loaded");
        return ERR_DEVICE_NOT_CONNECTED;
    }
    if (!pDeviceID) return ERR_NULL_PARAMETER;

    const char *name = (const char *)pName;  // NULL is valid (use default)
    return g_handleMgr.openDevice(name, pDeviceID);
}

KD_API long PTAPI PassThruClose(unsigned long DeviceID) {
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    return g_handleMgr.closeDevice(DeviceID);
}

KD_API long PTAPI PassThruConnect(unsigned long DeviceID, unsigned long ProtocolID,
                                   unsigned long Flags, unsigned long BaudRate,
                                   unsigned long *pChannelID) {
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    if (!pChannelID) return ERR_NULL_PARAMETER;

    // Validate protocol
    if (ProtocolID != J2534_CAN && ProtocolID != J2534_ISO15765 &&
        (ProtocolID < J2534_FD_CAN_CH1 || ProtocolID > J2534_FD_CAN_CH1 + 127) &&
        (ProtocolID < J2534_FD_ISO15765_CH1 || ProtocolID > J2534_FD_ISO15765_CH1 + 127)) {
        setLastError("Unsupported protocol 0x%lX (only CAN/ISO15765 supported)", ProtocolID);
        return ERR_INVALID_PROTOCOL_ID;
    }

    return g_handleMgr.openChannel(DeviceID, ProtocolID, Flags, BaudRate, pChannelID);
}

KD_API long PTAPI PassThruDisconnect(unsigned long ChannelID) {
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    return g_handleMgr.closeChannel(ChannelID);
}

KD_API long PTAPI PassThruReadMsgs(unsigned long ChannelID, PASSTHRU_MSG *pMsg,
                                    unsigned long *pNumMsgs, unsigned long Timeout) {
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    if (!pMsg || !pNumMsgs) return ERR_NULL_PARAMETER;

    ChannelState *ch = g_handleMgr.getChannel(ChannelID);
    if (!ch) return ERR_INVALID_CHANNEL_ID;

    unsigned long requested = *pNumMsgs;
    unsigned long received = 0;

    // For ISO15765 channels, reading is handled by the ISO-TP engine
    // For raw CAN, read directly from canlib
    if (ch->protocolId == J2534_CAN ||
        (ch->protocolId >= J2534_FD_CAN_CH1 && ch->protocolId <= J2534_FD_CAN_CH1 + 127)) {
        // Raw CAN read
        for (unsigned long i = 0; i < requested; i++) {
            long id = 0;
            unsigned char data[64];
            unsigned int dlc = 0, flag = 0;
            unsigned long timestamp = 0;

            canStatus st;
            if (i == 0 && Timeout > 0) {
                st = g_canlib.canReadWait(ch->canHandle, &id, data, &dlc, &flag, &timestamp, Timeout);
            } else {
                st = g_canlib.canRead(ch->canHandle, &id, data, &dlc, &flag, &timestamp);
            }

            if (st != canOK) break;

            // Skip error frames unless configured to report them
            if (flag & canMSG_ERROR_FRAME) continue;

            memset(&pMsg[received], 0, sizeof(PASSTHRU_MSG));
            pMsg[received].ProtocolID = ch->protocolId;
            pMsg[received].Timestamp = timestamp;
            pMsg[received].DataSize = 4 + dlc;  // 4 bytes CAN ID + data
            pMsg[received].ExtraDataIndex = pMsg[received].DataSize;

            // Pack CAN ID into first 4 bytes (big-endian per J2534 spec)
            pMsg[received].Data[0] = (unsigned char)((id >> 24) & 0xFF);
            pMsg[received].Data[1] = (unsigned char)((id >> 16) & 0xFF);
            pMsg[received].Data[2] = (unsigned char)((id >> 8) & 0xFF);
            pMsg[received].Data[3] = (unsigned char)(id & 0xFF);
            memcpy(&pMsg[received].Data[4], data, dlc);

            if (flag & canMSG_EXT)
                pMsg[received].RxStatus |= CAN_29BIT_ID;
            if (flag & canMSG_TXACK)
                pMsg[received].RxStatus |= TX_MSG_TYPE;

            received++;
        }
    } else {
        // ISO-TP read via engine
        if (!ch->isoTpEngine) {
            setLastError("ISO-TP engine not initialized");
            *pNumMsgs = 0;
            return ERR_FAILED;
        }
        *pNumMsgs = requested;
        return ch->isoTpEngine->receive(pMsg, pNumMsgs, Timeout);
    }

    *pNumMsgs = received;
    if (received == 0) return ERR_BUFFER_EMPTY;
    if (received < requested) return ERR_TIMEOUT;
    return STATUS_NOERROR;
}

KD_API long PTAPI PassThruWriteMsgs(unsigned long ChannelID, PASSTHRU_MSG *pMsg,
                                     unsigned long *pNumMsgs, unsigned long Timeout) {
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    if (!pMsg || !pNumMsgs) return ERR_NULL_PARAMETER;

    ChannelState *ch = g_handleMgr.getChannel(ChannelID);
    if (!ch) return ERR_INVALID_CHANNEL_ID;

    unsigned long count = *pNumMsgs;
    unsigned long sent = 0;

    for (unsigned long i = 0; i < count; i++) {
        if (pMsg[i].DataSize < 4) {
            *pNumMsgs = sent;
            return ERR_INVALID_MSG;
        }

        // Extract CAN ID from first 4 bytes (big-endian)
        long id = ((long)pMsg[i].Data[0] << 24) |
                  ((long)pMsg[i].Data[1] << 16) |
                  ((long)pMsg[i].Data[2] << 8) |
                  (long)pMsg[i].Data[3];

        unsigned int dlc = pMsg[i].DataSize - 4;
        unsigned int flag = 0;

        if (pMsg[i].TxFlags & CAN_29BIT_ID)
            flag |= canMSG_EXT;

        if (ch->protocolId == J2534_CAN ||
            (ch->protocolId >= J2534_FD_CAN_CH1 && ch->protocolId <= J2534_FD_CAN_CH1 + 127)) {
            // Raw CAN write
            canStatus st = g_canlib.canWrite(ch->canHandle, id, &pMsg[i].Data[4], dlc, flag);
            if (st != canOK) {
                setLastError("canWrite failed: %d", st);
                break;
            }
        } else {
            // ISO-TP write via engine
            if (!ch->isoTpEngine) {
                setLastError("ISO-TP engine not initialized");
                break;
            }
            // Extract CAN ID for routing
            unsigned long txId = ((unsigned long)pMsg[i].Data[0] << 24) |
                                 ((unsigned long)pMsg[i].Data[1] << 16) |
                                 ((unsigned long)pMsg[i].Data[2] << 8) |
                                 (unsigned long)pMsg[i].Data[3];
            unsigned int txCanFlags = (pMsg[i].TxFlags & CAN_29BIT_ID) ? canMSG_EXT : 0;
            unsigned long payloadLen = pMsg[i].DataSize - 4;

            long result = ch->isoTpEngine->transmit(txId, txCanFlags,
                                                     &pMsg[i].Data[4], payloadLen,
                                                     Timeout);
            if (result != STATUS_NOERROR) {
                *pNumMsgs = sent;
                return result;
            }
        }
        sent++;
    }

    *pNumMsgs = sent;
    if (sent == 0) return ERR_FAILED;
    if (sent < count) return ERR_TIMEOUT;

    // Wait for TX completion if timeout specified
    if (Timeout > 0 && sent > 0) {
        g_canlib.canWriteSync(ch->canHandle, Timeout);
    }

    return STATUS_NOERROR;
}

KD_API long PTAPI PassThruStartPeriodicMsg(unsigned long ChannelID, PASSTHRU_MSG *pMsg,
                                            unsigned long *pMsgID, unsigned long TimeInterval) {
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    if (!pMsg || !pMsgID) return ERR_NULL_PARAMETER;
    if (TimeInterval < 5 || TimeInterval > 65535) return ERR_INVALID_TIME_INTERVAL;

    return g_handleMgr.startPeriodic(ChannelID, pMsg, pMsgID, TimeInterval);
}

KD_API long PTAPI PassThruStopPeriodicMsg(unsigned long ChannelID, unsigned long MsgID) {
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    return g_handleMgr.stopPeriodic(ChannelID, MsgID);
}

KD_API long PTAPI PassThruStartMsgFilter(unsigned long ChannelID, unsigned long FilterType,
                                          PASSTHRU_MSG *pMaskMsg, PASSTHRU_MSG *pPatternMsg,
                                          PASSTHRU_MSG *pFlowControlMsg, unsigned long *pFilterID) {
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    if (!pFilterID) return ERR_NULL_PARAMETER;
    if (FilterType == FLOW_CONTROL_FILTER && !pFlowControlMsg) return ERR_NULL_PARAMETER;
    if (!pMaskMsg || !pPatternMsg) return ERR_NULL_PARAMETER;

    return g_handleMgr.addFilter(ChannelID, FilterType, pMaskMsg, pPatternMsg,
                                  pFlowControlMsg, pFilterID);
}

KD_API long PTAPI PassThruStopMsgFilter(unsigned long ChannelID, unsigned long FilterID) {
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    return g_handleMgr.removeFilter(ChannelID, FilterID);
}

KD_API long PTAPI PassThruSetProgrammingVoltage(unsigned long DeviceID,
                                                 unsigned long PinNumber, unsigned long Voltage) {
    // Kvaser interfaces are not J1962 connectors — not supported
    setLastError("SetProgrammingVoltage not supported on Kvaser interfaces");
    return ERR_NOT_SUPPORTED;
}

KD_API long PTAPI PassThruReadVersion(unsigned long DeviceID, char *pFirmwareVersion,
                                       char *pDllVersion, char *pApiVersion) {
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;

    DeviceState *dev = g_handleMgr.getDevice(DeviceID);
    if (!dev) return ERR_INVALID_DEVICE_ID;

    // Firmware version from CANlib channel data
    if (pFirmwareVersion) {
        unsigned short fwRev[4] = {0};
        if (g_canlib.canGetChannelData(g_config.canlibChannel,
                canCHANNELDATA_CARD_FIRMWARE_REV, fwRev, sizeof(fwRev)) == canOK) {
            snprintf(pFirmwareVersion, 80, "%d.%d.%d", fwRev[3], fwRev[2], fwRev[1]);
        } else {
            strncpy(pFirmwareVersion, "0.0.0", 80);
        }
    }

    if (pDllVersion)
        strncpy(pDllVersion, "1.0.0", 80);
    if (pApiVersion)
        strncpy(pApiVersion, "04.04", 80);

    return STATUS_NOERROR;
}

KD_API long PTAPI PassThruGetLastError(char *pErrorDescription) {
    if (!pErrorDescription) return ERR_NULL_PARAMETER;
    strncpy(pErrorDescription, getLastError(), 80);
    return STATUS_NOERROR;
}

KD_API long PTAPI PassThruIoctl(unsigned long ChannelID, unsigned long IoctlID,
                                 void *pInput, void *pOutput) {
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;

    ChannelState *ch = g_handleMgr.getChannel(ChannelID);
    if (!ch) return ERR_INVALID_CHANNEL_ID;

    switch (IoctlID) {
    case READ_VBATT:
        return readVbatt(ch, (unsigned long *)pOutput);

    case CLEAR_TX_BUFFER:
        g_canlib.canIoCtl(ch->canHandle, canIOCTL_FLUSH_TX_BUFFER, NULL, 0);
        return STATUS_NOERROR;

    case CLEAR_RX_BUFFER:
        g_canlib.canIoCtl(ch->canHandle, canIOCTL_FLUSH_RX_BUFFER, NULL, 0);
        return STATUS_NOERROR;

    case CLEAR_MSG_FILTERS:
        // Clear all filters for this channel
        for (int i = 0; i < MAX_FILTERS; i++)
            ch->filters[i].active = false;
        return STATUS_NOERROR;

    case CLEAR_PERIODIC_MSGS:
        // Stop all periodic messages for this channel
        for (int i = 0; i < MAX_PERIODIC; i++) {
            if (ch->periodic[i].active) {
                if (ch->periodic[i].timerHandle)
                    DeleteTimerQueueTimer(NULL, ch->periodic[i].timerHandle, NULL);
                ch->periodic[i].active = false;
            }
        }
        return STATUS_NOERROR;

    case GET_CONFIG: {
        if (!pInput) return ERR_NULL_PARAMETER;
        SCONFIG_LIST *list = (SCONFIG_LIST *)pInput;
        for (unsigned long i = 0; i < list->NumOfParams; i++) {
            switch (list->ConfigPtr[i].Parameter) {
            case DATA_RATE:         list->ConfigPtr[i].Value = ch->baudRate; break;
            case LOOPBACK:          list->ConfigPtr[i].Value = ch->loopback; break;
            case ISO15765_BS:       list->ConfigPtr[i].Value = ch->iso15765_bs; break;
            case ISO15765_STMIN:    list->ConfigPtr[i].Value = ch->iso15765_stmin; break;
            case ISO15765_BS_TX:    list->ConfigPtr[i].Value = ch->iso15765_bs_tx; break;
            case ISO15765_STMIN_TX: list->ConfigPtr[i].Value = ch->iso15765_stmin_tx; break;
            case ISO15765_WFT_MAX:  list->ConfigPtr[i].Value = ch->iso15765_wft_max; break;
            default:
                setLastError("Unsupported config parameter 0x%lX", list->ConfigPtr[i].Parameter);
                return ERR_INVALID_IOCTL_VALUE;
            }
        }
        return STATUS_NOERROR;
    }

    case SET_CONFIG: {
        if (!pInput) return ERR_NULL_PARAMETER;
        SCONFIG_LIST *list = (SCONFIG_LIST *)pInput;
        for (unsigned long i = 0; i < list->NumOfParams; i++) {
            switch (list->ConfigPtr[i].Parameter) {
            case DATA_RATE:         ch->baudRate = list->ConfigPtr[i].Value; break;
            case LOOPBACK:          ch->loopback = list->ConfigPtr[i].Value; break;
            case ISO15765_BS:       ch->iso15765_bs = list->ConfigPtr[i].Value; break;
            case ISO15765_STMIN:    ch->iso15765_stmin = list->ConfigPtr[i].Value; break;
            case ISO15765_BS_TX:    ch->iso15765_bs_tx = list->ConfigPtr[i].Value; break;
            case ISO15765_STMIN_TX: ch->iso15765_stmin_tx = list->ConfigPtr[i].Value; break;
            case ISO15765_WFT_MAX:  ch->iso15765_wft_max = list->ConfigPtr[i].Value; break;
            case ISO15765_PAD_VALUE: ch->iso15765_pad_value = list->ConfigPtr[i].Value; break;
            case CAN_MIXED_FORMAT:  ch->canMixedFormat = list->ConfigPtr[i].Value; break;
            default:
                setLastError("Unsupported config parameter 0x%lX", list->ConfigPtr[i].Parameter);
                return ERR_INVALID_IOCTL_VALUE;
            }
        }
        // Propagate updated ISO-TP config to engine
        if (ch->isoTpEngine) {
            ch->isoTpEngine->setConfig(ch->iso15765_bs_tx, ch->iso15765_stmin_tx,
                                        ch->iso15765_pad_value, ch->iso15765_wft_max);
        }
        return STATUS_NOERROR;
    }

    case READ_PROG_VOLTAGE:
        setLastError("ReadProgVoltage not supported on Kvaser interfaces");
        return ERR_NOT_SUPPORTED;

    default:
        setLastError("Unsupported IoctlID 0x%lX", IoctlID);
        return ERR_INVALID_IOCTL_ID;
    }
}
