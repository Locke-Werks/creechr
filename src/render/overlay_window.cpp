#include "render/overlay_window.h"
#include "util/logging.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPaintEvent>
#include <QRect>
#include <QScreen>
#include <QShowEvent>

#ifdef _WIN32
#  include <windows.h>
#endif

OverlayWindow::OverlayWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("creechr-overlay"));
    setWindowFlags(
        Qt::FramelessWindowHint
        | Qt::WindowStaysOnTopHint
        | Qt::Tool
        | Qt::WindowTransparentForInput
        | Qt::NoDropShadowWindowHint
    );
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);

    // recompute when screens come and go. yes this signal can fire
    // multiple times for a single monitor change. that's fine, it's
    // cheap and the geometry math is deterministic.
    connect(qApp, &QGuiApplication::screenAdded,   this, &OverlayWindow::recomputeGeometry);
    connect(qApp, &QGuiApplication::screenRemoved, this, &OverlayWindow::recomputeGeometry);
    connect(qApp, &QGuiApplication::primaryScreenChanged, this, &OverlayWindow::recomputeGeometry);

    recomputeGeometry();
    LOG_INFO(QStringLiteral("overlay constructed"));
}

OverlayWindow::~OverlayWindow() = default;

void OverlayWindow::recomputeGeometry()
{
    // union of every screen's geometry. note: QScreen::geometry() returns
    // logical pixels in qt6 by default, which is what we want for placing
    // a top-level widget. don't switch this to nativeGeometry() without
    // also rewriting all the sprite drawing math.
    QRect bounds;
    const auto screens = QGuiApplication::screens();
    for (QScreen* s : screens) {
        bounds = bounds.united(s->geometry());
    }
    if (bounds.isEmpty()) {
        // shouldn't happen but if it does, just don't move the window
        LOG_WARN(QStringLiteral("overlay: no screens reported, leaving geometry alone"));
        return;
    }
    setGeometry(bounds);
    LOG_DEBUG(QStringLiteral("overlay: bounds %1,%2 %3x%4")
        .arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height()));
}

void OverlayWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    applyClickThroughFlags();
}

void OverlayWindow::applyClickThroughFlags()
{
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) {
        return;
    }
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_LAYERED;     // required for translucency to actually compose
    exStyle |= WS_EX_TRANSPARENT; // hit-test pass-through
    exStyle |= WS_EX_TOOLWINDOW;  // no taskbar / no alt-tab
    exStyle |= WS_EX_NOACTIVATE;  // don't steal focus on click (we won't get clicks anyway)
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
    // SetWindowPos with no-move/no-size to apply the new style
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    LOG_DEBUG(QStringLiteral("overlay: click-through ex-styles applied"));
#endif
}

void OverlayWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);

    // dev marker so i can prove the overlay is actually rendering. it's
    // a small filled rectangle in the top-left of the virtual desktop.
    // gets ripped out the moment a real sprite shows up.
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.fillRect(QRect(8, 8, 32, 32), QColor(220, 50, 140, 200));
    p.setPen(QColor(255, 255, 255, 220));
    p.drawText(QRect(8, 8, 200, 32), Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("  creechr overlay"));
}
