#include "Logger.h"
#include "J2534Defs.h"
#include <stdio.h>
#include <string.h>

// Global logger instance
ReplayLogger g_logger;

//
// === Enum name lookups ===
//

const char* rjRetCodeName(long code) {
    switch (code) {
    case STATUS_NOERROR:            return "STATUS_NOERROR";
    case ERR_NOT_SUPPORTED:         return "ERR_NOT_SUPPORTED";
    case ERR_INVALID_CHANNEL_ID:    return "ERR_INVALID_CHANNEL_ID";
    case ERR_INVALID_PROTOCOL_ID:   return "ERR_INVALID_PROTOCOL_ID";
    case ERR_NULL_PARAMETER:        return "ERR_NULL_PARAMETER";
    case ERR_INVALID_IOCTL_VALUE:   return "ERR_INVALID_IOCTL_VALUE";
    case ERR_INVALID_FLAGS:         return "ERR_INVALID_FLAGS";
    case ERR_FAILED:                return "ERR_FAILED";
    case ERR_DEVICE_NOT_CONNECTED:  return "ERR_DEVICE_NOT_CONNECTED";
    case ERR_TIMEOUT:               return "ERR_TIMEOUT";
    case ERR_INVALID_MSG:           return "ERR_INVALID_MSG";
    case ERR_EXCEEDED_LIMIT:        return "ERR_EXCEEDED_LIMIT";
    case ERR_DEVICE_IN_USE:         return "ERR_DEVICE_IN_USE";
    case ERR_INVALID_IOCTL_ID:      return "ERR_INVALID_IOCTL_ID";
    case ERR_BUFFER_EMPTY:          return "ERR_BUFFER_EMPTY";
    case ERR_BUFFER_FULL:           return "ERR_BUFFER_FULL";
    case ERR_BUFFER_OVERFLOW:       return "ERR_BUFFER_OVERFLOW";
    case ERR_CHANNEL_IN_USE:        return "ERR_CHANNEL_IN_USE";
    case ERR_INVALID_FILTER_ID:     return "ERR_INVALID_FILTER_ID";
    case ERR_INVALID_BAUDRATE:      return "ERR_INVALID_BAUDRATE";
    case ERR_INVALID_DEVICE_ID:     return "ERR_INVALID_DEVICE_ID";
    default:                        return "UNKNOWN_ERROR";
    }
}

const char* rjProtocolName(unsigned long protocolId) {
    switch (protocolId) {
    case J2534_J1850VPW:        return "J1850VPW";
    case J2534_J1850PWM:        return "J1850PWM";
    case J2534_ISO9141:         return "ISO9141";
    case J2534_ISO14230:        return "ISO14230";
    case J2534_CAN:             return "CAN";
    case J2534_ISO15765:        return "ISO15765";
    case J2534_SCI_A_ENGINE:    return "SCI_A_ENGINE";
    case J2534_SCI_A_TRANS:     return "SCI_A_TRANS";
    case J2534_SCI_B_ENGINE:    return "SCI_B_ENGINE";
    case J2534_SCI_B_TRANS:     return "SCI_B_TRANS";
    default: {
        if (protocolId >= J2534_FD_CAN_CH1 && protocolId <= J2534_FD_CAN_CH1 + 127)
            return "FD_CAN";
        if (protocolId >= J2534_FD_ISO15765_CH1 && protocolId <= J2534_FD_ISO15765_CH1 + 127)
            return "FD_ISO15765";
        return "UNKNOWN_PROTO";
    }
    }
}

const char* rjIoctlName(unsigned long ioctlId) {
    switch (ioctlId) {
    case GET_CONFIG:    return "GET_CONFIG";
    case SET_CONFIG:    return "SET_CONFIG";
    case READ_VBATT:    return "READ_VBATT";
    case FIVE_BAUD_INIT: return "FIVE_BAUD_INIT";
    case FAST_INIT:     return "FAST_INIT";
    case CLEAR_TX_BUFFER: return "CLEAR_TX_BUFFER";
    case CLEAR_RX_BUFFER: return "CLEAR_RX_BUFFER";
    case CLEAR_PERIODIC_MSGS: return "CLEAR_PERIODIC_MSGS";
    case CLEAR_MSG_FILTERS: return "CLEAR_MSG_FILTERS";
    case CLEAR_FUNCT_MSG_LOOKUP_TABLE: return "CLEAR_FUNCT_MSG_LUT";
    case ADD_TO_FUNCT_MSG_LOOKUP_TABLE: return "ADD_FUNCT_MSG_LUT";
    case DELETE_FROM_FUNCT_MSG_LOOKUP_TABLE: return "DEL_FUNCT_MSG_LUT";
    case READ_PROG_VOLTAGE: return "READ_PROG_VOLTAGE";
    default:            return "UNKNOWN_IOCTL";
    }
}

const char* rjFilterTypeName(unsigned long filterType) {
    switch (filterType) {
    case PASS_FILTER:           return "PASS_FILTER";
    case BLOCK_FILTER:          return "BLOCK_FILTER";
    case FLOW_CONTROL_FILTER:   return "FLOW_CONTROL_FILTER";
    default:                    return "UNKNOWN_FILTER";
    }
}

//
// === ReplayLogger implementation ===
//

ReplayLogger::ReplayLogger()
    : level_(RLOG_OFF), hFile_(INVALID_HANDLE_VALUE), initialized_(false)
{
    InitializeCriticalSection(&lock_);
}

ReplayLogger::~ReplayLogger() {
    shutdown();
    DeleteCriticalSection(&lock_);
}

void ReplayLogger::init(int level, const char *filePath) {
    EnterCriticalSection(&lock_);
    level_ = level;

    if (level > RLOG_OFF && filePath && filePath[0]) {
        hFile_ = CreateFileA(filePath, FILE_APPEND_DATA,
                             FILE_SHARE_READ, NULL,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    initialized_ = true;
    LeaveCriticalSection(&lock_);
}

void ReplayLogger::shutdown() {
    EnterCriticalSection(&lock_);
    if (hFile_ != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile_);
        hFile_ = INVALID_HANDLE_VALUE;
    }
    level_ = RLOG_OFF;
    initialized_ = false;
    LeaveCriticalSection(&lock_);
}

void ReplayLogger::writeTimestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    int len = sprintf(buf, "[%02d:%02d:%02d.%03d] ",
                      st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    writeRaw(buf, len);
}

void ReplayLogger::writeRaw(const char *buf, int len) {
    if (hFile_ != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile_, buf, (DWORD)len, &written, NULL);
    }
    // Also write to stderr
    HANDLE hStderr = GetStdHandle(STD_ERROR_HANDLE);
    if (hStderr != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hStderr, buf, (DWORD)len, &written, NULL);
    }
}

void ReplayLogger::vlog(const char *levelTag, const char *fmt, va_list args) {
    EnterCriticalSection(&lock_);
    if (!initialized_ || level_ <= RLOG_OFF) {
        LeaveCriticalSection(&lock_);
        return;
    }

    writeTimestamp();
    if (levelTag) {
        int tagLen = (int)strlen(levelTag);
        writeRaw(levelTag, tagLen);
    }

    char line[1024];
    int len = vsnprintf(line, sizeof(line), fmt, args);
    if (len < 0) len = 0;
    if (len >= (int)sizeof(line)) len = (int)sizeof(line) - 1;
    line[len] = '\0';

    writeRaw(line, len);
    writeRaw("\r\n", 2);
    LeaveCriticalSection(&lock_);
}

void ReplayLogger::verbose(const char *fmt, ...) {
    if (level_ < RLOG_VERBOSE) return;
    va_list args;
    va_start(args, fmt);
    vlog("[VERBOSE] ", fmt, args);
    va_end(args);
}

void ReplayLogger::debug(const char *fmt, ...) {
    if (level_ < RLOG_DEBUG) return;
    va_list args;
    va_start(args, fmt);
    vlog("[DEBUG] ", fmt, args);
    va_end(args);
}

void ReplayLogger::hexDump(const char *label, const unsigned char *data, unsigned int len) {
    if (level_ < RLOG_DEBUG || !data || len == 0) return;

    char buf[1024];
    int pos = 0;
    pos += sprintf(buf + pos, "%s [%u bytes]: ", label, len);
    for (unsigned int i = 0; i < len && i < 256 && pos < (int)sizeof(buf) - 4; i++) {
        pos += sprintf(buf + pos, "%02X", data[i]);
        if (i + 1 < len && i + 1 < 256) buf[pos++] = '-';
    }
    buf[pos] = '\0';
    debug("%s", buf);
}

void ReplayLogger::apiEntry(const char *funcName, const char *argsFmt, ...) {
    if (level_ < RLOG_VERBOSE) return;

    EnterCriticalSection(&lock_);
    writeTimestamp();

    char line[1024];
    int len = snprintf(line, sizeof(line), "[API] %s(", funcName);
    if (argsFmt) {
        va_list args;
        va_start(args, argsFmt);
        len += vsnprintf(line + len, sizeof(line) - len, argsFmt, args);
        va_end(args);
    }
    len += snprintf(line + len, sizeof(line) - len, ")\r\n");
    if (len >= (int)sizeof(line)) len = (int)sizeof(line) - 1;
    writeRaw(line, len);
    LeaveCriticalSection(&lock_);
}

void ReplayLogger::apiReturn(const char *funcName, long retCode) {
    if (level_ < RLOG_VERBOSE) return;
    verbose("%s => %s (%ld)", funcName, rjRetCodeName(retCode), retCode);
}
