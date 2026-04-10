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
#include <QGuiApplication>
#include <QRandomGenerator>
#include <QRect>
#include <QScreen>
#include <QtGlobal>
#include <memory>

#ifdef _WIN32
#  include <windows.h>
#  include <dwmapi.h>
#endif

namespace cr {

// === states ===

namespace {

// is creechr currently standing on a real platform? "platform" means
// either the screen floor OR a window whose top is roughly at his
// foot y AND whose x-range contains him. when standing on a window,
// the actual current top is fed back via outNewFloorY so the caller
// can RIDE the window if it shifted a few pixels (window manager snap,
// user dragging the title bar, etc).
//
// returns false when the platform vanished out from under him —
// caller should transition to flung. this is the gravity-from-loss-
// of-support path.
bool hasPlatformUnder(const Creechr& c, const WorldContext& world, int* outNewFloorY = nullptr)
{
    const int floorY = c.floorY();
    if (floorY >= world.virtualDesktop.bottom() - (kSpriteHeight + 1)) {
        if (outNewFloorY) *outNewFloorY = floorY;
        return true;
    }
    const int feetY = floorY + kSpriteHeight;
    const int x = static_cast<int>(c.position().x());
    for (const QRect& w : world.windowRects) {
        if (qAbs(w.top() - feetY) <= 8 && w.left() <= x && x <= w.right()) {
            if (outNewFloorY) *outNewFloorY = w.top() - kSpriteHeight;
            return true;
        }
    }
    return false;
}

// pause-the-walk-for-a-bit. 1-3s of standing around. while standing,
// fire random one-shot personality behaviors (blink, yawn, scratch)
// on a 1.5-3.5s timer so he doesnt look like a frozen corpse.
class IdleState : public State
{
public:
    QString name() const override { return QStringLiteral("idle"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        c.setVelocity({ 0, 0 });
        c.animator().setAnimation(QStringLiteral("idle"));
        m_remaining = 1200 + QRandomGenerator::global()->bounded(2400);
        m_subMs = 0;
        m_subThreshold = randomSubThreshold();
        m_inBehavior = false;
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
    {
        // platform check first — if the window he was standing on
        // closed or moved out from under him, fall.
        int rideY = 0;
        if (!hasPlatformUnder(c, world, &rideY)) {
            c.setVelocity({ 0, 0 });
            return QStringLiteral("flung");
        }
        if (rideY != c.floorY()) {
            c.setFloorY(rideY);
            QPointF p = c.position();
            p.setY(rideY);
            c.setPosition(p);
        }

        // pending heist takes priority over EVERYTHING else, including
        // sleep. otherwise the orchestrator can queue a heist and
        // creechr will just nap on top of it.
        if (c.heist() && !c.heist()->grabbed) {
            return QStringLiteral("heist_approach");
        }
        if (world.msSinceLastInput > 30000) {
            return QStringLiteral("sleep");
        }

        // personality micro-behaviors. if we're playing a one-shot anim,
        // wait for it to finish then go back to the regular idle pose.
        // otherwise count up to the next behavior trigger.
        if (m_inBehavior) {
            if (c.animator().finished()) {
                c.animator().setAnimation(QStringLiteral("idle"));
                m_inBehavior = false;
                m_subMs = 0;
                m_subThreshold = randomSubThreshold();
            }
        } else {
            m_subMs += deltaMs;
            if (m_subMs >= m_subThreshold) {
                fireMicroBehavior(c);
            }
        }

        m_remaining -= deltaMs;
        if (m_remaining > 0) {
            return {};
        }
        return QStringLiteral("walk");
    }

private:
    static int randomSubThreshold()
    {
        return 1500 + QRandomGenerator::global()->bounded(2000);
    }

    void fireMicroBehavior(Creechr& c)
    {
        const int roll = QRandomGenerator::global()->bounded(10);
        if (roll < 4) {
            c.animator().setAnimation(QStringLiteral("blink"), /*reset*/true);
        } else if (roll < 7) {
            c.animator().setAnimation(QStringLiteral("scratch"), /*reset*/true);
        } else if (roll < 9) {
            c.animator().setAnimation(QStringLiteral("yawn"), /*reset*/true);
        } else {
            // 10% chance: just say something instead of animating.
            // doesnt set m_inBehavior because there's no anim to wait
            // for — return early.
            const QStringList lines = {
                QStringLiteral("ugh."),
                QStringLiteral("..."),
                QStringLiteral("hi"),
                QStringLiteral("bored"),
                QStringLiteral("feed me"),
                QStringLiteral("this place sucks"),
                QStringLiteral("hmm"),
                QStringLiteral("mine"),
                QStringLiteral("where am i"),
                QStringLiteral("what"),
            };
            c.speakRandom(lines, 1600);
            return;
        }
        m_inBehavior = true;
    }

    int m_remaining = 0;
    int m_subMs = 0;
    int m_subThreshold = 0;
    bool m_inBehavior = false;
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
        // platform check first. if the window he was walking on closed
        // or moved out from under him, drop into flung. if it moved a
        // few pixels, RIDE it.
        int rideY = 0;
        if (!hasPlatformUnder(c, world, &rideY)) {
            // damp horizontal velocity so he doesnt fly off in a sprint
            c.setVelocity({ c.velocity().x() * 0.5, 0 });
            return QStringLiteral("flung");
        }
        if (rideY != c.floorY()) {
            c.setFloorY(rideY);
            QPointF p = c.position();
            p.setY(rideY);
            c.setPosition(p);
        }

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
                // walked off the edge — go down. 40% chance he rappels
                // down dramatically instead of climbing the wall.
                pos.setX(qBound<qreal>(leftLimit, pos.x(), rightLimit));
                c.setPosition(pos);
                const int floorBottom = world.virtualDesktop.bottom() - kSpriteHeight;
                if (QRandomGenerator::global()->bounded(10) < 4) {
                    c.setRappelAnchor(static_cast<int>(pos.x()),
                                      world.virtualDesktop.bottom());
                    return QStringLiteral("rappel_descend");
                }
                c.setClimbTarget(static_cast<int>(pos.x()), floorBottom);
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
                // first: try to find a RAPPEL target — a window whose
                // x-range contains creechr's current x and whose top is
                // well above his current floor. if found, ~40% chance
                // we rappel up to it instead of doing the regular pick.
                static const bool kForceRappel = !qgetenv("CREECHR_RAPPEL_NOW").isEmpty();
                {
                    int bestIdx = -1;
                    int bestTop = INT_MAX;
                    for (int i = 0; i < world.windowRects.size(); ++i) {
                        const QRect& w = world.windowRects[i];
                        if (w.height() < 64 || w.width() < 64) continue;
                        if (pos.x() < w.left() || pos.x() > w.right()) continue;
                        if (w.top() >= c.floorY() - 80) continue; // not high enough to be interesting
                        if (w.top() < kSpriteHeight) continue;    // would clip his head off the top
                        if (w.top() < bestTop) {
                            bestTop = w.top();
                            bestIdx = i;
                        }
                    }
                    if (bestIdx >= 0
                        && (kForceRappel || QRandomGenerator::global()->bounded(10) < 4)) {
                        const QRect& w = world.windowRects[bestIdx];
                        c.setRappelAnchor(static_cast<int>(pos.x()), w.top());
                        LOG_DEBUG(QStringLiteral("walk: rappel target window %1 top=%2 (creechr y=%3)")
                            .arg(bestIdx).arg(w.top()).arg(c.floorY()));
                        return QStringLiteral("shoot_rappel");
                    }
                }
                // pick a random window. require some minimum size.
                const int idx = QRandomGenerator::global()->bounded(world.windowRects.size());
                const QRect& w = world.windowRects[idx];
                if (w.height() >= 64 && w.width() >= 64
                    && w.top() >= kSpriteHeight) {
                    // last clause: skip windows whose top is too high
                    // to fit creechr above them. otherwise his sprite
                    // ends up at y = w.top() - 48, which goes negative
                    // for maximized windows and qt clips his head off.
                    // long-standing readme issue, finally addressed.
                    const int leftDist  = qAbs(static_cast<int>(pos.x()) - w.left());
                    const int rightDist = qAbs(static_cast<int>(pos.x()) - w.right());
                    // 50% chance: gnaw on the nearer edge instead of
                    // climbing. gnaw doesn't steal anything — just chew
                    // on the window for a few seconds. user can shake
                    // creechr off by dragging the window vigorously.
                    // override: CREECHR_GNAW_NOW=1 forces every attempt
                    // to be a gnaw, for "show me it works" runs.
                    static const bool kForceGnaw = !qgetenv("CREECHR_GNAW_NOW").isEmpty();
                    const bool wantGnaw = kForceGnaw
                        || QRandomGenerator::global()->bounded(10) < 5;
                    if (wantGnaw) {
                        const int targetX = (leftDist <= rightDist)
                            ? w.left() : (w.right() - kSpriteWidth);
                        void* hwnd = (idx < world.windowHwnds.size())
                            ? world.windowHwnds[idx] : nullptr;
                        c.setGnawTarget(targetX, hwnd);
                        LOG_DEBUG(QStringLiteral("walk: gnaw target window %1 (%2x%3) at x=%4")
                            .arg(idx).arg(w.width()).arg(w.height()).arg(targetX));
                        return QStringLiteral("approach_gnaw");
                    }
                    // pick edge: 60% chance the FARTHER edge so he
                    // actually traverses the screen, 40% nearer.
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

// helper used by GnawState — get a window's current frame in qt logical
// pixels, accounting for the dwm shadow lie and primary-screen dpr.
// returns an empty rect if the window is gone.
namespace {
#ifdef _WIN32
QRect currentDwmFrame(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return {};
    RECT r{};
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r));
    if (FAILED(hr) && !GetWindowRect(hwnd, &r)) return {};
    // per-monitor dpi using GetDpiForWindow so mixed-dpi setups work.
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) {
        QScreen* primary = QGuiApplication::primaryScreen();
        dpi = primary ? static_cast<UINT>(primary->devicePixelRatio() * 96.0) : 96;
    }
    const qreal dpr = dpi / 96.0;
    return QRect(
        QPoint(static_cast<int>(r.left  / dpr), static_cast<int>(r.top    / dpr)),
        QPoint(static_cast<int>(r.right / dpr - 1), static_cast<int>(r.bottom / dpr - 1))
    );
}
#endif
} // namespace

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

// === rappel states ===
//
// shoot_rappel: brief windup. plays the grab anim (arms reach up) for
//   ~300ms, sets a rappel anchor on creechr at the target point, then
//   transitions into rappel_climb or rappel_descend depending on which
//   direction the anchor is.
//
// rappel_climb: vertical climb up the line at 200 px/sec. anim is
//   climb_up (arms scissoring upward). when feet reach the anchor's y,
//   he's on top of the target window. clear rappel, set new floorY,
//   transition to walk.
//
// rappel_descend: vertical descent at 240 px/sec. anim is climb_down.
//   when he hits the floor (or another platform), clear rappel,
//   transition to walk.
//
// the visible rope is drawn by OverlayWindow, which checks
// creechr.rappelActive() and reads creechr.rappelAnchorX/Y.

class ShootRappelState : public State
{
public:
    QString name() const override { return QStringLiteral("shoot_rappel"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        c.setVelocity({ 0, 0 });
        c.animator().setAnimation(QStringLiteral("grab"), /*reset*/true);
        c.speakRandom({
            QStringLiteral("up i go"),
            QStringLiteral("weee"),
            QStringLiteral("here we go"),
            QStringLiteral("tally ho"),
        }, 1400);
        m_phaseMs = 0;
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext&) override
    {
        m_phaseMs += deltaMs;
        if (m_phaseMs > 350) {
            // launch puff at the anchor — visual for "the hook bit in"
            c.spawnPuff(QPointF(c.rappelAnchorX(), c.rappelAnchorY()),
                        4, QColor(80, 80, 80, 220), 350);
            // direction: anchor above us = climb, anchor below = descend
            if (c.rappelAnchorY() < c.position().y()) {
                return QStringLiteral("rappel_climb");
            } else {
                return QStringLiteral("rappel_descend");
            }
        }
        return {};
    }

private:
    int m_phaseMs = 0;
};

class RappelClimbState : public State
{
public:
    QString name() const override { return QStringLiteral("rappel_climb"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        c.setVelocity({ 0, -200 });
        c.animator().setAnimation(QStringLiteral("climb_up"));
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext&) override
    {
        const double dt = deltaMs / 1000.0;
        QPointF pos = c.position() + c.velocity() * dt;
        // creechr's "feet" land at anchorY - kSpriteHeight (so his
        // foot y = anchorY which is the platform top)
        const int targetTopLeftY = c.rappelAnchorY() - kSpriteHeight;
        if (pos.y() <= targetTopLeftY) {
            pos.setY(targetTopLeftY);
            c.setPosition(pos);
            c.setFloorY(targetTopLeftY);
            c.clearRappelAnchor();
            return QStringLiteral("walk");
        }
        c.setPosition(pos);
        return {};
    }
};

class RappelDescendState : public State
{
public:
    QString name() const override { return QStringLiteral("rappel_descend"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        c.setVelocity({ 0, 240 });
        c.animator().setAnimation(QStringLiteral("climb_down"));
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext&) override
    {
        const double dt = deltaMs / 1000.0;
        QPointF pos = c.position() + c.velocity() * dt;
        const int targetTopLeftY = c.rappelAnchorY() - kSpriteHeight;
        if (pos.y() >= targetTopLeftY) {
            pos.setY(targetTopLeftY);
            c.setPosition(pos);
            c.setFloorY(targetTopLeftY);
            c.clearRappelAnchor();
            return QStringLiteral("walk");
        }
        c.setPosition(pos);
        return {};
    }
};

// === gnaw / flung states ===
//
// gnaw: creechr walks up to a window edge and chews on it for a while
// without stealing anything. while gnawing he's "attached" — every tick
// his position snaps to the window's current top-left + the offset he
// had when he latched on. so dragging the window drags him too. shake
// the window fast enough and he gets flung off.
//
// flung: physics. gravity, bounce off the floor, friction, eventually
// settles back to walking. used by the shake-off mechanic and (TODO)
// any other "creechr is suddenly in space" path.

namespace gnaw_constants {
constexpr int  kGnawDurationMs       = 6000;     // bite on the window for 6s
constexpr int  kShakeWindowMs        = 800;      // sliding window for shake detection
constexpr int  kShakeMinSignChanges  = 4;        // need this many flips in kShakeWindowMs
constexpr int  kShakeMinVel          = 6;        // px/tick to count as a "real" move
constexpr double kGravityPxPerSec2   = 1500.0;
constexpr double kBounceRestitution  = 0.42;
constexpr double kBounceFriction     = 0.62;
constexpr int  kSettledFramesMin     = 30;
} // namespace gnaw_constants

class ApproachGnawState : public State
{
public:
    QString name() const override { return QStringLiteral("approach_gnaw"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        m_giveUpMs = 0;
        const int targetX = c.gnawTargetX();
        const bool right = targetX > c.position().x();
        c.setFacingRight(right);
        c.setVelocity({ right ? 100.0 : -100.0, 0.0 });
        c.animator().setAnimation(right ? QStringLiteral("walk_right")
                                        : QStringLiteral("walk_left"));
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext& /*world*/) override
    {
        m_giveUpMs += deltaMs;
        // gnaw is low-stakes and doesnt steal anything, so unlike heist
        // states it does NOT abort on user input. only timeout aborts.
        if (m_giveUpMs > 8000) {
            c.clearGnawTarget();
            return QStringLiteral("walk");
        }
        QPointF pos = c.position() + c.velocity() * (deltaMs / 1000.0);
        c.setPosition(pos);
        if (qAbs(static_cast<int>(pos.x()) - c.gnawTargetX()) <= 4) {
            c.setPosition({ static_cast<qreal>(c.gnawTargetX()), pos.y() });
            return QStringLiteral("gnaw");
        }
        return {};
    }

private:
    int m_giveUpMs = 0;
};

class GnawState : public State
{
public:
    QString name() const override { return QStringLiteral("gnaw"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        c.setVelocity({ 0, 0 });
        c.animator().setAnimation(QStringLiteral("bite"), /*reset*/true);
        c.speakRandom({
            QStringLiteral("om nom"),
            QStringLiteral("crunch"),
            QStringLiteral("delicious"),
            QStringLiteral("this is mine now"),
            QStringLiteral("nom nom"),
            QStringLiteral("hngh"),
        }, 1500);
        m_durationMs = 0;
        m_signChanges = 0;
        m_shakeWindowMs = 0;
        m_lastVxSign = 0;
        m_lastWindowTopLeft = QPoint();
        m_attachOffset = QPoint();
        m_initialized = false;

#ifdef _WIN32
        HWND hwnd = static_cast<HWND>(c.gnawHwnd());
        if (!hwnd || !IsWindow(hwnd)) {
            // window died between approach and gnaw — fall instead
            m_initialized = false;
            return;
        }
        const QRect frame = currentDwmFrame(hwnd);
        if (frame.isEmpty()) return;
        m_lastWindowTopLeft = frame.topLeft();
        m_attachOffset = QPoint(static_cast<int>(c.position().x()) - frame.left(),
                                static_cast<int>(c.position().y()) - frame.top());
        m_initialized = true;
        LOG_INFO(QStringLiteral("gnaw: latched onto window at (%1,%2) offset (%3,%4)")
            .arg(frame.left()).arg(frame.top())
            .arg(m_attachOffset.x()).arg(m_attachOffset.y()));
#endif
    }

    void exit(Creechr& c, const WorldContext&) override
    {
        c.clearGnawTarget();
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext&) override
    {
#ifdef _WIN32
        HWND hwnd = static_cast<HWND>(c.gnawHwnd());
        if (!m_initialized || !hwnd || !IsWindow(hwnd)) {
            // window vanished — drop into the void
            c.setVelocity({ 0, 80 });
            return QStringLiteral("flung");
        }
        const QRect frame = currentDwmFrame(hwnd);
        if (frame.isEmpty()) {
            c.setVelocity({ 0, 80 });
            return QStringLiteral("flung");
        }

        // shake detection: track sign of x velocity per tick. count flips
        // in a sliding ~800ms window. if there have been kShakeMinSignChanges
        // direction reversals with non-trivial magnitude, fling.
        const int dx = frame.left() - m_lastWindowTopLeft.x();
        const int dy = frame.top()  - m_lastWindowTopLeft.y();
        m_lastWindowTopLeft = frame.topLeft();

        const int curSign = (qAbs(dx) >= gnaw_constants::kShakeMinVel)
            ? (dx > 0 ? 1 : -1)
            : 0;
        if (curSign != 0 && m_lastVxSign != 0 && curSign != m_lastVxSign) {
            m_signChanges++;
        }
        if (curSign != 0) m_lastVxSign = curSign;

        m_shakeWindowMs += deltaMs;
        if (m_shakeWindowMs > gnaw_constants::kShakeWindowMs) {
            m_shakeWindowMs = 0;
            m_signChanges = 0;
        }
        if (m_signChanges >= gnaw_constants::kShakeMinSignChanges) {
            // flung. give him velocity in the current shake direction
            // plus an upward kick proportional to the shake intensity.
            const double impulseX = dx * 18.0;
            const double impulseY = -260.0 - qAbs(dy) * 6.0;
            c.setVelocity({ impulseX, impulseY });
            LOG_INFO(QStringLiteral("gnaw: SHAKEN OFF, fling vel (%1, %2)")
                .arg(impulseX).arg(impulseY));
            return QStringLiteral("flung");
        }

        // follow the window
        c.setPosition({ static_cast<qreal>(frame.left() + m_attachOffset.x()),
                        static_cast<qreal>(frame.top()  + m_attachOffset.y()) });

        m_durationMs += deltaMs;
        if (m_durationMs >= gnaw_constants::kGnawDurationMs) {
            // done gnawing, walk away
            c.setFloorY(static_cast<int>(c.position().y()));
            return QStringLiteral("walk");
        }
#else
        Q_UNUSED(deltaMs);
        return QStringLiteral("walk");
#endif
        return {};
    }

private:
    bool m_initialized = false;
    QPoint m_attachOffset;
    QPoint m_lastWindowTopLeft;
    int m_durationMs = 0;
    int m_signChanges = 0;
    int m_shakeWindowMs = 0;
    int m_lastVxSign = 0;
};

class FlungState : public State
{
public:
    QString name() const override { return QStringLiteral("flung"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        // velocity was set by whoever flung him. set animation to hang
        // (arms up) which reads as flailing.
        c.animator().setAnimation(QStringLiteral("hang"));
        c.speakRandom({
            QStringLiteral("OW"),
            QStringLiteral("DICK"),
            QStringLiteral("aaaa"),
            QStringLiteral("fuck"),
            QStringLiteral("HEY"),
            QStringLiteral("rude"),
            QStringLiteral("WHY"),
        }, 2200);
        m_settledFrames = 0;
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
    {
        const double dt = deltaMs / 1000.0;
        QPointF vel = c.velocity();
        QPointF pos = c.position();

        vel.setY(vel.y() + gnaw_constants::kGravityPxPerSec2 * dt);
        pos += vel * dt;

        const int floorY = world.virtualDesktop.bottom() - kSpriteHeight;
        const int leftEdge  = world.virtualDesktop.left();
        const int rightEdge = world.virtualDesktop.right() - kSpriteWidth;

        // floor bounce
        if (pos.y() >= floorY) {
            const bool wasFalling = vel.y() > 60;
            pos.setY(floorY);
            if (vel.y() > 0) {
                vel.setY(-vel.y() * gnaw_constants::kBounceRestitution);
                vel.setX( vel.x() * gnaw_constants::kBounceFriction);
            }
            // dust puff on hard landings
            if (wasFalling) {
                c.spawnPuff(QPointF(pos.x() + kSpriteWidth / 2.0,
                                    pos.y() + kSpriteHeight),
                            6, QColor(180, 170, 165, 220), 500);
            }
        }
        // wall bounce (less interesting but stops him going off screen)
        if (pos.x() < leftEdge) {
            pos.setX(leftEdge);
            vel.setX(-vel.x() * gnaw_constants::kBounceRestitution);
        } else if (pos.x() > rightEdge) {
            pos.setX(rightEdge);
            vel.setX(-vel.x() * gnaw_constants::kBounceRestitution);
        }

        c.setPosition(pos);
        c.setVelocity(vel);

        const bool onFloor = (pos.y() >= floorY - 1);
        const bool slow = (qAbs(vel.x()) < 30.0 && qAbs(vel.y()) < 50.0);
        if (onFloor && slow) {
            m_settledFrames++;
            if (m_settledFrames >= gnaw_constants::kSettledFramesMin) {
                // recompose
                c.setVelocity({ 0, 0 });
                c.setFloorY(floorY);
                return QStringLiteral("idle");
            }
        } else {
            m_settledFrames = 0;
        }
        return {};
    }

private:
    int m_settledFrames = 0;
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
        c.speakRandom({
            QStringLiteral("yoink"),
            QStringLiteral("MINE"),
            QStringLiteral("got it"),
            QStringLiteral("ha"),
            QStringLiteral("gotcha"),
            QStringLiteral("haha"),
        }, 1400);

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
            h->carriedPixmap = capture::fitForCarry(pm);
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
            h->carriedPixmap = capture::fitForCarry(pm);
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
            // shrink to creechr's hand size BEFORE storing. captureWindow
            // returns the full window pixmap (often 800px+) which made
            // the carried bitmap loom over the entire desktop. that was
            // funny but not the look we're going for. fitForCarry caps
            // at 64x48 preserving aspect ratio.
            h->carriedPixmap = capture::fitForCarry(pm);
            m_capturedOk = true;
            LOG_INFO(QStringLiteral("heist: pre-captured %1 (orig %2x%3, carry %4x%5)")
                .arg(h->target.label).arg(pm.width()).arg(pm.height())
                .arg(h->carriedPixmap.width()).arg(h->carriedPixmap.height()));
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
            // small splash of body-color particles at the carry anchor
            // — visual feedback for "the bite landed"
            c.spawnPuff(QPointF(c.carryAnchorScreen()), 5, QColor(220, 50, 140, 230), 400);
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

// HeistWait used to just sit there for 30-90s playing the idle anim,
// which made it look like creechr forgot what he was doing. now he
// actively GUARDS the stash with the same micro-behavior loop IdleState
// uses, but with biting (gnaw at the loot) and gloating speech mixed
// in. dragon-on-his-hoard vibe.
class HeistWaitState : public State
{
public:
    QString name() const override { return QStringLiteral("heist_wait"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        c.setVelocity({ 0, 0 });
        c.animator().setAnimation(QStringLiteral("idle"));
        c.speakRandom({
            QStringLiteral("all mine"),
            QStringLiteral("yes"),
            QStringLiteral("good"),
            QStringLiteral("yesss"),
            QStringLiteral("delicious"),
        }, 1700);
        m_subMs = 0;
        m_subThreshold = randomThreshold();
        m_inBehavior = false;
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext&) override
    {
        HeistContext* h = c.heist();
        if (!h) return QStringLiteral("idle");
        if (QDateTime::currentMSecsSinceEpoch() >= h->returnAtMs) {
            return QStringLiteral("heist_return");
        }

        // micro-behavior loop: same shape as IdleState's
        if (m_inBehavior) {
            if (c.animator().finished()) {
                c.animator().setAnimation(QStringLiteral("idle"));
                m_inBehavior = false;
                m_subMs = 0;
                m_subThreshold = randomThreshold();
            }
        } else {
            m_subMs += deltaMs;
            if (m_subMs >= m_subThreshold) {
                fireGuardBehavior(c);
            }
        }
        return {};
    }

private:
    static int randomThreshold()
    {
        return 1300 + QRandomGenerator::global()->bounded(2200);
    }

    void fireGuardBehavior(Creechr& c)
    {
        const int roll = QRandomGenerator::global()->bounded(10);
        if (roll < 4) {
            // bite at the loot — most common, fits the vibe
            c.animator().setAnimation(QStringLiteral("bite"), /*reset*/true);
            m_inBehavior = true;
        } else if (roll < 6) {
            c.animator().setAnimation(QStringLiteral("blink"), /*reset*/true);
            m_inBehavior = true;
        } else if (roll < 8) {
            // look around: flip facing direction
            c.setFacingRight(!c.facingRight());
            m_subMs = 0;
            m_subThreshold = randomThreshold();
        } else {
            // gloat
            c.speakRandom({
                QStringLiteral("mine"),
                QStringLiteral("ha"),
                QStringLiteral("yes"),
                QStringLiteral("hehe"),
                QStringLiteral("nom"),
                QStringLiteral("all mine"),
                QStringLiteral("hngh"),
            }, 1500);
            m_subMs = 0;
            m_subThreshold = randomThreshold();
        }
    }

    int m_subMs = 0;
    int m_subThreshold = 0;
    bool m_inBehavior = false;
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

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
    {
        HeistContext* h = c.heist();
        if (!h) return QStringLiteral("idle");
        QPointF pos = c.position() + c.velocity() * (deltaMs / 1000.0);
        c.setPosition(pos);

        const int targetX = h->originalFrame.left();
        if (qAbs(static_cast<int>(pos.x()) - targetX) <= 6) {
            // arrived. restore via hoard, then drop a trophy in the
            // nest as a permanent (per-session) cosmetic marker.
            if (auto* hoard = c.hoard(); hoard && !h->hoardId.isEmpty()) {
                hoard->restoreById(h->hoardId);
            }
            c.addTrophy(h->carriedPixmap, world.virtualDesktop);
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

    QString tick(int /*deltaMs*/, Creechr& c, const WorldContext& world) override
    {
        // pending heist wakes him up too — orchestrator can queue a
        // heist while creechr is napping and we want him to act on it.
        if (c.heist() && !c.heist()->grabbed) {
            return QStringLiteral("wake");
        }
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
    m_states.registerState(std::make_unique<ApproachGnawState>());
    m_states.registerState(std::make_unique<GnawState>());
    m_states.registerState(std::make_unique<FlungState>());
    m_states.registerState(std::make_unique<ShootRappelState>());
    m_states.registerState(std::make_unique<RappelClimbState>());
    m_states.registerState(std::make_unique<RappelDescendState>());
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
    tickParticles(deltaMs);
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

void Creechr::speak(const QString& text, int durationMs)
{
    m_speechText = text;
    m_speechExpiryMs = QDateTime::currentMSecsSinceEpoch() + durationMs;
}

void Creechr::speakRandom(const QStringList& options, int durationMs)
{
    if (options.isEmpty()) return;
    const int idx = QRandomGenerator::global()->bounded(options.size());
    speak(options[idx], durationMs);
}

QString Creechr::currentSpeech() const
{
    if (m_speechText.isEmpty()) return {};
    if (QDateTime::currentMSecsSinceEpoch() >= m_speechExpiryMs) return {};
    return m_speechText;
}

void Creechr::spawnPuff(QPointF where, int count, QColor color, int lifetimeMs)
{
    auto* rng = QRandomGenerator::global();
    for (int i = 0; i < count; ++i) {
        Particle pt;
        pt.pos = where;
        // random outward velocity in a half-disk pointing roughly upward
        const double angle = (rng->bounded(140) - 70) * 3.14159 / 180.0; // -70..+70 deg from up
        const double speed = 40.0 + rng->bounded(60);
        pt.vel = QPointF(std::sin(angle) * speed, -std::cos(angle) * speed);
        pt.lifetimeMs = lifetimeMs - 100 + rng->bounded(200);
        pt.color = color;
        m_particles.push_back(pt);
        if (m_particles.size() > 256) {
            // hard cap so a runaway state doesnt accumulate forever
            m_particles.removeFirst();
        }
    }
}

void Creechr::addTrophy(const QPixmap& pm, const QRect& virtualDesktop)
{
    if (pm.isNull()) return;
    constexpr int kMaxTrophies = 8;
    constexpr int kNestW = 140;
    constexpr int kNestH = 90;
    auto* rng = QRandomGenerator::global();
    // nest is anchored to the bottom-right corner of the desktop with
    // a small inset so it doesnt collide with the taskbar. random
    // scatter within the nest area to make the pile look messy.
    const int nestX = virtualDesktop.right() - kNestW - 16;
    const int nestY = virtualDesktop.bottom() - kNestH - 8;
    Trophy t;
    t.pixmap = pm;
    t.nestPos = QPoint(nestX + rng->bounded(kNestW - pm.width()),
                       nestY + rng->bounded(kNestH - pm.height()));
    m_trophies.push_back(std::move(t));
    if (m_trophies.size() > kMaxTrophies) {
        m_trophies.removeFirst();
    }
}

void Creechr::tickParticles(int deltaMs)
{
    constexpr double kParticleGravity = 380.0;
    const double dt = deltaMs / 1000.0;
    for (int i = m_particles.size() - 1; i >= 0; --i) {
        Particle& pt = m_particles[i];
        pt.ageMs += deltaMs;
        if (pt.ageMs >= pt.lifetimeMs) {
            m_particles.removeAt(i);
            continue;
        }
        pt.vel.setY(pt.vel.y() + kParticleGravity * dt);
        pt.pos += pt.vel * dt;
    }
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
