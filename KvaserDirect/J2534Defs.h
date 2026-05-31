#pragma once
// KvaserDirect — J2534 DLL using Kvaser CANlib directly
// Replaces stock Kvaser j2534api.dll to avoid ISO-TP frame error issues

#include <windows.h>

// J2534 calling convention
#define PTAPI __stdcall

// DLL export macro
#ifdef KVASERDIRECT_EXPORTS
#define KD_API extern "C" __declspec(dllexport)
#else
#define KD_API extern "C" __declspec(dllimport)
#endif

//
// J2534-1 v04.04 ProtocolID Values
//
#define J2534_J1850VPW          0x01
#define J2534_J1850PWM          0x02
#define J2534_ISO9141           0x03
#define J2534_ISO14230          0x04
#define J2534_CAN               0x05
#define J2534_ISO15765          0x06
#define J2534_SCI_A_ENGINE      0x07
#define J2534_SCI_A_TRANS       0x08
#define J2534_SCI_B_ENGINE      0x09
#define J2534_SCI_B_TRANS       0x0A

// J2534-2 CAN FD protocol IDs
#define J2534_FD_CAN_CH1        0x00009000
#define J2534_FD_ISO15765_CH1   0x00009400

//
// J2534-1 v04.04 Error Values
//
#define STATUS_NOERROR              0x00
#define ERR_NOT_SUPPORTED           0x01
#define ERR_INVALID_CHANNEL_ID      0x02
#define ERR_INVALID_PROTOCOL_ID     0x03
#define ERR_NULL_PARAMETER          0x04
#define ERR_INVALID_IOCTL_VALUE     0x05
#define ERR_INVALID_FLAGS           0x06
#define ERR_FAILED                  0x07
#define ERR_DEVICE_NOT_CONNECTED    0x08
#define ERR_TIMEOUT                 0x09
#define ERR_INVALID_MSG             0x0A
#define ERR_INVALID_TIME_INTERVAL   0x0B
#define ERR_EXCEEDED_LIMIT          0x0C
#define ERR_INVALID_MSG_ID          0x0D
#define ERR_DEVICE_IN_USE           0x0E
#define ERR_INVALID_IOCTL_ID        0x0F
#define ERR_BUFFER_EMPTY            0x10
#define ERR_BUFFER_FULL             0x11
#define ERR_BUFFER_OVERFLOW         0x12
#define ERR_PIN_INVALID             0x13
#define ERR_CHANNEL_IN_USE          0x14
#define ERR_MSG_PROTOCOL_ID         0x15
#define ERR_INVALID_FILTER_ID       0x16
#define ERR_NO_FLOW_CONTROL         0x17
#define ERR_NOT_UNIQUE              0x18
#define ERR_INVALID_BAUDRATE        0x19
#define ERR_INVALID_DEVICE_ID       0x1A

//
// Connect Flags
//
#define CAN_29BIT_ID                0x0100
#define ISO9141_NO_CHECKSUM         0x0200
#define CAN_ID_BOTH                 0x0800
#define ISO9141_K_LINE_ONLY         0x1000

//
// Filter Types
//
#define PASS_FILTER                 0x00000001
#define BLOCK_FILTER                0x00000002
#define FLOW_CONTROL_FILTER         0x00000003

//
// IOCTL IDs
//
#define GET_CONFIG                  0x01
#define SET_CONFIG                  0x02
#define READ_VBATT                  0x03
#define FIVE_BAUD_INIT              0x04
#define FAST_INIT                   0x05
#define CLEAR_TX_BUFFER             0x07
#define CLEAR_RX_BUFFER             0x08
#define CLEAR_PERIODIC_MSGS         0x09
#define CLEAR_MSG_FILTERS           0x0A
#define CLEAR_FUNCT_MSG_LOOKUP_TABLE      0x0B
#define ADD_TO_FUNCT_MSG_LOOKUP_TABLE     0x0C
#define DELETE_FROM_FUNCT_MSG_LOOKUP_TABLE 0x0D
#define READ_PROG_VOLTAGE           0x0E

//
// Configuration Parameter IDs
//
#define DATA_RATE                   0x01
#define LOOPBACK                    0x03
#define NODE_ADDRESS                0x04
#define NETWORK_LINE                0x05
#define BIT_SAMPLE_POINT            0x17
#define SYNC_JUMP_WIDTH             0x18
#define ISO15765_BS                 0x1E
#define ISO15765_STMIN              0x1F
#define ISO15765_BS_TX              0x22
#define ISO15765_STMIN_TX           0x23
#define ISO15765_WFT_MAX            0x25
#define CAN_MIXED_FORMAT            0x00008000
#define ISO15765_PAD_VALUE          0x00008028

// J2534-2 CAN FD config
#define FD_CAN_DATA_PHASE_RATE      0x00008029

//
// RxStatus flags
//
#define TX_MSG_TYPE                 0x0001
#define START_OF_MESSAGE            0x0002
#define RX_BREAK                    0x0004
#define TX_INDICATION               0x0008
#define ISO15765_PADDING_ERROR      0x0010
#define ISO15765_ADDR_TYPE          0x0080

//
// TxFlags
//
#define ISO15765_FRAME_PAD          0x0040
#define WAIT_P3_MIN_ONLY            0x0200

//
// Structures
//
typedef struct {
    unsigned long Parameter;
    unsigned long Value;
} SCONFIG;

typedef struct {
    unsigned long NumOfParams;
    SCONFIG*      ConfigPtr;
} SCONFIG_LIST;

typedef struct {
    unsigned long NumOfBytes;
    unsigned char* BytePtr;
} SBYTE_ARRAY;

typedef struct {
    unsigned long ProtocolID;
    unsigned long RxStatus;
    unsigned long TxFlags;
    unsigned long Timestamp;
    unsigned long DataSize;
    unsigned long ExtraDataIndex;
    unsigned char Data[4128];
} PASSTHRU_MSG;

//
// J2534 API exports
//
KD_API long PTAPI PassThruOpen(void *pName, unsigned long *pDeviceID);
KD_API long PTAPI PassThruClose(unsigned long DeviceID);
KD_API long PTAPI PassThruConnect(unsigned long DeviceID, unsigned long ProtocolID,
                                   unsigned long Flags, unsigned long BaudRate,
                                   unsigned long *pChannelID);
KD_API long PTAPI PassThruDisconnect(unsigned long ChannelID);
KD_API long PTAPI PassThruReadMsgs(unsigned long ChannelID, PASSTHRU_MSG *pMsg,
                                    unsigned long *pNumMsgs, unsigned long Timeout);
KD_API long PTAPI PassThruWriteMsgs(unsigned long ChannelID, PASSTHRU_MSG *pMsg,
                                     unsigned long *pNumMsgs, unsigned long Timeout);
KD_API long PTAPI PassThruStartPeriodicMsg(unsigned long ChannelID, PASSTHRU_MSG *pMsg,
                                            unsigned long *pMsgID, unsigned long TimeInterval);
KD_API long PTAPI PassThruStopPeriodicMsg(unsigned long ChannelID, unsigned long MsgID);
KD_API long PTAPI PassThruStartMsgFilter(unsigned long ChannelID, unsigned long FilterType,
                                          PASSTHRU_MSG *pMaskMsg, PASSTHRU_MSG *pPatternMsg,
                                          PASSTHRU_MSG *pFlowControlMsg, unsigned long *pFilterID);
KD_API long PTAPI PassThruStopMsgFilter(unsigned long ChannelID, unsigned long FilterID);
KD_API long PTAPI PassThruSetProgrammingVoltage(unsigned long DeviceID,
                                                 unsigned long PinNumber, unsigned long Voltage);
KD_API long PTAPI PassThruReadVersion(unsigned long DeviceID, char *pFirmwareVersion,
                                       char *pDllVersion, char *pApiVersion);
KD_API long PTAPI PassThruGetLastError(char *pErrorDescription);
KD_API long PTAPI PassThruIoctl(unsigned long ChannelID, unsigned long IoctlID,
                                 void *pInput, void *pOutput);
