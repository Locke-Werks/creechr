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

    // y of the platform creechr is currently standing on. equals
    // virtualDesktop.bottom - 32 when on the floor; equals window.top - 32
    // when standing on a window. updated by climb states.
    int floorY() const { return m_floorY; }
    void setFloorY(int y) { m_floorY = y; }

    // when climbing, the (x,y) we're heading to. only meaningful while
    // a climb state is active. -1 means "no climb in progress".
    int climbTargetX() const { return m_climbTargetX; }
    int climbTargetY() const { return m_climbTargetY; }
    void setClimbTarget(int x, int y) { m_climbTargetX = x; m_climbTargetY = y; }
    void clearClimbTarget() { m_climbTargetX = -1; m_climbTargetY = -1; }

    // first-time setup — must be called once after construction so the
    // initial state can fire its enter() callback against a real world.
    void initialize(const WorldContext& world);

private:
    QPointF m_position { 100.0, 600.0 };
    QPointF m_velocity { 0.0,   0.0   };
    bool m_facingRight = true;
    int m_floorY = 0;
    int m_climbTargetX = -1;
    int m_climbTargetY = -1;

    Animator m_animator;
    StateMachine m_states;
};

} // namespace cr
