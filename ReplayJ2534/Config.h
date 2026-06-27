#pragma once
// ReplayJ2534 — Configuration and global state
#include <windows.h>

struct ReplayConfig {
    char configPath[MAX_PATH];     // Path to scenario.json
    int  logLevel;                 // 0=off, 1=verbose, 2=debug
    char logOutputPath[MAX_PATH];  // Optional output log path
    int  instant;                  // 1=instant mode (no delays)
};

extern ReplayConfig g_config;
extern bool g_initialized;

// Thread-local last error
void setLastError(const char *fmt, ...);
const char* getLastError();
