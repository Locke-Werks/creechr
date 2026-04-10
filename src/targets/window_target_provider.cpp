#include "targets/window_target_provider.h"
#include "util/logging.h"

#include <QRandomGenerator>

namespace cr {

WindowTargetProvider::WindowTargetProvider(WindowEnumerator* enumerator)
    : m_enumerator(enumerator)
{
}

QVector<HeistTarget> WindowTargetProvider::candidates(const QRect& virtualDesktop) const
{
    QVector<HeistTarget> out;
    if (!m_enumerator) return out;

    const auto windows = m_enumerator->snapshot();
    for (const auto& w : windows) {
        // size filter: too big = it's a maximized window or someone's
        // ide. too small = probably an artifact, like a one-pixel
        // tooltip we filtered for size already but you never know.
        if (w.frame.width() > 600 || w.frame.height() > 600) continue;
        if (w.frame.width() < 80  || w.frame.height() < 60)  continue;

        // must fit (mostly) inside the desktop or it's not a real window
        const QRect inter = w.frame.intersected(virtualDesktop);
        if (inter.width()  < w.frame.width() / 2)  continue;
        if (inter.height() < w.frame.height() / 2) continue;

        // class names we explicitly opt out of — too risky to mess with
        if (w.className.contains(QStringLiteral("Consent"), Qt::CaseInsensitive)) continue;
        if (w.className.contains(QStringLiteral("Logon"),   Qt::CaseInsensitive)) continue;

        HeistTarget t;
        t.kind = TargetKind::Window;
        t.screenRect = w.frame;
        t.hwnd = w.hwnd;
        t.label = w.title.isEmpty() ? w.className : w.title;
        out.push_back(t);
    }
    return out;
}

std::optional<HeistTarget> WindowTargetProvider::pickRandom(const QRect& virtualDesktop) const
{
    const auto cands = candidates(virtualDesktop);
    if (cands.isEmpty()) return std::nullopt;
    const int idx = QRandomGenerator::global()->bounded(cands.size());
    return cands[idx];
}

} // namespace cr
