#pragma once
// ElmJ2534 — Minimal logger for ELM327 link diagnostics

#include <windows.h>
#include <stdarg.h>

#define ELOG_OFF      0
#define ELOG_VERBOSE  1
#define ELOG_DEBUG    2

class ElmLogger {
public:
    ElmLogger();
    ~ElmLogger();

    void init(int level, const char *filePath);
    void shutdown();

    bool isVerbose() const { return level_ >= ELOG_VERBOSE; }
    bool isDebug()   const { return level_ >= ELOG_DEBUG; }
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

extern ElmLogger g_logger;

// Enum name lookup helpers
const char* elRetCodeName(long code);
const char* elProtocolName(unsigned long protocolId);
const char* elIoctlName(unsigned long ioctlId);
const char* elFilterTypeName(unsigned long filterType);
