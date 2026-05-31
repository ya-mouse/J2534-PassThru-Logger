#include "Logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

KdLogger g_logger;

// J2534 error code to name mapping
static const char* retCodeName(long code) {
    switch (code) {
    case 0:  return "STATUS_NOERROR";
    case 1:  return "ERR_NOT_SUPPORTED";
    case 2:  return "ERR_INVALID_CHANNEL_ID";
    case 3:  return "ERR_INVALID_PROTOCOL_ID";
    case 4:  return "ERR_NULL_PARAMETER";
    case 5:  return "ERR_INVALID_IOCTL_VALUE";
    case 6:  return "ERR_INVALID_FLAGS";
    case 7:  return "ERR_FAILED";
    case 8:  return "ERR_DEVICE_NOT_CONNECTED";
    case 9:  return "ERR_TIMEOUT";
    case 10: return "ERR_INVALID_MSG";
    case 11: return "ERR_INVALID_TIME_INTERVAL";
    case 12: return "ERR_EXCEEDED_LIMIT";
    case 13: return "ERR_INVALID_MSG_ID";
    case 14: return "ERR_DEVICE_IN_USE";
    case 15: return "ERR_INVALID_IOCTL_ID";
    case 16: return "ERR_BUFFER_EMPTY";
    case 17: return "ERR_BUFFER_FULL";
    case 18: return "ERR_BUFFER_OVERFLOW";
    case 19: return "ERR_PIN_INVALID";
    case 20: return "ERR_CHANNEL_IN_USE";
    case 21: return "ERR_MSG_PROTOCOL_ID";
    case 22: return "ERR_INVALID_FILTER_ID";
    case 23: return "ERR_NO_FLOW_CONTROL";
    case 24: return "ERR_NOT_UNIQUE";
    case 25: return "ERR_INVALID_BAUDRATE";
    case 26: return "ERR_INVALID_DEVICE_ID";
    default: return "ERR_UNKNOWN";
    }
}

KdLogger::KdLogger()
    : level_(KD_LOG_OFF), hFile_(INVALID_HANDLE_VALUE), initialized_(false)
{
    InitializeCriticalSection(&lock_);
}

KdLogger::~KdLogger() {
    shutdown();
    DeleteCriticalSection(&lock_);
}

void KdLogger::init(int level, const char *filePath) {
    EnterCriticalSection(&lock_);
    level_ = level;

    if (level_ > KD_LOG_OFF && filePath && filePath[0] != '\0') {
        hFile_ = CreateFileA(filePath,
                             GENERIC_WRITE,
                             FILE_SHARE_READ,
                             NULL,
                             OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL,
                             NULL);
        if (hFile_ != INVALID_HANDLE_VALUE) {
            // Seek to end for append
            SetFilePointer(hFile_, 0, NULL, FILE_END);
            initialized_ = true;

            // Write session header
            char hdr[256];
            SYSTEMTIME st;
            GetLocalTime(&st);
            int n = snprintf(hdr, sizeof(hdr),
                "\n=== KvaserDirect session %04d-%02d-%02d %02d:%02d:%02d (level=%d) ===\n",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond, level_);
            writeRaw(hdr, n);
        }
    }
    LeaveCriticalSection(&lock_);
}

void KdLogger::shutdown() {
    EnterCriticalSection(&lock_);
    if (hFile_ != INVALID_HANDLE_VALUE) {
        writeRaw("=== session end ===\n", 20);
        CloseHandle(hFile_);
        hFile_ = INVALID_HANDLE_VALUE;
    }
    initialized_ = false;
    level_ = KD_LOG_OFF;
    LeaveCriticalSection(&lock_);
}

void KdLogger::writeTimestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char ts[32];
    int n = snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%03d ",
                     st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    writeRaw(ts, n);
}

void KdLogger::writeRaw(const char *buf, int len) {
    if (hFile_ == INVALID_HANDLE_VALUE || len <= 0) return;
    DWORD written = 0;
    WriteFile(hFile_, buf, (DWORD)len, &written, NULL);
}

void KdLogger::verbose(const char *fmt, ...) {
    if (level_ < KD_LOG_VERBOSE || !initialized_) return;
    EnterCriticalSection(&lock_);
    writeTimestamp();
    writeRaw("[V] ", 4);

    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) writeRaw(buf, n);
    writeRaw("\n", 1);
    LeaveCriticalSection(&lock_);
}

void KdLogger::debug(const char *fmt, ...) {
    if (level_ < KD_LOG_DEBUG || !initialized_) return;
    EnterCriticalSection(&lock_);
    writeTimestamp();
    writeRaw("[D] ", 4);

    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) writeRaw(buf, n);
    writeRaw("\n", 1);
    LeaveCriticalSection(&lock_);
}

void KdLogger::hexDump(const char *label, const unsigned char *data, unsigned int len) {
    if (level_ < KD_LOG_DEBUG || !initialized_) return;
    EnterCriticalSection(&lock_);
    writeTimestamp();
    char hdr[128];
    int n = snprintf(hdr, sizeof(hdr), "[D] %s (%u bytes):", label, len);
    writeRaw(hdr, n);

    // Print hex in rows of 16
    for (unsigned int i = 0; i < len; i++) {
        if (i % 16 == 0) {
            char lineHdr[16];
            int ln = snprintf(lineHdr, sizeof(lineHdr), "\n     %04X: ", i);
            writeRaw(lineHdr, ln);
        }
        char hex[4];
        int hn = snprintf(hex, sizeof(hex), "%02X ", data[i]);
        writeRaw(hex, hn);
    }
    writeRaw("\n", 1);
    LeaveCriticalSection(&lock_);
}

void KdLogger::isoTpState(const char *direction, unsigned long canId,
                           const char *fromState, const char *toState) {
    if (level_ < KD_LOG_DEBUG || !initialized_) return;
    EnterCriticalSection(&lock_);
    writeTimestamp();
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "[D] ISO-TP %s ID=0x%03lX: %s -> %s\n",
                     direction, canId, fromState, toState);
    writeRaw(buf, n);
    LeaveCriticalSection(&lock_);
}

void KdLogger::apiEntry(const char *funcName, const char *argsFmt, ...) {
    if (level_ < KD_LOG_VERBOSE || !initialized_) return;
    EnterCriticalSection(&lock_);
    writeTimestamp();
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "[V] -> %s(", funcName);
    writeRaw(buf, n);

    va_list args;
    va_start(args, argsFmt);
    n = vsnprintf(buf, sizeof(buf), argsFmt, args);
    va_end(args);
    if (n > 0) writeRaw(buf, n);
    writeRaw(")\n", 2);
    LeaveCriticalSection(&lock_);
}

void KdLogger::apiReturn(const char *funcName, long retCode) {
    if (level_ < KD_LOG_VERBOSE || !initialized_) return;
    EnterCriticalSection(&lock_);
    writeTimestamp();
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "[V] <- %s = %s (%ld)\n",
                     funcName, retCodeName(retCode), retCode);
    writeRaw(buf, n);
    LeaveCriticalSection(&lock_);
}
