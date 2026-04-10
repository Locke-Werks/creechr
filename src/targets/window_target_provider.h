// scans the window enumerator's snapshot for "things creechr could
// reasonably steal". in v0.2 that means:
//   - small (< 600x600 logical pixels)
//   - not maximized (frame doesn't span the screen)
//   - not the shell, not the overlay, not a system process
//   - actually visible
//
// returns nullopt when nothing fits the bill. caller will then try the
// cursor provider, or just have creechr go for a walk instead.
#pragma once

#include "targets/target_provider.h"
#include "world/window_enumerator.h"

#include <QVector>
#include <optional>

namespace cr {

class WindowTargetProvider
{
public:
    explicit WindowTargetProvider(WindowEnumerator* enumerator);

    QVector<HeistTarget> candidates(const QRect& virtualDesktop) const;
    std::optional<HeistTarget> pickRandom(const QRect& virtualDesktop) const;

private:
    WindowEnumerator* m_enumerator;
};

} // namespace cr
