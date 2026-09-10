// ElmJ2534 — the 14 J2534 v04.04 exports
//
// Implements: docs/elm-j2534-design.md → "J2534 mapping". Thin wrappers around
// g_device, mirroring ReplayJ2534/J2534Api.cpp: log the entry, guard on
// g_initialized, reject NULL pointers here (so ElmDevice never has to), log the
// return code, and hex-dump payloads at debug level.
//
// Everything that touches the adapter lives in ElmDevice; nothing in this file
// blocks on hardware except through it.

#include "J2534Defs.h"
#include "ElmDevice.h"
#include "Logger.h"
#include "Config.h"

#include <stdio.h>
#include <string.h>

// Global device (defined in dllmain.cpp)
extern ElmDevice g_device;

ELM_API long PTAPI PassThruOpen(void *pName, unsigned long *pDeviceID) {
    g_logger.apiEntry("PassThruOpen", "name=%s",
                      pName ? (const char *)pName : "NULL");

    if (!g_initialized) {
        setLastError("ElmJ2534 not initialized");
        g_logger.apiReturn("PassThruOpen", ERR_DEVICE_NOT_CONNECTED);
        return ERR_DEVICE_NOT_CONNECTED;
    }
    if (!pDeviceID) return ERR_NULL_PARAMETER;

    // This is where the Bluetooth SPP connect + ELM handshake happen, and it
    // can take the full open timeout (15 s by default) — deliberately NOT in
    // DllMain, where the loader lock would make it a process-wide hang.
    long ret = g_device.openDevice(pName, pDeviceID);
    g_logger.apiReturn("PassThruOpen", ret);
    if (ret == STATUS_NOERROR)
        g_logger.verbose("  DeviceID=%lu", *pDeviceID);
    return ret;
}

ELM_API long PTAPI PassThruClose(unsigned long DeviceID) {
    g_logger.apiEntry("PassThruClose", "DeviceID=%lu", DeviceID);
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    long ret = g_device.closeDevice(DeviceID);
    g_logger.apiReturn("PassThruClose", ret);
    return ret;
}

ELM_API long PTAPI PassThruConnect(unsigned long DeviceID, unsigned long ProtocolID,
                                   unsigned long Flags, unsigned long BaudRate,
                                   unsigned long *pChannelID) {
    g_logger.apiEntry("PassThruConnect", "DeviceID=%lu, Proto=%s(0x%lX), Flags=0x%lX, Baud=%lu",
                      DeviceID, elProtocolName(ProtocolID), ProtocolID, Flags, BaudRate);

    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    if (!pChannelID) return ERR_NULL_PARAMETER;

    long ret = g_device.connect(DeviceID, ProtocolID, Flags, BaudRate, pChannelID);
    g_logger.apiReturn("PassThruConnect", ret);
    if (ret == STATUS_NOERROR)
        g_logger.verbose("  ChannelID=%lu", *pChannelID);
    return ret;
}

ELM_API long PTAPI PassThruDisconnect(unsigned long ChannelID) {
    g_logger.apiEntry("PassThruDisconnect", "ChannelID=%lu", ChannelID);
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    long ret = g_device.disconnect(ChannelID);
    g_logger.apiReturn("PassThruDisconnect", ret);
    return ret;
}

ELM_API long PTAPI PassThruReadMsgs(unsigned long ChannelID, PASSTHRU_MSG *pMsg,
                                    unsigned long *pNumMsgs, unsigned long Timeout) {
    g_logger.apiEntry("PassThruReadMsgs", "Ch=%lu, NumMsgs=%lu, Timeout=%lu",
                      ChannelID, pNumMsgs ? *pNumMsgs : 0, Timeout);

    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    if (!pMsg || !pNumMsgs) return ERR_NULL_PARAMETER;

    long ret = g_device.readMsgs(ChannelID, pMsg, pNumMsgs, Timeout);
    g_logger.apiReturn("PassThruReadMsgs", ret);
    if (ret == STATUS_NOERROR || *pNumMsgs > 0) {
        g_logger.verbose("  received %lu msgs", *pNumMsgs);
        if (g_logger.isDebug()) {
            for (unsigned long m = 0; m < *pNumMsgs; m++)
                g_logger.hexDump("  RX", pMsg[m].Data, pMsg[m].DataSize);
        }
    }
    return ret;
}

ELM_API long PTAPI PassThruWriteMsgs(unsigned long ChannelID, PASSTHRU_MSG *pMsg,
                                     unsigned long *pNumMsgs, unsigned long Timeout) {
    g_logger.apiEntry("PassThruWriteMsgs", "Ch=%lu, NumMsgs=%lu, Timeout=%lu",
                      ChannelID, pNumMsgs ? *pNumMsgs : 0, Timeout);

    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    if (!pMsg || !pNumMsgs) return ERR_NULL_PARAMETER;

    if (g_logger.isDebug()) {
        for (unsigned long m = 0; m < *pNumMsgs; m++)
            g_logger.hexDump("  TX", pMsg[m].Data, pMsg[m].DataSize);
    }

    // Synchronous: the ELM exchange (and every recovery it needs) completes
    // before this returns, so the responses are already queued.
    long ret = g_device.writeMsgs(ChannelID, pMsg, pNumMsgs, Timeout);
    g_logger.apiReturn("PassThruWriteMsgs", ret);
    return ret;
}

ELM_API long PTAPI PassThruStartPeriodicMsg(unsigned long ChannelID, PASSTHRU_MSG *pMsg,
                                            unsigned long *pMsgID, unsigned long TimeInterval) {
    g_logger.apiEntry("PassThruStartPeriodicMsg", "Ch=%lu, Interval=%lu",
                      ChannelID, TimeInterval);

    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    if (!pMsg || !pMsgID) return ERR_NULL_PARAMETER;
    if (TimeInterval < 5 || TimeInterval > 65535) return ERR_INVALID_TIME_INTERVAL;

    static unsigned long nextPeriodicId = 1;
    *pMsgID = nextPeriodicId++;
    g_logger.verbose("  PeriodicMsg accepted (MsgID=%lu) — no-op: the ELM327 has "
                     "no free-running TX, every frame costs one host exchange",
                     *pMsgID);
    return STATUS_NOERROR;
}

ELM_API long PTAPI PassThruStopPeriodicMsg(unsigned long ChannelID, unsigned long MsgID) {
    g_logger.apiEntry("PassThruStopPeriodicMsg", "Ch=%lu, MsgID=%lu", ChannelID, MsgID);
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    g_logger.verbose("  PeriodicMsg stopped (MsgID=%lu) — no-op", MsgID);
    return STATUS_NOERROR;
}

ELM_API long PTAPI PassThruStartMsgFilter(unsigned long ChannelID, unsigned long FilterType,
                                          PASSTHRU_MSG *pMaskMsg, PASSTHRU_MSG *pPatternMsg,
                                          PASSTHRU_MSG *pFlowControlMsg, unsigned long *pFilterID) {
    g_logger.apiEntry("PassThruStartMsgFilter", "Ch=%lu, Type=%s",
                      ChannelID, elFilterTypeName(FilterType));

    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    if (!pFilterID) return ERR_NULL_PARAMETER;
    // Project convention (P-LIVE-002): a missing mask/pattern is a NULL
    // parameter, not "match everything".
    if (!pMaskMsg || !pPatternMsg) return ERR_NULL_PARAMETER;
    if (FilterType == FLOW_CONTROL_FILTER && !pFlowControlMsg) return ERR_NULL_PARAMETER;

    long ret = g_device.startMsgFilter(ChannelID, FilterType, pMaskMsg,
                                       pPatternMsg, pFlowControlMsg, pFilterID);
    g_logger.apiReturn("PassThruStartMsgFilter", ret);
    if (ret == STATUS_NOERROR)
        g_logger.verbose("  FilterID=%lu", *pFilterID);
    return ret;
}

ELM_API long PTAPI PassThruStopMsgFilter(unsigned long ChannelID, unsigned long FilterID) {
    g_logger.apiEntry("PassThruStopMsgFilter", "Ch=%lu, FilterID=%lu", ChannelID, FilterID);
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    long ret = g_device.stopMsgFilter(ChannelID, FilterID);
    g_logger.apiReturn("PassThruStopMsgFilter", ret);
    return ret;
}

ELM_API long PTAPI PassThruSetProgrammingVoltage(unsigned long DeviceID,
                                                 unsigned long PinNumber,
                                                 unsigned long Voltage) {
    g_logger.apiEntry("PassThruSetProgrammingVoltage", "Device=%lu Pin=%lu Voltage=%lu",
                      DeviceID, PinNumber, Voltage);
    // The ELM327 has no programmable supply on the OBD pins.
    setLastError("SetProgrammingVoltage not supported: the ELM327 has no "
                 "controllable pin voltage");
    g_logger.apiReturn("PassThruSetProgrammingVoltage", ERR_NOT_SUPPORTED);
    return ERR_NOT_SUPPORTED;
}

ELM_API long PTAPI PassThruReadVersion(unsigned long DeviceID, char *pFirmwareVersion,
                                       char *pDllVersion, char *pApiVersion) {
    g_logger.apiEntry("PassThruReadVersion", "DeviceID=%lu", DeviceID);
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;

    long ret = g_device.readVersion(DeviceID, pFirmwareVersion, pDllVersion,
                                    pApiVersion);
    g_logger.apiReturn("PassThruReadVersion", ret);
    if (ret == STATUS_NOERROR)
        g_logger.verbose("  firmware='%s' dll='%s' api='%s'",
                         pFirmwareVersion ? pFirmwareVersion : "",
                         pDllVersion ? pDllVersion : "",
                         pApiVersion ? pApiVersion : "");
    return ret;
}

ELM_API long PTAPI PassThruGetLastError(char *pErrorDescription) {
    if (!pErrorDescription) return ERR_NULL_PARAMETER;
    // The thread-local string carries the detail for the calling thread; the
    // device copy is the fallback for a call from a thread that never made one.
    const char *tlsErr = getLastError();
    if (tlsErr[0]) {
        strncpy(pErrorDescription, tlsErr, ELM_LAST_ERROR_LEN - 1);
        pErrorDescription[ELM_LAST_ERROR_LEN - 1] = '\0';
        return STATUS_NOERROR;
    }
    return g_device.getLastError(pErrorDescription);
}

ELM_API long PTAPI PassThruIoctl(unsigned long ChannelID, unsigned long IoctlID,
                                 void *pInput, void *pOutput) {
    g_logger.apiEntry("PassThruIoctl", "Ch=%lu, Ioctl=%s(0x%lX)",
                      ChannelID, elIoctlName(IoctlID), IoctlID);

    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;

    long ret = g_device.ioctl(ChannelID, IoctlID, pInput, pOutput);

    if (IoctlID == READ_VBATT && ret == STATUS_NOERROR && pOutput)
        g_logger.verbose("  VBATT=%lu mV", *(unsigned long *)pOutput);

    g_logger.apiReturn("PassThruIoctl", ret);
    return ret;
}
