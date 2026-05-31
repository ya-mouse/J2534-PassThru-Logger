#include "J2534Defs.h"
#include "CanlibLoader.h"
#include "HandleManager.h"
#include "Config.h"
#include "Logger.h"
#include <stdio.h>

// Global state
CanlibApi g_canlib;
HandleManager g_handleMgr;
bool g_initialized = false;
KdConfig g_config;

// Thread-local last error string
static __thread char tls_lastError[256] = {0};

void setLastError(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(tls_lastError, sizeof(tls_lastError), fmt, args);
    va_end(args);
}

const char* getLastError() {
    return tls_lastError;
}

static DWORD readRegDword(HKEY key, const char *name, DWORD defaultVal) {
    DWORD value = 0, size = sizeof(DWORD), type = 0;
    if (RegQueryValueExA(key, name, NULL, &type, (BYTE*)&value, &size) == ERROR_SUCCESS
        && type == REG_DWORD)
        return value;
    return defaultVal;
}

static void readRegString(HKEY key, const char *name, char *buf, DWORD bufSize) {
    DWORD type = 0, size = bufSize;
    buf[0] = '\0';
    RegQueryValueExA(key, name, NULL, &type, (BYTE*)buf, &size);
    if (type != REG_SZ) buf[0] = '\0';
}

static void loadConfig() {
    // Defaults
    g_config.canlibChannel = 0;
    g_config.shareCanlibChannels = 0;
    g_config.acceptVirtualChannels = 0;
    g_config.vbattSource = 2;       // auto
    g_config.vbattIoPin = 0;
    g_config.mockVbattMv = 12000;   // 12.0V
    g_config.logLevel = 0;          // off
    g_config.logFilePath[0] = '\0';
    g_config.logFormat = 0;
    g_config.defaultBaudRate = 500000;

    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\KvaserDirect", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        g_config.canlibChannel       = (int)readRegDword(key, "CanlibChannel", 0);
        g_config.shareCanlibChannels = (int)readRegDword(key, "ShareCanlibChannels", 0);
        g_config.acceptVirtualChannels = (int)readRegDword(key, "AcceptVirtualChannels", 0);
        g_config.vbattSource         = (int)readRegDword(key, "VbattSource", 2);
        g_config.vbattIoPin          = (int)readRegDword(key, "VbattIoPin", 0);
        g_config.mockVbattMv         = readRegDword(key, "MockVbattMv", 12000);
        g_config.logLevel            = (int)readRegDword(key, "LogLevel", 0);
        g_config.logFormat           = (int)readRegDword(key, "LogFormat", 0);
        g_config.defaultBaudRate     = readRegDword(key, "DefaultBaudRate", 500000);
        readRegString(key, "LogFilePath", g_config.logFilePath, MAX_PATH);
        RegCloseKey(key);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        loadConfig();

        // Initialize logger before anything else
        g_logger.init(g_config.logLevel, g_config.logFilePath);

        if (!canlibLoad(&g_canlib)) {
            g_logger.verbose("canlib32.dll load FAILED");
            g_initialized = false;
        } else {
            g_initialized = true;
            g_logger.verbose("canlib32.dll loaded OK, channel=%d", g_config.canlibChannel);
        }
        break;

    case DLL_PROCESS_DETACH:
        if (g_initialized) {
            g_logger.verbose("DLL detaching, unloading canlib");
            canlibUnload(&g_canlib);
            g_initialized = false;
        }
        g_logger.shutdown();
        break;
    }
    return TRUE;
}
