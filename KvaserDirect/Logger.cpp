#include "Logger.h"
#include "Config.h"
#include "J2534Defs.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

KdLogger g_logger;

// ═══════════════════════════════════════════════════════════════════════════════
// Enum name lookups
// ═══════════════════════════════════════════════════════════════════════════════

const char* kdRetCodeName(long code) {
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

const char* kdProtocolName(unsigned long protocolId) {
    switch (protocolId) {
    case 0x01: return "J1850VPW";
    case 0x02: return "J1850PWM";
    case 0x03: return "ISO9141";
    case 0x04: return "ISO14230";
    case 0x05: return "CAN";
    case 0x06: return "ISO15765";
    case 0x07: return "SCI_A_ENGINE";
    case 0x08: return "SCI_A_TRANS";
    case 0x09: return "SCI_B_ENGINE";
    case 0x0A: return "SCI_B_TRANS";
    default:
        if (protocolId >= 0x8000 && protocolId <= 0x807F) {
            static char buf[32];
            snprintf(buf, sizeof(buf), "CAN_CH%lu", protocolId - 0x8000 + 1);
            return buf;
        }
        if (protocolId >= 0x8080 && protocolId <= 0x80FF) {
            static char buf[32];
            snprintf(buf, sizeof(buf), "ISO15765_CH%lu", protocolId - 0x8080 + 1);
            return buf;
        }
        return "UNKNOWN_PROTO";
    }
}

const char* kdIoctlName(unsigned long ioctlId) {
    switch (ioctlId) {
    case 0x01: return "GET_CONFIG";
    case 0x02: return "SET_CONFIG";
    case 0x03: return "READ_VBATT";
    case 0x04: return "FIVE_BAUD_INIT";
    case 0x05: return "FAST_INIT";
    case 0x07: return "CLEAR_TX_BUFFER";
    case 0x08: return "CLEAR_RX_BUFFER";
    case 0x09: return "CLEAR_PERIODIC_MSGS";
    case 0x0A: return "CLEAR_MSG_FILTERS";
    case 0x0B: return "CLEAR_FUNCT_MSG_LOOKUP_TABLE";
    case 0x0C: return "ADD_TO_FUNCT_MSG_LOOKUP_TABLE";
    case 0x0D: return "DELETE_FROM_FUNCT_MSG_LOOKUP_TABLE";
    case 0x0E: return "READ_PROG_VOLTAGE";
    case 0x0F: return "SW_CAN_HS";
    case 0x10: return "SW_CAN_NS";
    case 0x11: return "SET_POLL_RESPONSE";
    case 0x12: return "BECOME_MASTER";
    default:   return "UNKNOWN_IOCTL";
    }
}

const char* kdFilterTypeName(unsigned long filterType) {
    switch (filterType) {
    case 0x01: return "PASS_FILTER";
    case 0x02: return "BLOCK_FILTER";
    case 0x03: return "FLOW_CONTROL";
    default:   return "UNKNOWN_FILTER";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Logger implementation
// ═══════════════════════════════════════════════════════════════════════════════

// Find next available numbered JSON file using binary search.
// Given "C:\logs\trace.json", tries trace_00001.json, trace_00002.json, ...
// Algorithm: double N until file doesn't exist, then bisect.
static void findNextJsonPath(const char *basePath, char *out, int outSize) {
    // Split at last dot
    char stem[MAX_PATH], ext[32];
    strncpy(stem, basePath, MAX_PATH - 1);
    stem[MAX_PATH - 1] = '\0';
    ext[0] = '\0';

    char *dot = strrchr(stem, '.');
    char *sep = strrchr(stem, '\\');
    if (dot && (!sep || dot > sep)) {
        strncpy(ext, dot, sizeof(ext) - 1);
        ext[sizeof(ext) - 1] = '\0';
        *dot = '\0';
    } else {
        strcpy(ext, ".json");
    }

    auto buildPath = [&](int n, char *buf, int bufSize) {
        snprintf(buf, bufSize, "%s_%05d%s", stem, n, ext);
    };

    auto fileExists = [](const char *path) -> bool {
        DWORD attr = GetFileAttributesA(path);
        return (attr != INVALID_FILE_ATTRIBUTES);
    };

    // Phase 1: exponential search — find upper bound where file doesn't exist
    int lo = 1, hi = 1;
    char probe[MAX_PATH];
    buildPath(hi, probe, MAX_PATH);
    while (fileExists(probe)) {
        lo = hi;
        hi *= 2;
        if (hi > 99999) { hi = 99999; break; }
        buildPath(hi, probe, MAX_PATH);
    }

    // Phase 2: binary search between lo and hi for first non-existing
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        buildPath(mid, probe, MAX_PATH);
        if (fileExists(probe))
            lo = mid + 1;
        else
            hi = mid;
    }

    buildPath(lo, out, outSize);
}

KdLogger::KdLogger()
    : level_(KD_LOG_OFF), hFile_(INVALID_HANDLE_VALUE), initialized_(false),
      lastLineLen_(0), repeatCount_(0)
{
    lastLine_[0] = '\0';
    InitializeCriticalSection(&lock_);
}

KdLogger::~KdLogger() {
    shutdown();
    DeleteCriticalSection(&lock_);
}

void KdLogger::init(int level, const char *filePath) {
    EnterCriticalSection(&lock_);
    level_ = level;
    lastLineLen_ = 0;
    repeatCount_ = 0;
    lastLine_[0] = '\0';

    if (level_ > KD_LOG_OFF && filePath && filePath[0] != '\0') {
        char resolvedPath[MAX_PATH];
        strncpy(resolvedPath, filePath, MAX_PATH - 1);
        resolvedPath[MAX_PATH - 1] = '\0';

        // For JSON format, find next available numbered file
        if (g_config.logFormat == 1) {
            findNextJsonPath(filePath, resolvedPath, MAX_PATH);
        }

        hFile_ = CreateFileA(resolvedPath,
                             GENERIC_WRITE,
                             FILE_SHARE_READ,
                             NULL,
                             OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL,
                             NULL);
        if (hFile_ != INVALID_HANDLE_VALUE) {
            SetFilePointer(hFile_, 0, NULL, FILE_END);
            initialized_ = true;

            if (g_config.logFormat == 1) {
                // JSON: write opening bracket if file is empty
                LARGE_INTEGER size;
                GetFileSizeEx(hFile_, &size);
                if (size.QuadPart == 0)
                    writeRaw("[\n", 2);
            } else {
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
    }
    LeaveCriticalSection(&lock_);
}

void KdLogger::shutdown() {
    EnterCriticalSection(&lock_);
    if (initialized_) {
        flushDedup();
        if (g_config.logFormat == 1) {
            writeRaw("\n]\n", 3);  // JSON array close
        } else {
            writeRaw("=== session end ===\n", 20);
        }
    }
    if (hFile_ != INVALID_HANDLE_VALUE) {
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

void KdLogger::flushDedup() {
    if (repeatCount_ > 1) {
        char rep[64];
        int n = snprintf(rep, sizeof(rep), "             ...repeated %lu times\n", repeatCount_);
        writeRaw(rep, n);
    }
    repeatCount_ = 0;
}

void KdLogger::emitLine(const char *buf, int len) {
    // Compare with last line (without timestamp prefix)
    if (lastLineLen_ == len && memcmp(lastLine_, buf, len) == 0) {
        repeatCount_++;
        return;
    }

    // Different line — flush previous dedup, emit new
    flushDedup();

    writeTimestamp();
    writeRaw(buf, len);
    writeRaw("\n", 1);

    // Remember this line for dedup
    lastLineLen_ = (len < (int)sizeof(lastLine_) - 1) ? len : (int)sizeof(lastLine_) - 1;
    memcpy(lastLine_, buf, lastLineLen_);
    lastLine_[lastLineLen_] = '\0';
    repeatCount_ = 1;
}

void KdLogger::verbose(const char *fmt, ...) {
    if (level_ < KD_LOG_VERBOSE || !initialized_) return;
    EnterCriticalSection(&lock_);

    char buf[1024];
    int prefix = snprintf(buf, sizeof(buf), "[V] ");
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf + prefix, sizeof(buf) - prefix, fmt, args);
    va_end(args);
    if (n > 0) emitLine(buf, prefix + n);
    LeaveCriticalSection(&lock_);
}

void KdLogger::debug(const char *fmt, ...) {
    if (level_ < KD_LOG_DEBUG || !initialized_) return;
    EnterCriticalSection(&lock_);

    char buf[1024];
    int prefix = snprintf(buf, sizeof(buf), "[D] ");
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf + prefix, sizeof(buf) - prefix, fmt, args);
    va_end(args);
    if (n > 0) emitLine(buf, prefix + n);
    LeaveCriticalSection(&lock_);
}

void KdLogger::hexDump(const char *label, const unsigned char *data, unsigned int len) {
    if (level_ < KD_LOG_DEBUG || !initialized_) return;
    EnterCriticalSection(&lock_);
    flushDedup();  // hex dumps break dedup sequence
    writeTimestamp();
    char hdr[128];
    int n = snprintf(hdr, sizeof(hdr), "[D] %s (%u bytes):", label, len);
    writeRaw(hdr, n);

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
    lastLineLen_ = 0;  // Reset dedup after hex dump
    LeaveCriticalSection(&lock_);
}

void KdLogger::isoTpState(const char *direction, unsigned long canId,
                           const char *fromState, const char *toState) {
    if (level_ < KD_LOG_DEBUG || !initialized_) return;
    EnterCriticalSection(&lock_);
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "[D] ISO-TP %s ID=0x%03lX: %s -> %s",
                     direction, canId, fromState, toState);
    emitLine(buf, n);
    LeaveCriticalSection(&lock_);
}

void KdLogger::apiEntry(const char *funcName, const char *argsFmt, ...) {
    if (level_ < KD_LOG_VERBOSE || !initialized_) return;
    EnterCriticalSection(&lock_);
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "[V] -> %s(", funcName);

    va_list args;
    va_start(args, argsFmt);
    int an = vsnprintf(buf + n, sizeof(buf) - n, argsFmt, args);
    va_end(args);
    if (an > 0) n += an;
    n += snprintf(buf + n, sizeof(buf) - n, ")");
    emitLine(buf, n);
    LeaveCriticalSection(&lock_);
}

void KdLogger::apiReturn(const char *funcName, long retCode) {
    if (level_ < KD_LOG_VERBOSE || !initialized_) return;
    EnterCriticalSection(&lock_);
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "[V] <- %s = %s (%ld)",
                     funcName, kdRetCodeName(retCode), retCode);
    emitLine(buf, n);
    LeaveCriticalSection(&lock_);
}
