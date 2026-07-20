// FullscreenDetector — knows whether the user is in a fullscreen game
// or a powerpoint deck. when true, creechr should freeze and the
// overlay should hide so we don't ruin the experience.
//
// primary source is SHQueryUserNotificationState, which catches
// exclusive d3d and presentation mode. borderless-windowed games (how
// basically everything ships now) don't register there, so there's a
// second check: foreground window with no WS_CAPTION covering its
// whole monitor. the caption test is the load-bearing part — it's
// what keeps maximized normal apps from false-positiving, which is
// exactly the trap the old comment here warned about.
#pragma once

namespace cr {

class FullscreenDetector
{
public:
    FullscreenDetector() = default;

    // our own overlay is a borderless window covering everything; it
    // must never count as a fullscreen app (it shouldnt be foreground
    // either, WS_EX_NOACTIVATE, but belt and braces)
    void setSelfHwnd(void* hwnd) { m_selfHwnd = hwnd; }

    bool isFullscreenActive();

    // QUNS_BUSY: the user flipped do-not-disturb on. he stays visible
    // and animated, he just doesnt commit crimes. cached by the last
    // isFullscreenActive() call.
    bool userRequestedQuiet() const { return m_userQuiet; }

private:
    void* m_selfHwnd = nullptr;
    bool m_userQuiet = false;
};

} // namespace cr
