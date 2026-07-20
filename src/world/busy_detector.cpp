#include "world/busy_detector.h"
#include "util/logging.h"

#ifdef _WIN32
#  include <windows.h>
#  include <tlhelp32.h>
#endif

#include <string>

namespace cr {

#ifdef _WIN32
namespace {

// stale-entry filter. apps that crash or get uninstalled leave
// LastUsedTimeStop at 0 FOREVER (this very box had media servers from
// two uninstalled tiktok studio versions "holding" the mic for
// months). a nonpackaged entry only counts as live if its exe still
// exists on disk AND a process with that image name is running.
bool nonPackagedEntryLooksLive(const wchar_t* keyName)
{
    // key name is the exe path with backslashes turned into '#'
    std::wstring path(keyName);
    for (auto& ch : path) {
        if (ch == L'#') ch = L'\\';
    }
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return false; // exe is gone, entry is a ghost
    }
    const size_t slash = path.find_last_of(L'\\');
    const std::wstring base =
        (slash == std::wstring::npos) ? path : path.substr(slash + 1);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return true; // cant verify, err toward polite
    }
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, base.c_str()) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

// does any subkey of `subPath` have LastUsedTimeStart != 0 with
// LastUsedTimeStop == 0? both packaged apps (direct subkeys of the
// capability) and win32 apps (subkeys of its NonPackaged child, exe
// paths with the backslashes turned into #) follow this shape. the
// NonPackaged key itself shows up in the parent enumeration and just
// has no timestamps, which reads as "not in use". fine.
bool anyEntryInUse(const wchar_t* subPath, bool nonPackaged)
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
            if (!nonPackaged || nonPackagedEntryLooksLive(name)) {
                LOG_DEBUG(QStringLiteral("busy detector: live entry %1")
                    .arg(QString::fromWCharArray(name)));
                inUse = true;
            }
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
    struct Probe { const wchar_t* path; bool nonPackaged; };
    static const Probe kProbes[] = {
        { L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\microphone", false },
        { L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\microphone\\NonPackaged", true },
        { L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\webcam", false },
        { L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\webcam\\NonPackaged", true },
    };
    for (const Probe& p : kProbes) {
        if (anyEntryInUse(p.path, p.nonPackaged)) {
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
