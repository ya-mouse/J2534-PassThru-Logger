#pragma once
// ElmJ2534 — link abstraction over the ELM327 transport
//
// Implements: docs/elm-j2534-design.md → "ElmSession interfaces (T3 contract)"
//
// The pure IElmLink interface below has NO windows.h dependency and compiles
// natively (macOS/Linux) — that is what lets tests/test_session.cpp drive the
// whole session logic through a fake link on the host. The Win32 serial
// implementation is declared under #ifdef _WIN32 only.

#include <string>
#include <stdint.h>

// One bidirectional byte pipe to the adapter. Injected into ElmSession;
// FakeElmLink in tests, SerialElmLink in the DLL.
class IElmLink {
public:
    virtual ~IElmLink() {}

    // Bring the link up. openTimeoutSec caps how long a blocking open (the
    // Bluetooth stack can hang inside the driver) may take. False + err on
    // failure or timeout.
    virtual bool open(const char *port, int baud, int openTimeoutSec,
                      std::string &err) = 0;
    virtual void close() = 0;

    // All-or-nothing: false when any byte could not be written.
    virtual bool write(const char *data, int len) = 0;

    // Blocks up to timeoutMs. Returns bytes read; 0 = timeout with no bytes;
    // -1 = link error (SerialElmLink::lastError() carries the detail — the
    // interface itself has no err out-param).
    virtual int read(uint8_t *buf, int maxLen, int timeoutMs) = 0;

    // Drop any unread input (stale replies from an abandoned exchange).
    virtual void discardInput() = 0;
};

#ifdef _WIN32

#include <windows.h>

// Win32 COM-port link (real serial or Bluetooth SPP — both are \\.\COMx).
//
// Opening a BT SPP port BLOCKS INSIDE THE WINDOWS BLUETOOTH STACK and never
// raises: an unpowered dongle or a stale link key presents as an infinite
// hang in CreateFileA (see candroid-fw tools/elm327.py open_with_timeout).
// open() therefore runs CreateFileA on a worker thread with a deadline and
// abandons the thread on expiry; if the abandoned CreateFileA succeeds later,
// the worker closes the orphan handle itself — no handle may be left open
// behind a timed-out connect, or the next attempt sees the port as busy.
class SerialElmLink : public IElmLink {
public:
    SerialElmLink();
    virtual ~SerialElmLink();

    virtual bool open(const char *port, int baud, int openTimeoutSec,
                      std::string &err);
    virtual void close();
    virtual bool write(const char *data, int len);
    virtual int read(uint8_t *buf, int maxLen, int timeoutMs);
    virtual void discardInput();

    // Detail behind the most recent false/-1 (read() has no err out-param).
    const std::string &lastError() const { return lastError_; }

private:
    SerialElmLink(const SerialElmLink &);            // owns a HANDLE
    SerialElmLink &operator=(const SerialElmLink &);

    bool openThreaded(const char *devPath, DWORD timeoutMs, std::string &err);
    bool configure(int baud, std::string &err);

    HANDLE handle_;
    std::string lastError_;
};

#endif // _WIN32
