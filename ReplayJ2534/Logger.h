#pragma once
// ReplayJ2534 — Minimal logger for replay diagnostics

#include <windows.h>
#include <stdarg.h>

#define RLOG_OFF      0
#define RLOG_VERBOSE  1
#define RLOG_DEBUG    2

class ReplayLogger {
public:
    ReplayLogger();
    ~ReplayLogger();

    void init(int level, const char *filePath);
    void shutdown();

    bool isVerbose() const { return level_ >= RLOG_VERBOSE; }
    bool isDebug()   const { return level_ >= RLOG_DEBUG; }
    int  level()     const { return level_; }

    void verbose(const char *fmt, ...);
    void debug(const char *fmt, ...);

    void hexDump(const char *label, const unsigned char *data, unsigned int len);

    void apiEntry(const char *funcName, const char *argsFmt, ...);
    void apiReturn(const char *funcName, long retCode);

private:
    int              level_;
    HANDLE           hFile_;
    CRITICAL_SECTION lock_;
    bool             initialized_;

    void writeRaw(const char *buf, int len);
    void writeTimestamp();
    void vlog(const char *levelTag, const char *fmt, va_list args);
};

extern ReplayLogger g_logger;

// Enum name lookup helpers
const char* rjRetCodeName(long code);
const char* rjProtocolName(unsigned long protocolId);
const char* rjIoctlName(unsigned long ioctlId);
const char* rjFilterTypeName(unsigned long filterType);
