// Minimal Win32 type stubs for native (macOS/Linux) test builds
#pragma once
#include <stdint.h>

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

// J2534 calling convention (no-op on non-Windows)
#define PTAPI
#define KD_API extern "C"

// Critical section stubs (not used in ConfigStore tests)
typedef struct { int dummy; } CRITICAL_SECTION;
typedef struct { int dummy; } CONDITION_VARIABLE;
