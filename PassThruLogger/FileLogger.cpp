#include "stdafx.h"
#include "FileLogger.h"
#include <cstdio>
#include <ctime>

static const char* g_funcNames[] = {
	"PassThruOpen",
	"PassThruClose",
	"PassThruConnect",
	"PassThruDisconnect",
	"PassThruReadMsgs",
	"PassThruWriteMsgs",
	"PassThruStartPeriodicMsg",
	"PassThruStopPeriodicMsg",
	"PassThruStartMsgFilter",
	"PassThruStopMsgFilter",
	"PassThruSetProgrammingVoltage",
	"PassThruReadVersion",
	"PassThruGetLastError",
	"PassThruIoctl"
};

FileLogger::FileLogger() : m_file(nullptr), m_enabled(false), m_format(LOG_FORMAT_TEXT), m_firstJsonEntry(true) {}

FileLogger::~FileLogger() {
	if (m_file) {
		if (m_format == LOG_FORMAT_JSON)
			fprintf(m_file, "\n]\n");
		fflush(m_file);
		fclose(m_file);
	}
}

void FileLogger::init(const FileLogConfig& config) {
	if (config.filePath.empty()) {
		m_enabled = false;
		return;
	}

	m_format = config.format;
	m_file = fopen(config.filePath.c_str(), "a");
	if (!m_file) {
		m_enabled = false;
		return;
	}

	m_enabled = true;

	if (m_format == LOG_FORMAT_JSON) {
		// Check if file is empty → start array
		fseek(m_file, 0, SEEK_END);
		if (ftell(m_file) == 0) {
			fprintf(m_file, "[\n");
		} else {
			// Existing file: assume we're appending inside the array
			// Seek back to overwrite the trailing "]\n" if present
			m_firstJsonEntry = false;
		}
	}
}

const char* FileLogger::funcName(uint8_t funcId) const {
	if (funcId < sizeof(g_funcNames) / sizeof(g_funcNames[0]))
		return g_funcNames[funcId];
	return "Unknown";
}

void FileLogger::logEvent(const EventRecord& event) {
	if (!m_enabled || !m_file) return;

	if (m_format == LOG_FORMAT_JSON)
		writeJson(event);
	else
		writeText(event);
}

void FileLogger::writeText(const EventRecord& event) {
	// Timestamp as HH:MM:SS.mmm
	DWORD tick = event.timestamp;
	unsigned int ms = tick % 1000;
	unsigned int sec = (tick / 1000) % 60;
	unsigned int min = (tick / 60000) % 60;
	unsigned int hr = (tick / 3600000) % 24;

	fprintf(m_file, "[%02u:%02u:%02u.%03u] %s() -> 0x%08X",
		hr, min, sec, ms, funcName(event.funcId), (unsigned int)event.returnCode);

	if (event.repeatCount > 1)
		fprintf(m_file, " ... repeated %u times", event.repeatCount);

	fprintf(m_file, "\n");
}

void FileLogger::writeJson(const EventRecord& event) {
	if (!m_firstJsonEntry)
		fprintf(m_file, ",\n");
	m_firstJsonEntry = false;

	fprintf(m_file, "  {\"func\":\"%s\",\"funcId\":%u,\"timestamp\":%u,\"returnCode\":%d,\"count\":%u}",
		funcName(event.funcId), (unsigned int)event.funcId,
		(unsigned int)event.timestamp, (int)event.returnCode,
		(unsigned int)event.repeatCount);
}

void FileLogger::flush() {
	if (m_file) fflush(m_file);
}
