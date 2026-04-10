#include "targets/cursor_target_provider.h"

#include <QCursor>
#include <QPoint>
#include <QRect>

namespace cr {

HeistTarget CursorTargetProvider::current() const
{
    const QPoint p = QCursor::pos();
    HeistTarget t;
    t.kind = TargetKind::Cursor;
    // 16x16 nominal cursor box. it's not the actual cursor size on
    // every theme but it's a sensible heist anchor.
    t.screenRect = QRect(p.x() - 8, p.y() - 8, 16, 16);
    t.hwnd = nullptr;
    t.label = QStringLiteral("cursor");
    return t;
}

} // namespace cr
