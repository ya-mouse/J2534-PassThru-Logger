#include "stdafx.h"
#include "EventRingBuffer.h"
#include <cstring>

EventRingBuffer::EventRingBuffer(unsigned int capacity)
	: m_capacity(capacity), m_head(0), m_count(0), m_hasLast(false) {
	m_buffer = new EventRecord[capacity];
	memset(m_buffer, 0, sizeof(EventRecord) * capacity);
	memset(&m_lastEvent, 0, sizeof(EventRecord));
	InitializeCriticalSection(&m_lock);
}

EventRingBuffer::~EventRingBuffer() {
	DeleteCriticalSection(&m_lock);
	delete[] m_buffer;
}

bool EventRingBuffer::isDuplicate(uint8_t funcId, const uint8_t* params, uint16_t paramsLen) const {
	if (!m_hasLast) return false;
	if (m_lastEvent.funcId != funcId) return false;
	if (m_lastEvent.paramsLen != paramsLen) return false;
	if (paramsLen > 0 && memcmp(m_lastEvent.params, params, paramsLen) != 0) return false;
	return true;
}

void EventRingBuffer::pushNew(uint8_t funcId, int32_t returnCode, const uint8_t* params, uint16_t paramsLen) {
	EventRecord& slot = m_buffer[m_head % m_capacity];
	slot.funcId = funcId;
	slot.timestamp = GetTickCount();
	slot.returnCode = returnCode;
	slot.repeatCount = 1;
	slot.paramsLen = (paramsLen <= EVENT_MAX_PARAMS) ? paramsLen : EVENT_MAX_PARAMS;
	if (params && slot.paramsLen > 0)
		memcpy(slot.params, params, slot.paramsLen);

	// Update last event for dedup
	memcpy(&m_lastEvent, &slot, sizeof(EventRecord));
	m_hasLast = true;

	m_head++;
	if (m_count < m_capacity) m_count++;
}

void EventRingBuffer::push(uint8_t funcId, int32_t returnCode, const uint8_t* params, uint16_t paramsLen) {
	EnterCriticalSection(&m_lock);

	if (isDuplicate(funcId, params, paramsLen)) {
		// Increment repeat count on the last buffer entry
		unsigned int lastIdx = (m_head == 0) ? (m_capacity - 1) : (m_head - 1);
		lastIdx = lastIdx % m_capacity;
		m_buffer[lastIdx].repeatCount++;
		m_lastEvent.repeatCount++;
	} else {
		pushNew(funcId, returnCode, params, paramsLen);
	}

	LeaveCriticalSection(&m_lock);
}

unsigned int EventRingBuffer::count() const {
	return m_count;
}

unsigned int EventRingBuffer::headIndex() const {
	return m_head;
}

unsigned int EventRingBuffer::read(unsigned int startIndex, EventRecord* outBuf, unsigned int maxCount, unsigned int& outNextIndex) const {
	EnterCriticalSection(const_cast<CRITICAL_SECTION*>(&m_lock));

	unsigned int copied = 0;

	// Calculate the oldest available index
	unsigned int oldestAvailable = (m_head > m_capacity) ? (m_head - m_capacity) : 0;

	// Clamp startIndex to oldest available
	if (startIndex < oldestAvailable)
		startIndex = oldestAvailable;

	for (unsigned int i = startIndex; i < m_head && copied < maxCount; i++) {
		unsigned int bufIdx = i % m_capacity;
		memcpy(&outBuf[copied], &m_buffer[bufIdx], sizeof(EventRecord));
		copied++;
	}

	outNextIndex = startIndex + copied;

	LeaveCriticalSection(const_cast<CRITICAL_SECTION*>(&m_lock));
	return copied;
}
