#pragma once
// Dynamic loader for Kvaser canlib32.dll
// All functions loaded at runtime via GetProcAddress

#include <windows.h>

// CANlib types (from canlib.h)
typedef int CanHandle;
typedef int canStatus;

#define canOK                   0
#define canERR_PARAM           -1
#define canERR_NOMSG           -2
#define canERR_NOTFOUND        -3
#define canERR_NOMEM           -4
#define canERR_NOCHANNELS      -5
#define canERR_INTERRUPTED     -6
#define canERR_TIMEOUT         -7
#define canERR_NOTINITIALIZED  -8
#define canERR_NOHANDLES       -9
#define canERR_INVHANDLE       -10
#define canERR_DRIVER          -14
#define canERR_TXBUFOFL        -15
#define canERR_HARDWARE        -16
#define canERR_DYNALOAD        -17
#define canERR_DYNALIB         -18
#define canERR_DYNAINIT        -19

// canOpenChannel flags
#define canOPEN_EXCLUSIVE           0x0008
#define canOPEN_REQUIRE_EXTENDED    0x0010
#define canOPEN_ACCEPT_VIRTUAL      0x0020
#define canOPEN_OVERRIDE_EXCLUSIVE  0x0040
#define canOPEN_REQUIRE_INIT_ACCESS 0x0100
#define canOPEN_NO_INIT_ACCESS      0x0200
#define canOPEN_ACCEPT_LARGE_DLC    0x0400
#define canOPEN_CAN_FD              0x0400
#define canOPEN_CAN_FD_NONISO       0x0800

// canRead/canWrite message flags
#define canMSG_RTR              0x0001
#define canMSG_STD              0x0002
#define canMSG_EXT              0x0004
#define canMSG_WAKEUP           0x0008
#define canMSG_NERR             0x0010
#define canMSG_ERROR_FRAME      0x0020
#define canMSG_TXACK            0x0040
#define canMSG_TXRQ             0x0080
#define canMSG_DELAY_MSG        0x0100

// canMSGERR flags (in flag output from canRead)
#define canMSGERR_MASK          0xff00
#define canMSGERR_HW_OVERRUN    0x0200
#define canMSGERR_SW_OVERRUN    0x0400
#define canMSGERR_STUFF         0x0800
#define canMSGERR_FORM          0x1000
#define canMSGERR_CRC           0x2000
#define canMSGERR_BIT0          0x4000
#define canMSGERR_BIT1          0x8000
#define canMSGERR_OVERRUN       0x0600
#define canMSGERR_BIT           0xC000
#define canMSGERR_BUSERR        0xF800

// canIOCTL codes
#define canIOCTL_FLUSH_RX_BUFFER    10
#define canIOCTL_FLUSH_TX_BUFFER    11
#define canIOCTL_SET_TIMER_SCALE    6
#define canIOCTL_SET_LOCAL_TXECHO   45

// Predefined bus speeds
#define canBITRATE_1M       (-1)
#define canBITRATE_500K     (-2)
#define canBITRATE_250K     (-3)
#define canBITRATE_125K     (-4)
#define canBITRATE_100K     (-5)
#define canBITRATE_62K      (-6)
#define canBITRATE_50K      (-7)
#define canBITRATE_83K      (-8)
#define canBITRATE_10K      (-9)

// canGetChannelData items
#define canCHANNELDATA_CHANNEL_CAP          1
#define canCHANNELDATA_CARD_TYPE            4
#define canCHANNELDATA_CARD_NUMBER          5
#define canCHANNELDATA_CHAN_NO_ON_CARD      6
#define canCHANNELDATA_CARD_SERIAL_NO       7
#define canCHANNELDATA_CARD_FIRMWARE_REV    9
#define canCHANNELDATA_CARD_HARDWARE_REV    10
#define canCHANNELDATA_DEVDESCR_ASCII       26
#define canCHANNELDATA_CHANNEL_NAME         13

// Function pointer typedefs
typedef void    (__stdcall *PFN_canInitializeLibrary)(void);
typedef canStatus (__stdcall *PFN_canGetNumberOfChannels)(int *channelCount);
typedef canStatus (__stdcall *PFN_canGetChannelData)(int channel, int item, void *buffer, size_t bufsize);
typedef CanHandle (__stdcall *PFN_canOpenChannel)(int channel, int flags);
typedef canStatus (__stdcall *PFN_canClose)(const CanHandle hnd);
typedef canStatus (__stdcall *PFN_canBusOn)(const CanHandle hnd);
typedef canStatus (__stdcall *PFN_canBusOff)(const CanHandle hnd);
typedef canStatus (__stdcall *PFN_canSetBusParams)(const CanHandle hnd, long freq, unsigned int tseg1, unsigned int tseg2, unsigned int sjw, unsigned int noSamp, unsigned int syncmode);
typedef canStatus (__stdcall *PFN_canRead)(const CanHandle hnd, long *id, void *msg, unsigned int *dlc, unsigned int *flag, unsigned long *time);
typedef canStatus (__stdcall *PFN_canReadWait)(const CanHandle hnd, long *id, void *msg, unsigned int *dlc, unsigned int *flag, unsigned long *time, unsigned long timeout);
typedef canStatus (__stdcall *PFN_canWrite)(const CanHandle hnd, long id, void *msg, unsigned int dlc, unsigned int flag);
typedef canStatus (__stdcall *PFN_canWriteSync)(const CanHandle hnd, unsigned long timeout);
typedef canStatus (__stdcall *PFN_canIoCtl)(const CanHandle hnd, unsigned int func, void *buf, unsigned int buflen);
typedef canStatus (__stdcall *PFN_canReadStatus)(const CanHandle hnd, unsigned long *flags);
typedef canStatus (__stdcall *PFN_canGetErrorText)(canStatus err, char *buf, unsigned int bufsiz);
typedef canStatus (__stdcall *PFN_canRequestBusStatistics)(const CanHandle hnd);

// kvIo functions (optional — only available on devices with I/O modules)
typedef canStatus (__stdcall *PFN_kvIoGetNumberOfPins)(const CanHandle hnd, unsigned int *pinCount);
typedef canStatus (__stdcall *PFN_kvIoConfirmConfig)(const CanHandle hnd);
typedef canStatus (__stdcall *PFN_kvIoPinGetInfo)(const CanHandle hnd, unsigned int pin, int item, void *buffer, unsigned int bufsize);
typedef canStatus (__stdcall *PFN_kvIoPinSetInfo)(const CanHandle hnd, unsigned int pin, int item, const void *buffer, unsigned int bufsize);
typedef canStatus (__stdcall *PFN_kvIoPinSetAnalog)(const CanHandle hnd, unsigned int pin, float value);
typedef canStatus (__stdcall *PFN_kvIoPinGetAnalog)(const CanHandle hnd, unsigned int pin, float *value);
typedef canStatus (__stdcall *PFN_kvIoPinSetDigital)(const CanHandle hnd, unsigned int pin, unsigned int value);
typedef canStatus (__stdcall *PFN_kvIoPinGetDigital)(const CanHandle hnd, unsigned int pin, unsigned int *value);

// kvIo pin info items
#define kvIO_INFO_GET_DIRECTION         1
#define kvIO_INFO_GET_PIN_TYPE          2
#define kvIO_INFO_GET_NUMBER_OF_BITS    5
#define kvIO_INFO_GET_RANGE_MIN         6
#define kvIO_INFO_GET_RANGE_MAX         7
#define kvIO_INFO_GET_MODULE_NUMBER     12
#define kvIO_INFO_GET_SERIAL_NUMBER     13
#define kvIO_INFO_GET_FW_VERSION        14

// kvIo pin types
#define kvIO_PIN_TYPE_DIGITAL   1
#define kvIO_PIN_TYPE_ANALOG    2
#define kvIO_PIN_TYPE_RELAY     3

// kvIo pin directions
#define kvIO_PIN_DIRECTION_IN   4
#define kvIO_PIN_DIRECTION_OUT  8

// Loader state
struct CanlibApi {
    HMODULE hModule;

    PFN_canInitializeLibrary    canInitializeLibrary;
    PFN_canGetNumberOfChannels  canGetNumberOfChannels;
    PFN_canGetChannelData       canGetChannelData;
    PFN_canOpenChannel          canOpenChannel;
    PFN_canClose                canClose;
    PFN_canBusOn                canBusOn;
    PFN_canBusOff               canBusOff;
    PFN_canSetBusParams         canSetBusParams;
    PFN_canRead                 canRead;
    PFN_canReadWait             canReadWait;
    PFN_canWrite                canWrite;
    PFN_canWriteSync            canWriteSync;
    PFN_canIoCtl                canIoCtl;
    PFN_canReadStatus           canReadStatus;
    PFN_canGetErrorText         canGetErrorText;
    PFN_canRequestBusStatistics canRequestBusStatistics;

    // Optional I/O pin functions
    PFN_kvIoGetNumberOfPins     kvIoGetNumberOfPins;
    PFN_kvIoConfirmConfig       kvIoConfirmConfig;
    PFN_kvIoPinGetInfo          kvIoPinGetInfo;
    PFN_kvIoPinSetInfo          kvIoPinSetInfo;
    PFN_kvIoPinSetAnalog        kvIoPinSetAnalog;
    PFN_kvIoPinGetAnalog        kvIoPinGetAnalog;
    PFN_kvIoPinSetDigital       kvIoPinSetDigital;
    PFN_kvIoPinGetDigital       kvIoPinGetDigital;
};

// Load canlib32.dll and resolve all function pointers
// Returns true on success, false if canlib32.dll not found or missing core functions
bool canlibLoad(CanlibApi *api);

// Unload canlib32.dll
void canlibUnload(CanlibApi *api);
