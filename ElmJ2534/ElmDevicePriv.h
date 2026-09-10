#pragma once
// ElmJ2534 — internals shared by the three ElmDevice translation units
//
// ElmDevice is split across three TUs to stay inside the project's 500-LOC
// file budget: ElmDevice.cpp (lifecycle, channels, versioning),
// ElmDeviceMsgs.cpp (WriteMsgs/ReadMsgs, exchange recovery, RX queue) and
// ElmDeviceFilters.cpp (message filters, ioctl). Nothing here is part of the
// DLL's surface — all three implement the same class, and this header holds
// the byte/id plumbing and the logging shim they share.

#include "ElmDevice.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include "Logger.h"
#define ELM_LOGV(...) g_logger.verbose(__VA_ARGS__)
#define ELM_LOGD(...) g_logger.debug(__VA_ARGS__)
#else
// Native builds stay sans-win32: Logger.h pulls in the real windows.h, and the
// host suites assert on return codes plus the fake link's command record.
#define ELM_LOGV(...) ((void)0)
#define ELM_LOGD(...) ((void)0)
#endif

// ISO15765 flow-control parameters are one byte on the wire (BS, STmin); a
// bigger value cannot be expressed in ATFCSD even if we chose to send one.
static const unsigned long ELM_FC_PARAM_MAX = 0xFF;

// J2534 ISO15765 carries the CAN id in Data[0..3], big-endian.
static inline uint32_t beIdFromData(const unsigned char *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static inline void putBeId(unsigned char *dst, uint32_t id) {
    dst[0] = (unsigned char)((id >> 24) & 0xFF);
    dst[1] = (unsigned char)((id >> 16) & 0xFF);
    dst[2] = (unsigned char)((id >> 8) & 0xFF);
    dst[3] = (unsigned char)(id & 0xFF);
}

// Filter messages for ISO15765 carry ONLY the id, so 4 bytes is the minimum.
static inline bool beIdFromMsg(const PASSTHRU_MSG &msg, uint32_t &out) {
    if (msg.DataSize < ELM_CAN_ID_BYTES) return false;
    out = beIdFromData(msg.Data);
    return true;
}

// 11-bit ids print as three hex digits — the shape every ELM command and every
// bench log uses ("7E8", not "000007E8").
static inline std::string idText(uint32_t id) {
    char buf[16];
    snprintf(buf, sizeof(buf), (id <= 0x7FF) ? "%03X" : "%08X", (unsigned)id);
    return std::string(buf);
}

static inline const char *recoveryName(ElmRecovery kind) {
    switch (kind) {
    case ElmRecovery::None:          return "None";
    case ElmRecovery::Retry:         return "Retry";
    case ElmRecovery::Wait:          return "Wait";
    case ElmRecovery::ResetProtocol: return "ResetProtocol";
    case ElmRecovery::WarmStart:     return "WarmStart";
    case ElmRecovery::Unsupported:   return "Unsupported";
    case ElmRecovery::Reinitialise:  return "Reinitialise";
    default:                         return "?";
    }
}

// J2534 fixes the ReadVersion buffers at 80 bytes: fill 79 and terminate, the
// convention Simulator.cpp uses (strncpy alone may leave no NUL).
static inline void copyFixed(char *dst, const char *src) {
    if (!dst) return;
    strncpy(dst, src, ELM_VERSION_FIELD_LEN - 1);
    dst[ELM_VERSION_FIELD_LEN - 1] = '\0';
}
