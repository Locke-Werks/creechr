#include "creature/creechr.h"
#include "creature/world_context.h"
#include "heist/bitmap_capture.h"
#include "heist/hoard.h"
#include "util/crash_guard.h"
#include "render/sprite_atlas.h"
#include "targets/extension_target_provider.h"
#include "util/app_snark.h"
#include "util/logging.h"
#include "util/win32_helpers.h"

#include <QCursor>
#include <QDateTime>
#include <QGuiApplication>
#include <QRandomGenerator>
#include <QRect>
#include <QScreen>
#include <QtGlobal>
#include <cmath>
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
        // flee from scary admin window — caller already set velocity
        if (c.wantsFlee()) {
            c.consumeFlee();
            return QStringLiteral("flung");
        }
        // platform check — if the window he was standing on closed
        // or moved out from under him, fall.
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
        // the idle-timeout swing. otherwise the orchestrator can queue
        // a heist and creechr will just start swinging on top of it.
        if (c.heist() && !c.heist()->grabbed) {
            return QStringLiteral("heist_approach");
        }
        // idle-timeout swing: user has stepped away. only trigger when
        // we're NOT in the middle of a micro-behavior anim, so the
        // current scratch/yawn/blink finishes first. suppressed while
        // the user is busy: "idle at the machine" during a call means
        // they're presenting, and swinging from their pointer on a
        // shared screen is a firing offense.
        if (world.msSinceLastInput > 30000 && !m_inBehavior && !world.userBusy) {
            return QStringLiteral("cursor_swing");
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
                fireMicroBehavior(c, world.userBusy);
                if (m_pouncing) {
                    m_pouncing = false;
                    return QStringLiteral("flung");
                }
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

    void fireMicroBehavior(Creechr& c, bool userBusy)
    {
        // 1-in-20 chance: POUNCE at the cursor like a cat. compute the
        // direction from creechr to cursor, set velocity in that
        // direction with an upward arc, transition to flung. flung's
        // physics handle the rest — gravity, bounce, settle. dramatic
        // and entirely new every time because the cursor moves.
        // suppressed mid-call: lunging at the presenters pointer is
        // not the vibe.
        if (!userBusy && QRandomGenerator::global()->bounded(20) == 0) {
            const QPoint cursor = QCursor::pos();
            const double dx = cursor.x() - (c.position().x() + 24);
            const double dy = cursor.y() - (c.position().y() + 24);
            const double mag = std::sqrt(dx * dx + dy * dy);
            if (mag > 1.0 && mag < 600.0) {
                const double speed = 220.0 + qMin(mag, 400.0) * 0.6;
                c.setFacingRight(dx >= 0);
                c.setVelocity({ (dx / mag) * speed, -180.0 + (dy / mag) * speed * 0.3 });
                c.speakRandom({
                    QStringLiteral("POUNCE"),
                    QStringLiteral("yoink"),
                    QStringLiteral("RAH"),
                    QStringLiteral("got u"),
                    QStringLiteral("HA"),
                }, 1500);
                m_pouncing = true; // ask the outer tick to transition
                return;
            }
        }

        const int roll = QRandomGenerator::global()->bounded(10);
        if (roll < 4) {
            c.animator().setAnimation(QStringLiteral("blink"), /*reset*/true);
        } else if (roll < 7) {
            c.animator().setAnimation(QStringLiteral("scratch"), /*reset*/true);
        } else if (roll < 9) {
            c.animator().setAnimation(QStringLiteral("yawn"), /*reset*/true);
        } else {
            // 10% chance: speak instead of animating. doesnt set
            // m_inBehavior because there's no anim to wait for.
            // PREFER an app-specific snark line about whatever's in
            // the foreground — that's where the personality lives.
            // fall back to generic if no specific lines exist.
            const QString snark = cr::currentAppSnark();
            if (!snark.isEmpty()) {
                c.speak(snark, 1900);
                return;
            }
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
    bool m_pouncing = false;
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
        // walk speed varies per session: 60% normal stroll, 25% slow
        // amble, 15% hurried scurry. makes the back-and-forth less
        // metronomic.
        auto* rng = QRandomGenerator::global();
        const int rollSpeed = rng->bounded(100);
        double speed = 60.0;
        if (rollSpeed < 25)      speed = 38.0;  // slow
        else if (rollSpeed < 85) speed = 60.0;  // normal
        else                     speed = 105.0; // fast
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
        // flee from scary admin window — caller already set velocity
        if (c.wantsFlee()) {
            c.consumeFlee();
            return QStringLiteral("flung");
        }
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

        // NOTE: walk does NOT check for idle-swing here anymore. user
        // wants creechr to finish whatever hes doing before shooting
        // his grapple at the cursor. the check only lives in IdleState
        // now, which is the natural rest point after walk/climb/etc
        // complete. worst case delay is one walk cycle (up to ~14s)
        // but the user isnt looking anyway during idle.
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
                // down dramatically instead of climbing the wall. the
                // rappel anchor is EXACTLY at the window's top edge at
                // his current x (where he'd plant a grappling hook).
                // the descend state then jumps him off with free-fall
                // + pendulum physics.
                pos.setX(qBound<qreal>(leftLimit, pos.x(), rightLimit));
                c.setPosition(pos);
                const int floorBottom = world.virtualDesktop.bottom() - kSpriteHeight;
                if (QRandomGenerator::global()->bounded(10) < 4) {
                    const int anchorX = static_cast<int>(pos.x()) + kSpriteWidth / 2;
                    const int anchorY = c.floorY() + kSpriteHeight;
                    c.setRappelAnchor(anchorX, anchorY);
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
                        // anchor offset: aim for one of the window's
                        // top corners rather than directly above creechr,
                        // so the climb has a diagonal rope and creechr
                        // visibly swings on the way up. random pick
                        // between left and right corner.
                        const int leftCorner  = w.left();
                        const int rightCorner = w.right();
                        const int anchorX = (QRandomGenerator::global()->bounded(2) == 0)
                            ? leftCorner : rightCorner;
                        c.setRappelAnchor(anchorX, w.top());
                        LOG_DEBUG(QStringLiteral("walk: rappel target window %1 top=%2 corner=%3 (creechr y=%4)")
                            .arg(bestIdx).arg(w.top()).arg(anchorX).arg(c.floorY()));
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

// === rappel pendulum helpers ===
//
// the rope acts as a constraint: creechr's "hand point" (top-center of
// his sprite, where the rope visually attaches) is at distance L from
// the anchor. theta is the angle from straight-down measured at the
// anchor. position from anchor is (L*sin(theta), L*cos(theta)).
//
// pendulum equation: theta'' = -(g/L) * sin(theta) - k * theta'
// where k is angular damping (energy loss per swing).
//
// for the descend case L is fixed and the swing dampens out.
// for the climb case L shrinks linearly (he's pulling himself up).
namespace rappel {
constexpr double kGravity        = 1500.0;
constexpr double kDescendDamping = 0.55;
constexpr double kClimbDamping   = 1.20;  // climbing tightens his grip
constexpr double kClimbSpeed     = 95.0;  // L shrinks by this per second
constexpr double kBounceLoss     = 0.45;  // wall bounce restitution
constexpr double kHandOffsetY    = 4.0;   // rope attaches above his head a little

QPointF handPoint(const Creechr& c)
{
    return QPointF(c.position().x() + kSpriteWidth / 2.0,
                   c.position().y() + kHandOffsetY);
}

void positionFromTheta(Creechr& c, double L, double theta)
{
    const QPointF anchor(c.rappelAnchorX(), c.rappelAnchorY());
    const double hx = anchor.x() + L * std::sin(theta);
    const double hy = anchor.y() + L * std::cos(theta);
    c.setPosition({ hx - kSpriteWidth / 2.0, hy - kHandOffsetY });
}

// is this window rect "the same" window the rope is anchored to?
// we don't track the source window directly — heuristic: any of its
// four corners is within 12 px of the anchor point.
bool isAnchorWindow(const QRect& w, int ax, int ay)
{
    // is the anchor AT / ON / INSIDE this window (with a small tolerance)?
    //
    // earlier versions only checked the four corners, which caught the
    // rappel-UP case (anchor lands on a window's corner) but MISSED the
    // rappel-DOWN case (anchor is along the middle of a window's top
    // edge where creechr walked off). missing that meant creechr's
    // bbox kept colliding with his own source window every tick, wall-
    // bounce lock-stepped him into place, and he just hung there.
    // expanded.contains(anchor) handles both cases.
    const int tol = 14;
    const QRect expanded(w.left() - tol, w.top() - tol,
                         w.width() + 2 * tol, w.height() + 2 * tol);
    return expanded.contains(QPoint(ax, ay));
}

// check if creechr's bbox at the proposed position intersects any
// window other than the rope's source/target window. used for swing
// collision detection in both rappel states.
bool wallCollision(const QRect& bbox, const WorldContext& world,
                   int anchorX, int anchorY)
{
    for (const QRect& w : world.windowRects) {
        if (w.isEmpty()) continue;
        if (isAnchorWindow(w, anchorX, anchorY)) continue;
        if (w.intersects(bbox)) return true;
    }
    return false;
}

} // namespace rappel

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
            QStringLiteral("hold tight"),
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

// rappel UP: anchored at the target window's corner (probably off to
// one side from creechr's current x). he hangs from the rope and
// pulls himself up — L shrinks linearly with time. while shrinking,
// pendulum physics make him swing. high damping so he doesnt do
// wild laps around the anchor.
class RappelClimbState : public State
{
public:
    QString name() const override { return QStringLiteral("rappel_climb"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        c.setVelocity({ 0, 0 });
        c.animator().setAnimation(QStringLiteral("climb_up"));
        const QPointF anchor(c.rappelAnchorX(), c.rappelAnchorY());
        const QPointF hand = rappel::handPoint(c);
        const double dx = hand.x() - anchor.x();
        const double dy = hand.y() - anchor.y();
        m_L = std::sqrt(dx * dx + dy * dy);
        if (m_L < 30.0) m_L = 30.0;
        m_theta = std::atan2(dx, dy); // angle from straight-down
        m_thetaVel = 0.0;
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
    {
        const double dt = deltaMs / 1000.0;

        // pull the rope in
        m_L -= rappel::kClimbSpeed * dt;
        if (m_L < 14.0) {
            // arrived at the anchor — snap to a position INSIDE the
            // target window. the anchor is at one of the window's top
            // corners; we need to find which window and offset creechr
            // so his bbox sits on top of the window rather than
            // straddling the corner (which would make hasPlatformUnder
            // reject the landing and immediately drop him). find the
            // window whose top matches the anchor y, figure out whether
            // the anchor is the LEFT or RIGHT corner, and offset
            // accordingly.
            const int ax = c.rappelAnchorX();
            const int ay = c.rappelAnchorY();
            int landX = ax - kSpriteWidth / 2; // fallback
            for (const QRect& w : world.windowRects) {
                if (qAbs(w.top() - ay) > 6) continue;
                if (qAbs(w.left() - ax) <= 8) {
                    // left corner — stand just inside the window's left edge
                    landX = w.left() + 4;
                    break;
                }
                if (qAbs(w.right() - ax) <= 8) {
                    // right corner — stand just inside the window's right edge
                    landX = w.right() - kSpriteWidth - 4;
                    break;
                }
            }
            const int feetY = ay - kSpriteHeight;
            c.setPosition({ static_cast<double>(landX),
                            static_cast<double>(feetY) });
            c.setFloorY(feetY);
            c.clearRappelAnchor();
            LOG_DEBUG(QStringLiteral("rappel_climb: landed at (%1,%2) (anchor was %3,%4)")
                .arg(landX).arg(feetY).arg(ax).arg(ay));
            return QStringLiteral("walk");
        }

        // pendulum integration
        const double aTheta = -(rappel::kGravity / m_L) * std::sin(m_theta)
                              - rappel::kClimbDamping * m_thetaVel;
        m_thetaVel += aTheta * dt;
        m_theta    += m_thetaVel * dt;
        // clamp angular velocity to prevent numerical explosion as L
        // shrinks (the system gets stiffer — (g/L) grows — and a naive
        // euler step can go unstable). cap at ~6 rad/sec which is
        // plenty for any visually-plausible swing.
        if (m_thetaVel > 6.0)  m_thetaVel = 6.0;
        if (m_thetaVel < -6.0) m_thetaVel = -6.0;

        // compute position. NO wall collision check — user explicitly
        // asked for rappel-up to pass through window edges. bouncing
        // off walls while climbing looked like the rope was a fuse
        // and then he teleported to the top and fell. phases through
        // now, looks like magic, works.
        rappel::positionFromTheta(c, m_L, m_theta);
        return {};
    }

private:
    double m_L = 100.0;
    double m_theta = 0.0;
    double m_thetaVel = 0.0;
};

// rappel DOWN: anchored where creechr was when he jumped off the edge
// of a window. two phases:
//   FreeFall: he leaps off the edge with some lateral velocity, falls
//     freely until the rope pulls taut at L_max
//   Pendulum: constrained pendulum swing. damps out over time. wall
//     collisions reflect angular velocity. exits when his feet reach
//     the screen floor.
class RappelDescendState : public State
{
public:
    QString name() const override { return QStringLiteral("rappel_descend"); }

    void enter(Creechr& c, const WorldContext& world) override
    {
        c.animator().setAnimation(QStringLiteral("hang"));
        c.speakRandom({
            QStringLiteral("weeeee"),
            QStringLiteral("down i go"),
            QStringLiteral("geronimo"),
            QStringLiteral("yeehaw"),
            QStringLiteral("LOOK OUT"),
            QStringLiteral("descending"),
        }, 1700);

        const int anchorX = c.rappelAnchorX();
        const int anchorY = c.rappelAnchorY();
        const int floorY  = world.virtualDesktop.bottom() - kSpriteHeight;
        // rope length: we want the bottom of the swing (theta=0) to
        // put creechr's sprite top-left at floorY.
        //   pos.y_bottom = anchor.y + L * cos(0) - kHandOffsetY
        //                = anchor.y + L - kHandOffsetY
        // solve for L: L = floorY - anchor.y + kHandOffsetY
        // (note the PLUS — earlier version had minus, which made the
        // rope 8 px too short and he never touched the floor.)
        m_L = static_cast<double>(qMax(80,
            floorY - anchorY + static_cast<int>(rappel::kHandOffsetY)));

        // jump off! initial position just beside the anchor (so his
        // hand is a few px below the anchor), lateral velocity in his
        // current facing direction.
        const double dirSign = c.facingRight() ? 1.0 : -1.0;
        c.setPosition({ static_cast<double>(anchorX - kSpriteWidth / 2)
                        + dirSign * 6.0,
                        static_cast<double>(anchorY - static_cast<int>(rappel::kHandOffsetY)) });
        c.setVelocity({ dirSign * 90.0, 40.0 });
        m_phase = Phase::FreeFall;
        m_thetaVel = 0.0;
        m_theta = 0.0;
        m_freeFallMs = 0;
        LOG_DEBUG(QStringLiteral("rappel_descend: anchor=(%1,%2) L=%3 floorY=%4 dir=%5")
            .arg(anchorX).arg(anchorY).arg(m_L).arg(floorY).arg(dirSign));
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
    {
        const double dt = deltaMs / 1000.0;
        const QPointF anchor(c.rappelAnchorX(), c.rappelAnchorY());

        if (m_phase == Phase::FreeFall) {
            m_freeFallMs += deltaMs;
            QPointF vel = c.velocity();
            QPointF pos = c.position() + vel * dt;
            vel.setY(vel.y() + rappel::kGravity * dt);
            c.setVelocity(vel);
            c.setPosition(pos);

            // emergency bailout: if free-fall lasts too long OR he's
            // already past the floor (rope too short, something wrong),
            // just drop him via the flung physics instead of the rope.
            const int floorY = world.virtualDesktop.bottom() - kSpriteHeight;
            if (m_freeFallMs > 2500 || pos.y() >= floorY) {
                LOG_WARN(QStringLiteral("rappel_descend: free-fall bailout after %1 ms, pos.y=%2 floorY=%3")
                    .arg(m_freeFallMs).arg(pos.y()).arg(floorY));
                c.clearRappelAnchor();
                return QStringLiteral("flung");
            }

            // is the rope taut yet?
            const QPointF hand = rappel::handPoint(c);
            const double dx = hand.x() - anchor.x();
            const double dy = hand.y() - anchor.y();
            const double dist = std::sqrt(dx * dx + dy * dy);
            if (dist >= m_L) {
                // snap onto the constraint surface and convert linear
                // velocity to angular
                m_theta = std::atan2(dx, dy);
                // tangent direction at angle theta is (cos, -sin)
                const double tangX =  std::cos(m_theta);
                const double tangY = -std::sin(m_theta);
                const double tangSpeed = vel.x() * tangX + vel.y() * tangY;
                m_thetaVel = tangSpeed / m_L;
                m_phase = Phase::Pendulum;
                rappel::positionFromTheta(c, m_L, m_theta);
                c.setVelocity({ 0, 0 });
                LOG_DEBUG(QStringLiteral("rappel_descend: pendulum engage theta=%1 thetaVel=%2")
                    .arg(m_theta).arg(m_thetaVel));
            }
        } else {
            // PENDULUM
            const double aTheta = -(rappel::kGravity / m_L) * std::sin(m_theta)
                                  - rappel::kDescendDamping * m_thetaVel;
            m_thetaVel += aTheta * dt;
            m_theta    += m_thetaVel * dt;

            const double hx = anchor.x() + m_L * std::sin(m_theta);
            const double hy = anchor.y() + m_L * std::cos(m_theta);
            const QRect bbox(static_cast<int>(hx - kSpriteWidth / 2.0),
                             static_cast<int>(hy - rappel::kHandOffsetY),
                             kSpriteWidth, kSpriteHeight);

            if (rappel::wallCollision(bbox, world,
                                      c.rappelAnchorX(), c.rappelAnchorY())) {
                m_thetaVel = -m_thetaVel * rappel::kBounceLoss;
                c.spawnPuff(QPointF(hx, hy), 4, QColor(180, 170, 165, 200), 320);
            } else {
                rappel::positionFromTheta(c, m_L, m_theta);
            }

            // landing condition: foot y reaches the screen floor.
            // happens at the bottom of the swing (theta near 0).
            const int floorY = world.virtualDesktop.bottom() - kSpriteHeight;
            if (c.position().y() >= floorY - 2) {
                c.setPosition({ c.position().x(), static_cast<double>(floorY) });
                c.setFloorY(floorY);
                c.setVelocity({ 0, 0 });
                c.clearRappelAnchor();
                return QStringLiteral("walk");
            }
        }
        return {};
    }

private:
    enum class Phase { FreeFall, Pendulum };
    Phase m_phase = Phase::FreeFall;
    double m_L = 100.0;
    double m_theta = 0.0;
    double m_thetaVel = 0.0;
    int m_freeFallMs = 0;
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
        // whoever launched him may have pre-set a better line (the
        // pounce's "RAH", the caught-red-handed excuse). only whine
        // generically if nobody had anything to say.
        if (c.currentSpeech().isEmpty()) {
            c.speakRandom({
                QStringLiteral("OW"),
                QStringLiteral("DICK"),
                QStringLiteral("aaaa"),
                QStringLiteral("fuck"),
                QStringLiteral("HEY"),
                QStringLiteral("rude"),
                QStringLiteral("WHY"),
            }, 2200);
        }
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
// happy path: heist_approach -> heist_grab -> heist_carry ->
//   heist_stash -> heist_wait -> heist_toss -> (item sinks, restore
//   fires at the bottom edge) -> idle
// abort path: carry aborts walk back via heist_return, which restores
//   inline at the origin and drops a trophy. cursor heists skip
//   approach (grapple reel) and skip return (the glide IS the return).
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
        // announce intent the moment the heist visibly starts. user
        // gets feedback that creechr has decided to commit a crime.
        c.speakRandom({
            QStringLiteral("ooh"),
            QStringLiteral("i want that one"),
            QStringLiteral("mine soon"),
            QStringLiteral("brb"),
            QStringLiteral("oh thats nice"),
            QStringLiteral("hehehe"),
            QStringLiteral("perfect"),
            QStringLiteral("dont mind me"),
            QStringLiteral("this looks important"),
            QStringLiteral("sneaky time"),
        }, 1700);
        const QRect& f = c.heist()->target.screenRect;
        if (c.heist()->target.kind == TargetKind::Cursor) {
            // cursor heists dont walk anywhere. the whole bit is that
            // he plants his feet, fires the grappling hook, and the
            // POINTER comes to HIM. HeistGrab owns the reel.
            m_grappleInstead = true;
            c.setVelocity({ 0, 0 });
            c.setFacingRight(f.center().x() > c.position().x());
            c.animator().setAnimation(QStringLiteral("idle"));
            return;
        }
        m_grappleInstead = false;
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
        if (m_grappleInstead) {
            return QStringLiteral("heist_grab");
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
    bool m_grappleInstead = false;
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
        // state objects live forever, so EVERY counter gets reset here
        // or the second heist inherits the first one's leftovers
        m_capturedOk = false;
        m_phase = Phase::Grabbing;
        m_reelHauling = false;
        m_reelMs = 0;
        m_biteMsLeft = 0;
        m_domAckWaitMs = 0;
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
            // grapple time. grab anim doubles as the hook toss, then
            // the Reeling phase hauls the pointer down the line into
            // his hands. no walking, no teleporting: the rope renders
            // off the rappel anchor, which we pin to the cursor and
            // drag inward every tick.
            m_phase = Phase::Reeling;
            m_reelHauling = false;
            m_reelMs = 0;
            const QPoint cur = QCursor::pos();
            c.setFacingRight(cur.x() > c.position().x());
            c.setRappelAnchor(cur.x(), cur.y());
        }
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
    {
        HeistContext* h = c.heist();
        if (!h) {
            // heist yanked out from under us (tray release fired while
            // we were mid-reel). dont leave a rope drawn to nowhere.
            c.clearRappelAnchor();
            return QStringLiteral("idle");
        }

        if (!m_capturedOk) {
            // capture failed in enter() — abort cleanly
            c.clearHeist();
            return QStringLiteral("idle");
        }

        if (m_phase == Phase::Reeling) {
            // real user input kills the bit instantly. our own
            // SetCursorPos calls dont count as input, so anything
            // fresh here is an actual hand on an actual mouse.
            if (world.msSinceLastInput < heist_helpers::kHeistInputAbortMs
                || cr::win32::anyMouseButtonDown()) {
                LOG_INFO(QStringLiteral("heist: user moved during cursor reel, dropping the line"));
                c.clearRappelAnchor();
                c.clearHeist();
                return QStringLiteral("idle");
            }
            m_reelMs += deltaMs;
            if (m_reelMs > 5000) {
                // hook is stuck on something. cut the line, go home.
                LOG_WARN(QStringLiteral("heist: cursor reel timed out"));
                c.clearRappelAnchor();
                c.clearHeist();
                return QStringLiteral("idle");
            }
            // let the hook-toss anim land before hauling
            if (!c.animator().finished() && !m_reelHauling) {
                return {};
            }
            if (!m_reelHauling) {
                m_reelHauling = true;
                // hand-over-hand. climb frames read as hauling a line
                // when he's standing still.
                c.animator().setAnimation(QStringLiteral("climb_up"));
                c.takeCursorCustody(h->originalFrame.center());
            }
#ifdef _WIN32
            const QPointF hand(c.position().x() + 24.0, c.position().y() + 2.0);
            const QPoint curNow = QCursor::pos();
            const QPointF delta = hand - QPointF(curNow);
            const double dist = std::hypot(delta.x(), delta.y());
            if (dist <= 22.0) {
                // pointer in hand. chomp, then off to the corner.
                c.clearRappelAnchor();
                h->grabbed = true;
                c.animator().setAnimation(QStringLiteral("bite"), /*reset*/true);
                c.spawnPuff(QPointF(c.carryAnchorScreen()), 5, QColor(220, 50, 140, 230), 400);
                m_phase = Phase::Biting;
                m_biteMsLeft = 480;
                return {};
            }
            const double step = qMin(dist, 1100.0 * (deltaMs / 1000.0));
            const QPointF next = QPointF(curNow) + delta * (step / dist);
            const QPoint nativeNext = cr::win32::logicalToNative(next);
            SetCursorPos(nativeNext.x(), nativeNext.y());
            // rope follows the pointer: the hook is ON the cursor
            c.setRappelAnchor(static_cast<int>(next.x()),
                              static_cast<int>(next.y()));
#else
            m_phase = Phase::Biting;
            m_biteMsLeft = 480;
#endif
            return {};
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
                    // from this exact moment until stash registers the
                    // hoard entry, a crash would strand a hidden window
                    // nobody knows about. the in-flight slot covers it.
                    crashguard::setInFlight(h->target.hwnd, h->originalFrame);
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
    enum class Phase { Grabbing, Reeling, Biting };
    Phase m_phase = Phase::Grabbing;
    bool m_capturedOk = false;
    bool m_reelHauling = false;
    int m_reelMs = 0;
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
            if (h->target.kind == TargetKind::Cursor) {
                // no walk-back for the pointer: clearHeist starts the
                // reel-home glide, and him strolling to the old cursor
                // spot dragging nothing looked like a bug
                c.clearHeist();
                return QStringLiteral("idle");
            }
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
                c.clearHeist();
                return QStringLiteral("idle");
            }
            // from the first SetCursorPos onward we owe the user their
            // pointer back. custody is settled in clearHeist() so every
            // exit path pays the debt, not just the polite ones.
            c.takeCursorCustody(h->originalFrame.center());
#ifdef _WIN32
            // SetCursorPos takes PHYSICAL pixels when the process is
            // dpi aware (we are, per-monitor v2); creechr math is
            // LOGICAL. logicalToNative does the per-screen conversion
            // (a blanket primary-dpr multiply is only correct on the
            // primary screen and diverges on mixed-dpi setups).
            int cx = qBound(world.virtualDesktop.left()  + 4,
                            static_cast<int>(pos.x()) + 16,
                            world.virtualDesktop.right() - 4);
            int cy = qBound(world.virtualDesktop.top()   + 4,
                            static_cast<int>(pos.y()) + 16,
                            world.virtualDesktop.bottom() - 4);
            const QPoint nativeDrag = cr::win32::logicalToNative(QPointF(cx, cy));
            SetCursorPos(nativeDrag.x(), nativeDrag.y());
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

    QString tick(int /*deltaMs*/, Creechr& c, const WorldContext& world) override
    {
        HeistContext* h = c.heist();
        if (!h) return QStringLiteral("idle");
        c.setVelocity({ 0, 0 });

        // cursor heist returns the cursor immediately at the stash spot.
        // it's a 1.2-1.8s gag, not a long wait.
        if (h->target.kind == TargetKind::Cursor) {
            h->returnAtMs = QDateTime::currentMSecsSinceEpoch();
        } else {
            // boredom timer: creechr guards his stash before he gets
            // bored and tosses it. window comes from settings via the
            // world context (gremlin mode shortens it; the caught-red-
            // handed check bounds the damage either way).
            h->returnAtMs = QDateTime::currentMSecsSinceEpoch()
                + world.stashWaitMinMs
                + QRandomGenerator::global()->bounded(qMax(1, world.stashWaitRangeMs));
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
            // v2 crash-insurance identity: if we die before this entry
            // gets restored, the next launch uses these to find the
            // window again. see Hoard::attemptOrphanRestore.
            e.originFrame = h->originalFrame;
#ifdef _WIN32
            if (h->target.kind == TargetKind::Window) {
                HWND idh = static_cast<HWND>(h->target.hwnd);
                if (idh && IsWindow(idh)) {
                    wchar_t cls[256] = {};
                    GetClassNameW(idh, cls, 256);
                    e.className = QString::fromWCharArray(cls);
                    wchar_t wtitle[512] = {};
                    GetWindowTextW(idh, wtitle, 512);
                    e.title = QString::fromWCharArray(wtitle);
                    DWORD wpid = 0;
                    GetWindowThreadProcessId(idh, &wpid);
                    e.pid = wpid;
                }
            }
#endif
            if (h->target.kind == TargetKind::DomElement) {
                e.opaqueId = h->target.opaqueId;
            }
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
            // the hoard's table owns this window now (add -> persist
            // -> syncSlots), so the in-flight slot has done its job
            crashguard::clearInFlight();
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

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
    {
        HeistContext* h = c.heist();
        if (!h) return QStringLiteral("idle");

        // the user came back. their window is currently HIDDEN and
        // every second it stays hidden reads as "this app broke my
        // desktop", not "haha funny gremlin". restore it on the spot,
        // act natural, get out of there. no trophy: he got caught.
        if (world.msSinceLastInput < heist_helpers::kHeistInputAbortMs) {
            LOG_INFO(QStringLiteral("heist: caught red-handed, restoring %1")
                .arg(h->target.label));
            if (auto* hoard = c.hoard(); hoard && !h->hoardId.isEmpty()) {
                hoard->restoreById(h->hoardId);
            }
            // puff where the loot was sitting so the vanish doesnt
            // read as a glitch
            c.spawnPuff(QPointF(h->stashedAt) + QPointF(24.0, 24.0),
                        6, QColor(255, 255, 255, 200), 500);
            c.speakRandom({
                QStringLiteral("you saw nothing"),
                QStringLiteral("it fell"),
                QStringLiteral("i found it like this"),
                QStringLiteral("was gonna give it back"),
                QStringLiteral("this isnt what it looks like"),
                QStringLiteral("we dont need to talk about this"),
            }, 2000);
            // startle hop away from the cursor, then flung handles the
            // landing. velocity picked to read as "jumped out of skin".
            const bool cursorIsRight = world.cursorPos.x() > c.position().x();
            c.setVelocity({ cursorIsRight ? -260.0 : 260.0, -220.0 });
            c.clearHeist();
            return QStringLiteral("flung");
        }

        if (QDateTime::currentMSecsSinceEpoch() >= h->returnAtMs) {
            // bored now, toss it and let it sink into the void
            return QStringLiteral("heist_toss");
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

// HeistToss: brief "throwing away" moment. creechr plays the grab
// animation (arms forward), speaks a bored line, moves the carried
// pixmap into the sinking-items list with a small upward pop, then
// clears the heist context so the orchestrator can queue the next
// one. the actual restore (for window heists) happens LATER, when
// the sinking item falls past the bottom of the screen.
class HeistTossState : public State
{
public:
    QString name() const override { return QStringLiteral("heist_toss"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        c.setVelocity({ 0, 0 });
        c.animator().setAnimation(QStringLiteral("grab"), /*reset*/true);
        c.speakRandom({
            QStringLiteral("bored"),
            QStringLiteral("eh"),
            QStringLiteral("nah"),
            QStringLiteral("im done with this"),
            QStringLiteral("meh"),
            QStringLiteral("toss"),
            QStringLiteral("whatever"),
            QStringLiteral("not mine anymore"),
            QStringLiteral("bye"),
        }, 1600);

        HeistContext* h = c.heist();
        if (!h) return;
        if (h->carriedPixmap.isNull()) return;

        // drop it from wherever it's currently sitting. the stash
        // position is where HeistCarry dropped it. small random
        // horizontal velocity for flavor, small upward pop so it
        // pauses before falling.
        auto* rng = QRandomGenerator::global();
        const double vx = (rng->bounded(40) - 20); // -20..20 px/sec
        const double vy = -30.0;                    // small upward pop
        c.addSinkingItem(h->carriedPixmap, h->stashedAt,
                         QPointF(vx, vy), h->hoardId);
        LOG_INFO(QStringLiteral("heist: tossed %1 (will sink + restore %2)")
            .arg(h->target.label)
            .arg(h->hoardId.isEmpty() ? QStringLiteral("no-op") : h->hoardId));
    }

    QString tick(int /*deltaMs*/, Creechr& c, const WorldContext&) override
    {
        // hold the toss pose until the grab anim finishes, then clear
        // the heist context and return to idle. the sinking item is
        // already in creechr's m_sinking list from enter().
        if (c.animator().finished()) {
            c.clearHeist();
            return QStringLiteral("idle");
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
        m_elapsedMs = 0;
        if (!c.heist()) return;
        const QPoint orig = c.heist()->originalFrame.topLeft();
        const bool right = orig.x() > c.position().x();
        c.setFacingRight(right);
        // still carrying the thing, so still using the carry walk anim
        c.animator().setAnimation(right ? QStringLiteral("carry_right")
                                        : QStringLiteral("carry_left"));
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
    {
        HeistContext* h = c.heist();
        if (!h) return QStringLiteral("idle");
        m_elapsedMs += deltaMs;

        const int targetX = h->originalFrame.left();
        const int dx = targetX - static_cast<int>(c.position().x());
        // direction is re-derived EVERY tick. a fat dt (laptop resume
        // hands us up to 500ms) can step clean past the 6px arrival
        // window, and a set-once velocity then walks him off the
        // desktop forever with the loot. recomputed direction makes
        // overshoot self-correct; the time cap covers everything else.
        const bool arrived = qAbs(dx) <= 6
            || m_elapsedMs > heist_helpers::kHeistMaxNonStashMs;
        if (!arrived) {
            const bool right = dx > 0;
            if (c.facingRight() != right) {
                c.setFacingRight(right);
                c.animator().setAnimation(right ? QStringLiteral("carry_right")
                                                : QStringLiteral("carry_left"));
            }
            c.setVelocity({ right ? 100.0 : -100.0, 0.0 });
            c.setPosition(c.position() + c.velocity() * (deltaMs / 1000.0));
            return {};
        }

        // arrived (or gave up trying): give it back RIGHT HERE, in
        // whatever phase the heist is in. this used to be a
        // restoreById guarded on hoardId, but heist_return is only
        // ever entered from the carry abort, which is pre-stash, so
        // hoardId was ALWAYS empty and the restore never fired: the
        // hidden window stayed hidden forever while he pocketed a
        // trophy for the job. inline restore covers every phase.
        c.restoreHeldLootInline();
        c.addTrophy(h->carriedPixmap, world.virtualDesktop);
        // cursor return happens inside clearHeist via cursor custody
        c.clearHeist();
        return QStringLiteral("idle");
    }

private:
    int m_elapsedMs = 0;
};

// CursorSwing: when the system has been idle long enough that creechr
// would normally sleep, he instead fires his grapple at the mouse
// cursor and swings from it like a pendulum.
//
// two phases:
//   Approach: plays "grab" animation (hook toss), then "climb_up"
//     while the rope shrinks from the initial creechr-to-cursor
//     distance down to the target swing length (120 px). pendulum
//     physics are active during approach so he swings slightly as
//     he pulls himself up.
//   Swing: L fixed at 120, pendulum with very low damping. self-
//     kicks every 2 seconds when the swing amplitude decays, and
//     speaks on each kick. the moment the user touches anything or
//     the cursor drifts, he releases with his current tangential
//     velocity and transitions to flung.
class CursorSwingState : public State
{
public:
    QString name() const override { return QStringLiteral("cursor_swing"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        const QPoint cursor = QCursor::pos();
        c.setRappelAnchor(cursor.x(), cursor.y());
        m_anchorStart = cursor;
        m_kickCooldown = 0;
        m_approachShotMs = 0;
        m_targetL = 120.0;

        // compute current distance from creechrs hand to the cursor
        const QPointF hand = rappel::handPoint(c);
        const double dx = hand.x() - cursor.x();
        const double dy = hand.y() - cursor.y();
        m_L = std::sqrt(dx * dx + dy * dy);
        m_theta = std::atan2(dx, dy);
        m_thetaVel = 0.0;

        if (m_L <= m_targetL + 20) {
            // already close enough — skip the approach and just swing
            if (m_L < 60.0) m_L = 60.0;
            m_phase = Phase::Swing;
            c.animator().setAnimation(QStringLiteral("hang"));
            m_thetaVel = 1.6;
            c.speakRandom({
                QStringLiteral("wheeee"),
                QStringLiteral("look at this"),
                QStringLiteral("swinging"),
                QStringLiteral("physics"),
                QStringLiteral("WHEEE"),
                QStringLiteral("hi cursor"),
            }, 1800);
        } else {
            // approach: shoot the grapple, then climb the rope
            m_phase = Phase::Approach;
            c.animator().setAnimation(QStringLiteral("grab"), /*reset*/true);
            c.speakRandom({
                QStringLiteral("grapple out"),
                QStringLiteral("hook ho"),
                QStringLiteral("gotcha cursor"),
                QStringLiteral("hold still"),
                QStringLiteral("aim"),
                QStringLiteral("steady"),
                QStringLiteral("INCOMING"),
            }, 1500);
        }
    }

    void exit(Creechr& c, const WorldContext&) override
    {
        c.clearRappelAnchor();
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
    {
        // exit if the user came back
        if (world.msSinceLastInput < 600) {
            releaseRope(c);
            return QStringLiteral("flung");
        }
        const QPoint cursor = QCursor::pos();
        const int dxCursor = cursor.x() - m_anchorStart.x();
        const int dyCursor = cursor.y() - m_anchorStart.y();
        if (dxCursor * dxCursor + dyCursor * dyCursor > 8 * 8) {
            releaseRope(c);
            return QStringLiteral("flung");
        }

        const double dt = deltaMs / 1000.0;

        if (m_phase == Phase::Approach) {
            // hold the hook-toss pose for the grab anim duration
            m_approachShotMs += deltaMs;
            if (m_approachShotMs < 380) {
                // pendulum runs but L doesnt shrink yet — creechr
                // dangles on a long rope briefly before pulling up
                integratePendulum(dt, /*damping*/0.25);
                rappel::positionFromTheta(c, m_L, m_theta);
                return {};
            }
            // hook has landed. switch to climb anim and start pulling
            // the rope in.
            if (c.animator().currentAnimation() != QLatin1String("climb_up")) {
                // pick climb_up vs climb_down based on whether the
                // anchor is above or below creechr's current position
                const bool anchorAbove = c.rappelAnchorY() < static_cast<int>(c.position().y());
                c.animator().setAnimation(anchorAbove
                    ? QStringLiteral("climb_up")
                    : QStringLiteral("climb_down"));
            }

            // shrink L toward the target swing length
            constexpr double kClimbSpeed = 110.0;
            m_L -= kClimbSpeed * dt;
            if (m_L <= m_targetL) {
                m_L = m_targetL;
                m_phase = Phase::Swing;
                c.animator().setAnimation(QStringLiteral("hang"));
                // preserve momentum from the approach pendulum if it
                // built up any; otherwise kick him going
                if (std::abs(m_thetaVel) < 0.4) {
                    m_thetaVel = (m_thetaVel < 0 ? -1.6 : 1.6);
                } else {
                    m_thetaVel *= 1.2;
                }
                c.speakRandom({
                    QStringLiteral("wheeee!"),
                    QStringLiteral("LOOK AT ME"),
                    QStringLiteral("swinging time"),
                    QStringLiteral("WHEEEE"),
                    QStringLiteral("physics!"),
                    QStringLiteral("im doing it"),
                }, 1600);
            } else {
                // continue integrating pendulum during the climb
                integratePendulum(dt, /*damping*/0.25);
            }
            rappel::positionFromTheta(c, m_L, m_theta);
            return {};
        }

        // === Swing phase ===
        integratePendulum(dt, /*damping*/0.12);

        // self-kick with speech when the swing amplitude dies out.
        // each kick announces itself so the user sees motion + a
        // bubble every ~2 seconds while the swing is going.
        m_kickCooldown -= deltaMs;
        if (m_kickCooldown <= 0
            && std::abs(m_thetaVel) < 0.35
            && std::abs(m_theta) < 0.20) {
            m_thetaVel = (QRandomGenerator::global()->bounded(2) == 0) ? 1.8 : -1.8;
            m_kickCooldown = 2000;
            c.speakRandom({
                QStringLiteral("more!"),
                QStringLiteral("again"),
                QStringLiteral("push!"),
                QStringLiteral("wheee!"),
                QStringLiteral("keep going"),
                QStringLiteral("harder"),
                QStringLiteral("one more"),
                QStringLiteral("hahaha"),
                QStringLiteral("MORE"),
                QStringLiteral("weeeee"),
                QStringLiteral("kick"),
                QStringLiteral("yes"),
            }, 1400);
        }

        rappel::positionFromTheta(c, m_L, m_theta);
        return {};
    }

private:
    enum class Phase { Approach, Swing };

    void integratePendulum(double dt, double damping)
    {
        const double aTheta = -(rappel::kGravity / m_L) * std::sin(m_theta)
                              - damping * m_thetaVel;
        m_thetaVel += aTheta * dt;
        m_theta    += m_thetaVel * dt;
        if (m_thetaVel > 8.0)  m_thetaVel = 8.0;
        if (m_thetaVel < -8.0) m_thetaVel = -8.0;
    }

    void releaseRope(Creechr& c)
    {
        // convert current angular velocity to linear so the flung
        // transition feels continuous — if he was swinging rightward
        // when the rope released, he flies rightward and down
        const double tangSpeed = m_thetaVel * m_L;
        const double vx = tangSpeed *  std::cos(m_theta);
        const double vy = tangSpeed * -std::sin(m_theta);
        c.setVelocity({ vx, vy });
    }

    Phase m_phase = Phase::Approach;
    double m_L = 120.0;
    double m_targetL = 120.0;
    double m_theta = 0.0;
    double m_thetaVel = 0.0;
    QPoint m_anchorStart;
    int m_kickCooldown = 0;
    int m_approachShotMs = 0;
};

// nap. just sit there with eyes closed. periodically emits a small
// "zzz" speech bubble so its visibly clear hes asleep, not crashed.
class SleepState : public State
{
public:
    QString name() const override { return QStringLiteral("sleep"); }

    void enter(Creechr& c, const WorldContext&) override
    {
        c.setVelocity({ 0, 0 });
        c.animator().setAnimation(QStringLiteral("sleep"));
        m_zMs = 0;
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
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
        // periodic zzz so the sleep is visibly happening
        m_zMs += deltaMs;
        if (m_zMs >= 2200) {
            m_zMs = 0;
            const QStringList zs = {
                QStringLiteral("z"),
                QStringLiteral("zz"),
                QStringLiteral("zzz"),
                QStringLiteral("..."),
            };
            c.speakRandom(zs, 1600);
        }
        return {};
    }

private:
    int m_zMs = 0;
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
    m_states.registerState(std::make_unique<CursorSwingState>());
    m_states.registerState(std::make_unique<HeistApproachState>());
    m_states.registerState(std::make_unique<HeistGrabState>());
    m_states.registerState(std::make_unique<HeistCarryState>());
    m_states.registerState(std::make_unique<HeistStashState>());
    m_states.registerState(std::make_unique<HeistWaitState>());
    m_states.registerState(std::make_unique<HeistTossState>());
    m_states.registerState(std::make_unique<HeistReturnState>());
}

void Creechr::beginHeist(HeistTarget t)
{
    m_heist.emplace();
    m_heist->target = std::move(t);
}

void Creechr::clearHeist()
{
    // settle the cursor debt first so no exit path can forget it
    returnCursorIfHeld();
    // whatever in-flight window the heist may have been protecting is
    // either restored or in the hoard by the time anyone clears the
    // heist; the scratch slot must not outlive it
    crashguard::clearInFlight();
    m_heist.reset();
}

void Creechr::restoreHeldLootInline()
{
    if (!m_heist) return;
    HeistContext& h = *m_heist;
    if (!h.hoardId.isEmpty()) {
        // stashed: the hoard owns the restore, fire it
        if (m_hoard) m_hoard->restoreById(h.hoardId);
        return;
    }
    // pre-stash. window: only hidden once grabbed flipped. put it back
    // by hand, same moves as the restore lambda stash WOULD have made.
#ifdef _WIN32
    if (h.grabbed && h.target.kind == TargetKind::Window) {
        HWND hwnd = static_cast<HWND>(h.target.hwnd);
        if (hwnd && IsWindow(hwnd)) {
            SetWindowPos(hwnd, HWND_TOP,
                         h.originalFrame.left(), h.originalFrame.top(),
                         h.originalFrame.width(), h.originalFrame.height(),
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
            LOG_INFO(QStringLiteral("restore inline: un-hid %1 pre-stash")
                .arg(h.target.label));
        }
    }
#endif
    // dom: the element leaves the page at requestSteal time, BEFORE
    // grabbed flips (the ack can take seconds). so restore whenever a
    // dom heist got as far as having an id at all; the content script
    // no-ops ids it never stole.
    if (h.target.kind == TargetKind::DomElement && m_ext
        && !h.target.opaqueId.isEmpty()) {
        m_ext->requestRestore(h.target.opaqueId);
    }
}

void Creechr::giveBackLootNow()
{
    if (!m_heist) {
        // nothing in hand, but custody may still be owed (paranoia)
        returnCursorIfHeld();
        return;
    }
    restoreHeldLootInline();
    clearHeist();
}

void Creechr::takeCursorCustody(QPoint homeLogical)
{
    // if we still owe the pointer a return (custody held, or a return
    // glide is mid-flight), the ORIGINAL home stands. overwriting it
    // with a mid-glide position would launder the debt: the pointer
    // would "return" to wherever we ourselves dragged it, and across
    // back-to-back heists it ratchets into a corner.
    if (!m_cursorCustody && !m_cursorGlide.active) {
        m_cursorHome = homeLogical;
    }
    m_cursorCustody = true;
    // taking custody cancels any in-flight return: the pointer is his
    // again, no point finishing the previous reel
    m_cursorGlide.active = false;
}

void Creechr::returnCursorIfHeld()
{
    if (!m_cursorCustody) return;
    m_cursorCustody = false;
#ifdef _WIN32
    // if the user is driving the mouse right now (fresh input or a
    // button held), do NOT move the pointer at all. they have already
    // reclaimed it wherever it is; yanking it would fight them.
    // custody just dissolves.
    if (cr::win32::millisSinceLastInput() < 250 || cr::win32::anyMouseButtonDown()) {
        LOG_INFO(QStringLiteral("cursor custody: user is driving, leaving pointer be"));
        return;
    }
    // reel it home instead of teleporting it. duration scales with
    // distance so short returns feel snappy and a cross-monitor haul
    // doesnt look like the pointer got possessed. the actual movement
    // happens in tickCursorGlide.
    const QPoint cur = QCursor::pos();
    const double dist = std::hypot(double(m_cursorHome.x() - cur.x()),
                                   double(m_cursorHome.y() - cur.y()));
    m_cursorGlide.active = true;
    m_cursorGlide.from = QPointF(cur);
    m_cursorGlide.to = QPointF(m_cursorHome);
    m_cursorGlide.elapsedMs = 0;
    m_cursorGlide.durationMs = qBound(220, static_cast<int>(dist / 1.6), 800);
    LOG_INFO(QStringLiteral("cursor custody: reeling pointer home to %1,%2 over %3ms")
        .arg(m_cursorHome.x()).arg(m_cursorHome.y()).arg(m_cursorGlide.durationMs));
#endif
}

void Creechr::tickCursorGlide(int deltaMs)
{
    if (!m_cursorGlide.active) return;
#ifdef _WIN32
    // the user grabbing the mouse mid-reel wins instantly. our own
    // SetCursorPos calls do not count as input, so anything fresh
    // here is a real hand on a real mouse.
    if (cr::win32::millisSinceLastInput() < 200 || cr::win32::anyMouseButtonDown()) {
        m_cursorGlide.active = false;
        return;
    }
    m_cursorGlide.elapsedMs += deltaMs;
    double t = static_cast<double>(m_cursorGlide.elapsedMs)
             / static_cast<double>(qMax(1, m_cursorGlide.durationMs));
    if (t > 1.0) t = 1.0;
    // ease-out: fast yank off the line, gentle landing
    const double e = 1.0 - (1.0 - t) * (1.0 - t);
    const QPointF p = m_cursorGlide.from + (m_cursorGlide.to - m_cursorGlide.from) * e;
    const QPoint native = cr::win32::logicalToNative(p);
    SetCursorPos(native.x(), native.y());
    if (t >= 1.0) m_cursorGlide.active = false;
#else
    m_cursorGlide.active = false;
#endif
}

void Creechr::finishCursorGlideNow()
{
    if (!m_cursorGlide.active) return;
#ifdef _WIN32
    // quit path: the tick loop is done for, so play the rest of the
    // reel here. double speed because nobody wants to watch a cursor
    // animation delay their shutdown. worst case ~400ms.
    while (m_cursorGlide.active) {
        tickCursorGlide(16);
        ::Sleep(8);
    }
#else
    m_cursorGlide.active = false;
#endif
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
        // freeze during fullscreen apps. spec §4.6. a half-finished
        // cursor reel dies here too: moving the pointer while a game
        // has it is far worse than abandoning it mid-path.
        m_cursorGlide.active = false;
        return;
    }
    m_states.tick(deltaMs, *this, world);
    tickCursorGlide(deltaMs);
    // if the user is back, anything still lazily drifting toward the
    // void gets gravity-assisted. their window restores within about a
    // second instead of five. the leisurely sink is for an empty room.
    if (world.msSinceLastInput < 300 && !m_sinking.isEmpty()) {
        for (SinkingItem& s : m_sinking) {
            s.vy = qMax(s.vy, 700.0);
        }
    }
    // sinking items update at logic rate so their restores fire in
    // sync with the rest of the world. purely visual until the moment
    // they pass the bottom edge and hoard->restoreById lands.
    tickSinkingItems(deltaMs, world.virtualDesktop);
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

void Creechr::addSinkingItem(const QPixmap& pm, QPoint startPos,
                              QPointF initialVel, const QString& hoardId)
{
    if (pm.isNull()) return;
    SinkingItem s;
    s.pixmap = pm;
    s.x = startPos.x();
    s.y = startPos.y();
    s.vx = initialVel.x();
    s.vy = initialVel.y();
    s.hoardId = hoardId;
    m_sinking.push_back(s);
}

void Creechr::tickSinkingItems(int deltaMs, const QRect& virtualDesktop)
{
    constexpr double kSinkGravity = 85.0;  // slow enough to read as "sinking"
    const double dt = deltaMs / 1000.0;
    const int bottomLimit = virtualDesktop.bottom() + 4;
    for (int i = m_sinking.size() - 1; i >= 0; --i) {
        SinkingItem& s = m_sinking[i];
        s.vy += kSinkGravity * dt;
        s.x += s.vx * dt;
        s.y += s.vy * dt;
        // fully past the bottom edge?
        if (static_cast<int>(s.y) > bottomLimit) {
            if (m_hoard && !s.hoardId.isEmpty()) {
                LOG_INFO(QStringLiteral("sinking item %1 sank, restoring").arg(s.hoardId));
                m_hoard->restoreById(s.hoardId);
            }
            m_sinking.removeAt(i);
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
