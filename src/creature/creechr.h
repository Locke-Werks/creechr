// Creechr — the entity. position, velocity, facing, current state.
// owns its own Animator (which references the shared SpriteAtlas) and
// its own StateMachine. all the actual behavior lives in state classes.
#pragma once

#include "render/animator.h"
#include "creature/state_machine.h"

#include <QPointF>
#include <QRect>

namespace cr {

class SpriteAtlas;
struct WorldContext;

class Creechr
{
public:
    explicit Creechr(const SpriteAtlas& atlas);

    // logic tick — runs at ~10Hz. advances state machine and physics.
    void tickLogic(int deltaMs, const WorldContext& world);

    // render tick — runs at ~30Hz. advances the animator only.
    void tickRender(int deltaMs);

    // where to blit the current frame, in virtual-desktop logical coords.
    // top-left of the sprite quad. width/height come from the animator.
    QRect drawRect() const;
    QRect frameSrcRect() const;

    // accessors used by states
    QPointF position() const { return m_position; }
    void setPosition(QPointF p) { m_position = p; }

    QPointF velocity() const { return m_velocity; }
    void setVelocity(QPointF v) { m_velocity = v; }

    bool facingRight() const { return m_facingRight; }
    void setFacingRight(bool r) { m_facingRight = r; }

    Animator& animator() { return m_animator; }
    const Animator& animator() const { return m_animator; }

    StateMachine& stateMachine() { return m_states; }

    // first-time setup — must be called once after construction so the
    // initial state can fire its enter() callback against a real world.
    void initialize(const WorldContext& world);

private:
    QPointF m_position { 100.0, 600.0 };
    QPointF m_velocity { 0.0,   0.0   };
    bool m_facingRight = true;

    Animator m_animator;
    StateMachine m_states;
};

} // namespace cr
