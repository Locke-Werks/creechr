// UiaTargetProvider — walks the UIA tree of the current foreground
// window (and the taskbar) looking for buttons / hyperlinks / images /
// menu items, and hands them out as HeistTargets.
//
// UIA lives on its OWN worker thread with its own MTA COM init, the
// way the spec always said it should (the v0.3 main-thread shortcut
// finally paid its stutter bill and got evicted). the main thread
// only ever touches by-value snapshots under a mutex; no raw UIA
// pointer crosses the boundary.
//
// pickRandom never blocks: it serves from the last snapshot when
// fresh (<15s) and kicks the worker for a new one either way. a cold
// snapshot returns nullopt, the orchestrator falls through to a
// cursor heist, and the SECOND uia roll hits warm data.
#pragma once

#include "targets/target_provider.h"

#include <QRect>
#include <QString>
#include <QVector>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

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

    // wake the worker for a fresh scan. returns immediately.
    void requestScan(const QRect& virtualDesktop);

    // pick a random element from the freshest snapshot, or nullopt if
    // the snapshot is cold/empty. always kicks a refresh so repeated
    // interest keeps the data warm.
    std::optional<HeistTarget> pickRandom(const QRect& virtualDesktop);

private:
    void workerMain();
    // worker-side only. void* so this header stays UIAutomation.h-free.
    QVector<UiaSnapshotItem> runScan(void* automation, const QRect& virtualDesktop);
    void scanFromRoot(void* root, void* cond, void* sourceHwnd,
                      const QRect& virtualDesktop,
                      QVector<UiaSnapshotItem>& out);

    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_scanRequested = false;
    bool m_quit = false;
    QRect m_pendingVd;
    QVector<UiaSnapshotItem> m_snapshot;
    qint64 m_snapshotMs = 0;
};

} // namespace cr
