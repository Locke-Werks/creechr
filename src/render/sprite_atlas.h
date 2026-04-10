// SpriteAtlas — a single texture + named animations.
//
// in v0.1 the atlas is generated programmatically (see makePlaceholder)
// because i don't have art yet. when there's real art, drop a PNG at
// assets/sprites/creechr_atlas.png and call loadFromFile() instead.
// the rest of the engine doesn't care which one populated it.
#pragma once

#include <QHash>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <QVector>

namespace cr {

// the canonical cell size for creechr's sprite. used by Creechr too,
// for things like "where's the floor relative to my y position" and
// "how far off the right edge of the screen is too far". if you change
// these, the world geometry math in creature/creechr.cpp keeps up
// because it reads these constants instead of hard-coding them.
constexpr int kSpriteWidth  = 48;
constexpr int kSpriteHeight = 48;

struct AnimFrame {
    QRect src;        // rect in the atlas pixmap
    int durationMs;   // how long to hold this frame
};

struct Animation {
    QVector<AnimFrame> frames;
    bool looping = true;
    int frameWidth = 32;
    int frameHeight = 32;
};

class SpriteAtlas
{
public:
    SpriteAtlas();

    // populate with the placeholder programmatic atlas. always succeeds.
    void makePlaceholder();

    // load from a PNG on disk. returns false if it can't, in which case
    // the previous contents are preserved (or you get an empty atlas).
    bool loadFromFile(const QString& path);

    const QPixmap& pixmap() const { return m_pixmap; }
    const Animation* find(const QString& name) const;

    bool hasAnimation(const QString& name) const { return m_anims.contains(name); }

private:
    QPixmap m_pixmap;
    QHash<QString, Animation> m_anims;
};

} // namespace cr
