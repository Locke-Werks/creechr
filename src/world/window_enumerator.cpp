#include "world/window_enumerator.h"
#include "util/logging.h"

#include <QString>
#include <QVarLengthArray>

#ifdef _WIN32
#  include <windows.h>
#  include <dwmapi.h>
#endif

namespace cr {

#ifdef _WIN32

namespace {

struct EnumState {
    QVector<WindowInfo>* out;
    HWND self;
};

bool isClassNameToSkip(const QString& cn)
{
    // shell windows. these aren't real things creechr should care about.
    // some apps still use the old name "Progman" for the desktop manager.
    if (cn == QLatin1String("Progman")) return true;
    if (cn == QLatin1String("WorkerW")) return true;
    if (cn == QLatin1String("Shell_TrayWnd")) return true;
    if (cn == QLatin1String("Shell_SecondaryTrayWnd")) return true;
    if (cn == QLatin1String("Windows.UI.Core.CoreWindow")) return true;
    // qt's own tooltip / menu hwnds during heists — paranoia, can't hurt
    if (cn.startsWith(QLatin1String("Qt6"))) return true;
    return false;
}

QString classNameOf(HWND hwnd)
{
    wchar_t buf[256] = {};
    int n = GetClassNameW(hwnd, buf, 256);
    return QString::fromWCharArray(buf, n);
}

QString titleOf(HWND hwnd)
{
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return {};
    QVarLengthArray<wchar_t, 256> buf(len + 1);
    const int n = GetWindowTextW(hwnd, buf.data(), len + 1);
    return QString::fromWCharArray(buf.data(), n);
}

bool isCloaked(HWND hwnd)
{
    // win10+ uses dwm cloaking for things like uwp apps that are
    // technically "alive" but not actually visible. they show up in
    // EnumWindows but you don't want creechr climbing them.
    BOOL cloaked = FALSE;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return SUCCEEDED(hr) && cloaked != 0;
}

QRect dwmFrameRect(HWND hwnd)
{
    // GetWindowRect lies on win10+ — it includes the invisible 8ish-pixel
    // shadow that DWM draws around windows. DWMWA_EXTENDED_FRAME_BOUNDS
    // gives you the actual visible frame. use it. always. forever.
    RECT r = {};
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r));
    if (FAILED(hr)) {
        // dwm refused. fall back to GetWindowRect and accept the offset.
        if (!GetWindowRect(hwnd, &r)) {
            return {};
        }
        LOG_DEBUG(QStringLiteral("dwm extended frame failed for hwnd, using GetWindowRect"));
    }
    return QRect(QPoint(r.left, r.top), QPoint(r.right - 1, r.bottom - 1));
}

BOOL CALLBACK enumProc(HWND hwnd, LPARAM lparam)
{
    EnumState* st = reinterpret_cast<EnumState*>(lparam);
    if (!hwnd || hwnd == st->self) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    // skip child windows even if they sneak in (shouldn't with EnumWindows)
    if (GetWindow(hwnd, GW_OWNER) != nullptr) {
        // owned popups can be windows we still want to know about — like
        // detached menus — but for v0.1 we ignore them. revisit later.
        return TRUE;
    }
    if (isCloaked(hwnd)) return TRUE;

    const QString cn = classNameOf(hwnd);
    if (isClassNameToSkip(cn)) return TRUE;

    const QRect r = dwmFrameRect(hwnd);
    if (r.width() < 24 || r.height() < 24) {
        return TRUE; // too tiny to be a real window
    }

    WindowInfo info;
    info.hwnd = hwnd;
    info.frame = r;
    info.className = cn;
    info.title = titleOf(hwnd);
    st->out->push_back(info);
    return TRUE;
}

} // namespace

WindowEnumerator::WindowEnumerator() = default;

QVector<WindowInfo> WindowEnumerator::snapshot()
{
    QVector<WindowInfo> result;
    result.reserve(32);
    EnumState st { &result, m_self };
    // EnumWindows walks top-level windows in z-order (topmost first)
    EnumWindows(&enumProc, reinterpret_cast<LPARAM>(&st));
    return result;
}

QVector<QRect> WindowEnumerator::snapshotRects()
{
    QVector<QRect> rects;
    for (const auto& w : snapshot()) {
        rects.push_back(w.frame);
    }
    return rects;
}

#else // !_WIN32

WindowEnumerator::WindowEnumerator() = default;
QVector<WindowInfo> WindowEnumerator::snapshot() { return {}; }
QVector<QRect> WindowEnumerator::snapshotRects() { return {}; }

#endif

} // namespace cr
