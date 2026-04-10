#include "render/overlay_window.h"
#include "creature/creechr.h"
#include "render/sprite_atlas.h"
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
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    if (!m_creechr || !m_atlas) {
        return;
    }

    const QPoint widgetOriginEarly = geometry().topLeft();

    // trophies: small messy pile of past stolen things in the corner.
    // drawn FIRST so creechr and his current carry sit on top.
    for (const auto& t : m_creechr->trophies()) {
        p.drawPixmap(t.nestPos - widgetOriginEarly, t.pixmap);
    }

    // creechr's drawRect is in virtual-desktop coords. our widget's
    // top-left maps to the virtual desktop's top-left, but if the
    // virtual desktop has a negative origin (multimon to the left of
    // primary), we need to subtract our own geometry().topLeft()
    // before passing to QPainter (which works in widget-local coords).
    const QPoint widgetOrigin = widgetOriginEarly;

    // rappel line, drawn UNDER creechr so it looks like the rope comes
    // from inside his hands rather than over them
    if (m_creechr->rappelActive()) {
        const QPoint anchor(m_creechr->rappelAnchorX(), m_creechr->rappelAnchorY());
        // line origin: top-center of creechr's sprite, where his hands
        // are when reaching upward (matches climb_up / grab pose)
        const QPoint hands(
            static_cast<int>(m_creechr->position().x()) + 24,
            static_cast<int>(m_creechr->position().y()) + 2
        );
        QPen pen(QColor(20, 0, 15, 230));
        pen.setWidth(2);
        p.setPen(pen);
        p.drawLine(hands - widgetOrigin, anchor - widgetOrigin);
        // small grappling-hook dot at the anchor
        p.fillRect(QRect(anchor.x() - widgetOrigin.x() - 2,
                         anchor.y() - widgetOrigin.y() - 2, 5, 5),
                   QColor(20, 0, 15));
    }

    const QRect dst = m_creechr->drawRect().translated(-widgetOrigin);
    const QRect src = m_creechr->frameSrcRect();
    p.drawPixmap(dst, m_atlas->pixmap(), src);

    // particles — drawn AFTER creechr so they pop in front. small
    // 3x3 squares with alpha based on remaining lifetime.
    for (const auto& pt : m_creechr->particles()) {
        const double t = static_cast<double>(pt.ageMs) / qMax(1, pt.lifetimeMs);
        const int alpha = static_cast<int>(255.0 * (1.0 - t));
        QColor c = pt.color;
        c.setAlpha(qBound(0, alpha, 255));
        p.fillRect(QRect(static_cast<int>(pt.pos.x()) - widgetOrigin.x() - 1,
                         static_cast<int>(pt.pos.y()) - widgetOrigin.y() - 1,
                         3, 3),
                   c);
    }

    // carried/stashed bitmap, if there's an active heist with a pixmap.
    // when stashed it sits where it was dropped. while creechr is
    // carrying it, it's centered on his visible hands (carryAnchorScreen)
    // so the carry pose actually looks like he's holding the thing.
    if (const auto* h = m_creechr->heist(); h && !h->carriedPixmap.isNull()) {
        QPoint where;
        if (h->stashed) {
            where = h->stashedAt;
        } else {
            const QPoint anchor = m_creechr->carryAnchorScreen();
            const QSize  pms    = h->carriedPixmap.size();
            where = QPoint(anchor.x() - pms.width()  / 2,
                           anchor.y() - pms.height() / 2);
        }
        p.drawPixmap(where - widgetOrigin, h->carriedPixmap);
    }

    // speech bubble. small white rounded rect with dark border, drawn
    // above creechr's head. text is whatever he's currently muttering;
    // currentSpeech() auto-expires.
    const QString speech = m_creechr->currentSpeech();
    if (!speech.isEmpty()) {
        QFont f = p.font();
        f.setPointSize(9);
        f.setBold(true);
        p.setFont(f);
        const QFontMetrics fm(f);
        const int textW = fm.horizontalAdvance(speech);
        const int textH = fm.height();
        const int padX = 6;
        const int padY = 3;
        const int bubW = textW + padX * 2;
        const int bubH = textH + padY * 2;
        // anchor: just above his head, slightly offset toward his
        // facing direction so it doesnt cover his face
        const int cx = static_cast<int>(m_creechr->position().x()) + 24;
        const int cy = static_cast<int>(m_creechr->position().y()) - 6;
        const QRect bubble(cx - bubW / 2, cy - bubH, bubW, bubH);
        const QRect bubbleLocal = bubble.translated(-widgetOrigin);

        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(QColor(255, 255, 255, 240));
        p.setPen(QPen(QColor(20, 0, 15), 1));
        p.drawRoundedRect(bubbleLocal, 4, 4);
        // little tail pointing down toward creechr's head
        const int tailX = bubbleLocal.center().x();
        const int tailTop = bubbleLocal.bottom();
        p.drawLine(tailX - 3, tailTop, tailX, tailTop + 4);
        p.drawLine(tailX + 3, tailTop, tailX, tailTop + 4);

        p.setPen(QColor(20, 0, 15));
        p.drawText(bubbleLocal.adjusted(padX, padY, -padX, -padY),
                   Qt::AlignCenter, speech);
        p.setRenderHint(QPainter::Antialiasing, false);
    }
}
