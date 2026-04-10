// Animator — given a SpriteAtlas, plays one animation at a time.
// tick(deltaMs) advances the frame counter. currentFrameRect() returns
// what to blit. setAnimation() switches with optional reset.
#pragma once

#include <QRect>
#include <QString>

namespace cr {

class SpriteAtlas;
struct Animation;

class Animator
{
public:
    explicit Animator(const SpriteAtlas& atlas);

    void setAnimation(const QString& name, bool resetIfSame = false);
    const QString& currentAnimation() const { return m_current; }
    bool finished() const { return m_finished; }

    void tick(int deltaMs);

    QRect currentFrameRect() const;
    int frameWidth() const;
    int frameHeight() const;

private:
    const SpriteAtlas& m_atlas;
    QString m_current;
    const Animation* m_anim = nullptr;
    int m_frameIndex = 0;
    int m_elapsedInFrame = 0;
    bool m_finished = false;
};

} // namespace cr
