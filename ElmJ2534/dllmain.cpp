// ElmJ2534 — DLL entry, global state and configuration
//
// Implements: docs/elm-j2534-design.md → "Configuration" and the constraint
// "No blocking work in DllMain". Mirrors ReplayJ2534/dllmain.cpp: globals, the
// thread-local last-error string behind Config.h's set/getLastError, registry
// helpers and loadConfig().
//
// DllMain ATTACH deliberately does NOT open the COM port: a Bluetooth SPP open
// blocks inside the Windows BT stack (an unpowered dongle or a stale link key
// never raises), and blocking under the loader lock takes the whole process
// with it. PassThruOpen does it, on the caller's thread, with a deadline.

#include "J2534Defs.h"
#include "ElmDevice.h"
#include "Config.h"
#include "Logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global state
ElmDevice g_device;
ElmConfig g_config;
bool g_initialized = false;

// Configuration defaults (registry BaudRate/OpenTimeoutSec fall back to these).
// BT SPP ignores the baud rate, but CreateFile wants a DCB rate and 38400 is
// what every clone answers to.
static const int ELM_CFG_DEFAULT_BAUD = 38400;
static const int ELM_CFG_DEFAULT_OPEN_TIMEOUT_SEC = 15;

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
    buf[bufSize - 1] = '\0';
}

// SerialElmLink::open() takes a bare "COM3" and adds the \\.\ prefix itself
// (required for COM10+), so a configured device path is normalised here rather
// than producing \\.\ \\.\COM3 at open time.
static void setComPort(const char *value) {
    if (!value || !value[0]) return;
    if (strncmp(value, "\\\\.\\", 4) == 0) value += 4;
    // snprintf, not strncpy: the field is 16 bytes and a longer value is bogus
    // anyway — truncating it silently is fine, warning about it is not.
    snprintf(g_config.comPort, sizeof(g_config.comPort), "%s", value);
}

static void loadConfig() {
    memset(&g_config, 0, sizeof(g_config));
    g_config.baudRate = ELM_CFG_DEFAULT_BAUD;
    g_config.openTimeoutSec = ELM_CFG_DEFAULT_OPEN_TIMEOUT_SEC;
    g_config.logLevel = ELOG_OFF;
    // comPort stays empty when neither env nor registry names one: PassThruOpen
    // then falls back to the Bluetooth SPP scan.

    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\ElmJ2534",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        char buf[MAX_PATH];
        readRegString(key, "ComPort", buf, sizeof(buf));
        setComPort(buf);
        g_config.baudRate = (int)readRegDword(key, "BaudRate",
                                              (DWORD)g_config.baudRate);
        g_config.logLevel = (int)readRegDword(key, "LogLevel",
                                              (DWORD)g_config.logLevel);
        readRegString(key, "LogOutputPath", g_config.logOutputPath,
                      sizeof(g_config.logOutputPath));
        g_config.openTimeoutSec = (int)readRegDword(key, "OpenTimeoutSec",
                                                    (DWORD)g_config.openTimeoutSec);
        RegCloseKey(key);
    }

    // Environment outranks the registry: it is what a bench run sets to point
    // at a different dongle without touching the machine's configuration.
    char buf[MAX_PATH];
    if (GetEnvironmentVariableA("ELM_J2534_PORT", buf, sizeof(buf)) > 0)
        setComPort(buf);
    char num[32];
    if (GetEnvironmentVariableA("ELM_J2534_BAUD", num, sizeof(num)) > 0)
        g_config.baudRate = atoi(num);
    if (GetEnvironmentVariableA("ELM_J2534_LOGLEVEL", num, sizeof(num)) > 0)
        g_config.logLevel = atoi(num);
    if (GetEnvironmentVariableA("ELM_J2534_OPENTIMEOUT", num, sizeof(num)) > 0)
        g_config.openTimeoutSec = atoi(num);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        loadConfig();

        g_logger.init(g_config.logLevel, g_config.logOutputPath);
        g_logger.verbose("ElmJ2534 DLL loading");
        g_logger.verbose("  Port: '%s'%s", g_config.comPort,
                         g_config.comPort[0] ? "" : " (empty — auto-detect on open)");
        g_logger.verbose("  Baud: %d  LogLevel: %d  OpenTimeout: %ds",
                         g_config.baudRate, g_config.logLevel,
                         g_config.openTimeoutSec);

        // Config + session object only: the port opens in PassThruOpen.
        g_device.init(g_config);
        g_initialized = true;
        g_logger.verbose("ElmJ2534 initialized (adapter not probed yet)");
        break;

    case DLL_PROCESS_DETACH: {
        // lpReserved distinguishes the two unload paths, and they need opposite
        // behaviour (design spec → Constraints, DETACH carve-out):
        //   != NULL → PROCESS EXIT. The OS has already terminated every other
        //             thread, so a lock one of them held is never released and
        //             an ATLP exchange can never complete: CloseHandle only,
        //             no lock, no adapter I/O — otherwise exit hangs.
        //   == NULL → FreeLibrary. Other threads are still alive, so a bounded
        //             ATLP (≤2 s worst case: write timeout + 1 s read deadline)
        //             is worth it — but only with the lock free
        //             right now; shutdown() uses TryEnterCriticalSection and
        //             falls back to a raw close.
        const bool freeLibrary = (lpReserved == NULL);
        g_logger.verbose("ElmJ2534 DLL detaching (%s)",
                         freeLibrary ? "FreeLibrary" : "process exit");
        g_device.shutdown(freeLibrary);
        g_initialized = false;
        g_logger.shutdown();
        break;
    }
    }
    return TRUE;
}
