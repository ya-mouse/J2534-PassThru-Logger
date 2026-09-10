#pragma once
// ElmJ2534 — Configuration and global state
#include <windows.h>

struct ElmConfig {
    char comPort[16];              // COM port to open (e.g. "COM3"); empty = auto-detect
    int  baudRate;                 // BT SPP ignores this, but CreateFile needs a DCB rate
    int  logLevel;                 // 0=off, 1=verbose, 2=debug
    char logOutputPath[MAX_PATH];  // Optional output log path
    int  openTimeoutSec;           // Deadline for the threaded COM open (BT stack can hang)
};

extern ElmConfig g_config;
extern bool g_initialized;

// Thread-local last error
void setLastError(const char *fmt, ...);
const char* getLastError();
