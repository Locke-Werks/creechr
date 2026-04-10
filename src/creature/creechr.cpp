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

    void enter(Creechr& c, const WorldContext& world) override
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
        // park y on the bottom of the desktop
        const int floorY = world.virtualDesktop.bottom() - 32;
        c.setPosition({ c.position().x(), static_cast<double>(floorY) });
    }

    QString tick(int deltaMs, Creechr& c, const WorldContext& world) override
    {
        if (world.msSinceLastInput > 30000) {
            return QStringLiteral("sleep");
        }
        const double dt = deltaMs / 1000.0;
        QPointF pos = c.position() + c.velocity() * dt;

        // bounce off screen edges
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
        c.setPosition(pos);

        m_remaining -= deltaMs;
        if (m_remaining <= 0) {
            return QStringLiteral("idle");
        }
        return {};
    }

private:
    int m_remaining = 0;
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
    m_states.registerState(std::make_unique<SleepState>());
    m_states.registerState(std::make_unique<WakeState>());
}

void Creechr::initialize(const WorldContext& world)
{
    // park him on the bottom of the primary screen and start in idle
    m_position = QPointF(world.virtualDesktop.left() + 200,
                         world.virtualDesktop.bottom() - 32);
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
