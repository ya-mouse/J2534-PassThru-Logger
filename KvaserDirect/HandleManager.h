#pragma once
// Handle management for J2534 device/channel/filter IDs
// Maps opaque J2534 handles to internal state

#include "J2534Defs.h"
#include "CanlibLoader.h"
#include "IsoTpEngine.h"

// Maximum limits
#define MAX_CHANNELS    16
#define MAX_FILTERS     32
#define MAX_PERIODIC    16

// Filter entry
struct FilterEntry {
    bool            active;
    unsigned long   filterType;     // PASS/BLOCK/FLOW_CONTROL
    unsigned long   maskId;         // CAN ID mask
    unsigned long   patternId;      // CAN ID pattern
    unsigned long   flowControlId;  // CAN ID for our FC responses (ISO-TP only)
    unsigned long   maskFlags;      // 29-bit flag in mask
    unsigned long   patternFlags;   // 29-bit flag in pattern
};

// Periodic message entry
struct PeriodicMsgEntry {
    bool            active;
    unsigned long   intervalMs;
    PASSTHRU_MSG    msg;
    HANDLE          timerHandle;    // Timer queue timer
};

// Channel state
struct ChannelState {
    bool            active;
    unsigned long   channelId;      // J2534 channel ID (our handle)
    unsigned long   deviceId;       // Parent device ID
    unsigned long   protocolId;     // CAN, ISO15765, etc.
    unsigned long   flags;          // Connect flags (CAN_29BIT_ID, etc.)
    unsigned long   baudRate;
    CanHandle       canHandle;      // CANlib handle

    // Configuration
    unsigned long   dataRate;
    unsigned long   loopback;
    unsigned long   iso15765_bs;
    unsigned long   iso15765_stmin;
    unsigned long   iso15765_bs_tx;
    unsigned long   iso15765_stmin_tx;
    unsigned long   iso15765_wft_max;
    unsigned long   iso15765_pad_value;
    unsigned long   canMixedFormat;

    // Filters
    FilterEntry     filters[MAX_FILTERS];
    unsigned long   nextFilterId;

    // Periodic messages
    PeriodicMsgEntry periodic[MAX_PERIODIC];
    unsigned long   nextPeriodicId;

    // ISO-TP engine (for ISO15765 protocol channels)
    IsoTpEngine    *isoTpEngine;

    // RX thread (for ISO15765 — polls CAN and feeds processFrame)
    HANDLE          rxThread;
    HANDLE          rxStopEvent;
    CRITICAL_SECTION rxLock;
};

// Device state
struct DeviceState {
    bool            active;
    unsigned long   deviceId;
    int             canlibChannel;  // Underlying CANlib channel number
    char            deviceName[256];
};

// Global handle manager
class HandleManager {
public:
    HandleManager();
    ~HandleManager();

    // Device operations
    long openDevice(const char *name, unsigned long *pDeviceId);
    long closeDevice(unsigned long deviceId);
    DeviceState* getDevice(unsigned long deviceId);

    // Channel operations
    long openChannel(unsigned long deviceId, unsigned long protocolId,
                     unsigned long flags, unsigned long baudRate,
                     unsigned long *pChannelId);
    long closeChannel(unsigned long channelId);
    ChannelState* getChannel(unsigned long channelId);

    // Filter operations
    long addFilter(unsigned long channelId, unsigned long filterType,
                   PASSTHRU_MSG *pMask, PASSTHRU_MSG *pPattern,
                   PASSTHRU_MSG *pFlowControl, unsigned long *pFilterId);
    long removeFilter(unsigned long channelId, unsigned long filterId);

    // Periodic message operations
    long startPeriodic(unsigned long channelId, PASSTHRU_MSG *pMsg,
                       unsigned long *pMsgId, unsigned long interval);
    long stopPeriodic(unsigned long channelId, unsigned long msgId);

private:
    DeviceState     devices_[4];        // Max 4 devices
    ChannelState    channels_[MAX_CHANNELS];
    unsigned long   nextDeviceId_;
    CRITICAL_SECTION lock_;
};

// Global instance
extern HandleManager g_handleMgr;
