#include "world/busy_detector.h"
#include "util/logging.h"

#ifdef _WIN32
#  include <windows.h>
#endif

namespace cr {

#ifdef _WIN32
namespace {

// does any subkey of `subPath` have LastUsedTimeStart != 0 with
// LastUsedTimeStop == 0? both packaged apps (direct subkeys of the
// capability) and win32 apps (subkeys of its NonPackaged child, exe
// paths with the backslashes turned into #) follow this shape. the
// NonPackaged key itself shows up in the parent enumeration and just
// has no timestamps, which reads as "not in use". fine.
bool anyEntryInUse(const wchar_t* subPath)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subPath, 0,
                      KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    bool inUse = false;
    for (DWORD i = 0; !inUse; ++i) {
        wchar_t name[512];
        DWORD nameLen = 512;
        if (RegEnumKeyExW(key, i, name, &nameLen,
                          nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
        }
        HKEY sub = nullptr;
        if (RegOpenKeyExW(key, name, 0, KEY_READ, &sub) != ERROR_SUCCESS) {
            continue;
        }
        unsigned long long start = 0;
        unsigned long long stop = 0;
        DWORD sz = sizeof(start);
        const LONG rStart = RegGetValueW(sub, nullptr, L"LastUsedTimeStart",
                                         RRF_RT_REG_QWORD, nullptr, &start, &sz);
        sz = sizeof(stop);
        const LONG rStop = RegGetValueW(sub, nullptr, L"LastUsedTimeStop",
                                        RRF_RT_REG_QWORD, nullptr, &stop, &sz);
        if (rStart == ERROR_SUCCESS && rStop == ERROR_SUCCESS
            && start != 0 && stop == 0) {
            inUse = true;
        }
        RegCloseKey(sub);
    }
    RegCloseKey(key);
    return inUse;
}

} // namespace
#endif

bool BusyDetector::refresh()
{
    bool nowBusy = false;
#ifdef _WIN32
    static const wchar_t* kPaths[] = {
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\microphone",
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\microphone\\NonPackaged",
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\webcam",
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\webcam\\NonPackaged",
    };
    for (const wchar_t* p : kPaths) {
        if (anyEntryInUse(p)) {
            nowBusy = true;
            break;
        }
    }
#endif
    if (nowBusy != m_busy) {
        LOG_INFO(nowBusy
            ? QStringLiteral("busy detector: mic/camera in use, going polite")
            : QStringLiteral("busy detector: call over, resuming crimes"));
    }
    m_busy = nowBusy;
    return m_busy;
}

} // namespace cr
