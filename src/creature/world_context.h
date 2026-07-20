// WorldContext — the read-only snapshot of the world that gets passed
// into state machine ticks. it's a struct on purpose. nothing in here
// owns anything. nothing in here is mutated by states. they look at it
// and they make decisions.
//
// the actual sources for these fields live elsewhere:
//   virtualDesktop  → computed from QGuiApplication::screens()
//   cursorPos       → QCursor::pos()
//   msSinceLastInput→ Win32 GetLastInputInfo (added in v0.2)
//   floorWindows    → WindowEnumerator (added in next commit)
#pragma once

#include <QPoint>
#include <QRect>
#include <QVector>

namespace cr {

struct WorldContext {
    QRect virtualDesktop;
    QPoint cursorPos;
    int msSinceLastInput = 0;

    // populated by WindowEnumerator. parallel vectors — windowRects[i]
    // and windowHwnds[i] describe the same window. hwnd is void* here
    // so this header doesnt have to drag in windows.h; the consumer
    // (heist + gnaw states in creechr.cpp) reinterpret_casts to HWND.
    QVector<QRect>  windowRects;
    QVector<void*>  windowHwnds;
    // per-window position delta since the previous snapshot (~100ms
    // ago in the cached-world refresh). zero for windows that didnt
    // exist last tick or didnt move. large values mean the user is
    // currently DRAGGING that window, which creechr can react to.
    QVector<QPoint> windowDeltas;

    // is the user in a fullscreen game / presentation? if true the
    // overlay should hide and the creature should just freeze.
    bool fullscreenActive = false;

    // is the user probably in a call (mic/camera held by some app)?
    // filled by the busy detector; states use it to skip the showier
    // antics while somebody is presenting their screen.
    bool userBusy = false;

    // settings-scaled knobs delivered to states without the states
    // ever learning that a Settings type exists. filled by CreechrApp
    // from the live settings every tick.
    int stashWaitMinMs = 6000;
    int stashWaitRangeMs = 10000;
};

} // namespace cr
