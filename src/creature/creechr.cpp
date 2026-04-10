#include "creature/creechr.h"
#include "creature/world_context.h"
#include "heist/bitmap_capture.h"
#include "heist/hoard.h"
#include "render/sprite_atlas.h"
#include "targets/extension_target_provider.h"
#include "util/logging.h"
#include "util/win32_helpers.h"

#include <QCursor>
#include <QDateTime>
#include <QRandomGenerator>
#include <QRect>
#include <QtGlobal>
#include <memory>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace cr {

// === states ===

namespace {

// pause-the-walk-for-a-bit. 1-3s of standing around looking shifty.
class IdleState : public State
{
public:
    QString name() const override { return QStringLiteral("idle"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        c.setVelocity({ 0, 0 });
        c.animator().setAnimation(QStringLiteral("idle"));
        // 800-2400ms of standing around
        m_remaining = 800 + (QRandomGenerator::global()->bounded(1600));
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
    {
        // pending heist takes priority over EVERYTHING else, including
        // sleep. otherwise the orchestrator can queue a heist and
        // creechr will just nap on top of it.
        if (c.heist() && !c.heist()->grabbed) {
            return QStringLiteral("heist_approach");
        }
        if (world.msSinceLastInput > 30000) {
            return QStringLiteral("sleep");
        }
        m_remaining -= deltaMs;
        if (m_remaining > 0) {
            return {};
        }
        return QStringLiteral("walk");
    }

private:
    int m_remaining = 0;
};

// horizontal stroll. constant velocity. bounces off screen edges.
// after a random distance, transitions back to idle.
class WalkState : public State
{
public:
    QString name() const override { return QStringLiteral("walk"); }

    void enter(Creechr& c, const WorldContext& /*world*/) override
    {
        // pick a direction at random unless we already have one
        if (qFuzzyIsNull(c.velocity().x())) {
            c.setFacingRight(QRandomGenerator::global()->bounded(2) == 0);
        }
        const double speed = 60.0; // pixels/sec
        c.setVelocity({ c.facingRight() ? speed : -speed, 0.0 });
        c.animator().setAnimation(c.facingRight() ? QStringLiteral("walk_right")
                                                   : QStringLiteral("walk_left"));
        // walk for 2-6 seconds before getting bored
        // walk for 6-14 seconds before getting bored. v0.1 had 2-6s
        // which meant he was constantly switching states and the user
        // mostly saw him standing still. longer walks → he actually
        // crosses the screen.
        m_remaining = 6000 + QRandomGenerator::global()->bounded(8000);
        // snap y to whatever platform we're on. don't re-park to floor
        // — that breaks walking on top of windows.
        c.setPosition({ c.position().x(), static_cast<double>(c.floorY()) });
        // first climb attempt 4-10s into the walk. v0.1 had 0.5-1.5s
        // which combined with always-pick-nearest-edge meant he was
        // climbing the same maximized window's left edge over and over
        // and never actually walked anywhere.
        m_climbCooldown = 4000 + QRandomGenerator::global()->bounded(6000);
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
    {
        if (world.msSinceLastInput > 30000) {
            return QStringLiteral("sleep");
        }
        const double dt = deltaMs / 1000.0;
        QPointF pos = c.position() + c.velocity() * dt;

        const bool onFloor = c.floorY() >= world.virtualDesktop.bottom() - (kSpriteHeight + 1);

        if (onFloor) {
            // on the floor: bounce off screen edges
            const int leftLimit  = world.virtualDesktop.left();
            const int rightLimit = world.virtualDesktop.right() - kSpriteWidth;
            if (pos.x() < leftLimit) {
                pos.setX(leftLimit);
                c.setFacingRight(true);
                c.setVelocity({ +qAbs(c.velocity().x()), c.velocity().y() });
                c.animator().setAnimation(QStringLiteral("walk_right"));
            } else if (pos.x() > rightLimit) {
                pos.setX(rightLimit);
                c.setFacingRight(false);
                c.setVelocity({ -qAbs(c.velocity().x()), c.velocity().y() });
                c.animator().setAnimation(QStringLiteral("walk_left"));
            }
        } else {
            // walking on top of a window: bounce off window's horizontal
            // extent, but if we walk off either edge of the WHOLE world
            // somehow, climb back down. (shouldn't happen.)
            //
            // we don't have a strong reference to which window we're on
            // — find one whose top is at our current y and whose x range
            // contains us, that's it.
            int leftLimit  = world.virtualDesktop.left();
            int rightLimit = world.virtualDesktop.right() - kSpriteWidth;
            for (const QRect& w : world.windowRects) {
                if (w.top() == c.floorY() + kSpriteHeight && w.left() <= pos.x() && pos.x() <= w.right()) {
                    leftLimit = w.left();
                    rightLimit = w.right() - kSpriteWidth;
                    break;
                }
            }
            if (pos.x() < leftLimit || pos.x() > rightLimit) {
                // walked off the edge of the window — climb down
                pos.setX(qBound<qreal>(leftLimit, pos.x(), rightLimit));
                c.setPosition(pos);
                c.setClimbTarget(static_cast<int>(pos.x()),
                                 world.virtualDesktop.bottom() - kSpriteHeight);
                return QStringLiteral("climb_down");
            }
        }
        c.setPosition(pos);

        // climb opportunity? only on the floor, and only if cooldown elapsed
        m_climbCooldown -= deltaMs;
        if (onFloor && m_climbCooldown <= 0) {
            if (world.windowRects.isEmpty()) {
                LOG_DEBUG(QStringLiteral("walk: climb attempt but no windows"));
                m_climbCooldown = 1000;
            } else {
                // pick a random window. require some minimum size.
                const int idx = QRandomGenerator::global()->bounded(world.windowRects.size());
                const QRect& w = world.windowRects[idx];
                if (w.height() >= 64 && w.width() >= 64) {
                    // pick edge: 60% chance the FARTHER edge so he
                    // actually traverses the screen, 40% nearer.
                    // (v0.1 always picked nearer, which meant he camped
                    // on the left edge of the maximized window forever.)
                    const int leftDist  = qAbs(static_cast<int>(pos.x()) - w.left());
                    const int rightDist = qAbs(static_cast<int>(pos.x()) - w.right());
                    const bool preferFar = QRandomGenerator::global()->bounded(10) < 6;
                    const bool useLeft = preferFar
                        ? (leftDist >  rightDist)
                        : (leftDist <= rightDist);
                    const int targetX = useLeft ? w.left() : w.right() - kSpriteWidth;
                    c.setClimbTarget(targetX, w.top() - kSpriteHeight);
                    LOG_DEBUG(QStringLiteral("walk: chose climb target window %1 (%2x%3) at (%4,%5) [%6 edge]")
                        .arg(idx).arg(w.width()).arg(w.height())
                        .arg(targetX).arg(w.top() - kSpriteHeight)
                        .arg(useLeft ? QStringLiteral("left") : QStringLiteral("right")));
                    return QStringLiteral("approach_wall");
                }
                m_climbCooldown = 800;
            }
        }

        m_remaining -= deltaMs;
        if (m_remaining <= 0) {
            return QStringLiteral("idle");
        }
        return {};
    }

private:
    int m_remaining = 0;
    int m_climbCooldown = 0;
};

// walk toward the climb target's x. when we get there, climb up.
class ApproachWallState : public State
{
public:
    QString name() const override { return QStringLiteral("approach_wall"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        const bool right = c.climbTargetX() > c.position().x();
        c.setFacingRight(right);
        c.setVelocity({ right ? 80.0 : -80.0, 0.0 });
        c.animator().setAnimation(right ? QStringLiteral("walk_right")
                                        : QStringLiteral("walk_left"));
        m_giveUpMs = 8000;
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
    {
        if (world.msSinceLastInput > 30000) {
            c.clearClimbTarget();
            return QStringLiteral("sleep");
        }
        const double dt = deltaMs / 1000.0;
        QPointF pos = c.position() + c.velocity() * dt;
        c.setPosition(pos);

        if (qAbs(static_cast<int>(pos.x()) - c.climbTargetX()) <= 2) {
            // arrived. snap and start climbing.
            c.setPosition({ static_cast<qreal>(c.climbTargetX()), pos.y() });
            return QStringLiteral("climb_up");
        }

        m_giveUpMs -= deltaMs;
        if (m_giveUpMs <= 0) {
            // walked too far without arriving (window probably moved or closed)
            c.clearClimbTarget();
            return QStringLiteral("walk");
        }
        return {};
    }

private:
    int m_giveUpMs = 0;
};

// snap x, move up at constant speed until we hit climbTargetY.
class ClimbUpState : public State
{
public:
    QString name() const override { return QStringLiteral("climb_up"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        c.setVelocity({ 0.0, -90.0 });
        c.animator().setAnimation(QStringLiteral("climb_up"));
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext&) override
    {
        const double dt = deltaMs / 1000.0;
        QPointF pos = c.position() + c.velocity() * dt;
        if (pos.y() <= c.climbTargetY()) {
            pos.setY(c.climbTargetY());
            c.setPosition(pos);
            c.setFloorY(c.climbTargetY());
            c.clearClimbTarget();
            return QStringLiteral("walk");
        }
        c.setPosition(pos);
        return {};
    }
};

// move down at constant speed until we hit climbTargetY (the floor).
class ClimbDownState : public State
{
public:
    QString name() const override { return QStringLiteral("climb_down"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        c.setVelocity({ 0.0, 90.0 });
        c.animator().setAnimation(QStringLiteral("climb_down"));
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext&) override
    {
        const double dt = deltaMs / 1000.0;
        QPointF pos = c.position() + c.velocity() * dt;
        if (pos.y() >= c.climbTargetY()) {
            pos.setY(c.climbTargetY());
            c.setPosition(pos);
            c.setFloorY(c.climbTargetY());
            c.clearClimbTarget();
            return QStringLiteral("walk");
        }
        c.setPosition(pos);
        return {};
    }
};

// === heist states ===
//
// flow: heist_approach -> heist_carry -> heist_wait -> heist_return -> idle
//   approach: walk to the target's nearest edge, then capture+hide
//   carry:    walk to chosen screen corner, then add to hoard
//   wait:     stand at corner for 30-90s
//   return:   walk back to original location, then restore
//
// any state along the way that detects user input (msSinceLastInput<300)
// or hard timeouts will abort and restore. spec §5.1 safety 2.

namespace heist_helpers {
constexpr int kHeistMaxNonStashMs = 60000;     // §5.1 safety 2
constexpr int kHeistInputAbortMs  = 300;        // user touched something
} // namespace heist_helpers

// little helper to compute the closer of (left, right) edges
static int nearerEdge(int x, int left, int right)
{
    return (qAbs(x - left) <= qAbs(x - right)) ? left : right;
}

class HeistApproachState : public State
{
public:
    QString name() const override { return QStringLiteral("heist_approach"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        m_elapsedMs = 0;
        if (!c.heist()) return;
        const QRect& f = c.heist()->target.screenRect;
        const int targetX = nearerEdge(static_cast<int>(c.position().x()),
                                       f.left() - 4, f.right() + 4);
        const bool right = targetX > c.position().x();
        c.setFacingRight(right);
        c.setVelocity({ right ? 100.0 : -100.0, 0.0 });
        c.animator().setAnimation(right ? QStringLiteral("walk_right")
                                        : QStringLiteral("walk_left"));
        m_targetX = targetX;
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
    {
        m_elapsedMs += deltaMs;
        if (m_elapsedMs > heist_helpers::kHeistMaxNonStashMs ||
            world.msSinceLastInput < heist_helpers::kHeistInputAbortMs) {
            LOG_INFO(QStringLiteral("heist: approach aborted"));
            c.clearHeist();
            return QStringLiteral("idle");
        }
        QPointF pos = c.position() + c.velocity() * (deltaMs / 1000.0);
        c.setPosition(pos);
        if (qAbs(static_cast<int>(pos.x()) - m_targetX) <= 4) {
            return QStringLiteral("heist_grab");
        }
        return {};
    }

private:
    int m_elapsedMs = 0;
    int m_targetX = 0;
};

// HeistGrab is now animation-driven so the bite is actually visible.
// enter() captures the pixmap (so we have it ready) and starts the
// "grab" one-shot animation. tick() waits for the grab anim to finish,
// THEN hides the source (for window heists) and transitions to carry.
// the source window stays visible during the grab anim because we
// want the user to see creechr lunge at it before it disappears.
class HeistGrabState : public State
{
public:
    QString name() const override { return QStringLiteral("heist_grab"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        m_capturedOk = false;
        m_phase = Phase::Grabbing;
        c.setVelocity({ 0, 0 });
        c.animator().setAnimation(QStringLiteral("grab"), /*reset*/true);

        HeistContext* h = c.heist();
        if (!h) return;

        if (h->target.kind == TargetKind::UiaElement) {
            // see §6.2 deviation note in v0.3 — no occluder, just
            // BitBlt the rect and have him carry the duplicate.
            QPixmap pm = capture::captureScreenRect(h->target.screenRect);
            if (pm.isNull()) {
                LOG_WARN(QStringLiteral("heist: BitBlt of uia rect failed"));
                return;
            }
            h->originalFrame = h->target.screenRect;
            h->carriedPixmap = pm;
            m_capturedOk = true;
            LOG_INFO(QStringLiteral("heist: pre-captured uia element %1").arg(h->target.label));
        }
        else if (h->target.kind == TargetKind::DomElement) {
            // dom heist: BitBlt the rect FIRST so we have the visual,
            // then ask the extension to actually remove the element from
            // the page. tick() waits for the ack before transitioning.
            QPixmap pm = capture::captureScreenRect(h->target.screenRect);
            if (pm.isNull()) {
                LOG_WARN(QStringLiteral("heist: BitBlt of dom rect failed"));
                return;
            }
            h->originalFrame = h->target.screenRect;
            h->carriedPixmap = pm;
            if (auto* ext = c.extensionProvider()) {
                // make sure no stale ack from a prior cycle is sitting around
                ext->consumeStealAck(h->target.opaqueId);
                ext->requestSteal(h->target.opaqueId);
            } else {
                LOG_WARN(QStringLiteral("heist: dom target but no ext provider"));
                return;
            }
            m_capturedOk = true;
            LOG_INFO(QStringLiteral("heist: pre-captured dom element %1, awaiting steal_ack")
                .arg(h->target.label));
        }
        else if (h->target.kind == TargetKind::Window) {
#ifdef _WIN32
            HWND hwnd = static_cast<HWND>(h->target.hwnd);
            if (!hwnd || !IsWindow(hwnd)) {
                LOG_WARN(QStringLiteral("heist: target window died before grab"));
                return;
            }
            QPixmap pm = capture::captureWindow(hwnd);
            if (pm.isNull()) {
                LOG_WARN(QStringLiteral("heist: PrintWindow returned null"));
                return;
            }
            RECT r = {};
            GetWindowRect(hwnd, &r);
            h->originalFrame = QRect(QPoint(r.left, r.top),
                                     QPoint(r.right - 1, r.bottom - 1));
            h->carriedPixmap = pm;
            m_capturedOk = true;
            LOG_INFO(QStringLiteral("heist: pre-captured %1 (%2x%3)")
                .arg(h->target.label).arg(pm.width()).arg(pm.height()));
#endif
        } else if (h->target.kind == TargetKind::Cursor) {
            h->originalFrame = h->target.screenRect;
            m_capturedOk = true;
        }
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
    {
        HeistContext* h = c.heist();
        if (!h) return QStringLiteral("idle");

        if (!m_capturedOk) {
            // capture failed in enter() — abort cleanly
            c.clearHeist();
            return QStringLiteral("idle");
        }

        if (m_phase == Phase::Grabbing) {
            if (!c.animator().finished()) return {};
            // grab anim done. for DOM heists ALSO wait for the
            // extension's steal_ack so we know the element is actually
            // gone from the page before we proceed. 4-second hard
            // timeout in case the ack never comes.
            if (h->target.kind == TargetKind::DomElement) {
                if (auto* ext = c.extensionProvider()) {
                    if (!ext->hasStealAck(h->target.opaqueId)) {
                        m_domAckWaitMs += 16; // ~one tick
                        if (m_domAckWaitMs > 4000) {
                            LOG_WARN(QStringLiteral("heist: dom steal_ack timeout, aborting"));
                            c.clearHeist();
                            return QStringLiteral("idle");
                        }
                        return {};
                    }
                    ext->consumeStealAck(h->target.opaqueId);
                }
            }
            // grab anim done. NOW hide the source (window heists only)
            // and bridge into a brief bite loop.
#ifdef _WIN32
            if (h->target.kind == TargetKind::Window) {
                HWND hwnd = static_cast<HWND>(h->target.hwnd);
                if (hwnd && IsWindow(hwnd)) {
                    ShowWindow(hwnd, SW_HIDE);
                }
            }
#endif
            h->grabbed = true;
            c.animator().setAnimation(QStringLiteral("bite"), /*reset*/true);
            m_phase = Phase::Biting;
            m_biteMsLeft = 480; // ~6 frames of bite at 80ms each
            return {};
        }

        // Biting phase — chomp for a bit then move on. deltaMs because
        // we're at 60Hz now, not the 100ms-fixed assumption from v0.2.
        m_biteMsLeft -= deltaMs;
        if (m_biteMsLeft <= 0) {
            // pick a corner to carry to (was at the bottom of the old version)
            const QRect& vd = world.virtualDesktop;
            const bool topish  = QRandomGenerator::global()->bounded(2) == 0;
            const bool leftish = QRandomGenerator::global()->bounded(2) == 0;
            h->carryDestination = QPoint(
                leftish ? vd.left() + 32 : vd.right() - (kSpriteWidth * 2),
                topish  ? vd.top()  + 64 : vd.bottom() - (kSpriteHeight * 2)
            );
            return QStringLiteral("heist_carry");
        }
        return {};
    }

private:
    enum class Phase { Grabbing, Biting };
    Phase m_phase = Phase::Grabbing;
    bool m_capturedOk = false;
    int m_biteMsLeft = 0;
    int m_domAckWaitMs = 0;
};

class HeistCarryState : public State
{
public:
    QString name() const override { return QStringLiteral("heist_carry"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        m_elapsedMs = 0;
        if (!c.heist()) return;
        const bool right = c.heist()->carryDestination.x() > c.position().x();
        c.setFacingRight(right);
        c.setVelocity({ right ? 80.0 : -80.0, 0.0 });
        // carry-specific walk animation: arms held forward like he's
        // actually holding the thing, instead of swinging at his sides.
        c.animator().setAnimation(right ? QStringLiteral("carry_right")
                                        : QStringLiteral("carry_left"));
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
    {
        m_elapsedMs += deltaMs;
        HeistContext* h = c.heist();
        if (!h) return QStringLiteral("idle");

        if (m_elapsedMs > heist_helpers::kHeistMaxNonStashMs ||
            world.msSinceLastInput < heist_helpers::kHeistInputAbortMs) {
            LOG_INFO(QStringLiteral("heist: carry aborted, returning early"));
            return QStringLiteral("heist_return");
        }

        QPointF pos = c.position() + c.velocity() * (deltaMs / 1000.0);
        c.setPosition(pos);

        // for cursor heists, drag the cursor along with us — but bail
        // immediately if any mouse button is currently down (the user
        // is mid-click and grabbing the cursor would actively break
        // their workflow). also clamp to the virtual desktop bounds so
        // we never strand the pointer on a disconnected screen.
        if (h->target.kind == TargetKind::Cursor) {
            if (cr::win32::anyMouseButtonDown()) {
                LOG_INFO(QStringLiteral("heist: mouse button down mid-cursor-carry, releasing"));
                return QStringLiteral("heist_return");
            }
#ifdef _WIN32
            int cx = qBound(world.virtualDesktop.left()  + 4,
                            static_cast<int>(pos.x()) + 16,
                            world.virtualDesktop.right() - 4);
            int cy = qBound(world.virtualDesktop.top()   + 4,
                            static_cast<int>(pos.y()) + 16,
                            world.virtualDesktop.bottom() - 4);
            SetCursorPos(cx, cy);
#endif
        }

        if (qAbs(static_cast<int>(pos.x()) - h->carryDestination.x()) <= 6) {
            // arrived. stash.
            h->stashedAt = QPoint(static_cast<int>(pos.x()),
                                  static_cast<int>(pos.y()));
            return QStringLiteral("heist_stash");
        }
        return {};
    }

private:
    int m_elapsedMs = 0;
};

class HeistStashState : public State
{
public:
    QString name() const override { return QStringLiteral("heist_stash"); }

    QString tick(int /*deltaMs*/, Creechr& c, const WorldContext&) override
    {
        HeistContext* h = c.heist();
        if (!h) return QStringLiteral("idle");
        c.setVelocity({ 0, 0 });

        // cursor heist returns the cursor immediately at the stash spot.
        // it's a 1.2-1.8s gag, not a 30-second one.
        if (h->target.kind == TargetKind::Cursor) {
            h->returnAtMs = QDateTime::currentMSecsSinceEpoch();
        } else {
            h->returnAtMs = QDateTime::currentMSecsSinceEpoch()
                + 30000 + QRandomGenerator::global()->bounded(60000);
        }

        // register in hoard so the quit handler can restore us
        if (auto* hoard = c.hoard()) {
            HoardEntry e;
            switch (h->target.kind) {
            case TargetKind::Cursor:     e.kind = HoardKind::Cursor; break;
            case TargetKind::UiaElement: e.kind = HoardKind::Uia;    break;
            case TargetKind::DomElement: e.kind = HoardKind::Dom;    break;
            case TargetKind::Window:
            default:                     e.kind = HoardKind::Window; break;
            }
            e.label = h->target.label;
            e.pixmap = h->carriedPixmap;
            e.originPos = h->originalFrame.topLeft();
            e.stashPos  = h->stashedAt;
            e.hwnd = h->target.hwnd;
            const QRect orig = h->originalFrame;
#ifdef _WIN32
            HWND hwnd = static_cast<HWND>(h->target.hwnd);
            const TargetKind tk = h->target.kind;
            // dom restores need to ask the extension to put the element
            // back. capturing a raw pointer to the provider is fine — it
            // outlives the hoard (both owned by CreechrApp, destroyed
            // in declared order, hoard first).
            ExtensionTargetProvider* extPtr = c.extensionProvider();
            const QString domId = h->target.opaqueId;
            e.restore = [hwnd, orig, tk, extPtr, domId]() {
                if (tk == TargetKind::Window && hwnd && IsWindow(hwnd)) {
                    SetWindowPos(hwnd, HWND_TOP,
                                 orig.left(), orig.top(),
                                 orig.width(), orig.height(),
                                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
                }
                if (tk == TargetKind::DomElement && extPtr && !domId.isEmpty()) {
                    extPtr->requestRestore(domId);
                }
                // cursor doesn't need an explicit restore — we already
                // dragged it during carry, and on return we set it back.
                // uia doesn't need anything either — we never modified
                // the source app, the visual went into creechr's hands.
            };
#else
            e.restore = []() {};
#endif
            h->hoardId = hoard->add(e);
            h->stashed = true;
        }
        return QStringLiteral("heist_wait");
    }
};

class HeistWaitState : public State
{
public:
    QString name() const override { return QStringLiteral("heist_wait"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        c.setVelocity({ 0, 0 });
        c.animator().setAnimation(QStringLiteral("idle"));
    }

    QString tick(int /*deltaMs*/, Creechr& c, const WorldContext&) override
    {
        HeistContext* h = c.heist();
        if (!h) return QStringLiteral("idle");
        if (QDateTime::currentMSecsSinceEpoch() >= h->returnAtMs) {
            return QStringLiteral("heist_return");
        }
        return {};
    }
};

class HeistReturnState : public State
{
public:
    QString name() const override { return QStringLiteral("heist_return"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        if (!c.heist()) return;
        const QPoint orig = c.heist()->originalFrame.topLeft();
        const bool right = orig.x() > c.position().x();
        c.setFacingRight(right);
        c.setVelocity({ right ? 100.0 : -100.0, 0.0 });
        // still carrying the thing, so still using the carry walk anim
        c.animator().setAnimation(right ? QStringLiteral("carry_right")
                                        : QStringLiteral("carry_left"));
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext&) override
    {
        HeistContext* h = c.heist();
        if (!h) return QStringLiteral("idle");
        QPointF pos = c.position() + c.velocity() * (deltaMs / 1000.0);
        c.setPosition(pos);

        const int targetX = h->originalFrame.left();
        if (qAbs(static_cast<int>(pos.x()) - targetX) <= 6) {
            // arrived. restore via hoard.
            if (auto* hoard = c.hoard(); hoard && !h->hoardId.isEmpty()) {
                hoard->restoreById(h->hoardId);
            }
#ifdef _WIN32
            if (h->target.kind == TargetKind::Cursor) {
                SetCursorPos(h->originalFrame.center().x(),
                             h->originalFrame.center().y());
            }
#endif
            c.clearHeist();
            return QStringLiteral("idle");
        }
        return {};
    }
};

// nap. just sit there with eyes closed.
class SleepState : public State
{
public:
    QString name() const override { return QStringLiteral("sleep"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        c.setVelocity({ 0, 0 });
        c.animator().setAnimation(QStringLiteral("sleep"));
    }

    QString tick(int /*deltaMs*/, Creechr& /*c*/, const WorldContext& world) override
    {
        // any cursor activity wakes him up. the threshold is "input
        // happened in the last second", which is forgiving enough that
        // a single mouse twitch counts.
        if (world.msSinceLastInput < 1000) {
            return QStringLiteral("wake");
        }
        return {};
    }
};

// one-shot wake animation, then back to idle.
class WakeState : public State
{
public:
    QString name() const override { return QStringLiteral("wake"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        c.animator().setAnimation(QStringLiteral("wake"), /*reset*/true);
    }

    QString tick(int /*deltaMs*/, Creechr& c, const WorldContext& /*world*/) override
    {
        if (c.animator().finished()) {
            return QStringLiteral("idle");
        }
        return {};
    }
};

} // namespace

// === Creechr impl ===

Creechr::Creechr(const SpriteAtlas& atlas)
    : m_animator(atlas)
{
    m_states.registerState(std::make_unique<IdleState>());
    m_states.registerState(std::make_unique<WalkState>());
    m_states.registerState(std::make_unique<ApproachWallState>());
    m_states.registerState(std::make_unique<ClimbUpState>());
    m_states.registerState(std::make_unique<ClimbDownState>());
    m_states.registerState(std::make_unique<SleepState>());
    m_states.registerState(std::make_unique<WakeState>());
    m_states.registerState(std::make_unique<HeistApproachState>());
    m_states.registerState(std::make_unique<HeistGrabState>());
    m_states.registerState(std::make_unique<HeistCarryState>());
    m_states.registerState(std::make_unique<HeistStashState>());
    m_states.registerState(std::make_unique<HeistWaitState>());
    m_states.registerState(std::make_unique<HeistReturnState>());
}

void Creechr::beginHeist(HeistTarget t)
{
    m_heist.emplace();
    m_heist->target = std::move(t);
}

void Creechr::clearHeist()
{
    m_heist.reset();
}

void Creechr::initialize(const WorldContext& world)
{
    // park him on the bottom of the primary screen and start in idle
    m_floorY = world.virtualDesktop.bottom() - kSpriteHeight;
    m_position = QPointF(world.virtualDesktop.left() + 200,
                         static_cast<double>(m_floorY));
    m_states.changeTo(QStringLiteral("idle"), *this, world);
    LOG_INFO(QStringLiteral("creechr initialized at %1,%2")
        .arg(m_position.x()).arg(m_position.y()));
}

void Creechr::tickLogic(int deltaMs, const WorldContext& world)
{
    if (world.fullscreenActive) {
        return; // freeze during fullscreen apps. spec §4.6
    }
    m_states.tick(deltaMs, *this, world);
}

void Creechr::tickRender(int deltaMs)
{
    m_animator.tick(deltaMs);
}

QRect Creechr::drawRect() const
{
    return QRect(static_cast<int>(m_position.x()),
                 static_cast<int>(m_position.y()),
                 m_animator.frameWidth(),
                 m_animator.frameHeight());
}

QRect Creechr::frameSrcRect() const
{
    return m_animator.currentFrameRect();
}

QPoint Creechr::carryAnchorScreen() const
{
    // matches the carry/grab pose drawn in sprite_atlas.cpp:
    //   shoulders at cell-y = (4 + 13) = 17
    //   carry hand offset = ( ±8, 3 )  →  cell-y of hands ≈ 20
    //   shoulder x: left=9, right=38 (cell-local)
    // when facing right: hands extend out the right side, around cell-x=46
    // when facing left:  hands extend out the left  side, around cell-x= 1
    const int handY = 20;
    const int handX = m_facingRight ? 46 : 2;
    return QPoint(static_cast<int>(m_position.x()) + handX,
                  static_cast<int>(m_position.y()) + handY);
}

} // namespace cr
