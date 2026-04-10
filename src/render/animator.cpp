#include "render/animator.h"
#include "render/sprite_atlas.h"
#include "util/logging.h"

namespace cr {

Animator::Animator(const SpriteAtlas& atlas)
    : m_atlas(atlas)
{
}

void Animator::setAnimation(const QString& name, bool resetIfSame)
{
    if (name == m_current && !resetIfSame) {
        return;
    }
    const Animation* a = m_atlas.find(name);
    if (!a) {
        LOG_WARN(QStringLiteral("animator: unknown animation '%1', ignoring").arg(name));
        return;
    }
    m_current = name;
    m_anim = a;
    m_frameIndex = 0;
    m_elapsedInFrame = 0;
    m_finished = false;
}

void Animator::tick(int deltaMs)
{
    if (!m_anim || m_anim->frames.isEmpty() || m_finished) {
        return;
    }
    m_elapsedInFrame += deltaMs;
    while (m_elapsedInFrame >= m_anim->frames[m_frameIndex].durationMs) {
        m_elapsedInFrame -= m_anim->frames[m_frameIndex].durationMs;
        m_frameIndex++;
        if (m_frameIndex >= m_anim->frames.size()) {
            if (m_anim->looping) {
                m_frameIndex = 0;
            } else {
                m_frameIndex = m_anim->frames.size() - 1;
                m_finished = true;
                break;
            }
        }
    }
}

QRect Animator::currentFrameRect() const
{
    if (!m_anim || m_anim->frames.isEmpty()) {
        return QRect();
    }
    return m_anim->frames[m_frameIndex].src;
}

int Animator::frameWidth() const
{
    return m_anim ? m_anim->frameWidth : 0;
}

int Animator::frameHeight() const
{
    return m_anim ? m_anim->frameHeight : 0;
}

} // namespace cr
