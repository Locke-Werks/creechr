// little win32-only helpers that don't deserve their own files.
#pragma once

#include <QPoint>
#include <QPointF>

namespace cr::win32 {

// returns "ms since the user last touched any input device" using
// GetLastInputInfo. on non-windows it returns 0.
int millisSinceLastInput();

// is any mouse button currently down? checks LBUTTON/RBUTTON/MBUTTON
// via GetAsyncKeyState. used by cursor heist abort logic.
bool anyMouseButtonDown();

// qt-logical -> native-physical for SetCursorPos. qt 6 on windows
// keeps each screen's native ORIGIN as its logical origin and scales
// only sizes within the screen, so the correct inverse is a scale
// around the origin of the screen CONTAINING the point. multiplying
// by the primary dpr (the old way) is only right on the primary
// screen; off it, a reel loop built on that conversion literally
// diverges and flings the pointer into a monitor edge.
QPoint logicalToNative(QPointF logicalPos);

} // namespace cr::win32
