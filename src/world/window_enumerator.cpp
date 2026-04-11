#include "world/window_enumerator.h"
#include "util/logging.h"

#include <QGuiApplication>
#include <QScreen>
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

// returns the dpi scale (1.0, 1.25, 1.5, 2.0, ...) for the monitor a
// given window lives on. uses GetDpiForWindow which is win10 1607+
// — older versions get a 1.0 fallback which is wrong but doesnt crash.
qreal dpiScaleForWindow(HWND hwnd)
{
    if (!hwnd) return 1.0;
    // GetDpiForWindow is in user32 already linked. returns DPI per inch
    // (96 = 100%, 144 = 150%, 192 = 200%). zero on failure.
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) {
        QScreen* primary = QGuiApplication::primaryScreen();
        return primary ? primary->devicePixelRatio() : 1.0;
    }
    return dpi / 96.0;
}

QRect dwmFrameRect(HWND hwnd)
{
    // GetWindowRect lies on win10+ — it includes the invisible 8ish-pixel
    // shadow that DWM draws around windows. DWMWA_EXTENDED_FRAME_BOUNDS
    // gives you the actual visible frame. use it. always. forever.
    //
    // BOTH apis return PHYSICAL pixels regardless of dpi awareness. qt's
    // QScreen::geometry() returns LOGICAL pixels. divisor is THIS window's
    // monitor dpi (not the primary screen's), so mixed-dpi multi-monitor
    // setups are now correct.
    RECT r = {};
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r));
    if (FAILED(hr)) {
        // dwm refused. fall back to GetWindowRect and accept the offset.
        if (!GetWindowRect(hwnd, &r)) {
            return {};
        }
        LOG_DEBUG(QStringLiteral("dwm extended frame failed for hwnd, using GetWindowRect"));
    }
    const qreal dpr = dpiScaleForWindow(hwnd);
    if (dpr <= 0.0) {
        return QRect(QPoint(r.left, r.top), QPoint(r.right - 1, r.bottom - 1));
    }
    return QRect(
        QPoint(static_cast<int>(r.left / dpr),  static_cast<int>(r.top    / dpr)),
        QPoint(static_cast<int>(r.right / dpr - 1), static_cast<int>(r.bottom / dpr - 1))
    );
}

BOOL CALLBACK enumProc(HWND hwnd, LPARAM lparam)
{
    EnumState* st = reinterpret_cast<EnumState*>(lparam);
    if (!hwnd || hwnd == st->self) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (IsIconic(hwnd)) return TRUE; // minimized — frame is parked at INT_MIN
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
    // sanity check: frame must be plausibly on the desktop. windows
    // with top-left hugely negative are minimized-but-not-iconic, or
    // shell special-cases. either way creechr can't see them.
    if (r.left() < -10000 || r.top() < -10000
        || r.right() > 30000 || r.bottom() > 30000) {
        return TRUE;
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
    QVector<WindowInfo> raw;
    raw.reserve(32);
    EnumState st { &raw, m_self };
    // EnumWindows walks top-level windows in z-order (topmost first).
    // raw[0] is the topmost visible top-level window, raw[n-1] is the
    // bottom-most.
    EnumWindows(&enumProc, reinterpret_cast<LPARAM>(&st));

    // OCCLUSION FILTER.
    //
    // creechr should only interact with windows the USER can actually
    // see. a terminal sitting fully behind chrome is invisible and
    // rappelling to it looks like he's climbing an invisible ladder.
    //
    // for each window at index i, we test three representative points
    // along its TOP edge (creechr walks on the top edge, climbs its
    // corners, and rappels to its corners, so the top edge is what
    // matters). if ALL three points are contained inside some HIGHER
    // z-order window (j < i), the window is effectively occluded and
    // we drop it from the snapshot. if at least one point is clear,
    // we keep it.
    //
    // this misses the case where a window has a visible sliver in the
    // MIDDLE of its top edge but the three probe points are covered.
    // accept that for v0.x — the common case (fully behind a browser)
    // is what the user cares about.
    QVector<WindowInfo> visible;
    visible.reserve(raw.size());
    for (int i = 0; i < raw.size(); ++i) {
        const QRect& me = raw[i].frame;
        if (me.isEmpty()) continue;

        // probe points: left/center/right of the top edge, nudged 4 px
        // down so they're inside the window rather than exactly on it
        const QPoint probes[3] = {
            QPoint(me.left()  + 4,             me.top() + 4),
            QPoint((me.left() + me.right()) / 2, me.top() + 4),
            QPoint(me.right() - 4,             me.top() + 4),
        };

        bool anyVisible = false;
        for (const QPoint& p : probes) {
            bool covered = false;
            for (int j = 0; j < i; ++j) {
                if (raw[j].frame.contains(p)) {
                    covered = true;
                    break;
                }
            }
            if (!covered) {
                anyVisible = true;
                break;
            }
        }
        if (anyVisible) {
            visible.push_back(raw[i]);
        }
    }
    return visible;
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
