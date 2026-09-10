// ElmJ2534 — Bluetooth SPP COM port auto-detect (SetupDi)
//
// Implements: docs/elm-j2534-design.md → "Configuration"
// Selection rule ported from candroid-fw tools/elm327.py find_port(): among
// present COM-port device interfaces, accept only hardware ids containing
// BTHENUM but NOT LOCALMFG, then read the COM name out of the friendly name
// ("Standard Serial over Bluetooth link (COM3)" → "COM3").
//
// Fail-soft by design: env/registry config always outranks this scan, so any
// SetupDi misbehaviour is a returned false + err, never a crash or a hang.

#include "PortScan.h"

#ifdef _WIN32

#include <windows.h>
#include <initguid.h>   // must precede setupapi.h: instantiate the GUID here
#include <setupapi.h>
#include <stdio.h>
#include <string.h>
#include <vector>

// mingw's devguid.h/ntddser.h do not provide a usable
// GUID_DEVINTERFACE_COMPORT (ntddser.h needs WDM types); the GUID value is
// ABI-stable, so instantiate it directly.
DEFINE_GUID(GUID_DEVINTERFACE_COMPORT,
            0x86e0d1e0, 0x8089, 0x11d0, 0x9c, 0xe4, 0x08, 0x00, 0x3e, 0x30, 0x1f, 0x73);

namespace {

const DWORD ELM_HWID_BUF = 2048;      // multi-sz: a few ids, generous cap
const DWORD ELM_FRIENDLY_BUF = 512;
const DWORD ELM_DETAIL_MAX = 4096;    // device path cap: anything bigger is bogus
const size_t ELM_COM_NAME_BUF = 16;   // "COMnnnn" + NUL; matches ElmConfig.comPort

std::string scanUpper(const char *s) {
    std::string out(s);
    for (size_t i = 0; i < out.size(); i++)
        if (out[i] >= 'a' && out[i] <= 'z') out[i] = (char)(out[i] - 'a' + 'A');
    return out;
}

// True when any id in the REG_MULTI_SZ belongs to a remote BT device.
// LOCALMFG ids are counted separately for the diagnostic message — a scan
// that only ever sees the PC's own incoming server has a distinct meaning.
bool hwIdsAreRemoteBtSpp(const char *multiSz, bool &sawLocalMfg) {
    bool remote = false;
    for (const char *p = multiSz; *p; p += strlen(p) + 1) {
        const std::string id = scanUpper(p);
        if (id.find("BTHENUM") == std::string::npos) continue;
        if (id.find("LOCALMFG") != std::string::npos) {
            sawLocalMfg = true;
            continue;
        }
        remote = true;
    }
    return remote;
}

// "Standard Serial over Bluetooth link (COM3)" → "COM3". The COM number is
// between the LAST '(' and the ')' after it; names can contain parentheses.
bool parseComFromFriendlyName(const char *name, char *outPort, int outSize) {
    const std::string s(name);
    const size_t lp = s.rfind('(');
    const size_t rp = s.rfind(')');
    if (lp == std::string::npos || rp == std::string::npos || rp < lp + 5)
        return false;   // need at least "(COMx)"
    std::string com = s.substr(lp + 1, rp - lp - 1);
    if (com.compare(0, 3, "COM") != 0 && com.compare(0, 3, "com") != 0)
        return false;
    if (com[3] < '0' || com[3] > '9') return false;
    if ((int)com.size() + 1 > outSize) return false;
    strcpy(outPort, scanUpper(com.c_str()).c_str());
    return true;
}

bool readRegistryStringProp(HDEVINFO devs, SP_DEVINFO_DATA *did, DWORD prop,
                            char *buf, DWORD bufSize, DWORD expectType) {
    DWORD type = 0;
    if (!SetupDiGetDeviceRegistryPropertyA(devs, did, prop, &type,
                                           (PBYTE)buf, bufSize, NULL))
        return false;
    if (type != expectType) return false;
    buf[bufSize - 1] = '\0';
    if (bufSize >= 2) buf[bufSize - 2] = '\0';   // multi-sz: keep double NUL
    return true;
}

} // namespace

bool elmScanBluetoothComPort(char *outPort, int outSize, std::string &err) {
    if (!outPort || outSize < 5) {   // "COMx" + NUL minimum
        err = "elmScanBluetoothComPort: output buffer too small";
        return false;
    }
    outPort[0] = '\0';

    HDEVINFO devs = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_COMPORT, NULL, NULL,
                                         DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devs == INVALID_HANDLE_VALUE) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "SetupDiGetClassDevs failed (gle=%lu)",
                 (unsigned long)GetLastError());
        err = buf;
        return false;
    }

    bool sawLocalMfg = false;
    bool enumFailed = false;
    DWORD enumError = 0;
    DWORD enumIndex = 0;
    std::vector<char> hwid(ELM_HWID_BUF);
    std::vector<char> friendly(ELM_FRIENDLY_BUF);
    // Every remote BT SPP port on the machine, not just the first: more than
    // one device (a phone and a dongle, two dongles) can be paired at once and
    // SetupDi's enumeration order is driver-dependent. Picking the smallest
    // COM name makes the result deterministic across machines and reboots —
    // the same rule tools/elm327.py follows. Note this is LEXICOGRAPHIC, so
    // "COM10" sorts before "COM3": determinism beats intuition here, because
    // the alternative is a different port on every machine.
    std::vector<std::string> candidates;

    for (DWORD index = 0; ; index++) {
        SP_DEVICE_INTERFACE_DATA ifd;
        ifd.cbSize = sizeof(ifd);
        if (!SetupDiEnumDeviceInterfaces(devs, NULL, &GUID_DEVINTERFACE_COMPORT,
                                         index, &ifd)) {
            // ERROR_NO_MORE_ITEMS is the normal end of the enumeration. Any
            // other code means we stopped early and the scan is incomplete —
            // reporting "nothing found" there would send the user off to
            // re-pair a dongle Windows can in fact see.
            enumError = GetLastError();
            enumFailed = (enumError != ERROR_NO_MORE_ITEMS);
            enumIndex = index;
            break;
        }

        // First call fails by design and reports the needed size; it also
        // fills the SP_DEVINFO_DATA we query the registry properties through.
        SP_DEVINFO_DATA did;
        did.cbSize = sizeof(did);
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailA(devs, &ifd, NULL, 0, &needed, &did);
        if (needed == 0 || needed > ELM_DETAIL_MAX) continue;
        std::vector<BYTE> detailBuf(needed);
        SP_DEVICE_INTERFACE_DETAIL_DATA_A *detail =
            reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_A *>(&detailBuf[0]);
        // cbSize is the PACKED header size, which is not sizeof() of this type
        // in every toolchain: the SDK declares the struct packed(1) on x86
        // (cbSize 5) and unpacked on x64 (cbSize 8), and mingw's headers have
        // sat on both sides of that. A mismatch is not a compile error — SetupDi
        // rejects the buffer at run time with ERROR_INVALID_USER_BUFFER (1784)
        // and the port silently never shows up. This project builds x86-only
        // and mingw-w64 reports sizeof() == 5 here, which is what the API
        // wants; if a future toolchain reports 8, hardcode 5 for the x86 build.
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        if (!SetupDiGetDeviceInterfaceDetailA(devs, &ifd, detail, needed,
                                              NULL, &did))
            continue;

        if (!readRegistryStringProp(devs, &did, SPDRP_HARDWAREID,
                                    &hwid[0], ELM_HWID_BUF, REG_MULTI_SZ))
            continue;
        if (!hwIdsAreRemoteBtSpp(&hwid[0], sawLocalMfg))
            continue;   // not BT, or the PC's own incoming server — never open that

        if (!readRegistryStringProp(devs, &did, SPDRP_FRIENDLYNAME,
                                    &friendly[0], ELM_FRIENDLY_BUF, REG_SZ))
            continue;   // BT but nameless: skip rather than guess a COM number
        char port[ELM_COM_NAME_BUF];
        if (parseComFromFriendlyName(&friendly[0], port, (int)sizeof(port)))
            candidates.push_back(std::string(port));
    }

    SetupDiDestroyDeviceInfoList(devs);

    if (candidates.empty()) {
        outPort[0] = '\0';
        err = "no Bluetooth SPP COM port for a remote device found";
        if (sawLocalMfg)
            err += " (only a LOCALMFG port exists — that is this PC's own "
                   "incoming SPP server, not a dongle; opening it hangs "
                   "forever)";
        if (enumFailed) {
            char buf[96];
            snprintf(buf, sizeof(buf),
                     " (interface enumeration aborted at index %lu, gle=%lu)",
                     (unsigned long)enumIndex, (unsigned long)enumError);
            err += buf;
        }
        err += "; pair and power the ELM327 dongle, or set ELM_J2534_PORT / "
               "registry ComPort explicitly";
        return false;
    }

    size_t best = 0;
    for (size_t i = 1; i < candidates.size(); i++)
        if (candidates[i] < candidates[best]) best = i;
    if (candidates[best].size() + 1 > (size_t)outSize) {
        outPort[0] = '\0';
        err = "elmScanBluetoothComPort: output buffer too small for the match";
        return false;
    }
    strcpy(outPort, candidates[best].c_str());
    return true;
}

#else // !_WIN32

bool elmScanBluetoothComPort(char *outPort, int outSize, std::string &err) {
    (void)outSize;
    if (outPort) outPort[0] = '\0';
    err = "Bluetooth COM auto-detect is only available on Windows";
    return false;
}

#endif // _WIN32
