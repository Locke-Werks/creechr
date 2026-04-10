// WindowEnumerator — knows what windows exist on the desktop and where
// their REAL frames are (not the lying GetWindowRect frames).
//
// usage: WindowEnumerator e; e.setSelfHwnd(overlayHwnd);
//        const auto rects = e.snapshot();
//
// snapshot() does a fresh EnumWindows pass and returns the visible
// top-level frames in screen coordinates. it's cheap enough to call
// at 10Hz on a normal desktop. if you've got 400 windows open, well,
// future me's problem.
#pragma once

#include <QRect>
#include <QVector>

#ifdef _WIN32
#  include <windows.h>
typedef HWND CrHwnd;
#else
typedef void* CrHwnd;
#endif

namespace cr {

struct WindowInfo {
    CrHwnd hwnd;
    QRect frame;       // dwm-corrected, screen coords
    QString className; // win32 class name, useful for filtering
    QString title;     // for logging only, never trust this for logic
};

class WindowEnumerator
{
public:
    WindowEnumerator();

    // tell the enumerator which hwnd is the overlay so it doesn't
    // include creechr in his own world. set this exactly once after
    // the overlay is shown.
    void setSelfHwnd(CrHwnd hwnd) { m_self = hwnd; }

    QVector<WindowInfo> snapshot();

    // convenience: just the rects, in z-order (topmost first)
    QVector<QRect> snapshotRects();

private:
    CrHwnd m_self = nullptr;
};

} // namespace cr
