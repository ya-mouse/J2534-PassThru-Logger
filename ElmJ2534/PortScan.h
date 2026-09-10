#pragma once
// ElmJ2534 — best-effort Bluetooth SPP COM port auto-detect
//
// Implements: docs/elm-j2534-design.md → "Configuration" (auto-detect is the
// LAST priority: env ELM_J2534_PORT → registry ComPort → this scan).
//
// Declared unconditionally so callers compile everywhere; the implementation
// is SetupDi-based under _WIN32 and a documented no-op (false + err)
// elsewhere.

#include <string>

// Find the COM port of a paired remote Bluetooth device (the ELM327 dongle).
// Windows creates TWO SPP ports per pairing: the one whose hardware id is
// BTHENUM without LOCALMFG carries the remote MAC and is the dongle; the
// LOCALMFG one is this PC's own incoming SPP server and HANGS FOREVER if
// opened. With several remote pairings the LEXICOGRAPHICALLY SMALLEST COM name
// wins — SetupDi's enumeration order is driver-dependent, so anything else
// would pick a different port on a different day.
// Returns true with outPort filled ("COM3"), false + explanatory err.
bool elmScanBluetoothComPort(char *outPort, int outSize, std::string &err);
