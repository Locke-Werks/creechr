#include "util/win32_helpers.h"

#include <QGuiApplication>
#include <QScreen>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace cr::win32 {

int millisSinceLastInput()
{
#ifdef _WIN32
    LASTINPUTINFO lii = {};
    lii.cbSize = sizeof(lii);
    if (!GetLastInputInfo(&lii)) {
        return 0;
    }
    const DWORD now = GetTickCount();
    return static_cast<int>(now - lii.dwTime);
#else
    return 0;
#endif
}

bool anyMouseButtonDown()
{
#ifdef _WIN32
    // GetAsyncKeyState's high bit is "currently down". cast required.
    auto down = [](int vk) {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };
    return down(VK_LBUTTON) || down(VK_RBUTTON) || down(VK_MBUTTON);
#else
    return false;
#endif
}

QPoint logicalToNative(QPointF logicalPos)
{
    // per-screen scale around that screen's own origin. matches qt
    // 6.8's qhighdpiscaling model (fromNativeScreenGeometry preserves
    // the native top-left and scales only the size). screenAt can
    // return null for a point in the dead zone between monitors of
    // different heights; the primary is the least-wrong fallback.
    QScreen* s = QGuiApplication::screenAt(logicalPos.toPoint());
    if (!s) s = QGuiApplication::primaryScreen();
    if (!s) return logicalPos.toPoint();
    const QPointF origin = s->geometry().topLeft();
    const QPointF native = origin + (logicalPos - origin) * s->devicePixelRatio();
    return QPoint(qRound(native.x()), qRound(native.y()));
}

} // namespace cr::win32
