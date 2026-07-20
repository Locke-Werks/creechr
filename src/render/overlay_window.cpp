#include "render/overlay_window.h"
#include "creature/creechr.h"
#include "render/sprite_atlas.h"
#include "util/logging.h"

#include <QCursor>
#include <QGuiApplication>
#include <QHideEvent>
#include <QMouseEvent>
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
    // note the ABSENCE of Qt::WindowTransparentForInput and
    // WA_TransparentForMouseEvents: click-through is owned exclusively
    // by the native WS_EX_TRANSPARENT bit (applyClickThroughFlags sets
    // it, setInteractive toggles it). qt believes this window accepts
    // input, which is what makes mouse events deliverable during the
    // brief interactive windows when the cursor is on the creature.
    setWindowFlags(
        Qt::FramelessWindowHint
        | Qt::WindowStaysOnTopHint
        | Qt::Tool
        | Qt::WindowDoesNotAcceptFocus
        | Qt::NoDropShadowWindowHint
    );
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setMouseTracking(true);

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

void OverlayWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    // visibility churn resets to the resting state: fully click-through
    applyClickThroughFlags();
}

void OverlayWindow::setInteractive(bool interactive)
{
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) return;
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const bool transparentNow = (ex & WS_EX_TRANSPARENT) != 0;
    if (transparentNow != interactive) {
        return; // already in the requested state, 60Hz-free
    }
    LONG_PTR next = ex | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (interactive) {
        next &= ~WS_EX_TRANSPARENT;
    } else {
        next |= WS_EX_TRANSPARENT;
    }
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, next);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER
                 | SWP_FRAMECHANGED);
    LOG_DEBUG(interactive ? QStringLiteral("overlay: interactive ON")
                          : QStringLiteral("overlay: interactive OFF"));
#else
    Q_UNUSED(interactive);
#endif
}

bool OverlayWindow::nativeEvent(const QByteArray& eventType, void* message,
                                qintptr* result)
{
#ifdef _WIN32
    if (eventType == QByteArrayLiteral("windows_generic_MSG")) {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_NCHITTEST) {
            // secondary filter. windows only asks a window without
            // WS_EX_TRANSPARENT, i.e. during the interactive moments,
            // and only ever about the cursor location — so comparing
            // QCursor::pos() (logical) against logical rects sidesteps
            // the physical-coords-in-lParam mixed-dpi trap entirely.
            // HTTRANSPARENT here cannot rescue a misrouted click (the
            // OS routed it before asking), but it keeps qt from
            // synthesizing events for far-away points if a logic bug
            // ever leaves the interactive bit stuck cleared.
            if (m_creechr) {
                const QRect hit = m_creechr->drawRect().adjusted(-8, -8, 8, 8);
                if (hit.contains(QCursor::pos())) {
                    *result = HTCLIENT;
                    return true;
                }
            }
            *result = HTTRANSPARENT;
            return true;
        }
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

void OverlayWindow::mousePressEvent(QMouseEvent* event)
{
    emit sigMousePressed(event->globalPosition());
    event->accept();
}

void OverlayWindow::mouseMoveEvent(QMouseEvent* event)
{
    emit sigMouseMoved(event->globalPosition());
    event->accept();
}

void OverlayWindow::mouseReleaseEvent(QMouseEvent* event)
{
    emit sigMouseReleased(event->globalPosition());
    event->accept();
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

namespace {

// speech bubble body rect in virtual-desktop coords. ONE function used
// by both painting and the dirty-region math, because the two drifting
// apart means long lines leave ghost trails.
QRect speechBubbleBodyRect(const cr::Creechr& c, QFont f)
{
    const QString speech = c.currentSpeech();
    if (speech.isEmpty()) return {};
    f.setPointSize(9);
    f.setBold(true);
    const QFontMetrics fm(f);
    const int bubW = fm.horizontalAdvance(speech) + 6 * 2;
    const int bubH = fm.height() + 3 * 2;
    const int cx = static_cast<int>(c.position().x()) + 24;
    const int cy = static_cast<int>(c.position().y()) - 6;
    return QRect(cx - bubW / 2, cy - bubH, bubW, bubH);
}

} // namespace

QRegion OverlayWindow::computeSceneRegion() const
{
    QRegion region;
    if (!m_creechr) return region;

    // creature + a margin for the halo strokes, plus the foot shadow
    // strip below him
    region += m_creechr->drawRect().adjusted(-8, -8, 8, 14);

    if (m_creechr->rappelActive()) {
        const QPoint anchor(m_creechr->rappelAnchorX(), m_creechr->rappelAnchorY());
        const QPoint hands(static_cast<int>(m_creechr->position().x()) + 24,
                           static_cast<int>(m_creechr->position().y()) + 2);
        region += QRect(hands, anchor).normalized().adjusted(-6, -6, 6, 6);
    }

    for (const auto& pt : m_creechr->particles()) {
        region += QRect(static_cast<int>(pt.pos.x()) - 2,
                        static_cast<int>(pt.pos.y()) - 2, 6, 6);
    }

    // trophies are static, but they belong in the region every frame:
    // that way when the cap evicts the oldest one, its rect is still
    // in LAST frame's region and the union erases it. eight tiny
    // rects, not worth being clever about.
    for (const auto& t : m_creechr->trophies()) {
        region += QRect(t.nestPos, t.pixmap.size()).adjusted(-2, -2, 2, 2);
    }

    for (const auto& s : m_creechr->sinkingItems()) {
        region += QRect(static_cast<int>(s.x) - 1, static_cast<int>(s.y) - 1,
                        s.pixmap.width() + 2, s.pixmap.height() + 2);
    }

    if (const auto* h = m_creechr->heist(); h && !h->carriedPixmap.isNull()) {
        const QSize pms = h->carriedPixmap.size();
        if (h->stashed) {
            region += QRect(h->stashedAt, pms).adjusted(-2, -2, 2, 2);
        } else {
            const QPoint a = m_creechr->carryAnchorScreen();
            region += QRect(a.x() - pms.width() / 2 - 2, a.y() - pms.height() / 2 - 2,
                            pms.width() + 4, pms.height() + 4);
        }
    }

    const QRect bubble = speechBubbleBodyRect(*m_creechr, font());
    if (!bubble.isNull()) {
        region += bubble.adjusted(-2, -2, 2, 8); // +8 covers the tail
    }

    return region;
}

void OverlayWindow::updateScene()
{
    static const bool kFullRepaint = !qgetenv("CREECHR_FULL_REPAINT").isEmpty();
    if (kFullRepaint || !m_creechr) {
        update();
        return;
    }

    SceneStamp stamp;
    stamp.creature = m_creechr->drawRect();
    stamp.frameSrc = m_creechr->frameSrcRect();
    stamp.speech   = m_creechr->currentSpeech();
    stamp.anchor   = m_creechr->rappelActive()
        ? QPoint(m_creechr->rappelAnchorX(), m_creechr->rappelAnchorY())
        : QPoint(-1, -1);
    const auto* h = m_creechr->heist();
    stamp.carried  = h && !h->carriedPixmap.isNull();
    stamp.stash    = (h && h->stashed) ? h->stashedAt : QPoint(-1, -1);
    stamp.trophies = m_creechr->trophies().size();

    // particles and sinking loot move every tick by construction, so
    // their mere existence makes the frame dirty
    const bool inherentlyAnimated =
        !m_creechr->particles().isEmpty() || !m_creechr->sinkingItems().isEmpty();

    const QRegion cur = computeSceneRegion();

    if (!inherentlyAnimated && stamp == m_lastStamp && cur == m_lastSceneRegion) {
        return; // nothing observable changed, skip the frame
    }

    // previous-union-current guarantees the old position gets erased
    const QRegion dirty = (cur + m_lastSceneRegion)
        .translated(-geometry().topLeft());
    m_lastSceneRegion = cur;
    m_lastStamp = stamp;
    update(dirty);
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

    // sinking items: things creechr got bored of and tossed, currently
    // drifting down toward the bottom edge. drawn under creechr so if
    // he walks past one in midair, he's in front of it.
    for (const auto& s : m_creechr->sinkingItems()) {
        p.drawPixmap(QPoint(static_cast<int>(s.x),
                            static_cast<int>(s.y)) - widgetOriginEarly,
                     s.pixmap);
    }

    // creechr's drawRect is in virtual-desktop coords. our widget's
    // top-left maps to the virtual desktop's top-left, but if the
    // virtual desktop has a negative origin (multimon to the left of
    // primary), we need to subtract our own geometry().topLeft()
    // before passing to QPainter (which works in widget-local coords).
    const QPoint widgetOrigin = widgetOriginEarly;

    // rappel line, drawn UNDER creechr so it looks like the rope comes
    // from inside his hands rather than over them. two-pass outline:
    // wide white halo first so it's visible on dark wallpaper, then
    // the dark rope on top. same trick as the arms/legs in the
    // sprite atlas.
    if (m_creechr->rappelActive()) {
        const QPoint anchor(m_creechr->rappelAnchorX(), m_creechr->rappelAnchorY());
        // line origin: top-center of creechr's sprite, where his hands
        // are when reaching upward (matches climb_up / grab pose)
        const QPoint hands(
            static_cast<int>(m_creechr->position().x()) + 24,
            static_cast<int>(m_creechr->position().y()) + 2
        );
        const QPoint handsLocal  = hands  - widgetOrigin;
        const QPoint anchorLocal = anchor - widgetOrigin;

        p.setRenderHint(QPainter::Antialiasing, true);
        // white halo pass (4 px, slightly translucent so it doesnt
        // overpower light wallpapers)
        QPen haloPen(QColor(255, 255, 255, 220));
        haloPen.setWidth(4);
        haloPen.setCapStyle(Qt::RoundCap);
        p.setPen(haloPen);
        p.drawLine(handsLocal, anchorLocal);
        // dark rope pass (2 px) on top
        QPen ropePen(QColor(20, 0, 15, 240));
        ropePen.setWidth(2);
        ropePen.setCapStyle(Qt::RoundCap);
        p.setPen(ropePen);
        p.drawLine(handsLocal, anchorLocal);
        p.setRenderHint(QPainter::Antialiasing, false);

        // grappling-hook dot at the anchor: white halo underneath,
        // dark center on top, so its visible against any backdrop
        p.fillRect(QRect(anchorLocal.x() - 3, anchorLocal.y() - 3, 7, 7),
                   QColor(255, 255, 255, 220));
        p.fillRect(QRect(anchorLocal.x() - 2, anchorLocal.y() - 2, 5, 5),
                   QColor(20, 0, 15));
    }

    // shadow ellipse at creechr's feet — gives him visual grounding
    // so he doesnt look like he's floating an inch above the surface.
    // skip when flung (shadow on a falling creature looks wrong).
    {
        const QString sname = m_creechr->stateMachine().currentName();
        const bool drawShadow = (sname != QLatin1String("flung")
                              && sname != QLatin1String("rappel_climb")
                              && sname != QLatin1String("rappel_descend"));
        if (drawShadow) {
            const int shadowCx = static_cast<int>(m_creechr->position().x()) + 24;
            const int shadowCy = static_cast<int>(m_creechr->position().y()) + 46;
            const QRect shadow(shadowCx - 14, shadowCy - 3, 28, 6);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 70));
            p.drawEllipse(shadow.translated(-widgetOrigin));
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setBrush(Qt::NoBrush);
        }
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
        const int padX = 6;
        const int padY = 3;
        // geometry comes from the same helper the dirty-region math
        // uses, so the two can never disagree about where the bubble is
        const QRect bubble = speechBubbleBodyRect(*m_creechr, font());
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
