#include "util/win32_helpers.h"

#ifdef _WIN32
#  include <windows.h>
#endif

namespace cr::win32 {

int millisSinceLastInput()
{
#ifdef _WIN32
    LASTINPUTINFO lii = {};
    lii.cbSize = sizeof(lii);
    if (!GetLastInputInfo(&lii)) {
        return 0;
    }
    const DWORD now = GetTickCount();
    return static_cast<int>(now - lii.dwTime);
#else
    return 0;
#endif
}

bool anyMouseButtonDown()
{
#ifdef _WIN32
    // GetAsyncKeyState's high bit is "currently down". cast required.
    auto down = [](int vk) {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };
    return down(VK_LBUTTON) || down(VK_RBUTTON) || down(VK_MBUTTON);
#else
    return false;
#endif
}

} // namespace cr::win32
