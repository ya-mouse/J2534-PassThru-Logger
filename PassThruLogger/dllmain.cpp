// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include "RegUtils.h"
#include "Loader4.h"
#include "NetworkWriter.h"
#include "WireProtocolConstants.h"
#include "AutoConnect.h"
#include "EventRingBuffer.h"
#include "FileLogger.h"

unsigned int refcount = 0;
std::string driverKeyName;
bool loadedFine = FALSE;
std::unique_ptr<NetworkWriter> writer;
ConnectConfig connectConfig;

// Global ring buffer and file logger
EventRingBuffer* g_eventBuffer = nullptr;
FileLogger* g_fileLogger = nullptr;
CRITICAL_SECTION g_tcpLock;
static HANDLE g_retryThread = NULL;
static volatile bool g_shutdownRequested = false;

bool loadDriver() {

	HKEY LoggerSettingsKey;
	LONG lRes = RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Passthru Logger", 0, KEY_READ, &LoggerSettingsKey);
	bool bExistsAndSuccess(lRes == ERROR_SUCCESS);
	if (!bExistsAndSuccess) return FALSE;

	GetStringRegKey(LoggerSettingsKey, "DefaultDriverKey", driverKeyName, "");

	//Get the driver file name
	std::string driverKeyFullPath = "SOFTWARE\\PassThruSupport.04.04\\" + driverKeyName;
	HKEY DriverEntryKey;
	lRes = RegOpenKeyExA(HKEY_LOCAL_MACHINE, driverKeyFullPath.c_str(), 0, KEY_READ, &DriverEntryKey);
	bExistsAndSuccess = (lRes == ERROR_SUCCESS);
	if (!bExistsAndSuccess) return FALSE;

	std::string driverFilePath;
	GetStringRegKey(DriverEntryKey, "FunctionLibrary", driverFilePath, "");

	long loadResult = LoadJ2534Dll(driverFilePath.c_str());
	if (loadResult != 0) return FALSE;
	return TRUE;
}

// Send handshake to Control app over TCP
static void sendHandshake() {
	if (!writer || !writer->isConnected()) return;

	writer->writeShort(wireprotover::VER_0_0);
	writer->writeShort(j2534protover::VER_4_4);

	char filename[MAX_PATH];
	GetModuleFileNameA(NULL, filename, MAX_PATH);

	writer->writeByte(msgtype::reportParam);
	writer->writeByte(param::client);
	writer->write(filename);

	writer->writeByte(msgtype::reportParam);
	writer->writeByte(param::driver);
	writer->write(driverKeyName.c_str());
	writer->writeInt(0);

	writer->flush();
}

// Background thread: retry TCP connection every 5 seconds
static DWORD WINAPI tcpRetryThread(LPVOID) {
	while (!g_shutdownRequested) {
		Sleep(5000);
		if (g_shutdownRequested) break;

		EnterCriticalSection(&g_tcpLock);
		if (!writer || !writer->isConnected()) {
			if (!writer)
				writer = std::make_unique<NetworkWriter>(AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP);
			if (writer->connect("localhost", "2534")) {
				sendHandshake();
			}
		}
		LeaveCriticalSection(&g_tcpLock);
	}
	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		refcount++;
		if (refcount == 1) {
			WSADATA wsaData;
			int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
			if (iResult != 0) {
				printf("WSAStartup failed with error: %d\n", iResult);
				break;
			}

			InitializeCriticalSection(&g_tcpLock);

			// Load connect configuration from registry
			LoadConnectConfig(connectConfig);

			// Initialize ring buffer (capacity from registry, default 4096)
			DWORD bufferSize = 4096;
			{
				HKEY key;
				if (RegOpenKeyExA(HKEY_CURRENT_USER, LOGGER_REG_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS) {
					GetDWORDRegKey(key, "EventBufferSize", bufferSize, 4096);
					RegCloseKey(key);
				}
			}
			g_eventBuffer = new EventRingBuffer(bufferSize);

			// Initialize file logger from registry
			g_fileLogger = new FileLogger();
			{
				FileLogConfig logConfig;
				HKEY key;
				if (RegOpenKeyExA(HKEY_CURRENT_USER, LOGGER_REG_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS) {
					GetStringRegKey(key, "LogFilePath", logConfig.filePath, "");
					GetDWORDRegKey(key, "LogFormat", logConfig.format, LOG_FORMAT_TEXT);
					RegCloseKey(key);
				}
				g_fileLogger->init(logConfig);
			}

			// Try TCP connection (non-blocking attempt)
			writer = std::make_unique<NetworkWriter>(AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP);
			if (writer->connect("localhost", "2534")) {
				sendHandshake();
			}

			// Load the real J2534 driver — this is what determines loadedFine
			if (!loadDriver())
				break;

			loadedFine = TRUE;

			// Start background retry thread for TCP
			g_shutdownRequested = false;
			g_retryThread = CreateThread(NULL, 0, tcpRetryThread, NULL, 0, NULL);
		}
		break;
	case DLL_PROCESS_DETACH:
		if (refcount == 0) {
			g_shutdownRequested = true;
			if (g_retryThread) {
				WaitForSingleObject(g_retryThread, 3000);
				CloseHandle(g_retryThread);
				g_retryThread = NULL;
			}

			UnloadJ2534Dll();

			if (g_fileLogger) {
				g_fileLogger->flush();
				delete g_fileLogger;
				g_fileLogger = nullptr;
			}

			delete g_eventBuffer;
			g_eventBuffer = nullptr;

			writer = nullptr;
			DeleteCriticalSection(&g_tcpLock);
			WSACleanup();
		}
		break;
	}
	return TRUE;
}

