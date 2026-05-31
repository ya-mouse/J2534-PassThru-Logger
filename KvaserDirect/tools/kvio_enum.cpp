// kvio_enum — Enumerate Kvaser I/O pins and read analog values
// Usage: kvio_enum.exe [channel_number]
//   Defaults to channel 0 if not specified.
//   Lists all I/O pins with type, direction, range, and current value.

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// CANlib types
typedef int CanHandle;
typedef int canStatus;
#define canOK 0

// kvIo info items
#define kvIO_INFO_GET_DIRECTION       1
#define kvIO_INFO_GET_PIN_TYPE        2
#define kvIO_INFO_GET_NUMBER_OF_BITS  5
#define kvIO_INFO_GET_RANGE_MIN       6
#define kvIO_INFO_GET_RANGE_MAX       7
#define kvIO_INFO_GET_MODULE_NUMBER   12
#define kvIO_INFO_GET_SERIAL_NUMBER   13

// Pin types
#define kvIO_PIN_TYPE_DIGITAL   1
#define kvIO_PIN_TYPE_ANALOG    2
#define kvIO_PIN_TYPE_RELAY     3

// Pin directions
#define kvIO_PIN_DIRECTION_IN   4
#define kvIO_PIN_DIRECTION_OUT  8

// Function pointers
typedef void      (__stdcall *PFN_canInitializeLibrary)(void);
typedef CanHandle (__stdcall *PFN_canOpenChannel)(int channel, int flags);
typedef canStatus (__stdcall *PFN_canClose)(const CanHandle hnd);
typedef canStatus (__stdcall *PFN_canGetErrorText)(canStatus err, char *buf, unsigned int bufsiz);
typedef canStatus (__stdcall *PFN_canGetNumberOfChannels)(int *count);
typedef canStatus (__stdcall *PFN_canGetChannelData)(int channel, int item, void *buf, size_t bufsize);

typedef canStatus (__stdcall *PFN_kvIoGetNumberOfPins)(const CanHandle hnd, unsigned int *pinCount);
typedef canStatus (__stdcall *PFN_kvIoConfirmConfig)(const CanHandle hnd);
typedef canStatus (__stdcall *PFN_kvIoPinGetInfo)(const CanHandle hnd, unsigned int pin, int item, void *buffer, unsigned int bufsize);
typedef canStatus (__stdcall *PFN_kvIoPinGetAnalog)(const CanHandle hnd, unsigned int pin, float *value);
typedef canStatus (__stdcall *PFN_kvIoPinGetDigital)(const CanHandle hnd, unsigned int pin, unsigned int *value);
typedef canStatus (__stdcall *PFN_kvIoPinGetOutputRelay)(const CanHandle hnd, unsigned int pin, unsigned int *value);

// Globals
static HMODULE hCanlib = NULL;
static PFN_canInitializeLibrary  fn_canInitializeLibrary;
static PFN_canOpenChannel        fn_canOpenChannel;
static PFN_canClose              fn_canClose;
static PFN_canGetErrorText       fn_canGetErrorText;
static PFN_canGetNumberOfChannels fn_canGetNumberOfChannels;
static PFN_canGetChannelData     fn_canGetChannelData;
static PFN_kvIoGetNumberOfPins   fn_kvIoGetNumberOfPins;
static PFN_kvIoConfirmConfig     fn_kvIoConfirmConfig;
static PFN_kvIoPinGetInfo        fn_kvIoPinGetInfo;
static PFN_kvIoPinGetAnalog      fn_kvIoPinGetAnalog;
static PFN_kvIoPinGetDigital     fn_kvIoPinGetDigital;
static PFN_kvIoPinGetOutputRelay fn_kvIoPinGetOutputRelay;

static const char* errText(canStatus st) {
    static char buf[256];
    if (fn_canGetErrorText && fn_canGetErrorText(st, buf, sizeof(buf)) == canOK)
        return buf;
    sprintf(buf, "error %d", st);
    return buf;
}

static const char* pinTypeName(int type) {
    switch (type) {
        case kvIO_PIN_TYPE_DIGITAL: return "Digital";
        case kvIO_PIN_TYPE_ANALOG:  return "Analog";
        case kvIO_PIN_TYPE_RELAY:   return "Relay";
        default:                    return "Unknown";
    }
}

static const char* pinDirName(int dir) {
    switch (dir) {
        case kvIO_PIN_DIRECTION_IN:  return "Input";
        case kvIO_PIN_DIRECTION_OUT: return "Output";
        default:                     return "Unknown";
    }
}

static void listChannels() {
    if (!fn_canGetNumberOfChannels || !fn_canGetChannelData) return;

    int count = 0;
    if (fn_canGetNumberOfChannels(&count) != canOK || count == 0) {
        printf("No CANlib channels found.\n");
        return;
    }

    printf("Available CANlib channels:\n");
    for (int i = 0; i < count; i++) {
        char name[256] = {0};
        fn_canGetChannelData(i, 13 /*canCHANNELDATA_CHANNEL_NAME*/, name, sizeof(name));
        printf("  Channel %d: %s\n", i, name[0] ? name : "(unnamed)");
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    int channelNbr = 0;
    if (argc > 1) channelNbr = atoi(argv[1]);

    printf("=== Kvaser I/O Pin Enumerator ===\n\n");

    // Load canlib32.dll
    hCanlib = LoadLibraryA("canlib32.dll");
    if (!hCanlib) {
        printf("ERROR: Cannot load canlib32.dll\n");
        printf("Make sure Kvaser CANlib SDK is installed.\n");
        return 1;
    }

    // Resolve functions
    fn_canInitializeLibrary  = (PFN_canInitializeLibrary)GetProcAddress(hCanlib, "canInitializeLibrary");
    fn_canOpenChannel        = (PFN_canOpenChannel)GetProcAddress(hCanlib, "canOpenChannel");
    fn_canClose              = (PFN_canClose)GetProcAddress(hCanlib, "canClose");
    fn_canGetErrorText       = (PFN_canGetErrorText)GetProcAddress(hCanlib, "canGetErrorText");
    fn_canGetNumberOfChannels = (PFN_canGetNumberOfChannels)GetProcAddress(hCanlib, "canGetNumberOfChannels");
    fn_canGetChannelData     = (PFN_canGetChannelData)GetProcAddress(hCanlib, "canGetChannelData");
    fn_kvIoGetNumberOfPins   = (PFN_kvIoGetNumberOfPins)GetProcAddress(hCanlib, "kvIoGetNumberOfPins");
    fn_kvIoConfirmConfig     = (PFN_kvIoConfirmConfig)GetProcAddress(hCanlib, "kvIoConfirmConfig");
    fn_kvIoPinGetInfo        = (PFN_kvIoPinGetInfo)GetProcAddress(hCanlib, "kvIoPinGetInfo");
    fn_kvIoPinGetAnalog      = (PFN_kvIoPinGetAnalog)GetProcAddress(hCanlib, "kvIoPinGetAnalog");
    fn_kvIoPinGetDigital     = (PFN_kvIoPinGetDigital)GetProcAddress(hCanlib, "kvIoPinGetDigital");
    fn_kvIoPinGetOutputRelay = (PFN_kvIoPinGetOutputRelay)GetProcAddress(hCanlib, "kvIoPinGetOutputRelay");

    if (!fn_canInitializeLibrary || !fn_canOpenChannel || !fn_canClose) {
        printf("ERROR: canlib32.dll missing core functions\n");
        FreeLibrary(hCanlib);
        return 1;
    }

    if (!fn_kvIoGetNumberOfPins || !fn_kvIoPinGetInfo) {
        printf("ERROR: canlib32.dll missing kvIo functions (SDK too old?)\n");
        FreeLibrary(hCanlib);
        return 1;
    }

    // Initialize
    fn_canInitializeLibrary();
    listChannels();

    // Open channel
    printf("Opening channel %d...\n", channelNbr);
    CanHandle hnd = fn_canOpenChannel(channelNbr, 0);
    if (hnd < 0) {
        printf("ERROR: canOpenChannel(%d) failed: %s\n", channelNbr, errText(hnd));
        FreeLibrary(hCanlib);
        return 1;
    }

    // Get pin count
    unsigned int pinCount = 0;
    canStatus st = fn_kvIoGetNumberOfPins(hnd, &pinCount);
    if (st != canOK) {
        printf("kvIoGetNumberOfPins failed: %s\n", errText(st));
        printf("This device may not support I/O pins.\n");
        fn_canClose(hnd);
        FreeLibrary(hCanlib);
        return 1;
    }

    printf("Found %u I/O pin(s).\n\n", pinCount);

    if (pinCount == 0) {
        printf("No I/O pins available on this device.\n");
        fn_canClose(hnd);
        FreeLibrary(hCanlib);
        return 0;
    }

    // Confirm config to enable read/write
    st = fn_kvIoConfirmConfig(hnd);
    if (st != canOK) {
        printf("WARNING: kvIoConfirmConfig failed: %s\n", errText(st));
        printf("Continuing with info-only queries...\n\n");
    }

    // Enumerate all pins
    printf("%-4s %-8s %-7s %-5s %-12s %-12s %s\n",
           "Pin", "Type", "Dir", "Bits", "Range Min", "Range Max", "Value");
    printf("---- -------- ------- ----- ------------ ------------ ----------------\n");

    for (unsigned int i = 0; i < pinCount; i++) {
        int pinType = 0, direction = 0, bits = 0;
        float rangeMin = 0, rangeMax = 0;

        fn_kvIoPinGetInfo(hnd, i, kvIO_INFO_GET_PIN_TYPE, &pinType, sizeof(pinType));
        fn_kvIoPinGetInfo(hnd, i, kvIO_INFO_GET_DIRECTION, &direction, sizeof(direction));
        fn_kvIoPinGetInfo(hnd, i, kvIO_INFO_GET_NUMBER_OF_BITS, &bits, sizeof(bits));
        fn_kvIoPinGetInfo(hnd, i, kvIO_INFO_GET_RANGE_MIN, &rangeMin, sizeof(rangeMin));
        fn_kvIoPinGetInfo(hnd, i, kvIO_INFO_GET_RANGE_MAX, &rangeMax, sizeof(rangeMax));

        char valueStr[64] = "N/A";

        if (pinType == kvIO_PIN_TYPE_ANALOG) {
            if (direction == kvIO_PIN_DIRECTION_IN && fn_kvIoPinGetAnalog) {
                float val = 0;
                st = fn_kvIoPinGetAnalog(hnd, i, &val);
                if (st == canOK)
                    sprintf(valueStr, "%.4f V", val);
                else
                    sprintf(valueStr, "err: %s", errText(st));
            } else {
                strcpy(valueStr, "(output)");
            }
        } else if (pinType == kvIO_PIN_TYPE_DIGITAL) {
            if (fn_kvIoPinGetDigital) {
                unsigned int val = 0;
                st = fn_kvIoPinGetDigital(hnd, i, &val);
                if (st == canOK)
                    sprintf(valueStr, "%u", val);
                else
                    sprintf(valueStr, "err: %s", errText(st));
            }
        } else if (pinType == kvIO_PIN_TYPE_RELAY) {
            if (fn_kvIoPinGetOutputRelay) {
                unsigned int val = 0;
                st = fn_kvIoPinGetOutputRelay(hnd, i, &val);
                if (st == canOK)
                    sprintf(valueStr, "%s", val ? "ON" : "OFF");
                else
                    sprintf(valueStr, "err: %s", errText(st));
            }
        }

        printf("%-4u %-8s %-7s %-5d %-12.4f %-12.4f %s\n",
               i, pinTypeName(pinType), pinDirName(direction),
               bits, rangeMin, rangeMax, valueStr);
    }

    // Summary for analog inputs (likely VBATT candidates)
    printf("\n--- Analog Input Pins (VBATT candidates) ---\n");
    bool foundAnalogIn = false;
    for (unsigned int i = 0; i < pinCount; i++) {
        int pinType = 0, direction = 0;
        fn_kvIoPinGetInfo(hnd, i, kvIO_INFO_GET_PIN_TYPE, &pinType, sizeof(pinType));
        fn_kvIoPinGetInfo(hnd, i, kvIO_INFO_GET_DIRECTION, &direction, sizeof(direction));

        if (pinType == kvIO_PIN_TYPE_ANALOG && direction == kvIO_PIN_DIRECTION_IN) {
            float val = 0;
            st = fn_kvIoPinGetAnalog(hnd, i, &val);
            if (st == canOK) {
                printf("  Pin %u: %.4f V (%.0f mV)", i, val, val * 1000.0f);
                if (val > 10.0f && val < 16.0f)
                    printf("  <-- likely battery voltage!");
                printf("\n");
            } else {
                printf("  Pin %u: read error: %s\n", i, errText(st));
            }
            foundAnalogIn = true;
        }
    }
    if (!foundAnalogIn)
        printf("  (none found — device may not have analog I/O module)\n");

    printf("\nTo use a pin for VBATT in KvaserDirect, set registry:\n");
    printf("  HKCU\\Software\\KvaserDirect\\VbattSource = 1 (kvIo only) or 2 (auto)\n");
    printf("  HKCU\\Software\\KvaserDirect\\VbattIoPin  = <pin number from above>\n");

    fn_canClose(hnd);
    FreeLibrary(hCanlib);
    return 0;
}
