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

    // populated by WindowEnumerator in a later commit. empty here.
    QVector<QRect> windowRects;

    // is the user in a fullscreen game / presentation? if true the
    // overlay should hide and the creature should just freeze.
    bool fullscreenActive = false;
};

} // namespace cr
