// Minimal Win32 stubs for native (macOS/Linux) test builds of ElmJ2534.
//
// Copied from ReplayJ2534/tests/stubs/windows.h (which stays untouched) and
// extended with the synchronisation + tick primitives ElmDevice.cpp needs:
// the J2534 state machine is Win32-shaped (CRITICAL_SECTION / condvar /
// GetTickCount) and must compile natively so the FakeLink suites can run on
// the host. Registry and file APIs are deliberately ABSENT — config loading
// lives in dllmain.cpp, which is never part of a native build.
#pragma once
#include <stdint.h>
#include <time.h>

typedef uint32_t DWORD;
typedef int BOOL;
typedef unsigned long ULONG;
typedef void* HANDLE;
typedef void* HMODULE;
typedef void* HKEY;
typedef void* LPVOID;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef DWORD* LPDWORD;
typedef char* LPTSTR;

#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define INVALID_HANDLE_VALUE ((HANDLE)(-1))
#define REG_DWORD 4
#define REG_SZ 1
#define ERROR_SUCCESS 0
#define KEY_READ 0x20019
#define HKEY_CURRENT_USER ((HKEY)1)

typedef unsigned char BYTE;

typedef struct { int dummy; } CRITICAL_SECTION;
typedef struct { int dummy; } CONDITION_VARIABLE;

// The native suites are single-threaded: writes are synchronous, so the RX
// queue is already filled by the time readMsgs runs. A no-op lock plus a
// sleep-only condvar wait preserves the deadline arithmetic under test without
// pulling pthread semantics into the stub.
static inline void InitializeCriticalSection(CRITICAL_SECTION *cs) { (void)cs; }
static inline void DeleteCriticalSection(CRITICAL_SECTION *cs) { (void)cs; }
static inline void EnterCriticalSection(CRITICAL_SECTION *cs) { (void)cs; }
static inline void LeaveCriticalSection(CRITICAL_SECTION *cs) { (void)cs; }
// Single-threaded suites: the lock is never contended, so the try always wins
// (ElmDevice::shutdown's FreeLibrary path depends on this to reach the ATLP).
static inline BOOL TryEnterCriticalSection(CRITICAL_SECTION *cs) { (void)cs; return TRUE; }
static inline void InitializeConditionVariable(CONDITION_VARIABLE *cv) { (void)cv; }
static inline void WakeConditionVariable(CONDITION_VARIABLE *cv) { (void)cv; }
static inline void WakeAllConditionVariable(CONDITION_VARIABLE *cv) { (void)cv; }

static inline void Sleep(DWORD ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static inline BOOL SleepConditionVariableCS(CONDITION_VARIABLE *cv,
                                            CRITICAL_SECTION *cs, DWORD ms) {
    (void)cv; (void)cs;
    Sleep(ms);
    return TRUE;
}

static inline DWORD GetTickCount(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)((uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000);
}
