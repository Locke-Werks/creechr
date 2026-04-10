#include "render/sprite_atlas.h"
#include "util/logging.h"

#include <QImage>
#include <QPainter>

namespace cr {

namespace {

// the placeholder is a grid of 32x32 cells. each row is one animation.
// gross color scheme on purpose so it's obvious it's a placeholder.
constexpr int kCell = 32;

void drawCreechrFace(QPainter& p, const QRect& cell, int bobOffset, bool facingRight,
                     bool eyesClosed, QColor body)
{
    // body
    QRect b = cell.adjusted(4, 6 + bobOffset, -4, -2 + bobOffset);
    p.fillRect(b, body);
    p.setPen(QColor(20, 0, 15));
    p.drawRect(b);

    // eyes
    const int eyeY = b.top() + 6;
    const int leftEyeX  = b.left() + (facingRight ? 8  : 14);
    const int rightEyeX = b.left() + (facingRight ? 16 : 22);
    if (eyesClosed) {
        p.drawLine(leftEyeX,  eyeY + 1, leftEyeX  + 3, eyeY + 1);
        p.drawLine(rightEyeX, eyeY + 1, rightEyeX + 3, eyeY + 1);
    } else {
        p.fillRect(QRect(leftEyeX,  eyeY, 3, 3), QColor(255, 255, 255));
        p.fillRect(QRect(rightEyeX, eyeY, 3, 3), QColor(255, 255, 255));
        // pupil shifted in facing direction
        const int pupilDx = facingRight ? 1 : 0;
        p.fillRect(QRect(leftEyeX  + pupilDx, eyeY + 1, 1, 1), QColor(0, 0, 0));
        p.fillRect(QRect(rightEyeX + pupilDx, eyeY + 1, 1, 1), QColor(0, 0, 0));
    }

    // little feet at the bottom of the body
    p.fillRect(QRect(b.left() + 6,  b.bottom() - 1, 4, 2), QColor(20, 0, 15));
    p.fillRect(QRect(b.right() - 9, b.bottom() - 1, 4, 2), QColor(20, 0, 15));
}

} // namespace

SpriteAtlas::SpriteAtlas() = default;

const Animation* SpriteAtlas::find(const QString& name) const
{
    auto it = m_anims.constFind(name);
    return it == m_anims.constEnd() ? nullptr : &it.value();
}

bool SpriteAtlas::loadFromFile(const QString& path)
{
    QPixmap pm;
    if (!pm.load(path)) {
        LOG_WARN(QStringLiteral("sprite_atlas: failed to load %1").arg(path));
        return false;
    }
    // we don't yet have a manifest format, so loadFromFile only loads
    // the texture — animations still need to be defined in code. once
    // there's real art, ship a json sidecar and parse it here.
    m_pixmap = pm;
    LOG_INFO(QStringLiteral("sprite_atlas: loaded %1 (%2x%3)")
        .arg(path).arg(pm.width()).arg(pm.height()));
    return true;
}

void SpriteAtlas::makePlaceholder()
{
    // 8 columns x 8 rows of 32px cells = 256x256 atlas. plenty for v0.1.
    constexpr int kCols = 8;
    constexpr int kRows = 8;
    m_pixmap = QPixmap(kCols * kCell, kRows * kCell);
    m_pixmap.fill(Qt::transparent);

    QPainter p(&m_pixmap);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QColor body(220, 50, 140);

    auto cellRect = [](int col, int row) {
        return QRect(col * kCell, row * kCell, kCell, kCell);
    };

    // row 0: idle (1 frame)
    drawCreechrFace(p, cellRect(0, 0), 0, true, false, body);

    // row 1: walk_right (4 frames, 2-pixel vertical bob)
    for (int i = 0; i < 4; ++i) {
        const int bob = (i == 1 || i == 3) ? -1 : 0;
        drawCreechrFace(p, cellRect(i, 1), bob, true, false, body);
    }

    // row 2: walk_left (4 frames, mirrored)
    for (int i = 0; i < 4; ++i) {
        const int bob = (i == 1 || i == 3) ? -1 : 0;
        drawCreechrFace(p, cellRect(i, 2), bob, false, false, body);
    }

    // row 3: climb_up (2 frames)
    for (int i = 0; i < 2; ++i) {
        drawCreechrFace(p, cellRect(i, 3), i == 1 ? -1 : 0, true, false, body);
    }

    // row 4: climb_down (2 frames)
    for (int i = 0; i < 2; ++i) {
        drawCreechrFace(p, cellRect(i, 4), i == 1 ? 1 : 0, true, false, body);
    }

    // row 5: hang (1 frame)
    drawCreechrFace(p, cellRect(0, 5), 0, true, false, body);

    // row 6: sleep (1 frame, eyes closed, slumped)
    drawCreechrFace(p, cellRect(0, 6), 2, true, true, body);

    // row 7: wake (3 frames: closed, half, open)
    drawCreechrFace(p, cellRect(0, 7), 1, true, true,  body);
    drawCreechrFace(p, cellRect(1, 7), 0, true, true,  body);
    drawCreechrFace(p, cellRect(2, 7), 0, true, false, body);

    p.end();

    auto add = [this](const QString& name, std::initializer_list<QPoint> cells,
                       int durMs, bool looping)
    {
        Animation a;
        a.looping = looping;
        a.frameWidth = kCell;
        a.frameHeight = kCell;
        for (const QPoint& c : cells) {
            a.frames.push_back({ QRect(c.x() * kCell, c.y() * kCell, kCell, kCell), durMs });
        }
        m_anims.insert(name, a);
    };

    add("idle",       { {0,0} },                                 1000, true);
    add("walk_right", { {0,1}, {1,1}, {2,1}, {3,1} },             120, true);
    add("walk_left",  { {0,2}, {1,2}, {2,2}, {3,2} },             120, true);
    add("climb_up",   { {0,3}, {1,3} },                           160, true);
    add("climb_down", { {0,4}, {1,4} },                           160, true);
    add("hang",       { {0,5} },                                 1000, true);
    add("sleep",      { {0,6} },                                 1000, true);
    add("wake",       { {0,7}, {1,7}, {2,7} },                    180, false);

    LOG_INFO(QStringLiteral("sprite_atlas: placeholder atlas built (%1 anims)")
        .arg(m_anims.size()));
}

} // namespace cr
