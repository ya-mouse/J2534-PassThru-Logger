#pragma once
#include "stdafx.h"
#include "WireProtocolConstants.h"

// Maximum size of serialized parameters per event
#define EVENT_MAX_PARAMS 512

// Compact event record stored in the ring buffer
struct EventRecord {
	uint8_t funcId;           // J2534_0404func enum value
	uint32_t timestamp;       // GetTickCount() at first occurrence
	int32_t returnCode;       // J2534 return code
	uint32_t repeatCount;     // 1 = single occurrence, N = deduplicated
	uint16_t paramsLen;       // Length of serialized params
	uint8_t params[EVENT_MAX_PARAMS]; // Serialized parameter blob
};

// Thread-safe circular buffer for EventRecords
class EventRingBuffer {
public:
	EventRingBuffer(unsigned int capacity = 4096);
	~EventRingBuffer();

	// Push a new event (handles deduplication internally)
	void push(uint8_t funcId, int32_t returnCode, const uint8_t* params, uint16_t paramsLen);

	// Get total number of stored events
	unsigned int count() const;

	// Read events from startIndex (inclusive). Caller provides output array and max count.
	// Returns number of events copied. Sets outNextIndex to the index after last copied.
	unsigned int read(unsigned int startIndex, EventRecord* outBuf, unsigned int maxCount, unsigned int& outNextIndex) const;

	// Get current write index (for "read from here" on new connections)
	unsigned int headIndex() const;

private:
	EventRecord* m_buffer;
	unsigned int m_capacity;
	unsigned int m_head;       // Next write position
	unsigned int m_count;      // Total events written (wraps)
	CRITICAL_SECTION m_lock;

	// Last event for deduplication comparison
	EventRecord m_lastEvent;
	bool m_hasLast;

	bool isDuplicate(uint8_t funcId, const uint8_t* params, uint16_t paramsLen) const;
	void pushNew(uint8_t funcId, int32_t returnCode, const uint8_t* params, uint16_t paramsLen);
};
