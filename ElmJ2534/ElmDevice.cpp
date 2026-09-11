// ElmJ2534 — J2534 state machine: lifecycle, channels, versioning
//
// Implements: docs/elm-j2534-design.md → "J2534 mapping". Patterns mirrored
// from ReplayJ2534/Simulator.cpp: channel id allocation (including the
// P-LIVE-003 fix) and the 80-byte ReadVersion buffers.
//
// This TU is the lifecycle half of the split: WriteMsgs/ReadMsgs, the exchange
// recovery and the RX queue live in ElmDeviceMsgs.cpp, message filters and
// ioctl in ElmDeviceFilters.cpp. All three implement the one class and share
// ElmDevicePriv.h, so each file stays inside the project's LOC budget.
//
// Adapter handling follows the T3 handoff contract: ElmSession holds no lock,
// so every call is serialised here; request() reports truncation instead of
// forwarding partial payloads; and every recovery clears the adapter's
// addressing, so setTarget() is re-run before each retry.

#include "ElmDevicePriv.h"

#include <stdarg.h>

#ifdef _WIN32
#include "PortScan.h"
#endif

#ifndef _WIN32
namespace {
// The host has no serial transport. A link that refuses to open keeps the
// state machine uniform across builds and lets the suites exercise the
// open-failure path without injecting a fake.
class NullElmLink : public IElmLink {
public:
    virtual bool open(const char *port, int baud, int openTimeoutSec,
                      std::string &err) {
        (void)port; (void)baud; (void)openTimeoutSec;
        err = "no serial transport in this build (inject a link for testing)";
        return false;
    }
    virtual void close() {}
    virtual bool write(const char *data, int len) { (void)data; (void)len; return false; }
    virtual int read(uint8_t *buf, int maxLen, int timeoutMs) {
        (void)buf; (void)maxLen; (void)timeoutMs; return -1;
    }
    virtual void discardInput() {}
};
NullElmLink g_nullLink;
} // namespace
#endif

namespace {

// Version strings live here rather than in the header so that every TU
// including ElmDevice.h does not carry an unused copy.
const char ELM_DLL_VERSION[] = "ElmJ2534 1.0.0";
const char ELM_API_VERSION[] = "04.04";
// ReadVersion must answer even before the adapter has been probed.
const char ELM_FW_UNPROBED[] = "ELM327 (not probed)";

// Wording a support engineer greps for when the bench dongle is unpowered or
// unpaired: it names both configuration routes and the failed fallback.
const char ELM_NO_PORT_ERROR[] =
    "no COM port configured (ELM_J2534_PORT / HKCU\\Software\\ElmJ2534\\ComPort)"
    " and no Bluetooth SPP port found";
} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════════════

ElmDevice::ElmDevice()
    : initialized_(false), deviceOpen_(false), deviceId_(0),
      nextChannelId_(1), nextFilterId_(1), startTick_(0),
      waiters_(0), testLink_(NULL), session_(NULL) {
    memset(&cfg_, 0, sizeof(cfg_));
    InitializeCriticalSection(&lock_);
    InitializeConditionVariable(&rxCond_);
}

ElmDevice::~ElmDevice() {
    // Invariant: shutdown() has already drained the channels and woken every
    // parked reader (both DllMain DETACH and PassThruClose do it), so nobody
    // can still be inside SleepConditionVariableCS here. Deleting the lock
    // under a waiter is undefined behaviour, so a violation is reported rather
    // than silently ignored.
    if (waiters_ != 0) {
        // ELM_LOGV is a no-op natively and the logger is already shut down in
        // the DLL, so the only report that survives every build is the
        // debugger. Skip the delete rather than corrupt under a waiter —
        // leaking one CRITICAL_SECTION beats undefined behaviour.
#ifdef _WIN32
        OutputDebugStringA("ElmDevice: destroyed with reader(s) parked on "
                           "rxCond_ - teardown ran while ReadMsgs in flight\n");
#endif
        shutdown();
        return;
    }
    shutdown();
    DeleteCriticalSection(&lock_);
}

IElmLink &ElmDevice::transport() {
    if (testLink_) return *testLink_;
#ifdef _WIN32
    return serialLink_;
#else
    return g_nullLink;
#endif
}

void ElmDevice::init(const ElmConfig &cfg) {
    EnterCriticalSection(&lock_);
    shutdown();          // re-entrant, like Simulator::init
    cfg_ = cfg;
    session_ = new ElmSession(transport());
    initialized_ = true;
    ELM_LOGV("ElmDevice: init (port='%s' baud=%d openTimeout=%ds)",
             cfg_.comPort, cfg_.baudRate, cfg_.openTimeoutSec);
    LeaveCriticalSection(&lock_);
}

void ElmDevice::shutdown(bool allowBlocking) {
    if (!allowBlocking) {
        // Process exit (DLL_PROCESS_DETACH with lpReserved != NULL): the OS has
        // already terminated every other thread, so a lock one of them held is
        // never released and an ATLP exchange would never complete — taking
        // either here hangs exit. CloseHandle only, then clear state (safe: no
        // other thread can observe it) so the destructor does not retry.
        transport().close();
        channels_.clear();
        delete session_;
        session_ = NULL;
        deviceOpen_ = false;
        initialized_ = false;
        deviceId_ = 0;
        banner_.clear();
        startTick_ = 0;
        return;
    }
    // FreeLibrary / PassThruClose / normal teardown. TryEnter, never Enter:
    // waiting on a lock another thread holds mid-exchange would block an unload
    // for the length of an ELM exchange (seconds on a wedged link).
    if (!TryEnterCriticalSection(&lock_)) {
        ELM_LOGV("ElmDevice: shutdown skipped ATLP — another thread holds the "
                 "device lock; closing the port only");
        // Close the port so that thread's next read/write fails, but do NOT
        // free the session it is executing in or reset the state it is reading:
        // a use-after-free there is worse than letting the unload leak it.
        transport().close();
        return;
    }
    if (deviceOpen_ && session_) {
        // ATLP + close, bounded (≤2 s worst case: write timeout + the 1 s
        // ELM_SHUTDOWN_TIMEOUT_MS read deadline). PassThruClose
        // normally did this already; this is the app-unloaded-without-closing
        // path, and leaving the adapter awake drains the vehicle battery.
        session_->shutdown();
    }
    channels_.clear();
    deviceOpen_ = false;
    deviceId_ = 0;
    banner_.clear();
    startTick_ = 0;
    nextChannelId_ = 1;
    nextFilterId_ = 1;
    delete session_;
    session_ = NULL;
    initialized_ = false;
    WakeAllConditionVariable(&rxCond_);
    LeaveCriticalSection(&lock_);
}

void ElmDevice::attachLinkForTesting(IElmLink *link) {
    EnterCriticalSection(&lock_);
    testLink_ = link;
    if (initialized_) {
        // Rebuild so an already-initialised device picks the fake up; suites
        // normally attach before init().
        delete session_;
        session_ = new ElmSession(transport());
    }
    LeaveCriticalSection(&lock_);
}

// ═══════════════════════════════════════════════════════════════════════════
// Open / Close
// ═══════════════════════════════════════════════════════════════════════════

bool ElmDevice::resolvePort(char *outPort, int outSize, std::string &err) {
    if (cfg_.comPort[0]) {
        strncpy(outPort, cfg_.comPort, (size_t)(outSize - 1));
        outPort[outSize - 1] = '\0';
        return true;
    }
    std::string scanErr;
#ifdef _WIN32
    // Auto-detect is the LAST resort (env → registry → scan). Scanning is
    // cheap; opening what it finds is not, which is why this runs here and
    // never in DllMain. An injected test link has no system port to scan
    // for: scanning anyway would make suite results depend on whatever
    // Bluetooth hardware happens to be paired with the build/test machine.
    if (!testLink_ && elmScanBluetoothComPort(outPort, outSize, scanErr)) {
        ELM_LOGV("ElmDevice: auto-detected Bluetooth SPP port %s", outPort);
        return true;
    }
    if (testLink_) scanErr = "test link attached; system port scan skipped";
#else
    scanErr = "Bluetooth COM auto-detect is only available on Windows";
#endif
    err = std::string(ELM_NO_PORT_ERROR) + " (" + scanErr + ")";
    return false;
}

// Port open + the full ELM handshake. Any failure leaves the port closed: a
// half-initialised adapter (ATSP6 accepted, ATFCSH not) answers every later
// multi-frame read with silence and nothing to point at.
bool ElmDevice::bringUpSession(const char *port, std::string &err) {
    if (!session_->open(port, cfg_.baudRate, cfg_.openTimeoutSec, err))
        return false;
    std::string banner;
    if (!session_->initialise(banner, err)) {
        session_->shutdown();
        banner_.clear();
        return false;
    }
    port_ = port;
    banner_ = banner;   // space-free by construction ("ELM327v1.5")
    return true;
}

// A wedged link needs more than recover(Reinitialise): when RFCOMM itself is
// gone the session's first write fails, so the port has to be re-opened before
// the handshake can run again.
bool ElmDevice::reinitialiseLink(std::string &err) {
    if (session_->recover(ElmRecovery::Reinitialise, err))
        return true;
    if (!session_->open(port_.c_str(), cfg_.baudRate, cfg_.openTimeoutSec, err))
        return false;
    std::string banner;
    if (!session_->initialise(banner, err))
        return false;
    if (!banner.empty()) banner_ = banner;
    return true;
}

long ElmDevice::openDevice(void *pName, unsigned long *pDeviceId) {
    // pName carries nothing actionable: there is exactly one adapter and its
    // port comes from config/auto-detect, not from the caller.
    (void)pName;
    if (!pDeviceId) return ERR_NULL_PARAMETER;

    EnterCriticalSection(&lock_);
    if (!initialized_ || !session_) {
        noteError("ElmJ2534 is not initialized (DllMain did not run)");
        LeaveCriticalSection(&lock_);
        return ERR_DEVICE_NOT_CONNECTED;
    }
    if (deviceOpen_) {
        // Single-device DLL: a second open hands back the same id instead of
        // claiming hardware that does not exist.
        *pDeviceId = deviceId_;
        ELM_LOGV("ElmDevice: Open while active -> same DeviceId=%lu", deviceId_);
        LeaveCriticalSection(&lock_);
        return STATUS_NOERROR;
    }

    char port[16] = {0};
    std::string err;
    if (!resolvePort(port, (int)sizeof(port), err) ||
        !bringUpSession(port, err)) {
        noteError("%s", err.c_str());
        ELM_LOGV("ElmDevice: Open failed: %s", err.c_str());
        LeaveCriticalSection(&lock_);
        return ERR_DEVICE_NOT_CONNECTED;
    }

    deviceOpen_ = true;
    deviceId_ = ELM_DEVICE_ID;
    startTick_ = GetTickCount();
    channels_.clear();
    nextChannelId_ = 1;
    nextFilterId_ = 1;
    *pDeviceId = deviceId_;
    ELM_LOGV("ElmDevice: Open(%s) -> DeviceId=%lu firmware='%s'",
             port, deviceId_, banner_.c_str());
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

long ElmDevice::closeDevice(unsigned long deviceId) {
    EnterCriticalSection(&lock_);
    if (!deviceOpen_ || deviceId != deviceId_) {
        noteError("invalid device id %lu", deviceId);
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_DEVICE_ID;
    }
    // Channels die with the device: the J2534 contract for Close is that the
    // handle goes away, and stranding a channel would leave its caller waiting
    // on a queue nothing can ever fill again.
    channels_.clear();
    session_->shutdown();      // ATLP + close
    deviceOpen_ = false;
    deviceId_ = 0;
    banner_.clear();
    startTick_ = 0;
    nextChannelId_ = 1;
    nextFilterId_ = 1;
    WakeAllConditionVariable(&rxCond_);
    ELM_LOGV("ElmDevice: Close(%lu)", deviceId);
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

// ═══════════════════════════════════════════════════════════════════════════
// Connect / Disconnect
// ═══════════════════════════════════════════════════════════════════════════

// Mirrors Simulator::allocateChannelId including the P-LIVE-003 fix: when the
// preferred id is taken the search continues ABOVE it — falling back to
// nextChannelId_ would hand out an id below the preferred range.
unsigned long ElmDevice::allocateChannelId(unsigned long preferred) {
    if (preferred > 0 && channels_.find(preferred) == channels_.end())
        return preferred;
    unsigned long start = (preferred > 0) ? preferred + 1 : nextChannelId_;
    if (start < nextChannelId_) start = nextChannelId_;
    for (unsigned long id = start; ; id++) {
        if (channels_.find(id) == channels_.end()) {
            if (id >= nextChannelId_) nextChannelId_ = id + 1;
            return id;
        }
    }
}

ElmDevice::Channel* ElmDevice::findChannel(unsigned long id) {
    std::map<unsigned long, Channel>::iterator it = channels_.find(id);
    return it != channels_.end() ? &it->second : NULL;
}

long ElmDevice::connect(unsigned long deviceId, unsigned long protocolId,
                        unsigned long flags, unsigned long baudRate,
                        unsigned long *pChannelId) {
    if (!pChannelId) return ERR_NULL_PARAMETER;

    EnterCriticalSection(&lock_);
    if (!deviceOpen_ || deviceId != deviceId_) {
        noteError("invalid device id %lu", deviceId);
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_DEVICE_ID;
    }
    // One protocol, one baud: ATSP6 is the only mode the init plan programs,
    // and accepting another would make every later exchange a mystery.
    if (protocolId != J2534_ISO15765) {
        noteError("protocol 0x%lX not supported: the ELM327 is addressed as "
                  "ISO15765 (ATSP6) only", protocolId);
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_PROTOCOL_ID;
    }
    if (baudRate != ELM_CAN_BAUD_500K) {
        noteError("baud %lu not supported: ATSP6 pins the adapter to %lu",
                  baudRate, ELM_CAN_BAUD_500K);
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_BAUDRATE;
    }
    if (flags & CAN_29BIT_ID) {
        noteError("CAN_29BIT_ID not supported: ATSP6 selects 11-bit ISO15765");
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_FLAGS;
    }
    if (flags & ~ELM_SUPPORTED_CONNECT_FLAGS) {
        noteError("unsupported connect flags 0x%lX (only CAN_ID_BOTH is)",
                  flags & ~ELM_SUPPORTED_CONNECT_FLAGS);
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_FLAGS;
    }

    Channel ch;
    ch.id = allocateChannelId(ELM_PREFERRED_CHANNEL_ID);
    ch.protocolId = protocolId;
    ch.flags = flags;
    ch.baud = baudRate;
    ch.flowRespId = 0;
    ch.flowReqId = 0;
    channels_[ch.id] = ch;
    *pChannelId = ch.id;

    ELM_LOGV("ElmDevice: Connect(proto=0x%lX flags=0x%lX baud=%lu) -> ChannelId=%lu",
             protocolId, flags, baudRate, ch.id);
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

long ElmDevice::disconnect(unsigned long channelId) {
    EnterCriticalSection(&lock_);
    Channel *ch = findChannel(channelId);
    if (!ch) {
        noteError("invalid channel id %lu", channelId);
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_CHANNEL_ID;
    }
    // Erasing the channel drops its filters and its queued RX with it. The
    // adapter keeps its addressing — the next write re-asserts what it needs.
    channels_.erase(channelId);
    WakeAllConditionVariable(&rxCond_);
    ELM_LOGV("ElmDevice: Disconnect(%lu)", channelId);
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

// ═══════════════════════════════════════════════════════════════════════════
// ReadVersion / GetLastError
// ═══════════════════════════════════════════════════════════════════════════

long ElmDevice::readVersion(unsigned long deviceId, char *pFw, char *pDll,
                            char *pApi) {
    EnterCriticalSection(&lock_);
    if (!deviceOpen_ || deviceId != deviceId_) {
        noteError("invalid device id %lu", deviceId);
        LeaveCriticalSection(&lock_);
        return ERR_INVALID_DEVICE_ID;
    }
    copyFixed(pFw, banner_.empty() ? ELM_FW_UNPROBED : banner_.c_str());
    copyFixed(pDll, ELM_DLL_VERSION);
    copyFixed(pApi, ELM_API_VERSION);
    ELM_LOGV("ElmDevice: ReadVersion fw='%s' dll='%s' api='%s'",
             banner_.c_str(), ELM_DLL_VERSION, ELM_API_VERSION);
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

long ElmDevice::getLastError(char *pBuf) {
    if (!pBuf) return ERR_NULL_PARAMETER;
    EnterCriticalSection(&lock_);
    strncpy(pBuf, lastError_.c_str(), ELM_LAST_ERROR_LEN - 1);
    pBuf[ELM_LAST_ERROR_LEN - 1] = '\0';
    LeaveCriticalSection(&lock_);
    return STATUS_NOERROR;
}

void ElmDevice::noteError(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    // Both channels J2534 exposes: the TLS string behind PassThruGetLastError
    // (Config.h) and the device copy getLastError() falls back to.
    lastError_ = buf;
    setLastError("%s", buf);
    ELM_LOGV("ElmDevice: %s", buf);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test accessors
// ═══════════════════════════════════════════════════════════════════════════

bool ElmDevice::hasChannel(unsigned long channelId) {
    EnterCriticalSection(&lock_);
    const bool found = findChannel(channelId) != NULL;
    LeaveCriticalSection(&lock_);
    return found;
}

int ElmDevice::channelCount() {
    EnterCriticalSection(&lock_);
    const int n = (int)channels_.size();
    LeaveCriticalSection(&lock_);
    return n;
}

int ElmDevice::rxQueueSize(unsigned long channelId) {
    EnterCriticalSection(&lock_);
    Channel *ch = findChannel(channelId);
    const int n = ch ? (int)ch->rxQueue.size() : -1;
    LeaveCriticalSection(&lock_);
    return n;
}

std::string ElmDevice::firmware() {
    EnterCriticalSection(&lock_);
    const std::string s = banner_;
    LeaveCriticalSection(&lock_);
    return s;
}
