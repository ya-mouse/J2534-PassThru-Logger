#include "J2534Defs.h"
#include "Simulator.h"
#include "Logger.h"
#include "Config.h"
#include <string.h>
#include <stdio.h>

// Global simulator (declared in dllmain.cpp)
extern Simulator g_simulator;

//
// === J2534 API — Scenario-Driven Implementation ===
//

KD_API long PTAPI PassThruOpen(void *pName, unsigned long *pDeviceID) {
    g_logger.apiEntry("PassThruOpen", "name=%s",
                      pName ? (const char*)pName : "NULL");

    if (!g_initialized) {
        setLastError("ReplayJ2534 not initialized");
        g_logger.apiReturn("PassThruOpen", ERR_DEVICE_NOT_CONNECTED);
        return ERR_DEVICE_NOT_CONNECTED;
    }
    if (!pDeviceID) return ERR_NULL_PARAMETER;

    long ret = g_simulator.openDevice(pName, pDeviceID);
    g_logger.apiReturn("PassThruOpen", ret);
    if (ret == STATUS_NOERROR)
        g_logger.verbose("  DeviceID=%lu", *pDeviceID);
    return ret;
}

KD_API long PTAPI PassThruClose(unsigned long DeviceID) {
    g_logger.apiEntry("PassThruClose", "DeviceID=%lu", DeviceID);
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    long ret = g_simulator.closeDevice(DeviceID);
    g_logger.apiReturn("PassThruClose", ret);
    return ret;
}

KD_API long PTAPI PassThruConnect(unsigned long DeviceID, unsigned long ProtocolID,
                                   unsigned long Flags, unsigned long BaudRate,
                                   unsigned long *pChannelID) {
    g_logger.apiEntry("PassThruConnect", "DeviceID=%lu, Proto=%s(0x%lX), Flags=0x%lX, Baud=%lu",
                      DeviceID, rjProtocolName(ProtocolID), ProtocolID, Flags, BaudRate);

    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    if (!pChannelID) return ERR_NULL_PARAMETER;

    long ret = g_simulator.connect(DeviceID, ProtocolID, Flags, BaudRate, pChannelID);
    g_logger.apiReturn("PassThruConnect", ret);
    if (ret == STATUS_NOERROR)
        g_logger.verbose("  ChannelID=%lu", *pChannelID);
    return ret;
}

KD_API long PTAPI PassThruDisconnect(unsigned long ChannelID) {
    g_logger.apiEntry("PassThruDisconnect", "ChannelID=%lu", ChannelID);
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    long ret = g_simulator.disconnect(ChannelID);
    g_logger.apiReturn("PassThruDisconnect", ret);
    return ret;
}

KD_API long PTAPI PassThruReadMsgs(unsigned long ChannelID, PASSTHRU_MSG *pMsg,
                                    unsigned long *pNumMsgs, unsigned long Timeout) {
    g_logger.apiEntry("PassThruReadMsgs", "Ch=%lu, NumMsgs=%lu, Timeout=%lu",
                      ChannelID, pNumMsgs ? *pNumMsgs : 0, Timeout);

    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    if (!pMsg || !pNumMsgs) return ERR_NULL_PARAMETER;

    long ret = g_simulator.readMsgs(ChannelID, pMsg, pNumMsgs, Timeout);
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

KD_API long PTAPI PassThruWriteMsgs(unsigned long ChannelID, PASSTHRU_MSG *pMsg,
                                     unsigned long *pNumMsgs, unsigned long Timeout) {
    g_logger.apiEntry("PassThruWriteMsgs", "Ch=%lu, NumMsgs=%lu, Timeout=%lu",
                      ChannelID, pNumMsgs ? *pNumMsgs : 0, Timeout);

    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    if (!pMsg || !pNumMsgs) return ERR_NULL_PARAMETER;

    if (g_logger.isDebug()) {
        for (unsigned long m = 0; m < *pNumMsgs; m++)
            g_logger.hexDump("  TX", pMsg[m].Data, pMsg[m].DataSize);
    }

    long ret = g_simulator.writeMsgs(ChannelID, pMsg, pNumMsgs, Timeout);
    g_logger.apiReturn("PassThruWriteMsgs", ret);
    return ret;
}

KD_API long PTAPI PassThruStartPeriodicMsg(unsigned long ChannelID, PASSTHRU_MSG *pMsg,
                                            unsigned long *pMsgID, unsigned long TimeInterval) {
    g_logger.apiEntry("PassThruStartPeriodicMsg", "Ch=%lu, Interval=%lu", ChannelID, TimeInterval);

    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    if (!pMsg || !pMsgID) return ERR_NULL_PARAMETER;
    if (TimeInterval < 5 || TimeInterval > 65535) return ERR_INVALID_TIME_INTERVAL;

    static unsigned long nextPeriodicId = 1;
    *pMsgID = nextPeriodicId++;
    g_logger.verbose("  PeriodicMsg accepted (MsgID=%lu) - no-op in replay mode", *pMsgID);
    return STATUS_NOERROR;
}

KD_API long PTAPI PassThruStopPeriodicMsg(unsigned long ChannelID, unsigned long MsgID) {
    g_logger.apiEntry("PassThruStopPeriodicMsg", "Ch=%lu, MsgID=%lu", ChannelID, MsgID);
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    g_logger.verbose("  PeriodicMsg stopped (MsgID=%lu) - no-op in replay mode", MsgID);
    return STATUS_NOERROR;
}

KD_API long PTAPI PassThruStartMsgFilter(unsigned long ChannelID, unsigned long FilterType,
                                          PASSTHRU_MSG *pMaskMsg, PASSTHRU_MSG *pPatternMsg,
                                          PASSTHRU_MSG *pFlowControlMsg, unsigned long *pFilterID) {
    const char *ftName = (FilterType == PASS_FILTER) ? "PASS" :
                         (FilterType == BLOCK_FILTER) ? "BLOCK" :
                         (FilterType == FLOW_CONTROL_FILTER) ? "FLOW_CONTROL" : "?";
    g_logger.apiEntry("PassThruStartMsgFilter", "Ch=%lu, Type=%s", ChannelID, ftName);

    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    if (!pFilterID) return ERR_NULL_PARAMETER;
    if (!pMaskMsg || !pPatternMsg) return ERR_NULL_PARAMETER;
    if (FilterType == FLOW_CONTROL_FILTER && !pFlowControlMsg) return ERR_NULL_PARAMETER;

    long ret = g_simulator.startMsgFilter(ChannelID, FilterType, pMaskMsg,
                                            pPatternMsg, pFlowControlMsg, pFilterID);
    g_logger.apiReturn("PassThruStartMsgFilter", ret);
    if (ret == STATUS_NOERROR) {
        g_logger.verbose("  FilterID=%lu", *pFilterID);
    }
    return ret;
}

KD_API long PTAPI PassThruStopMsgFilter(unsigned long ChannelID, unsigned long FilterID) {
    g_logger.apiEntry("PassThruStopMsgFilter", "Ch=%lu, FilterID=%lu", ChannelID, FilterID);
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;
    long ret = g_simulator.stopMsgFilter(ChannelID, FilterID);
    g_logger.apiReturn("PassThruStopMsgFilter", ret);
    return ret;
}

KD_API long PTAPI PassThruSetProgrammingVoltage(unsigned long DeviceID,
                                                 unsigned long PinNumber,
                                                 unsigned long Voltage) {
    g_logger.apiEntry("PassThruSetProgrammingVoltage",
                      "Device=%lu Pin=%lu Voltage=%lu", DeviceID, PinNumber, Voltage);
    setLastError("SetProgrammingVoltage not supported in replay mode");
    return ERR_NOT_SUPPORTED;
}

KD_API long PTAPI PassThruReadVersion(unsigned long DeviceID, char *pFirmwareVersion,
                                       char *pDllVersion, char *pApiVersion) {
    g_logger.apiEntry("PassThruReadVersion", "DeviceID=%lu", DeviceID);
    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;

    long ret = g_simulator.readVersion(DeviceID, pFirmwareVersion, pDllVersion, pApiVersion);
    g_logger.apiReturn("PassThruReadVersion", ret);
    return ret;
}

KD_API long PTAPI PassThruGetLastError(char *pErrorDescription) {
    if (!pErrorDescription) return ERR_NULL_PARAMETER;
    const char *tlsErr = getLastError();
    if (tlsErr[0]) {
        strncpy(pErrorDescription, tlsErr, 80);
        pErrorDescription[79] = '\0';
        return STATUS_NOERROR;
    }
    return g_simulator.getLastError(pErrorDescription);
}

KD_API long PTAPI PassThruIoctl(unsigned long ChannelID, unsigned long IoctlID,
                                 void *pInput, void *pOutput) {
    g_logger.apiEntry("PassThruIoctl", "Ch=%lu, Ioctl=%s(0x%lX)",
                      ChannelID, rjIoctlName(IoctlID), IoctlID);

    if (!g_initialized) return ERR_DEVICE_NOT_CONNECTED;

    long ret = g_simulator.ioctl(ChannelID, IoctlID, pInput, pOutput);

    if (IoctlID == READ_VBATT && ret == STATUS_NOERROR && pOutput)
        g_logger.verbose("  VBATT=%lu mV", *(unsigned long*)pOutput);

    g_logger.apiReturn("PassThruIoctl", ret);
    return ret;
}
