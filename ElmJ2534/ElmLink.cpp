// ElmJ2534 — SerialElmLink: Win32 COM-port link with a deadline-guarded open
//
// Implements: docs/elm-j2534-design.md → "ElmSession interfaces (T3 contract)"
// Threaded-open pattern ported from candroid-fw tools/elm327.py
// (open_with_timeout): the RFCOMM connect blocks inside the Bluetooth stack
// and never raises, so the open runs on a worker thread that this call may
// abandon — and an abandoned CreateFileA that succeeds later must close its
// own orphan handle.
//
// Toolchain note: the project's mingw is the win32-thread model (GCC
// 12-win32), where <thread>/<mutex>/<condition_variable> do not exist — the
// deadline wait is WaitForSingleObject on the worker handle and the orphan
// flag is guarded by a CRITICAL_SECTION. Same semantics, Win32 spelling.

#ifdef _WIN32

#include "ElmLink.h"

#include <stdio.h>

// Config.h's registry default; repeated here so the link stays independent of
// the config layer (and testable/usable without it).
static const int ELM_DEFAULT_OPEN_TIMEOUT_SEC = 15;

// RFCOMM channels come up dirty: let the connection settle before listening.
static const DWORD ELM_OPEN_SETTLE_MS = 500;

// Per-read cap installed at configure() time; read() overrides per call.
static const DWORD ELM_INITIAL_READ_TIMEOUT_MS = 100;

static std::string winErrorText(const std::string &what, DWORD code) {
    char *msg = NULL;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, code, 0, (LPSTR)&msg, 0, NULL);
    char buf[512];
    snprintf(buf, sizeof(buf), "%s failed (gle=%lu%s%s)", what.c_str(),
             (unsigned long)code, msg ? ": " : "", msg ? msg : "");
    if (msg) LocalFree(msg);
    // FormatMessage tails its text with CRLF, which would break log lines.
    std::string out(buf);
    while (!out.empty() && (out[out.size() - 1] == '\r' || out[out.size() - 1] == '\n'))
        out.erase(out.size() - 1);
    return out;
}

// ── Threaded open ───────────────────────────────────────────────────────────

namespace {

// Shared state between the abandoning caller and the worker stuck in
// CreateFileA. Heap-allocated and refcounted because the worker can outlive
// open() by an unbounded time — that is the whole point of the deadline.
struct OpenCtx {
    CRITICAL_SECTION lock;
    LONG refs;              // caller + worker; last release frees
    std::string devPath;
    HANDLE result;          // valid iff finished
    DWORD winError;         // GetLastError of the worker's CreateFileA
    bool finished;          // worker published its result
    bool abandoned;         // caller gave up: worker must close its own orphan
};

void ctxRelease(OpenCtx *ctx) {
    if (InterlockedDecrement(&ctx->refs) == 0) {
        DeleteCriticalSection(&ctx->lock);
        delete ctx;
    }
}

DWORD WINAPI elmOpenWorker(LPVOID param) {
    OpenCtx *ctx = static_cast<OpenCtx *>(param);

    // THIS is the call that hangs inside bthport.sys/RFCOMM when the dongle
    // is unpowered or the link key is stale. No timeout parameter reaches it.
    HANDLE h = CreateFileA(ctx->devPath.c_str(), GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
    DWORD gle = (h == INVALID_HANDLE_VALUE) ? GetLastError() : 0;

    EnterCriticalSection(&ctx->lock);
    bool orphan = ctx->abandoned;
    if (!orphan) {
        ctx->result = h;
        ctx->winError = gle;
        ctx->finished = true;
    }
    LeaveCriticalSection(&ctx->lock);

    // A late success after the deadline is still a live RFCOMM channel that
    // nobody owns — close it here or the port stays busy for every retry.
    if (orphan && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    ctxRelease(ctx);
    return 0;
}

} // namespace

// ── SerialElmLink ───────────────────────────────────────────────────────────

SerialElmLink::SerialElmLink() : handle_(INVALID_HANDLE_VALUE) {}

SerialElmLink::~SerialElmLink() { close(); }

bool SerialElmLink::openThreaded(const char *devPath, DWORD timeoutMs,
                                 std::string &err) {
    OpenCtx *ctx = new OpenCtx();
    InitializeCriticalSection(&ctx->lock);
    ctx->refs = 2;   // caller + worker; on spawn failure both refs are dropped
    ctx->devPath = devPath;
    ctx->result = INVALID_HANDLE_VALUE;
    ctx->winError = 0;
    ctx->finished = false;
    ctx->abandoned = false;

    HANDLE thread = CreateThread(NULL, 0, elmOpenWorker, ctx, 0, NULL);
    if (!thread) {
        err = winErrorText("CreateThread for the COM open", GetLastError());
        ctxRelease(ctx);
        ctxRelease(ctx);
        return false;
    }

    WaitForSingleObject(thread, timeoutMs);

    HANDLE result = INVALID_HANDLE_VALUE;
    DWORD winError = 0;
    bool ok = false;
    EnterCriticalSection(&ctx->lock);
    if (ctx->finished) {
        ok = true;
        result = ctx->result;
        winError = ctx->winError;
    } else {
        ctx->abandoned = true;
    }
    LeaveCriticalSection(&ctx->lock);
    // Dropping our handle does not kill a running thread — it only releases
    // the kernel object, which the abandoned worker may hold for a long time.
    CloseHandle(thread);
    ctxRelease(ctx);

    if (!ok) {
        char buf[1024];
        snprintf(buf, sizeof(buf),
                 "%s did not open within %lus: the RFCOMM connect is blocked "
                 "inside the Windows Bluetooth stack, which never raises — an "
                 "unpowered dongle and a stale link key both look exactly "
                 "like this. Ask Windows why; the serial layer cannot: check "
                 "the System event log for BTHUSB events "
                 "(Get-WinEvent -FilterHashtable @{LogName='System';"
                 "ProviderName='BTHUSB'}). Events 16/37 (authentication "
                 "failed/rejected) mean the dongle is powered and in range "
                 "but the link key is stale: unpair BOTH halves, power-cycle "
                 "the dongle, re-pair. No events at all mean the dongle never "
                 "answered the page: power it. The abandoned open thread "
                 "closes its own handle if the driver ever returns.",
                 devPath, (unsigned long)(timeoutMs / 1000));
        err = buf;
        return false;
    }
    if (result == INVALID_HANDLE_VALUE) {
        err = winErrorText(std::string("CreateFile on ") + devPath, winError);
        err += " — is the port name right, the dongle paired, and no other "
               "program holding the port?";
        return false;
    }
    handle_ = result;
    return true;
}

bool SerialElmLink::configure(int baud, std::string &err) {
    DCB dcb;
    ZeroMemory(&dcb, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle_, &dcb)) {
        err = winErrorText("GetCommState", GetLastError());
        return false;
    }
    dcb.BaudRate = (DWORD)baud;   // ignored by BT SPP, but the port wants one
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    // No flow control in any direction: CTS/DSR/RTS lines on a Bluetooth SPP
    // port float, and gating writes on them stalls the first command forever.
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    // NUL and parity-mangled bytes are the parser's business (it drops junk at
    // byte level); driver-side dropping would silently eat frame bytes.
    dcb.fNull = FALSE;
    dcb.fErrorChar = FALSE;
    // A line glitch must not wedge every subsequent IO until the port reopens.
    dcb.fAbortOnError = FALSE;
    if (!SetCommState(handle_, &dcb)) {
        err = winErrorText("SetCommState (8N1, no flow control)", GetLastError());
        return false;
    }

    COMMTIMEOUTS ct;
    // MAXDWORD interval + 0 multiplier + constant cap = "return whatever has
    // arrived immediately; otherwise wait at most the constant" — exactly the
    // per-read deadline the session's exchange loop needs.
    ct.ReadIntervalTimeout = MAXDWORD;
    ct.ReadTotalTimeoutMultiplier = 0;
    ct.ReadTotalTimeoutConstant = ELM_INITIAL_READ_TIMEOUT_MS;
    ct.WriteTotalTimeoutMultiplier = 2;   // 2 ms/byte…
    ct.WriteTotalTimeoutConstant = 1000;  // …plus a fixed allowance
    if (!SetCommTimeouts(handle_, &ct)) {
        err = winErrorText("SetCommTimeouts", GetLastError());
        return false;
    }

    Sleep(ELM_OPEN_SETTLE_MS);
    // Flush both directions AFTER the settle: pre-pairing chatter and any
    // half-sent command from a previous session must not reach the parser.
    PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return true;
}

bool SerialElmLink::open(const char *port, int baud, int openTimeoutSec,
                         std::string &err) {
    close();
    if (!port || !port[0]) {
        err = "no COM port given (set ELM_J2534_PORT, the registry ComPort, "
              "or pair a Bluetooth dongle for auto-detect)";
        return false;
    }
    const int secs = openTimeoutSec > 0 ? openTimeoutSec
                                        : ELM_DEFAULT_OPEN_TIMEOUT_SEC;
    // The \\.\ prefix is required for COM10+ and harmless below.
    const std::string devPath = std::string("\\\\.\\") + port;
    if (!openThreaded(devPath.c_str(), (DWORD)secs * 1000, err))
        return false;
    if (!configure(baud, err)) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
        return false;
    }
    return true;
}

void SerialElmLink::close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

bool SerialElmLink::write(const char *data, int len) {
    if (handle_ == INVALID_HANDLE_VALUE) {
        lastError_ = "write on a closed link";
        return false;
    }
    if (!data || len <= 0) return true;
    int sent = 0;
    while (sent < len) {
        DWORD n = 0;
        if (!WriteFile(handle_, data + sent, (DWORD)(len - sent), &n, NULL)) {
            lastError_ = winErrorText("WriteFile", GetLastError());
            return false;
        }
        if (n == 0) {
            // WriteTotalTimeout* expired mid-command: a partial command on the
            // wire is worse than none — the adapter would answer the wrong thing.
            lastError_ = "short write: timeout after " +
                         std::to_string((long long)sent) + " of " +
                         std::to_string((long long)len) + " bytes";
            return false;
        }
        sent += (int)n;
    }
    return true;
}

int SerialElmLink::read(uint8_t *buf, int maxLen, int timeoutMs) {
    if (handle_ == INVALID_HANDLE_VALUE) {
        lastError_ = "read on a closed link";
        return -1;
    }
    if (!buf || maxLen <= 0) return 0;

    COMMTIMEOUTS ct;
    if (!GetCommTimeouts(handle_, &ct)) {
        lastError_ = winErrorText("GetCommTimeouts", GetLastError());
        return -1;
    }
    ct.ReadIntervalTimeout = MAXDWORD;
    ct.ReadTotalTimeoutMultiplier = 0;
    ct.ReadTotalTimeoutConstant = timeoutMs > 0 ? (DWORD)timeoutMs : 0;
    if (!SetCommTimeouts(handle_, &ct)) {
        lastError_ = winErrorText("SetCommTimeouts", GetLastError());
        return -1;
    }

    DWORD got = 0;
    if (!ReadFile(handle_, buf, (DWORD)maxLen, &got, NULL)) {
        // The BT SPP virtual COM port disappears when the dongle drops off
        // the bus mid-session; ReadFile failing is how that surfaces.
        lastError_ = winErrorText("ReadFile (adapter gone?)", GetLastError());
        return -1;
    }
    return (int)got;   // 0 = deadline expired with no bytes (link silent)
}

void SerialElmLink::discardInput() {
    if (handle_ != INVALID_HANDLE_VALUE)
        PurgeComm(handle_, PURGE_RXCLEAR);
}

#endif // _WIN32
