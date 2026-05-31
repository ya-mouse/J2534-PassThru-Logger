#pragma once
// KvaserDirect Debug/Verbose Logger
// Configurable via registry (HKCU\Software\KvaserDirect)
//   LogLevel: 0=off, 1=verbose, 2=debug
//   LogFilePath: file path to append logs (required if LogLevel > 0)

#include <windows.h>

// Log levels
#define KD_LOG_OFF      0
#define KD_LOG_VERBOSE  1   // Function calls + args + return code
#define KD_LOG_DEBUG    2   // + hex packet dumps + ISO-TP state transitions

class KdLogger {
public:
    KdLogger();
    ~KdLogger();

    // Initialize from registry config. Call once at DLL_PROCESS_ATTACH.
    void init(int level, const char *filePath);
    void shutdown();

    // Level check — use before expensive formatting
    bool isVerbose() const { return level_ >= KD_LOG_VERBOSE; }
    bool isDebug() const   { return level_ >= KD_LOG_DEBUG; }
    int  level() const     { return level_; }

    // Formatted log (printf-style). Prepends timestamp + level tag.
    void verbose(const char *fmt, ...);
    void debug(const char *fmt, ...);

    // Hex dump helper (debug level only)
    void hexDump(const char *label, const unsigned char *data, unsigned int len);

    // ISO-TP state transition (debug level)
    void isoTpState(const char *direction, unsigned long canId,
                    const char *fromState, const char *toState);

    // Log a J2534 API call entry (verbose)
    void apiEntry(const char *funcName, const char *argsFmt, ...);

    // Log a J2534 API call return (verbose)
    void apiReturn(const char *funcName, long retCode);

private:
    int             level_;
    HANDLE          hFile_;
    CRITICAL_SECTION lock_;
    bool            initialized_;

    void writeRaw(const char *buf, int len);
    void writeTimestamp();
};

// Global logger instance
extern KdLogger g_logger;
