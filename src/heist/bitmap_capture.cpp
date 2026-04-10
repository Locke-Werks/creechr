#include "heist/bitmap_capture.h"
#include "util/logging.h"

#include <QImage>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace cr::capture {

#ifdef _WIN32

namespace {

QPixmap pixmapFromHbitmap(HBITMAP hbm, int w, int h)
{
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;          // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    QImage img(w, h, QImage::Format_ARGB32);
    if (img.isNull()) return {};

    HDC screenDC = GetDC(nullptr);
    if (!screenDC) return {};
    const int got = GetDIBits(screenDC, hbm, 0, h, img.bits(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, screenDC);
    if (got == 0) return {};

    return QPixmap::fromImage(img);
}

} // namespace

QPixmap captureWindow(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return {};

    RECT r;
    if (!GetClientRect(hwnd, &r)) return {};
    const int w = r.right - r.left;
    const int h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return {};

    HDC hdcWin = GetWindowDC(hwnd);
    if (!hdcWin) return {};
    HDC hdcMem = CreateCompatibleDC(hdcWin);
    HBITMAP hbm = CreateCompatibleBitmap(hdcWin, w, h);
    HGDIOBJ old = SelectObject(hdcMem, hbm);

    // PW_RENDERFULLCONTENT (0x2) is the magic flag that makes uwp /
    // chromium / anything that uses dwm composition actually paint into
    // the dc instead of giving you a black rectangle.
    const BOOL ok = PrintWindow(hwnd, hdcMem, PW_RENDERFULLCONTENT);
    QPixmap pm;
    if (ok) {
        pm = pixmapFromHbitmap(hbm, w, h);
    } else {
        LOG_DEBUG(QStringLiteral("captureWindow: PrintWindow failed for hwnd %1")
            .arg(reinterpret_cast<qulonglong>(hwnd)));
    }

    SelectObject(hdcMem, old);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcWin);
    return pm;
}

QPixmap captureScreenRect(const QRect& rect)
{
    if (rect.isEmpty()) return {};
    const int w = rect.width();
    const int h = rect.height();

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return {};
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, w, h);
    HGDIOBJ old = SelectObject(hdcMem, hbm);

    BitBlt(hdcMem, 0, 0, w, h, hdcScreen, rect.x(), rect.y(), SRCCOPY | CAPTUREBLT);
    QPixmap pm = pixmapFromHbitmap(hbm, w, h);

    SelectObject(hdcMem, old);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    return pm;
}

#else // !_WIN32

QPixmap captureWindow(void*)               { return {}; }
QPixmap captureScreenRect(const QRect&)    { return {}; }

#endif

QPixmap fitForCarry(const QPixmap& src)
{
    if (src.isNull()) return src;
    constexpr int kMaxW = 64;
    constexpr int kMaxH = 48;
    if (src.width() <= kMaxW && src.height() <= kMaxH) {
        return src;
    }
    return src.scaled(kMaxW, kMaxH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace cr::capture
