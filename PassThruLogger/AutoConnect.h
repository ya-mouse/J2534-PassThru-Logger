#pragma once
#include "stdafx.h"
#include "J2534_v0404.h"

// Configuration for auto-injected PassThruConnect
struct ConnectConfig {
	DWORD protocolId;   // default: CAN (0x05)
	DWORD baudRate;     // default: 500000
	DWORD flags;        // default: 0
	bool autoInject;    // default: true
	std::string deviceName; // default: "" (empty = NULL)
	DWORD mockVbattMv;  // mock VBATT in millivolts; 0 = disabled
};

// Per-device state tracking
struct DeviceState {
	unsigned long deviceId;
	unsigned long injectedChannelId; // 0 if not injected
	unsigned long clientChannelId;   // 0 if client hasn't connected yet
	bool injected;
	CRITICAL_SECTION lock;
};

// Load connect configuration from registry
void LoadConnectConfig(ConnectConfig& config);

// Device state management
void InitDeviceState(unsigned long deviceId);
void DestroyDeviceState(unsigned long deviceId);
DeviceState* GetDeviceState(unsigned long deviceId);

// Auto-inject logic: returns the channel ID to use for channel-scoped calls.
// If auto-inject is needed and succeeds, returns the injected channel ID.
// If the client already has a channel, returns that.
// If the handle is already a channel ID (not a device ID), returns it unchanged.
// Returns 0 on injection failure.
unsigned long ResolveChannelId(unsigned long handle);

// Called when client does its own PassThruConnect
void OnClientConnect(unsigned long deviceId, unsigned long channelId);

// Called before PassThruClose to tear down injected channels
void CleanupBeforeClose(unsigned long deviceId);

// Check if a given handle matches a known device ID
bool IsDeviceId(unsigned long handle);
