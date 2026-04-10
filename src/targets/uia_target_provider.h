// UiaTargetProvider — walks the UIA tree of the current foreground
// window looking for buttons / hyperlinks / images / menu items, and
// hands them out as HeistTargets.
//
// SPEC DEVIATION (§6.1): the spec says "UIA on its own COM-init worker
// thread, never touch raw UIA pointers from the render thread". v0.3
// runs UIA on the MAIN thread because for the demo targets (notepad's
// menu bar, simple chrome elements) the cost is small and the threading
// scaffolding wasn't worth the code. if you start seeing main-thread
// stutter when UIA scans run, this is the first thing to refactor.
// COM init still happens once via CoInitializeEx at construct time.
//
// the snapshot returned is by-value and contains no live UIA pointers,
// so it's safe to consume from anywhere even though it was created on
// the main thread. when we move UIA off-thread for real, that interface
// stays the same.
#pragma once

#include "targets/target_provider.h"

#include <QRect>
#include <QString>
#include <QVector>
#include <optional>

namespace cr {

struct UiaSnapshotItem {
    QRect screenRect;          // dpi-corrected, in qt logical px
    QString name;              // the element's UIA name (button label)
    QString controlType;       // for logging
};

class UiaTargetProvider
{
public:
    UiaTargetProvider();
    ~UiaTargetProvider();

    // run a fresh scan of the current foreground window's UIA tree.
    // returns the (possibly empty) list of stealable items. cheap if
    // the foreground hasn't changed since last call (we cache).
    QVector<UiaSnapshotItem> scan(const QRect& virtualDesktop);

    // pick a random one and return it as a HeistTarget. nullopt if
    // there's nothing in the foreground worth stealing.
    std::optional<HeistTarget> pickRandom(const QRect& virtualDesktop);

private:
    // private — declared as void* in the header to avoid pulling in
    // UIAutomation.h. cpp side casts to the real types. sourceHwnd is
    // the hwnd the root element came from, used for per-monitor dpi.
    void scanFromRoot(void* root, void* cond, void* sourceHwnd,
                      const QRect& virtualDesktop,
                      QVector<UiaSnapshotItem>& out);

    bool m_comInitialized = false;
    void* m_automation = nullptr;     // IUIAutomation*, void* to keep this header clean
    QVector<UiaSnapshotItem> m_lastSnapshot;
};

} // namespace cr
