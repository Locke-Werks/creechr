// WindowGeometry — pure-function helpers on top of a list of window rects.
// "what's the closest climbable edge to point P", "is there a window
// whose top is at this y", that sort of thing. no state, no allocations
// beyond what the caller asked for.
#pragma once

#include <QPoint>
#include <QRect>
#include <QVector>

namespace cr {

// a vertical edge that creechr could climb. left edges and right edges
// are both candidates. the y range is the edge's vertical extent.
struct ClimbableEdge {
    int x;          // screen-coord x of the edge
    int yTop;       // top y of the edge (smaller y, higher on screen)
    int yBottom;    // bottom y of the edge
    bool isLeftEdge; // true if this is a window's left side, false for right
    int windowIndex; // index into the input rects array
};

namespace geom {

// returns every left/right edge across all windows. cheap.
QVector<ClimbableEdge> climbableEdges(const QVector<QRect>& windows);

// find the closest edge by horizontal distance to a point, considering
// only edges whose y range covers floorY (so a flat-on-the-floor creechr
// can actually reach it).
//
// returns -1 if there's nothing reachable within maxDx.
int nearestReachableEdge(const QVector<ClimbableEdge>& edges,
                         QPoint feet, int floorY, int maxDx);

// height of the floor at this x in the virtual desktop, given the
// virtualDesktop bounds and a list of window rects. for v0.1 we just
// return the bottom of the desktop (the taskbar / screen edge). later
// this could account for windows that are docked to the bottom.
int floorY(const QRect& virtualDesktop, const QVector<QRect>& windows, int x);

} // namespace geom
} // namespace cr
