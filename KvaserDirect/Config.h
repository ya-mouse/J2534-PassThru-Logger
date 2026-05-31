#pragma once
// Shared configuration and global state declarations
#include <windows.h>

struct KdConfig {
    int     canlibChannel;
    int     shareCanlibChannels;
    int     acceptVirtualChannels;
    int     vbattSource;            // 0=mock, 1=kvIo, 2=auto
    int     vbattIoPin;
    DWORD   mockVbattMv;
    int     logLevel;               // 0=off, 1=verbose, 2=debug
    char    logFilePath[MAX_PATH];
    int     logFormat;              // 0=text, 1=json
    DWORD   defaultBaudRate;
};

extern KdConfig g_config;
extern bool g_initialized;

void setLastError(const char *fmt, ...);
const char* getLastError();
