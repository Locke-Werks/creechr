// Creechr — the entity. position, velocity, facing, current state.
// owns its own Animator (which references the shared SpriteAtlas) and
// its own StateMachine. all the actual behavior lives in state classes.
#pragma once

#include "render/animator.h"
#include "creature/state_machine.h"
#include "targets/target_provider.h"

#include <QColor>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace cr {

class SpriteAtlas;
struct WorldContext;
class Hoard;
class ExtensionTargetProvider;

// a trophy: a permanent (per-session) visual marker dropped at a
// nest position whenever creechr successfully completes and returns
// a heist. accumulates in a corner of the screen as a small pile.
// purely cosmetic, no gameplay effect, capped at a small number.
struct Trophy {
    QPixmap pixmap;
    QPoint  nestPos;  // top-left in screen coords
};

// a SinkingItem is a stolen thing creechr got bored of and tossed. it
// drifts downward under slow gravity until it passes the bottom of
// the virtual desktop. at that moment, if hoardId is non-empty, the
// corresponding hoard entry's restore callback fires — for window
// heists that means the original window reappears. for uia / cursor
// heists it's a no-op restore. for dom heists it sends the restore
// message to the browser extension.
struct SinkingItem {
    QPixmap pixmap;
    double x = 0.0;
    double y = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    QString hoardId;   // empty if no restore needed
};

// tiny visual particle. lives a few hundred ms, fades by alpha based
// on age/lifetime, gets drawn by OverlayWindow as a small filled rect.
// no collision, no gravity (for now), pure visual flair.
struct Particle {
    QPointF pos;
    QPointF vel;
    int ageMs = 0;
    int lifetimeMs = 400;
    QColor color;
};

// everything about a heist in progress. zeroed out between heists.
struct HeistContext {
    HeistTarget target;
    QPixmap carriedPixmap;     // captured frame, drawn alongside creechr
    QRect originalFrame;       // where to put it back
    QPoint carryDestination;   // screen corner we're walking to
    QString hoardId;           // entry in the hoard, set after grab
    qint64 returnAtMs = 0;     // wall-clock time to start the return walk
    QPoint stashedAt;          // where we dropped the bitmap (for drawing)
    bool grabbed = false;
    bool stashed = false;
};

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

    // where the carried bitmap should be drawn relative to creechr's
    // visible hands. centered on the carried pixmap (so the overlay
    // subtracts half the pixmap size before blitting). depends on
    // facing direction so the carried thing actually sits in the hands
    // creechr's currently extending forward.
    QPoint carryAnchorScreen() const;

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
    const StateMachine& stateMachine() const { return m_states; }

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

    // gnaw target: a window we want to walk over and chew on. void* not
    // CrHwnd because the header isnt allowed to include windows.h.
    int gnawTargetX() const { return m_gnawTargetX; }
    void* gnawHwnd() const { return m_gnawHwnd; }
    void setGnawTarget(int x, void* hwnd) { m_gnawTargetX = x; m_gnawHwnd = hwnd; }
    void clearGnawTarget() { m_gnawTargetX = -1; m_gnawHwnd = nullptr; }

    // rappel anchor: the screen-space point where creechr's line is
    // currently attached. set by ShootRappel, cleared when rappel
    // states finish. overlay reads this to draw the visible line.
    int rappelAnchorX() const { return m_rappelAnchorX; }
    int rappelAnchorY() const { return m_rappelAnchorY; }
    bool rappelActive() const { return m_rappelAnchorX >= 0 && m_rappelAnchorY >= 0; }
    void setRappelAnchor(int x, int y) { m_rappelAnchorX = x; m_rappelAnchorY = y; }
    void clearRappelAnchor() { m_rappelAnchorX = -1; m_rappelAnchorY = -1; }

    // speech: a small one-line text bubble drawn near creechr's head.
    // states call speak() to set one. it auto-clears at expiry. the
    // overlay reads currentSpeech() each frame and draws if non-empty.
    void speak(const QString& text, int durationMs = 1800);
    void speakRandom(const QStringList& options, int durationMs = 1800);
    QString currentSpeech() const; // empty if no active speech

    // particles. spawnPuff drops `count` short-lived particles at the
    // given screen point with random outward velocity. tickParticles
    // ages and culls them. overlay reads particles() and draws each.
    void spawnPuff(QPointF where, int count, QColor color, int lifetimeMs = 450);
    void tickParticles(int deltaMs);
    const QVector<Particle>& particles() const { return m_particles; }

    // trophies — pemanent (per-session) cosmetic markers dropped after
    // successful heist returns. capped at 8 (oldest dropped). nest
    // position is computed from the virtual desktop bounds in the
    // bottom-right corner with random scatter so the pile looks messy.
    void addTrophy(const QPixmap& pm, const QRect& virtualDesktop);
    const QVector<Trophy>& trophies() const { return m_trophies; }

    // sinking items — stolen things creechr got bored of and tossed,
    // now drifting downward toward the bottom edge of the screen.
    // spawn one via addSinkingItem(). tickSinkingItems() advances the
    // physics and removes items that sank past the bottom; if they
    // had a hoardId, the hoard restore fires at that moment.
    void addSinkingItem(const QPixmap& pm, QPoint startPos,
                        QPointF initialVel, const QString& hoardId);
    void tickSinkingItems(int deltaMs, const QRect& virtualDesktop);
    const QVector<SinkingItem>& sinkingItems() const { return m_sinking; }

    // first-time setup — must be called once after construction so the
    // initial state can fire its enter() callback against a real world.
    void initialize(const WorldContext& world);

    // hoard is owned by CreechrApp; creechr just needs a borrowed pointer
    // so heist states can register/restore entries.
    void setHoard(Hoard* h) { m_hoard = h; }
    Hoard* hoard() { return m_hoard; }

    // extension target provider, also owned by CreechrApp. heist states
    // for DomElement targets call requestSteal/requestRestore on this
    // and poll hasStealAck/hasRestoreAck to know when to advance.
    // nullptr if the pipe server failed to start.
    void setExtensionProvider(ExtensionTargetProvider* p) { m_ext = p; }
    ExtensionTargetProvider* extensionProvider() { return m_ext; }

    // active heist (or nullopt if none). heist states own this lifecycle.
    HeistContext* heist() { return m_heist ? &*m_heist : nullptr; }
    const HeistContext* heist() const { return m_heist ? &*m_heist : nullptr; }
    void beginHeist(HeistTarget t);
    void clearHeist();

    // hand back whatever the active heist is holding RIGHT NOW, no
    // matter where in the pipeline it is, then clear the heist. exists
    // because of the nasty gap between grab (source already hidden)
    // and stash (hoard entry finally created): restoreAll alone cannot
    // see a window in that gap. used by quit, tray release, and
    // anything that startles him into dropping the goods.
    void giveBackLootNow();

    // wantsFlee: set externally (by CreechrApp's scary-admin detector)
    // when creechr should drop whatever he's doing and run away. read
    // by Idle/Walk states at the top of tick(). they pre-set velocity
    // before flipping the flag.
    bool wantsFlee() const { return m_wantsFlee; }
    void requestFlee() { m_wantsFlee = true; }
    void consumeFlee() { m_wantsFlee = false; }

    // cursor custody: set the moment a cursor heist physically moves
    // the user's pointer, cleared when we give it back. clearHeist()
    // calls returnCursorIfHeld(), and clearHeist sits on every heist
    // end path (abort, toss, return, releaseEverything), so the
    // pointer cannot be stranded no matter how the heist dies. the
    // return is skipped if the user is actively driving the mouse:
    // snapping it out from under them is worse than leaving it.
    void takeCursorCustody(QPoint homeLogical);
    void returnCursorIfHeld();
    bool hasCursorCustody() const { return m_cursorCustody; }

    // cursor glide: how the pointer travels when he gives it back.
    // a single SetCursorPos jump reads as a rendering glitch; a short
    // eased reel-home reads as him yanking the line. ticked from
    // tickLogic, killed the instant the user touches anything.
    void tickCursorGlide(int deltaMs);
    void finishCursorGlideNow(); // quit path: no ticks left, play it out
    bool cursorGlideActive() const { return m_cursorGlide.active; }

private:
    QPointF m_position { 100.0, 600.0 };
    QPointF m_velocity { 0.0,   0.0   };
    bool m_facingRight = true;
    int m_floorY = 0;
    int m_climbTargetX = -1;
    int m_climbTargetY = -1;
    int m_gnawTargetX = -1;
    void* m_gnawHwnd = nullptr;
    int m_rappelAnchorX = -1;
    int m_rappelAnchorY = -1;
    QString m_speechText;
    qint64 m_speechExpiryMs = 0;
    QVector<Particle>     m_particles;
    QVector<Trophy>       m_trophies;
    QVector<SinkingItem>  m_sinking;

    Hoard* m_hoard = nullptr;
    ExtensionTargetProvider* m_ext = nullptr;
    std::optional<HeistContext> m_heist;
    bool m_wantsFlee = false;
    bool m_cursorCustody = false;
    QPoint m_cursorHome;

    struct CursorGlide {
        bool active = false;
        QPointF from;
        QPointF to;
        int elapsedMs = 0;
        int durationMs = 0;
    };
    CursorGlide m_cursorGlide;

    Animator m_animator;
    StateMachine m_states;
};

} // namespace cr
