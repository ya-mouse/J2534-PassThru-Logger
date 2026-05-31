#pragma once
#include "stdafx.h"
#include "EventRingBuffer.h"

// Log format options
#define LOG_FORMAT_TEXT 0
#define LOG_FORMAT_JSON 1

struct FileLogConfig {
	std::string filePath;   // Empty = disabled
	DWORD format;           // LOG_FORMAT_TEXT or LOG_FORMAT_JSON
};

class FileLogger {
public:
	FileLogger();
	~FileLogger();

	// Initialize from registry config
	void init(const FileLogConfig& config);

	// Write an event record to the log file
	void logEvent(const EventRecord& event);

	// Flush file buffer
	void flush();

	bool isEnabled() const { return m_enabled; }

private:
	FILE* m_file;
	bool m_enabled;
	DWORD m_format;
	bool m_firstJsonEntry;

	void writeText(const EventRecord& event);
	void writeJson(const EventRecord& event);
	const char* funcName(uint8_t funcId) const;
};
