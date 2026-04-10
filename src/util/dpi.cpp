#include "util/dpi.h"

#ifdef _WIN32
#  include <windows.h>
#endif

namespace cr {

bool setupDpiAwareness()
{
#ifdef _WIN32
    // SetProcessDpiAwarenessContext is win10 1703+. on anything older
    // we'd need to fall back to SetProcessDpiAwareness or the manifest.
    // i am not supporting anything older. if you're on win7 in 2026,
    // this is the least of your problems.
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        return true;
    }
    // already set by something? GetLastError() == ERROR_ACCESS_DENIED
    // is the usual cause and means qt or windows already locked it in,
    // which is fine — just check the current value.
    DPI_AWARENESS_CONTEXT current = GetThreadDpiAwarenessContext();
    return AreDpiAwarenessContextsEqual(current, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#else
    return true;
#endif
}

} // namespace cr
