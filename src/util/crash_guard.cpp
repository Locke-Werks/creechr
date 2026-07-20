#include "util/crash_guard.h"

#ifdef _WIN32
#  include <windows.h>
#endif

namespace cr {
namespace crashguard {

#ifdef _WIN32
namespace {

// fixed storage, written by the main thread via syncSlots, read by the
// filter on whatever thread happened to explode. no locking: a torn
// read gives us a garbage hwnd at worst, and IsWindow eats garbage for
// breakfast. volatile so the compiler doesnt get clever about the
// cross-thread reads.
struct RawSlot {
    void* hwnd;
    int x, y, w, h;
};
RawSlot g_slots[kMaxSlots] = {};
volatile int g_count = 0;
RawSlot g_inFlight = {};
volatile bool g_inFlightActive = false;

LPTOP_LEVEL_EXCEPTION_FILTER g_previous = nullptr;

LONG WINAPI crashFilter(EXCEPTION_POINTERS* info)
{
    // we are dying. show every window we were holding. nothing here
    // may allocate or take locks; SetWindowPos on a FOREIGN window is
    // fine because the work happens on the target windows thread.
    if (g_inFlightActive) {
        HWND h = static_cast<HWND>(g_inFlight.hwnd);
        if (h && IsWindow(h)) {
            SetWindowPos(h, HWND_TOP,
                         g_inFlight.x, g_inFlight.y,
                         g_inFlight.w, g_inFlight.h,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOZORDER);
        }
    }
    const int n = g_count;
    for (int i = 0; i < n && i < kMaxSlots; ++i) {
        HWND h = static_cast<HWND>(g_slots[i].hwnd);
        if (!h || !IsWindow(h)) continue;
        if (g_slots[i].w > 0 && g_slots[i].h > 0) {
            SetWindowPos(h, HWND_TOP,
                         g_slots[i].x, g_slots[i].y,
                         g_slots[i].w, g_slots[i].h,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOZORDER);
        } else {
            ShowWindow(h, SW_SHOWNOACTIVATE);
        }
    }
    // hand off to whoever was here before us (WER, a debugger). the
    // point is to fix the desktop, not to swallow the crash.
    if (g_previous) return g_previous(info);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void install()
{
    g_previous = SetUnhandledExceptionFilter(&crashFilter);
}

void syncSlots(const GuardSlot* table, int count)
{
    if (count > kMaxSlots) count = kMaxSlots;
    // write the payload first, publish the count last, so a filter
    // firing mid-sync sees either the old table or a fully-written
    // prefix of the new one
    g_count = 0;
    for (int i = 0; i < count; ++i) {
        g_slots[i].hwnd = table[i].hwnd;
        g_slots[i].x = table[i].frame.x();
        g_slots[i].y = table[i].frame.y();
        g_slots[i].w = table[i].frame.width();
        g_slots[i].h = table[i].frame.height();
    }
    g_count = count;
}

void setInFlight(void* hwnd, QRect frame)
{
    g_inFlightActive = false;
    g_inFlight.hwnd = hwnd;
    g_inFlight.x = frame.x();
    g_inFlight.y = frame.y();
    g_inFlight.w = frame.width();
    g_inFlight.h = frame.height();
    g_inFlightActive = true;
}

void clearInFlight()
{
    g_inFlightActive = false;
}

#else

void install() {}
void syncSlots(const GuardSlot*, int) {}
void setInFlight(void*, QRect) {}
void clearInFlight() {}

#endif

} // namespace crashguard
} // namespace cr
