#include "stdafx.h"
#include "Loader4.h"
#include "RegUtils.h"
#include "NetworkWriter.h"
#include "WireProtocolConstants.h"
#include "AutoConnect.h"
#include "EventRingBuffer.h"
#include "FileLogger.h"

//Set in DLLMAIN
extern bool loadedFine;
extern std::unique_ptr<NetworkWriter> writer;
extern ConnectConfig connectConfig;
extern EventRingBuffer* g_eventBuffer;
extern FileLogger* g_fileLogger;
extern CRITICAL_SECTION g_tcpLock;

// A quick way to avoid the name mangling that __stdcall liked to do
// On MSVC: uses linker pragma. On mingw: exports via .def file.
#ifdef __GNUC__
#define EXPORT
#else
#define EXPORT comment(linker, "/EXPORT:" __FUNCTION__ "=" __FUNCDNAME__)
#endif

// Check if TCP is available for streaming
static bool tcpConnected() {
	return writer && writer->isConnected();
}

// Record event to ring buffer and file logger
// paramBuf/paramLen: compact representation of key params for dedup
static void recordEvent(uint8_t funcId, int32_t returnCode, const uint8_t* paramBuf = nullptr, uint16_t paramLen = 0) {
	if (g_eventBuffer)
		g_eventBuffer->push(funcId, returnCode, paramBuf, paramLen);

	if (g_fileLogger && g_fileLogger->isEnabled()) {
		EventRecord ev;
		ev.funcId = funcId;
		ev.timestamp = GetTickCount();
		ev.returnCode = returnCode;
		ev.repeatCount = 1;
		ev.paramsLen = paramLen;
		if (paramBuf && paramLen > 0)
			memcpy(ev.params, paramBuf, (paramLen <= EVENT_MAX_PARAMS) ? paramLen : EVENT_MAX_PARAMS);
		g_fileLogger->logEvent(ev);
	}
}

// --- TCP serialization helpers (conditional on connection) ---

bool writeParamPointer(void* maybe_ptr) {
	if (!tcpConnected()) return (maybe_ptr != NULL);
	writer->writeByte(wiredatatype::TYPE_POINTER);
	writer->writeByte(maybe_ptr == NULL ? pointernulltype::POINTER_NULL : pointernulltype::POINTER_NOTNULL);
	return (maybe_ptr != NULL);
}

void writeParamString(char* str) {
	if (!tcpConnected()) return;
	writer->writeByte(wiredatatype::TYPE_STRING);
	writer->write((char*)str);
}

void writeParamDataArray(char* data, unsigned long len) {
	if (!tcpConnected()) return;
	writer->writeByte(wiredatatype::TYPE_DATAARRAY);
	writer->write(data, len);
}

void writeParamInt(int val) {
	if (!tcpConnected()) return;
	writer->writeByte(wiredatatype::TYPE_INT);
	writer->writeInt(val);
}

void writeParamEnumValue(char t, int val) {
	if (!tcpConnected()) return;
	writer->writeByte(t);
	writer->write7BitEncodedInt(val);
}

void writeParamInOutInt(unsigned long preCallValue, unsigned long PostCallValue) {
	if (!tcpConnected()) return;
	writer->writeByte(wiredatatype::TYPE_INOUT_INT);
	writer->write7BitEncodedInt(preCallValue);
	writer->write7BitEncodedInt(PostCallValue);
}

void writeParamMsg(PASSTHRU_MSG& msg) {
	if (!tcpConnected()) return;
	writer->writeByte(wiredatatype::TYPE_MSG);

	writer->write7BitEncodedInt(msg.ProtocolID);
	writer->write7BitEncodedInt(msg.RxStatus);
	writer->write7BitEncodedInt(msg.TxFlags);
	writer->write7BitEncodedInt(msg.Timestamp);
	writer->write7BitEncodedInt(msg.ExtraDataIndex);
	writer->write((const char*)msg.Data, msg.DataSize);
}

void writeParamArrayStart(unsigned long count) {
	if (!tcpConnected()) return;
	writer->writeByte(wiredatatype::TYPE_ARRAY);
	writer->write7BitEncodedInt(count);
}

long writeParamEnd(long ret) {
	if (!tcpConnected()) return ret;
	writer->writeByte(wiredatatype::TYPE_END);
	writer->writeInt(ret);
	writer->flush();
	return ret;
}

void writeApiMsgHeader(J2534_0404func func) {
	if (!tcpConnected()) return;
	writer->writeByte(msgtype::J2534Msg);
	writer->writeByte(func);
}

//Api wrappers

PANDAJ2534DLL_API long PTAPI PassThruOpen(void *pName, unsigned long *pDeviceID) {
	#pragma EXPORT
	if (!loadedFine) return ERR_DEVICE_NOT_CONNECTED;

	// Use configured device name if pName is NULL and config has one
	void* effectiveName = pName;
	if (pName == NULL && !connectConfig.deviceName.empty()) {
		effectiveName = (void*)connectConfig.deviceName.c_str();
	}

	auto res = LocalOpen(effectiveName, pDeviceID);

	// Track device state for auto-inject
	if (res == STATUS_NOERROR && pDeviceID != NULL) {
		InitDeviceState(*pDeviceID);
	}

	// Record event (param: deviceID result)
	if (pDeviceID) {
		uint8_t buf[4];
		memcpy(buf, pDeviceID, 4);
		recordEvent(J2534_0404func::API_PassThruOpen, res, buf, 4);
	} else {
		recordEvent(J2534_0404func::API_PassThruOpen, res);
	}

	writeApiMsgHeader(J2534_0404func::API_PassThruOpen);

	if (writeParamPointer(pName))
		writeParamString((char*)pName);
	if (writeParamPointer(pDeviceID))
		writeParamInt(*pDeviceID);

	return writeParamEnd(res);
}

PANDAJ2534DLL_API long PTAPI PassThruClose(unsigned long DeviceID) {
	#pragma EXPORT
	if (!loadedFine) return ERR_DEVICE_NOT_CONNECTED;

	// Tear down any injected channel before closing
	CleanupBeforeClose(DeviceID);

	auto res = LocalClose(DeviceID);

	uint8_t buf[4];
	memcpy(buf, &DeviceID, 4);
	recordEvent(J2534_0404func::API_PassThruClose, res, buf, 4);

	writeApiMsgHeader(J2534_0404func::API_PassThruClose);

	writeParamInt(DeviceID);

	return writeParamEnd(res);
}

PANDAJ2534DLL_API long PTAPI PassThruConnect(unsigned long DeviceID, unsigned long ProtocolID,
											unsigned long Flags, unsigned long BaudRate, unsigned long *pChannelID) {
	#pragma EXPORT
	if (!loadedFine) return ERR_DEVICE_NOT_CONNECTED;
	auto res = LocalConnect(DeviceID, ProtocolID, Flags, BaudRate, pChannelID);

	// Track client's own channel for auto-inject logic
	if (res == STATUS_NOERROR && pChannelID != NULL) {
		OnClientConnect(DeviceID, *pChannelID);
	}

	// Record: DeviceID + ProtocolID + BaudRate
	uint8_t buf[12];
	memcpy(buf, &DeviceID, 4);
	memcpy(buf + 4, &ProtocolID, 4);
	memcpy(buf + 8, &BaudRate, 4);
	recordEvent(J2534_0404func::API_PassThruConnect, res, buf, 12);

	writeApiMsgHeader(J2534_0404func::API_PassThruConnect);

	writeParamInt(DeviceID);
	writeParamEnumValue(wiredatatype::TYPE_J2534_PROTOCOL_ID, ProtocolID);
	writeParamEnumValue(wiredatatype::TYPE_J2534_CONNECT_FLAGS, Flags);
	writeParamInt(BaudRate);
	if (writeParamPointer(pChannelID))
		writeParamInt(*pChannelID);

	return writeParamEnd(res);
}

PANDAJ2534DLL_API long PTAPI PassThruDisconnect(unsigned long ChannelID) {
	#pragma EXPORT
	if (!loadedFine) return ERR_DEVICE_NOT_CONNECTED;
	auto res = LocalDisconnect(ChannelID);

	// If client disconnected its own channel, reset state so re-injection can occur
	if (res == STATUS_NOERROR) {
		OnClientDisconnect(ChannelID);
	}

	uint8_t buf[4];
	memcpy(buf, &ChannelID, 4);
	recordEvent(J2534_0404func::API_PassThruDisconnect, res, buf, 4);

	writeApiMsgHeader(J2534_0404func::API_PassThruDisconnect);

	writeParamInt(ChannelID);

	return writeParamEnd(res);
}

PANDAJ2534DLL_API long PTAPI PassThruReadMsgs(unsigned long ChannelID, PASSTHRU_MSG *pMsg, unsigned long *pNumMsgs, unsigned long Timeout) {
	#pragma EXPORT
	if (!loadedFine) return ERR_DEVICE_NOT_CONNECTED;
	unsigned long preCallNumMsgs = *pNumMsgs;
	auto res = LocalReadMsgs(ChannelID, pMsg, pNumMsgs, Timeout);

	// Record: ChannelID + Timeout (compact for dedup — polling produces many identical calls)
	uint8_t buf[8];
	memcpy(buf, &ChannelID, 4);
	memcpy(buf + 4, &Timeout, 4);
	recordEvent(J2534_0404func::API_PassThruReadMsgs, res, buf, 8);

	writeApiMsgHeader(J2534_0404func::API_PassThruReadMsgs);

	writeParamInt(ChannelID);
	if (writeParamPointer(pMsg)) {
		if (pNumMsgs == NULL)
			writeParamString("{INV LEN}");
		else {
			writeParamArrayStart(*pNumMsgs);
			for (int i = 0; i < *pNumMsgs; i++)
				writeParamMsg(pMsg[i]);
		}
	}
	if (writeParamPointer(pNumMsgs))
		writeParamInOutInt(preCallNumMsgs, *pNumMsgs);
	writeParamInt(Timeout);

	return writeParamEnd(res);
}

PANDAJ2534DLL_API long PTAPI PassThruWriteMsgs(unsigned long ChannelID, PASSTHRU_MSG *pMsg, unsigned long *pNumMsgs, unsigned long Timeout) {
	#pragma EXPORT
	if (!loadedFine) return ERR_DEVICE_NOT_CONNECTED;
	unsigned long preCallNumMsgs = *pNumMsgs;
	auto res = LocalWriteMsgs(ChannelID, pMsg, pNumMsgs, Timeout);

	uint8_t buf[8];
	memcpy(buf, &ChannelID, 4);
	memcpy(buf + 4, &Timeout, 4);
	recordEvent(J2534_0404func::API_PassThruWriteMsgs, res, buf, 8);

	writeApiMsgHeader(J2534_0404func::API_PassThruWriteMsgs);

	writeParamInt(ChannelID);
	if (writeParamPointer(pMsg)) {
		if (pNumMsgs == NULL)
			writeParamString("{INV LEN}");
		else {
			writeParamArrayStart(*pNumMsgs);
			for (int i = 0; i < *pNumMsgs; i++)
				writeParamMsg(pMsg[i]);
		}
	}
	if (writeParamPointer(pNumMsgs))
		writeParamInOutInt(preCallNumMsgs, *pNumMsgs);
	writeParamInt(Timeout);

	return writeParamEnd(res);
}

PANDAJ2534DLL_API long PTAPI PassThruStartPeriodicMsg(unsigned long ChannelID, PASSTHRU_MSG *pMsg, unsigned long *pMsgID, unsigned long TimeInterval) {
	#pragma EXPORT
	if (!loadedFine) return ERR_DEVICE_NOT_CONNECTED;
	auto res = LocalStartPeriodicMsg(ChannelID, pMsg, pMsgID, TimeInterval);

	uint8_t buf[4];
	memcpy(buf, &ChannelID, 4);
	recordEvent(J2534_0404func::API_PassThruStartPeriodicMsg, res, buf, 4);

	writeApiMsgHeader(J2534_0404func::API_PassThruStartPeriodicMsg);

	writeParamInt(ChannelID);
	if (writeParamPointer(pMsg))
		writeParamMsg(*pMsg);
	if (writeParamPointer(pMsgID))
		writeParamInt(*pMsgID);
	writeParamInt(TimeInterval);

	return writeParamEnd(res);
}

PANDAJ2534DLL_API long PTAPI PassThruStopPeriodicMsg(unsigned long ChannelID, unsigned long MsgID) {
	#pragma EXPORT
	if (!loadedFine) return ERR_DEVICE_NOT_CONNECTED;
	auto res = LocalStopPeriodicMsg(ChannelID, MsgID);

	uint8_t buf[8];
	memcpy(buf, &ChannelID, 4);
	memcpy(buf + 4, &MsgID, 4);
	recordEvent(J2534_0404func::API_PassThruStopPeriodicMsg, res, buf, 8);

	writeApiMsgHeader(J2534_0404func::API_PassThruStopPeriodicMsg);

	writeParamInt(ChannelID);
	writeParamInt(MsgID);

	return writeParamEnd(res);
}

PANDAJ2534DLL_API long PTAPI PassThruStartMsgFilter(unsigned long ChannelID, unsigned long FilterType, PASSTHRU_MSG *pMaskMsg,
													PASSTHRU_MSG *pPatternMsg, PASSTHRU_MSG *pFlowControlMsg, unsigned long *pFilterID) {
	#pragma EXPORT
	if (!loadedFine) return ERR_DEVICE_NOT_CONNECTED;
	auto res = LocalStartMsgFilter(ChannelID, FilterType, pMaskMsg, pPatternMsg, pFlowControlMsg, pFilterID);

	uint8_t buf[8];
	memcpy(buf, &ChannelID, 4);
	memcpy(buf + 4, &FilterType, 4);
	recordEvent(J2534_0404func::API_PassThruStartMsgFilter, res, buf, 8);

	writeApiMsgHeader(J2534_0404func::API_PassThruStartMsgFilter);

	writeParamInt(ChannelID);
	writeParamEnumValue(wiredatatype::TYPE_J2534_FILTER_TYPE, FilterType);
	if (writeParamPointer(pMaskMsg))
		writeParamMsg(*pMaskMsg);
	if (writeParamPointer(pPatternMsg))
		writeParamMsg(*pPatternMsg);
	if (writeParamPointer(pFlowControlMsg))
		writeParamMsg(*pFlowControlMsg);
	writeParamInt(*pFilterID);

	return writeParamEnd(res);
}

PANDAJ2534DLL_API long PTAPI PassThruStopMsgFilter(unsigned long ChannelID, unsigned long FilterID) {
	#pragma EXPORT
	if (!loadedFine) return ERR_DEVICE_NOT_CONNECTED;
	auto res = LocalStopMsgFilter(ChannelID, FilterID);

	uint8_t buf[8];
	memcpy(buf, &ChannelID, 4);
	memcpy(buf + 4, &FilterID, 4);
	recordEvent(J2534_0404func::API_PassThruStopMsgFilter, res, buf, 8);

	writeApiMsgHeader(J2534_0404func::API_PassThruStopMsgFilter);

	writeParamInt(ChannelID);
	writeParamInt(FilterID);

	return writeParamEnd(res);
}

PANDAJ2534DLL_API long PTAPI PassThruSetProgrammingVoltage(unsigned long DeviceID, unsigned long PinNumber, unsigned long Voltage) {
	#pragma EXPORT
	if (!loadedFine) return ERR_DEVICE_NOT_CONNECTED;
	auto res = LocalSetProgrammingVoltage(DeviceID, PinNumber, Voltage);

	uint8_t buf[12];
	memcpy(buf, &DeviceID, 4);
	memcpy(buf + 4, &PinNumber, 4);
	memcpy(buf + 8, &Voltage, 4);
	recordEvent(J2534_0404func::API_PassThruSetProgrammingVoltage, res, buf, 12);

	writeApiMsgHeader(J2534_0404func::API_PassThruSetProgrammingVoltage);

	writeParamInt(DeviceID);
	writeParamEnumValue(wiredatatype::TYPE_J2534_PROG_VOLTAGE_PIN_NUMBER, PinNumber);
	writeParamEnumValue(wiredatatype::TYPE_J2534_PROG_VOLTAGE, Voltage);

	return writeParamEnd(res);
}

PANDAJ2534DLL_API long PTAPI PassThruReadVersion(unsigned long DeviceID, char *pFirmwareVersion, char *pDllVersion, char *pApiVersion) {
	#pragma EXPORT
	if (!loadedFine) return ERR_DEVICE_NOT_CONNECTED;
	auto res = LocalReadVersion(DeviceID, pFirmwareVersion, pDllVersion, pApiVersion);

	uint8_t buf[4];
	memcpy(buf, &DeviceID, 4);
	recordEvent(J2534_0404func::API_PassThruReadVersion, res, buf, 4);

	writeApiMsgHeader(J2534_0404func::API_PassThruReadVersion);

	writeParamInt(DeviceID);
	if (writeParamPointer(pFirmwareVersion))
		writeParamString(pFirmwareVersion);
	if (writeParamPointer(pDllVersion))
		writeParamString(pDllVersion);
	if (writeParamPointer(pApiVersion))
		writeParamString(pApiVersion);

	return writeParamEnd(res);
}

PANDAJ2534DLL_API long PTAPI PassThruGetLastError(char *pErrorDescription) {
	#pragma EXPORT
	if (!loadedFine) return ERR_DEVICE_NOT_CONNECTED;
	auto res = LocalGetLastError(pErrorDescription);

	recordEvent(J2534_0404func::API_PassThruGetLastError, res);

	writeApiMsgHeader(J2534_0404func::API_PassThruGetLastError);

	if (writeParamPointer(pErrorDescription))
		writeParamString(pErrorDescription);

	return writeParamEnd(res);
}

PANDAJ2534DLL_API long PTAPI PassThruIoctl(unsigned long ChannelID, unsigned long IoctlID, void *pInput, void *pOutput) {
	#pragma EXPORT
	if (!loadedFine) return ERR_DEVICE_NOT_CONNECTED;

	// Only inject channel for READ_VBATT when used with a device ID
	unsigned long resolvedId = ChannelID;
	if (IoctlID == READ_VBATT && connectConfig.autoInject) {
		resolvedId = ResolveChannelId(ChannelID);
	}

	auto res = LocalIoctl(resolvedId, IoctlID, pInput, pOutput);

	// Mock VBATT: if real driver returns ERR_NOT_SUPPORTED and mock is configured
	if (IoctlID == READ_VBATT && res == ERR_NOT_SUPPORTED && connectConfig.mockVbattMv > 0 && pOutput != NULL) {
		*((unsigned long*)pOutput) = connectConfig.mockVbattMv;
		res = STATUS_NOERROR;
	}

	// Record: ChannelID + IoctlID (compact for dedup)
	uint8_t buf[8];
	memcpy(buf, &ChannelID, 4);
	memcpy(buf + 4, &IoctlID, 4);
	recordEvent(J2534_0404func::API_PassThruIoctl, res, buf, 8);

	writeApiMsgHeader(J2534_0404func::API_PassThruIoctl);

	writeParamInt(ChannelID);
	writeParamEnumValue(wiredatatype::TYPE_J2534_IOCTL, IoctlID);

	switch (IoctlID) {
	case GET_CONFIG:								// pInput = SCONFIG_LIST, pOutput = NULL
	case SET_CONFIG:								// pInput = SCONFIG_LIST, pOutput = NULL
	{
		if (writeParamPointer(pInput)) { //pInput
			SCONFIG_LIST& configList = *(SCONFIG_LIST*)pInput;
			if (configList.ConfigPtr == NULL) {
				writeParamString("{NULL}");
			} else {
				writeParamArrayStart(configList.NumOfParams);
				for (int i = 0; i < configList.NumOfParams; i++) {
					SCONFIG& config = configList.ConfigPtr[i];
					writeParamArrayStart(2);
					writeParamEnumValue(wiredatatype::TYPE_J2534_CONFIG_PARAMS, config.Parameter);

					switch (config.Parameter) {
					case DATA_RATE:					// 5-500000
						writeParamInt(config.Value);
						break;

					case LOOPBACK:					// 0 (OFF), 1 (ON) [0]
						writeParamInt(config.Value);
						break;

					case NODE_ADDRESS:				// J1850PWM: 0x00-0xFF
					case ISO15765_BS:				// ISO15765: 0x0-0xFF [0]
					case ISO15765_STMIN:			// ISO15765: 0x0-0xFF [0]
					case ISO15765_WFT_MAX:			// ISO15765: 0x0-0xFF [0]
						writeParamInt(config.Value);
						break;

					case BIT_SAMPLE_POINT:			// CAN: 0-100 (1% per bit) [80]
					case SYNC_JUMP_WIDTH:			// CAN: 0-100 (1% per bit) [15]
						writeParamInt(config.Value);
						break;

					case NETWORK_LINE:				// J1850PWM: 0 (BUS_NORMAL), 1 (BUS_PLUS), 2 (BUS_MINUS) [0]
					case PARITY:					// ISO9141 or ISO14230: 0 (NO_PARITY), 1 (ODD_PARITY), 2 (EVEN_PARITY) [0]
					case DATA_BITS:					// ISO9141 or ISO14230: 0 (8 data bits), 1 (7 data bits) [0]
					case FIVE_BAUD_MOD:				// ISO9141 or ISO14230: 0 (ISO 9141-2/14230-4), 1 (Inv KB2), 2 (Inv Addr), 3 (ISO 9141) [0]
						writeParamInt(config.Value);
						break;

					case P1_MIN:					// ISO9141 or ISO14230: Not used by interface
					case P2_MIN:					// ISO9141 or ISO14230: Not used by interface
					case P2_MAX:					// ISO9141 or ISO14230: Not used by interface
					case P3_MAX:					// ISO9141 or ISO14230: Not used by interface
					case P4_MAX:					// ISO9141 or ISO14230: Not used by interface
						writeParamString("{UNUSED}");
						break;

					case P1_MAX:					// ISO9141 or ISO14230: 0x1-0xFFFF (.5 ms per bit) [40 (20ms)]
					case P3_MIN:					// ISO9141 or ISO14230: 0x0-0xFFFF (.5 ms per bit) [110 (55ms)]
					case P4_MIN:					// ISO9141 or ISO14230: 0x0-0xFFFF (.5 ms per bit) [10 (5ms)]
					case W0:						// ISO9141: 0x0-0xFFFF (1 ms per bit) [300]
					case W1:						// ISO9141 or ISO14230: 0x0-0xFFFF (1 ms per bit) [300]
					case W2:						// ISO9141 or ISO14230: 0x0-0xFFFF (1 ms per bit) [20]
					case W3:						// ISO9141 or ISO14230: 0x0-0xFFFF (1 ms per bit) [20]
					case W4:						// ISO9141 or ISO14230: 0x0-0xFFFF (1 ms per bit) [50]
					case W5:						// ISO9141 or ISO14230: 0x0-0xFFFF (1 ms per bit) [300]
					case TIDLE:						// ISO9141 or ISO14230: 0x0-0xFFFF (1 ms per bit) [300]
					case TINIL:						// ISO9141 or ISO14230: 0x0-0xFFFF (1 ms per bit) [25]
					case TWUP:						// ISO9141 or ISO14230: 0x0-0xFFFF (1 ms per bit) [50]
					case T1_MAX:					// SCI: 0x0-0xFFFF (1 ms per bit) [20]
					case T2_MAX:					// SCI: 0x0-0xFFFF (1 ms per bit) [100]
					case T3_MAX:					// SCI: 0x0-0xFFFF (1 ms per bit) [50]
					case T4_MAX:					// SCI: 0x0-0xFFFF (1 ms per bit) [20]
					case T5_MAX:					// SCI: 0x0-0xFFFF (1 ms per bit) [100]
					case ISO15765_BS_TX:			// ISO15765: 0x0-0xFF,0xFFFF [0xFFFF]
					case ISO15765_STMIN_TX:			// ISO15765: 0x0-0xFF,0xFFFF [0xFFFF]
						writeParamInt(config.Value);
						break;

					default:
						writeParamInt(config.Value);
						break;
					}
				}
			}
		}
		writeParamPointer(NULL); //pOutput
		break;
	}

	case FIVE_BAUD_INIT:							// pInput = SBYTE_ARRAY, pOutput = SBYTE_ARRAY
	{
		SBYTE_ARRAY *s = (SBYTE_ARRAY*)pInput;
		if (writeParamPointer(pInput)) { //pInput
			if (s->BytePtr == NULL)
				writeParamString("{NULL}");
			else
				writeParamDataArray((char *)s->BytePtr, s->NumOfBytes);
		}

		s = (SBYTE_ARRAY*)pOutput;
		if (writeParamPointer(pOutput)) { //pOutput
			if (s->BytePtr == NULL)
				writeParamString("{NULL}");
			else
				writeParamDataArray((char *)s->BytePtr, s->NumOfBytes);
		}
		break;
	}

	case FAST_INIT:									// pInput = PASSTHRU_MSG, pOutput = PASSTHRU_MSG
		if (writeParamPointer(pInput))
			writeParamMsg(*(PASSTHRU_MSG*)pInput);
		if (writeParamPointer(pOutput))
			writeParamMsg(*(PASSTHRU_MSG*)pOutput);
		break;

	case CLEAR_TX_BUFFER:							// pInput = NULL, pOutput = NULL
	case CLEAR_RX_BUFFER:							// pInput = NULL, pOutput = NULL
	case CLEAR_PERIODIC_MSGS:						// pInput = NULL, pOutput = NULL
	case CLEAR_MSG_FILTERS:							// pInput = NULL, pOutput = NULL
	case CLEAR_FUNCT_MSG_LOOKUP_TABLE:				// pInput = NULL, pOutput = NULL
	case SW_CAN_HS:									// pInput = NULL, pOutput = NULL
	case SW_CAN_NS:									// pInput = NULL, pOutput = NULL
		writeParamPointer(NULL); //pInput
		writeParamPointer(NULL); //pOutput
		break;

	case ADD_TO_FUNCT_MSG_LOOKUP_TABLE:				// pInput = SBYTE_ARRAY, pOutput = NULL
	case DELETE_FROM_FUNCT_MSG_LOOKUP_TABLE:		// pInput = SBYTE_ARRAY, pOutput = NULL
	case SET_POLL_RESPONSE:							// pInput = SBYTE_ARRAY, pOutput = NULL
	{
		SBYTE_ARRAY *s = (SBYTE_ARRAY*)pInput;
		if (writeParamPointer(pInput)) { //pInput
			if (s->BytePtr == NULL)
				writeParamString("{NULL}");
			else
				writeParamDataArray((char *)s->BytePtr, s->NumOfBytes);
		}
		writeParamPointer(NULL); //pOutput
		break;
	}

	case READ_VBATT:								// pInput = NULL, pOutput = unsigned long
	case READ_PROG_VOLTAGE:							// pInput = NULL, pOutput = unsigned long
		writeParamPointer(NULL); //pInput
		if (writeParamPointer(pOutput))
			writeParamInt((int)pOutput);
		break;

	case BECOME_MASTER:								// pInput = unsigned char, pOutput = NULL
		writeParamInt((int)pInput); //pInput
		writeParamPointer(NULL); //pOutput
		break;

	default:
		writeParamInt((int)pInput); //pInput
		writeParamInt((int)pOutput); //pOutput
	}

	return writeParamEnd(res);
}
