#include "world/fullscreen_detector.h"
#include "util/logging.h"

#ifdef _WIN32
#  include <windows.h>
#  include <shellapi.h>
#endif

namespace cr {

#ifdef _WIN32
namespace {

// borderless-fullscreen: the foreground window has no caption and
// covers its entire monitor (2px tolerance for driver rounding).
// GetWindowRect and rcMonitor are both physical pixels, so no dpr
// dance here — do NOT feed this the dwm frame rect, that one's been
// divided by a dpr already.
bool borderlessFullscreen(void* selfHwnd)
{
    HWND fg = GetForegroundWindow();
    if (!fg || fg == static_cast<HWND>(selfHwnd)) return false;

    wchar_t cls[64] = {};
    GetClassNameW(fg, cls, 64);
    // the shell's own full-coverage windows are not games
    if (wcscmp(cls, L"Progman") == 0
        || wcscmp(cls, L"WorkerW") == 0
        || wcscmp(cls, L"Shell_TrayWnd") == 0) {
        return false;
    }

    // maximized normal windows keep WS_CAPTION; borderless games drop
    // it. this single bit is what makes the check safe.
    const LONG_PTR style = GetWindowLongPtrW(fg, GWL_STYLE);
    if (style & WS_CAPTION) return false;

    RECT wr = {};
    if (!GetWindowRect(fg, &wr)) return false;
    HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return false;

    return wr.left   <= mi.rcMonitor.left   + 2
        && wr.top    <= mi.rcMonitor.top    + 2
        && wr.right  >= mi.rcMonitor.right  - 2
        && wr.bottom >= mi.rcMonitor.bottom - 2;
}

} // namespace
#endif

bool FullscreenDetector::isFullscreenActive()
{
#ifdef _WIN32
    m_userQuiet = false;
    QUERY_USER_NOTIFICATION_STATE state = QUNS_NOT_PRESENT;
    HRESULT hr = SHQueryUserNotificationState(&state);
    if (SUCCEEDED(hr)) {
        if (state == QUNS_RUNNING_D3D_FULL_SCREEN
            || state == QUNS_PRESENTATION_MODE) {
            return true;
        }
        // "do not disturb" — the promise at the top of this file,
        // finally kept: stay visible, commit no crimes
        m_userQuiet = (state == QUNS_BUSY);
    }
    // shell said nothing exciting; check for borderless-windowed
    // fullscreen ourselves (games, fullscreen video)
    return borderlessFullscreen(m_selfHwnd);
#else
    return false;
#endif
}

} // namespace cr
