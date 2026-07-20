// crash guard. last-ditch insurance for the one thing this app must
// never do: die with somebody's window still hidden. an unhandled-
// exception filter runs a fixed table of ShowWindow-equivalents on the
// way down, then lets WER carry on.
//
// this is BELT to hoard v2's SUSPENDERS: the json-based orphan restore
// on next boot handles taskkill /f (which no filter survives); this
// handles the plain crash where the next boot might be days away.
//
// the filter deliberately touches no heap, no Qt, no CRT state beyond
// user32 calls. sync happens from the main thread; the filter can run
// on any thread, and the benign race is settled by re-checking
// IsWindow at fire time.
#pragma once

#include <QRect>

namespace cr {
namespace crashguard {

// install the SetUnhandledExceptionFilter hook. call once, early.
void install();

// replace the slot table with the given windows. called by the hoard
// whenever its window-kind entries change. hwnd is void* so this
// header stays windows.h-free. (the parameter is NOT called "slots"
// because qt macro-eats that word. ask me how i know.)
struct GuardSlot {
    void* hwnd = nullptr;
    QRect frame;
};
void syncSlots(const GuardSlot* table, int count);

// max slots the fixed table holds. more simultaneous stolen windows
// than this would be a design problem, not a crash-guard problem.
inline constexpr int kMaxSlots = 16;

// the in-flight slot: a window hidden mid-heist that has NO hoard
// entry yet (grab happens before stash registers one). set at the
// ShowWindow(SW_HIDE) moment, cleared when the hoard takes over or
// the heist dies. independent of the main table.
void setInFlight(void* hwnd, QRect frame);
void clearInFlight();

} // namespace crashguard
} // namespace cr
