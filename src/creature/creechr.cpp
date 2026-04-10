#include "creature/creechr.h"
#include "creature/world_context.h"
#include "render/sprite_atlas.h"
#include "util/logging.h"

#include <QRandomGenerator>
#include <QRect>
#include <QtGlobal>
#include <memory>

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
        Q_UNUSED(c);
        if (world.msSinceLastInput > 30000) {
            return QStringLiteral("sleep");
        }
        m_remaining -= deltaMs;
        if (m_remaining <= 0) {
            return QStringLiteral("walk");
        }
        return {};
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
        m_remaining = 2000 + QRandomGenerator::global()->bounded(4000);
        // snap y to whatever platform we're on. don't re-park to floor
        // — that breaks walking on top of windows.
        c.setPosition({ c.position().x(), static_cast<double>(c.floorY()) });
        // first climb attempt within ~500-1500ms of walking. resetting
        // every walk enter is fine — it just means he'll think about
        // climbing soon after each idle break, which is what we want.
        m_climbCooldown = 500 + QRandomGenerator::global()->bounded(1000);
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
    {
        if (world.msSinceLastInput > 30000) {
            return QStringLiteral("sleep");
        }
        const double dt = deltaMs / 1000.0;
        QPointF pos = c.position() + c.velocity() * dt;

        const bool onFloor = c.floorY() >= world.virtualDesktop.bottom() - 33;

        if (onFloor) {
            // on the floor: bounce off screen edges
            const int leftLimit  = world.virtualDesktop.left();
            const int rightLimit = world.virtualDesktop.right() - 32;
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
            int rightLimit = world.virtualDesktop.right() - 32;
            for (const QRect& w : world.windowRects) {
                if (w.top() == c.floorY() + 32 && w.left() <= pos.x() && pos.x() <= w.right()) {
                    leftLimit = w.left();
                    rightLimit = w.right() - 32;
                    break;
                }
            }
            if (pos.x() < leftLimit || pos.x() > rightLimit) {
                // walked off the edge of the window — climb down
                pos.setX(qBound<qreal>(leftLimit, pos.x(), rightLimit));
                c.setPosition(pos);
                c.setClimbTarget(static_cast<int>(pos.x()),
                                 world.virtualDesktop.bottom() - 32);
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
                if (w.height() >= 48 && w.width() >= 48) {
                    const int leftDist  = qAbs(static_cast<int>(pos.x()) - w.left());
                    const int rightDist = qAbs(static_cast<int>(pos.x()) - w.right());
                    const int targetX = (leftDist <= rightDist) ? w.left() : w.right() - 32;
                    c.setClimbTarget(targetX, w.top() - 32);
                    LOG_DEBUG(QStringLiteral("walk: chose climb target window %1 (%2x%3) at (%4,%5)")
                        .arg(idx).arg(w.width()).arg(w.height()).arg(targetX).arg(w.top() - 32));
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
}

void Creechr::initialize(const WorldContext& world)
{
    // park him on the bottom of the primary screen and start in idle
    m_floorY = world.virtualDesktop.bottom() - 32;
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

} // namespace cr
