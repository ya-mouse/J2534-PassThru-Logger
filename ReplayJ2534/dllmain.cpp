#include "J2534Defs.h"
#include "Simulator.h"
#include "Config.h"
#include "Logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

// Global state
Simulator g_simulator;
ReplayConfig g_config;
bool g_initialized = false;

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

//
// === Configuration loading ===
//

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
    g_config.configPath[0] = '\0';
    g_config.logLevel = 0;
    g_config.logOutputPath[0] = '\0';
    g_config.instant = 0;

    // Priority 1: Environment variable for scenario path
    DWORD envLen = GetEnvironmentVariableA("REPLAY_J2534_CONFIG",
                                           g_config.configPath, MAX_PATH);
    if (envLen > 0 && envLen < MAX_PATH) {
        // Found in environment
    } else {
        // Priority 2: Registry
        HKEY key;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\ReplayJ2534",
                          0, KEY_READ, &key) == ERROR_SUCCESS) {
            readRegString(key, "ScenarioPath", g_config.configPath, MAX_PATH);
            g_config.logLevel = (int)readRegDword(key, "LogLevel", 0);
            readRegString(key, "LogOutputPath", g_config.logOutputPath, MAX_PATH);
            g_config.instant = (int)readRegDword(key, "Instant", 0);
            RegCloseKey(key);
        }
    }

    // Environment overrides for other settings
    char buf[16];
    if (GetEnvironmentVariableA("REPLAY_J2534_LOGLEVEL", buf, sizeof(buf)) > 0)
        g_config.logLevel = atoi(buf);
    if (GetEnvironmentVariableA("REPLAY_J2534_INSTANT", buf, sizeof(buf)) > 0)
        g_config.instant = atoi(buf);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        loadConfig();

        g_logger.init(g_config.logLevel, g_config.logOutputPath);
        g_logger.verbose("ReplayJ2534 DLL loading");
        g_logger.verbose("  Config: '%s'", g_config.configPath);
        g_logger.verbose("  Instant: %d", g_config.instant);

        if (g_config.configPath[0]) {
            if (g_simulator.init(g_config.configPath, g_config.instant != 0)) {
                g_initialized = true;
                g_logger.verbose("ReplayJ2534 initialized OK");
            } else {
                g_initialized = false;
                g_logger.verbose("ReplayJ2534 FAILED to initialize");
            }
        } else {
            g_initialized = false;
            g_logger.verbose("ReplayJ2534: no scenario config path set");
        }
        break;

    case DLL_PROCESS_DETACH:
        g_logger.verbose("ReplayJ2534 DLL detaching");
        g_simulator.shutdown();
        g_initialized = false;
        g_logger.shutdown();
        break;
    }
    return TRUE;
}
