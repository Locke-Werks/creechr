// little win32-only helpers that don't deserve their own files.
#pragma once

namespace cr::win32 {

// returns "ms since the user last touched any input device" using
// GetLastInputInfo. on non-windows it returns 0.
int millisSinceLastInput();

// is any mouse button currently down? checks LBUTTON/RBUTTON/MBUTTON
// via GetAsyncKeyState. used by cursor heist abort logic.
bool anyMouseButtonDown();

} // namespace cr::win32
