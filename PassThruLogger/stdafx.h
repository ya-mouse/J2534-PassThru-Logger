#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>
#include <string>
#include <memory>

#include "J2534_v0404.h"

// Suppress MSVC-specific pragmas when compiling with mingw
#ifdef __GNUC__
#define EXPORT_PRAGMA
#else
#define EXPORT_PRAGMA comment(linker, "/EXPORT:" __FUNCTION__ "=" __FUNCDNAME__)
#endif
