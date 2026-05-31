#include "CanlibLoader.h"

#define LOAD_FUNC(name) \
    api->name = (PFN_##name)GetProcAddress(api->hModule, #name); \
    if (!api->name) return false

#define LOAD_FUNC_OPTIONAL(name) \
    api->name = (PFN_##name)GetProcAddress(api->hModule, #name)

bool canlibLoad(CanlibApi *api) {
    memset(api, 0, sizeof(CanlibApi));

    api->hModule = LoadLibraryA("canlib32.dll");
    if (!api->hModule)
        return false;

    // Core functions (required)
    LOAD_FUNC(canInitializeLibrary);
    LOAD_FUNC(canGetNumberOfChannels);
    LOAD_FUNC(canGetChannelData);
    LOAD_FUNC(canOpenChannel);
    LOAD_FUNC(canClose);
    LOAD_FUNC(canBusOn);
    LOAD_FUNC(canBusOff);
    LOAD_FUNC(canSetBusParams);
    LOAD_FUNC(canRead);
    LOAD_FUNC(canReadWait);
    LOAD_FUNC(canWrite);
    LOAD_FUNC(canWriteSync);
    LOAD_FUNC(canIoCtl);
    LOAD_FUNC(canReadStatus);
    LOAD_FUNC(canGetErrorText);
    LOAD_FUNC(canRequestBusStatistics);

    // Optional I/O pin functions (not all devices have these)
    LOAD_FUNC_OPTIONAL(kvIoGetNumberOfPins);
    LOAD_FUNC_OPTIONAL(kvIoConfirmConfig);
    LOAD_FUNC_OPTIONAL(kvIoPinGetInfo);
    LOAD_FUNC_OPTIONAL(kvIoPinSetInfo);
    LOAD_FUNC_OPTIONAL(kvIoPinSetAnalog);
    LOAD_FUNC_OPTIONAL(kvIoPinGetAnalog);
    LOAD_FUNC_OPTIONAL(kvIoPinSetDigital);
    LOAD_FUNC_OPTIONAL(kvIoPinGetDigital);

    // Initialize CANlib
    api->canInitializeLibrary();
    return true;
}

void canlibUnload(CanlibApi *api) {
    if (api->hModule) {
        FreeLibrary(api->hModule);
        api->hModule = NULL;
    }
}
