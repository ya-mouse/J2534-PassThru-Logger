#pragma once
// ElmJ2534 — J2534 state machine over an ELM327 session
//
// Implements: docs/elm-j2534-design.md → "J2534 mapping", "RX PASSTHRU_MSG
// convention", "Constraints". Structure mirrors ReplayJ2534/Simulator.h: one
// object owns device + channel state, validates every transition, and returns
// J2534 error codes; J2534Api.cpp is a thin wrapper around it.
//
// Everything that touches the adapter runs under lock_: the ELM327 is strictly
// half-duplex (one command per '>' prompt) and ElmSession holds no lock of its
// own. PassThruWriteMsgs is therefore SYNCHRONOUS — the exchange completes and
// the RX queue is filled before it returns, which is why readMsgs only ever
// waits for a concurrent writer, never for the bus.

#include "J2534Defs.h"
#include "Config.h"
#include "ElmSession.h"
#include "ElmLink.h"

#include <deque>
#include <map>
#include <string>
#include <vector>

// ── Fixed identity of this DLL ──────────────────────────────────────────────
// One adapter, one device, one protocol: the ELM327 is a single half-duplex
// serial target, so the device id is a constant and a second PassThruOpen
// returns the same one rather than pretending to have more hardware.
static const unsigned long ELM_DEVICE_ID = 1;
// Channels start at 2 so a channel id can never collide with the device id:
// J2534 passes both through the same `handle` argument to Ioctl/Close, and an
// overlap would let PassThruClose(1) be read as "channel 1" (or vice versa).
static const unsigned long ELM_PREFERRED_CHANNEL_ID = 2;

// ATSP6 pins the adapter to 11-bit / 500 kbit ISO15765 — the only protocol and
// baud this DLL can honestly report (design spec → "J2534 mapping").
static const unsigned long ELM_CAN_BAUD_500K = 500000;
static const unsigned long ELM_SUPPORTED_CONNECT_FLAGS = CAN_ID_BOTH;

// J2534 ISO15765 carries the CAN id in Data[0..3], big-endian; a write must
// carry at least one payload byte for the adapter to have something to send.
static const unsigned long ELM_CAN_ID_BYTES = 4;
static const unsigned long ELM_MIN_WRITE_DATA_SIZE = ELM_CAN_ID_BYTES + 1;

// J2534 fixes these caller buffers at 80 bytes.
static const int ELM_VERSION_FIELD_LEN = 80;
static const int ELM_LAST_ERROR_LEN = 80;

class ElmDevice {
public:
    ElmDevice();
    ~ElmDevice();

    // Store config and build the session. Opens NO port: a Bluetooth SPP open
    // blocks inside the Windows BT stack and DllMain runs under the loader
    // lock, so the port comes up lazily in openDevice().
    void init(const ElmConfig &cfg);

    // Release the device. allowBlocking=true (PassThruClose, FreeLibrary with
    // the lock free) does the bounded ATLP + close under the lock.
    // allowBlocking=false is the PROCESS-EXIT path: the OS has already
    // terminated every other thread, so a lock one of them held is never
    // coming back — close the port handle only, take no lock, do no I/O.
    void shutdown(bool allowBlocking = true);
    bool isInitialized() const { return initialized_; }

    // Replace the transport with a fake BEFORE init() — the native and mingw
    // suites drive the whole state machine through a scripted link. Calling it
    // later rebuilds the session around the new link (test-only convenience).
    void attachLinkForTesting(IElmLink *link);

    // ── J2534 API handlers (same shape as Simulator's) ──
    long openDevice(void *pName, unsigned long *pDeviceId);
    long closeDevice(unsigned long deviceId);
    long connect(unsigned long deviceId, unsigned long protocolId,
                 unsigned long flags, unsigned long baudRate,
                 unsigned long *pChannelId);
    long disconnect(unsigned long channelId);
    long readMsgs(unsigned long channelId, PASSTHRU_MSG *pMsg,
                  unsigned long *pNumMsgs, unsigned long timeout);
    long writeMsgs(unsigned long channelId, PASSTHRU_MSG *pMsg,
                   unsigned long *pNumMsgs, unsigned long timeout);
    long ioctl(unsigned long handle, unsigned long ioctlId,
               void *pInput, void *pOutput);
    long startMsgFilter(unsigned long channelId, unsigned long filterType,
                        PASSTHRU_MSG *pMask, PASSTHRU_MSG *pPattern,
                        PASSTHRU_MSG *pFlow, unsigned long *pFilterId);
    long stopMsgFilter(unsigned long channelId, unsigned long filterId);
    long readVersion(unsigned long deviceId, char *pFw, char *pDll, char *pApi);
    long getLastError(char *pBuf);

    // ── Test accessors (take the lock) ──
    bool hasChannel(unsigned long channelId);
    int channelCount();
    int rxQueueSize(unsigned long channelId);
    std::string firmware();

private:
    // A filter is stored as the big-endian id pair the app handed over; the
    // hardware side (ATCRA) can only ever hold ONE id, so PASS/BLOCK filters
    // are applied host-side at RX enqueue time.
    struct Filter {
        unsigned long id;
        unsigned long type;
        uint32_t mask;
        uint32_t pattern;
        uint32_t flow;      // FLOW_CONTROL only: id the FC frame is sent to
    };

    struct Channel {
        unsigned long id;
        unsigned long protocolId;
        unsigned long flags;
        unsigned long baud;
        // From the channel's FLOW_CONTROL_FILTER: the id we flow-control for
        // (ATCRA) and the id flow-control frames go to (ATFCSH). Zero when no
        // such filter is installed — the session then emits ATCRA000
        // (accept-all) if a CRA was previously set; merely OMITTING ATCRA
        // would leave the chip's stale filter in force and the response side
        // silently shut.
        uint32_t flowRespId;
        uint32_t flowReqId;
        std::vector<Filter> filters;
        std::deque<PASSTHRU_MSG> rxQueue;
    };

    // Outcome of one adapter exchange, after the recovery the adapter asked
    // for has been applied and the request retried once.
    enum ExchangeOutcome {
        ExchangeOk,           // assemblies (possibly empty) are valid
        ExchangeSilent,       // frame went out, nothing answered (NODATA)
        ExchangeFailed,       // bus/adapter error, recovery exhausted
        ExchangeUnsupported,  // adapter answered '?'
        ExchangeLinkGone      // no prompt / IO error: adapter state unknowable
    };

    ElmConfig cfg_;
    bool initialized_;
    bool deviceOpen_;
    unsigned long deviceId_;
    unsigned long nextChannelId_;
    unsigned long nextFilterId_;
    DWORD startTick_;             // Timestamp base: ms since the port opened
    std::string port_;            // resolved COM port, for a link re-open
    std::string banner_;          // ELM identity, reported as firmware version
    std::string lastError_;       // device-level fallback for getLastError()
    std::map<unsigned long, Channel> channels_;

    CRITICAL_SECTION lock_;
    CONDITION_VARIABLE rxCond_;   // signaled on every RX enqueue
    // Readers currently parked on rxCond_. Teardown must not delete lock_
    // while this is non-zero — a waiter woken after the lock is gone is
    // undefined behaviour, so the destructor reports a violation.
    unsigned long waiters_;

    IElmLink *testLink_;          // non-NULL only in tests
#ifdef _WIN32
    SerialElmLink serialLink_;
#endif
    ElmSession *session_;         // created in init(); owns no lock

    // ── Helpers: all require lock_ held ──
    IElmLink &transport();
    Channel* findChannel(unsigned long id);
    unsigned long allocateChannelId(unsigned long preferred);
    bool resolvePort(char *outPort, int outSize, std::string &err);
    bool bringUpSession(const char *port, std::string &err);
    bool reinitialiseLink(std::string &err);
    void applyFlowIds(Channel &ch);
    bool rxFilterAllows(const Channel &ch, uint32_t respId);
    unsigned long elapsedMs() const;
    void noteError(const char *fmt, ...);

    void enqueueRx(Channel &ch, const PASSTHRU_MSG &msg);
    void enqueueEcho(Channel &ch, uint32_t reqId);
    void enqueueResponse(Channel &ch, uint32_t respId,
                         const std::vector<uint8_t> &payload);

    long writeOne(Channel &ch, const PASSTHRU_MSG &msg, unsigned long timeout);
    ExchangeOutcome runExchange(uint32_t reqId, uint32_t respId,
                                const uint8_t *payload, int payloadLen,
                                int exchangeMs,
                                std::vector<ElmAssembly> &assemblies);

    ElmDevice(const ElmDevice &);            // owns a lock and a session
    ElmDevice &operator=(const ElmDevice &);
};
