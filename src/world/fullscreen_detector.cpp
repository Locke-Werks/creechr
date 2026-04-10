#include "world/fullscreen_detector.h"
#include "util/logging.h"

#ifdef _WIN32
#  include <windows.h>
#  include <shellapi.h>
#endif

namespace cr {

bool FullscreenDetector::isFullscreenActive()
{
#ifdef _WIN32
    QUERY_USER_NOTIFICATION_STATE state = QUNS_NOT_PRESENT;
    HRESULT hr = SHQueryUserNotificationState(&state);
    if (FAILED(hr)) {
        // shell denied us. assume not fullscreen rather than locking
        // creechr in a frozen state forever.
        return false;
    }
    // both of these mean "user is doing the fullscreen thing".
    // QUNS_BUSY also exists but it just means "do not disturb is on" —
    // we still want creechr active during DND, just don't pop a heist.
    return state == QUNS_RUNNING_D3D_FULL_SCREEN
        || state == QUNS_PRESENTATION_MODE;
#else
    return false;
#endif
}

} // namespace cr
