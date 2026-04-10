// FullscreenDetector — knows whether the user is in a fullscreen game
// or a powerpoint deck. when true, creechr should freeze and the
// overlay should hide so we don't ruin the experience.
//
// uses SHQueryUserNotificationState which is the right api for this.
// don't go reinventing it with EnumWindows + IsZoomed + GetMonitorInfo
// — i did, it doesn't work, you'll catch maximized windows by mistake.
#pragma once

namespace cr {

class FullscreenDetector
{
public:
    FullscreenDetector() = default;
    bool isFullscreenActive();
};

} // namespace cr
