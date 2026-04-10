// BitmapCapture — wraps PrintWindow / BitBlt and gives you back a QPixmap.
// the only thing creechr needs from gdi.
#pragma once

#include <QPixmap>
#include <QRect>

#ifdef _WIN32
#  include <windows.h>
typedef HWND CrHwnd;
#else
typedef void* CrHwnd;
#endif

namespace cr::capture {

// PrintWindow with PW_RENDERFULLCONTENT — works for most windows including
// modern uwp/winui ones that won't render to a regular dc. returns a null
// pixmap on failure. caller can fall back to captureScreenRect.
QPixmap captureWindow(CrHwnd hwnd);

// BitBlt of a screen rect. always works for visible pixels.
QPixmap captureScreenRect(const QRect& rect);

} // namespace cr::capture
