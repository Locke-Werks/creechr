// OverlayWindow — the transparent canvas creechr lives on.
//
// the rules of this window are:
//   - frameless, always on top, no taskbar entry, no alt-tab entry
//   - completely click-through (mouse events fall through to whatever
//     is underneath, which is your actual desktop and your apps)
//   - covers the entire virtual desktop, recalculated whenever screens
//     change so you can plug a monitor in mid-run and not break things
//   - never steals focus, never appears in z-order above fullscreen apps
//
// if any of those become false you have a bug. probably the windows
// flags got reset by some qt update. check applyClickThroughFlags().
#pragma once

#include <QRect>
#include <QRegion>
#include <QString>
#include <QWidget>

namespace cr {
class Creechr;
class SpriteAtlas;
}

class OverlayWindow : public QWidget
{
    Q_OBJECT

public:
    explicit OverlayWindow(QWidget* parent = nullptr);
    ~OverlayWindow() override;

    // weak refs — owned by CreechrApp. set once at startup, never null
    // for the rest of the process lifetime.
    void setCreechr(const cr::Creechr* c) { m_creechr = c; }
    void setAtlas(const cr::SpriteAtlas* a) { m_atlas = a; }

    // dirty-region update: computes the rects the scene currently
    // occupies, unions with last frame's (so old positions get
    // erased), and invalidates only that. skips the frame entirely
    // when nothing observable changed. this window spans EVERY
    // monitor; repainting all of it at 60Hz for a 48px goblin was
    // most of our cpu bill. CREECHR_FULL_REPAINT=1 restores the old
    // behavior if partial updates ever ghost.
    void updateScene();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void recomputeGeometry();

private:
    // smash WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
    // WS_EX_NOACTIVATE onto the native HWND. qt sets some of these via
    // its window flags but i don't trust it not to drop them on a
    // restyle, so we set them again, manually, after every show().
    void applyClickThroughFlags();

    // current scene footprint in virtual-desktop coords
    QRegion computeSceneRegion() const;

    const cr::Creechr* m_creechr = nullptr;
    const cr::SpriteAtlas* m_atlas = nullptr;

    QRegion m_lastSceneRegion;

    // cheap change stamp: when this matches last tick AND nothing
    // inherently-animated (particles, sinking loot) is alive, the
    // frame is skipped outright
    struct SceneStamp {
        QRect creature;
        QRect frameSrc;
        QString speech;
        QPoint anchor;
        QPoint stash;
        int trophies = 0;
        bool carried = false;
        bool operator==(const SceneStamp& o) const = default;
    };
    SceneStamp m_lastStamp;
};
