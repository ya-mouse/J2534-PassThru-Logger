#include "stdafx.h"
#include "AutoConnect.h"
#include "RegUtils.h"
#include "Loader4.h"
#include <map>

static ConnectConfig g_config;
static std::map<unsigned long, DeviceState*> g_devices;
static CRITICAL_SECTION g_devicesLock;
static bool g_initialized = false;

void InitAutoConnect() {
	if (!g_initialized) {
		InitializeCriticalSection(&g_devicesLock);
		g_initialized = true;
	}
}

void LoadConnectConfig(ConnectConfig& config) {
	InitAutoConnect();

	// Defaults
	config.protocolId = CAN;       // 0x05
	config.baudRate = 500000;
	config.flags = 0;
	config.autoInject = true;
	config.deviceName = "";
	config.mockVbattMv = 0;

	HKEY key;
	LONG lRes = RegOpenKeyExA(HKEY_CURRENT_USER, LOGGER_REG_KEY, 0, KEY_READ, &key);
	if (lRes != ERROR_SUCCESS) return;

	DWORD val;
	if (GetDWORDRegKey(key, "ConnectProtocolID", val, CAN) == ERROR_SUCCESS)
		config.protocolId = val;
	if (GetDWORDRegKey(key, "ConnectBaudRate", val, 500000) == ERROR_SUCCESS)
		config.baudRate = val;
	if (GetDWORDRegKey(key, "ConnectFlags", val, 0) == ERROR_SUCCESS)
		config.flags = val;

	DWORD autoInject;
	if (GetDWORDRegKey(key, "AutoInjectConnect", autoInject, 1) == ERROR_SUCCESS)
		config.autoInject = (autoInject != 0);

	GetStringRegKey(key, "DeviceName", config.deviceName, "");

	DWORD mockMv;
	if (GetDWORDRegKey(key, "MockVbattMv", mockMv, 0) == ERROR_SUCCESS)
		config.mockVbattMv = mockMv;

	RegCloseKey(key);
	g_config = config;
}

void InitDeviceState(unsigned long deviceId) {
	InitAutoConnect();

	auto* state = new DeviceState();
	state->deviceId = deviceId;
	state->injectedChannelId = 0;
	state->clientChannelId = 0;
	state->injected = false;
	InitializeCriticalSection(&state->lock);

	EnterCriticalSection(&g_devicesLock);
	g_devices[deviceId] = state;
	LeaveCriticalSection(&g_devicesLock);
}

void DestroyDeviceState(unsigned long deviceId) {
	EnterCriticalSection(&g_devicesLock);
	auto it = g_devices.find(deviceId);
	if (it != g_devices.end()) {
		DeleteCriticalSection(&it->second->lock);
		delete it->second;
		g_devices.erase(it);
	}
	LeaveCriticalSection(&g_devicesLock);
}

DeviceState* GetDeviceState(unsigned long deviceId) {
	EnterCriticalSection(&g_devicesLock);
	DeviceState* result = nullptr;
	auto it = g_devices.find(deviceId);
	if (it != g_devices.end())
		result = it->second;
	LeaveCriticalSection(&g_devicesLock);
	return result;
}

bool IsDeviceId(unsigned long handle) {
	EnterCriticalSection(&g_devicesLock);
	bool found = (g_devices.find(handle) != g_devices.end());
	LeaveCriticalSection(&g_devicesLock);
	return found;
}

static bool ensureInjectedChannel(DeviceState* state) {
	EnterCriticalSection(&state->lock);
	if (!state->injected) {
		unsigned long channelId = 0;
		long rc = LocalConnect(state->deviceId, g_config.protocolId,
		                       g_config.flags, g_config.baudRate, &channelId);
		if (rc == STATUS_NOERROR) {
			state->injectedChannelId = channelId;
			state->injected = true;
		} else {
			LeaveCriticalSection(&state->lock);
			return false;
		}
	}
	LeaveCriticalSection(&state->lock);
	return true;
}

unsigned long ResolveChannelId(unsigned long handle) {
	if (!g_config.autoInject) return handle;

	DeviceState* state = GetDeviceState(handle);
	if (state == nullptr) {
		// Not a device ID — it's already a channel ID, pass through
		return handle;
	}

	// Handle is a device ID being used where a channel ID is expected
	if (state->clientChannelId != 0) {
		// Client already has its own channel — use it
		return state->clientChannelId;
	}

	// Need to inject
	if (ensureInjectedChannel(state)) {
		return state->injectedChannelId;
	}

	// Injection failed — return the handle as-is (will likely produce an error from real driver)
	return handle;
}

void OnClientConnect(unsigned long deviceId, unsigned long channelId) {
	DeviceState* state = GetDeviceState(deviceId);
	if (state == nullptr) return;

	EnterCriticalSection(&state->lock);
	state->clientChannelId = channelId;

	// Tear down injected channel since client now has its own
	if (state->injected && state->injectedChannelId != 0) {
		LocalDisconnect(state->injectedChannelId);
		state->injectedChannelId = 0;
		state->injected = false;
	}
	LeaveCriticalSection(&state->lock);
}

void CleanupBeforeClose(unsigned long deviceId) {
	DeviceState* state = GetDeviceState(deviceId);
	if (state == nullptr) return;

	EnterCriticalSection(&state->lock);
	if (state->injected && state->injectedChannelId != 0) {
		LocalDisconnect(state->injectedChannelId);
		state->injectedChannelId = 0;
		state->injected = false;
	}
	LeaveCriticalSection(&state->lock);

	DestroyDeviceState(deviceId);
}
