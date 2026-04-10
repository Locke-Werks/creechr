#include "world/window_geometry.h"

#include <algorithm>

namespace cr::geom {

QVector<ClimbableEdge> climbableEdges(const QVector<QRect>& windows)
{
    QVector<ClimbableEdge> out;
    out.reserve(windows.size() * 2);
    for (int i = 0; i < windows.size(); ++i) {
        const QRect& w = windows[i];
        if (w.isEmpty()) continue;
        out.push_back({ w.left(),  w.top(), w.bottom(), true,  i });
        out.push_back({ w.right(), w.top(), w.bottom(), false, i });
    }
    return out;
}

int nearestReachableEdge(const QVector<ClimbableEdge>& edges,
                         QPoint feet, int floorY, int maxDx)
{
    int bestIdx = -1;
    int bestDx = maxDx + 1;
    for (int i = 0; i < edges.size(); ++i) {
        const auto& e = edges[i];
        // edge has to extend at least up to where creechr's feet are
        // AND down to (or below) the floor he's currently on, otherwise
        // it's floating in space and he can't reach it.
        if (e.yBottom < floorY - 4) continue; // edge ends above the floor → unreachable from floor
        if (e.yTop >= floorY) continue;       // edge entirely below floor → also pointless
        const int dx = std::abs(e.x - feet.x());
        if (dx < bestDx) {
            bestDx = dx;
            bestIdx = i;
        }
    }
    return bestIdx;
}

int floorY(const QRect& virtualDesktop, const QVector<QRect>& windows, int /*x*/)
{
    // v0.1: floor is just the bottom of the virtual desktop. it's a lie
    // when the taskbar is at the top, or when there's a maximized window
    // and creechr should be standing on its title bar instead, but it's
    // good enough for "he walks along the bottom and bumps into stuff".
    Q_UNUSED(windows);
    return virtualDesktop.bottom();
}

} // namespace cr::geom
