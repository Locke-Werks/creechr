// dpi setup. one function. call it before QApplication is constructed.
// failing to do so means qt picks its own dpi mode and you spend the
// rest of the day wondering why your sprite is 1.5x too big on one
// monitor and pixel-perfect on the other.
#pragma once

namespace cr {

// must be called from main() BEFORE QApplication is constructed.
// sets per-monitor dpi v2 awareness on the process and returns whether
// it actually took effect (older windows / different awareness already
// set will return false). on non-windows it's a no-op that returns true.
bool setupDpiAwareness();

} // namespace cr
