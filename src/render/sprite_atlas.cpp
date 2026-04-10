#include "render/sprite_atlas.h"
#include "util/logging.h"

#include <QImage>
#include <QPainter>

namespace cr {

namespace {

// the placeholder is a grid of kSpriteWidth x kSpriteHeight cells. each
// row is one animation. ugly color scheme on purpose so it's obvious
// it's a placeholder. real art (one day, allegedly) drops in via
// loadFromFile() and replaces the whole texture.
constexpr int kCellW = kSpriteWidth;
constexpr int kCellH = kSpriteHeight;

const QColor kBody (220, 50, 140);   // creechr-pink
const QColor kInk  ( 20,  0,  15);   // outline / dark detail
const QColor kEye  (255, 255, 255);
const QColor kPupil(  0,   0,   0);
const QColor kTeeth(255, 240, 230);

// -------- pose params --------
//
// every frame is described by these (closed-form, no sprite sheet
// dependencies). the draw helpers below take a pose and paint into
// the given cell rect.

enum class MouthState { Closed, OpenSmall, OpenWide, Slack };

struct ArmPose {
    // the hand's offset from the shoulder, in cell-local pixels.
    // shoulder x is computed by the caller. positive y is downward.
    int dx;
    int dy;
};

struct LegPose {
    // the foot's offset from the leg's "hip" position. (0,0) means
    // straight down at default length. negative dx puts the foot
    // forward of the body in walk; positive puts it behind.
    int dx;
    int dy;   // small variation for vertical bob, usually 0
};

struct CreechrPose {
    int bodyBob = 0;          // vertical bob applied to whole body
    bool eyesClosed = false;
    bool facingRight = true;
    MouthState mouth = MouthState::Closed;
    ArmPose leftArm  { 0, 6 };  // hanging at side
    ArmPose rightArm { 0, 6 };
    LegPose leftLeg  { 0, 0 };
    LegPose rightLeg { 0, 0 };
};

// -------- draw helpers --------

void drawHead(QPainter& p, const QRect& cell, const CreechrPose& pose)
{
    // body sits roughly in the upper 60% of the cell, leaving room
    // for legs in the bottom 40%.
    const int bx = cell.left() + 9;
    const int by = cell.top()  + 4 + pose.bodyBob;
    const int bw = 30;
    const int bh = 20;

    // body
    p.fillRect(QRect(bx, by, bw, bh), kBody);
    p.setPen(kInk);
    p.drawRect(QRect(bx, by, bw - 1, bh - 1));

    // eyes — two 4x4 white squares with 2x2 pupils, pupils shifted
    // in the facing direction so they read as actually looking
    const int eyeY = by + 6;
    const int leftEyeX  = bx + 5;
    const int rightEyeX = bx + 19;
    if (pose.eyesClosed) {
        p.setPen(kInk);
        p.drawLine(leftEyeX,  eyeY + 2, leftEyeX  + 4, eyeY + 2);
        p.drawLine(rightEyeX, eyeY + 2, rightEyeX + 4, eyeY + 2);
    } else {
        p.fillRect(QRect(leftEyeX,  eyeY, 5, 4), kEye);
        p.fillRect(QRect(rightEyeX, eyeY, 5, 4), kEye);
        const int pupilDx = pose.facingRight ? 2 : 1;
        p.fillRect(QRect(leftEyeX  + pupilDx, eyeY + 1, 2, 2), kPupil);
        p.fillRect(QRect(rightEyeX + pupilDx, eyeY + 1, 2, 2), kPupil);
    }

    // mouth — at the bottom of the head/body. width and height vary
    // by state. teeth show only when wide open (bite).
    const int mx = bx + 8;
    const int mw = 14;
    const int my = by + 14;
    switch (pose.mouth) {
    case MouthState::Closed:
        p.setPen(kInk);
        p.drawLine(mx, my + 2, mx + mw - 1, my + 2);
        break;
    case MouthState::Slack:
        // sleeping / dazed: thin half-open line
        p.setPen(kInk);
        p.drawLine(mx + 2, my + 2, mx + mw - 3, my + 2);
        p.drawLine(mx + 2, my + 3, mx + mw - 3, my + 3);
        break;
    case MouthState::OpenSmall:
        p.fillRect(QRect(mx + 2, my, mw - 4, 4), kPupil);
        p.setPen(kInk);
        p.drawRect(QRect(mx + 2, my, mw - 5, 3));
        break;
    case MouthState::OpenWide:
        p.fillRect(QRect(mx, my - 1, mw, 6), kPupil);
        p.setPen(kInk);
        p.drawRect(QRect(mx, my - 1, mw - 1, 5));
        // a couple of jagged teeth so the bite reads as a bite
        p.fillRect(QRect(mx + 2,  my, 2, 2), kTeeth);
        p.fillRect(QRect(mx + 6,  my, 2, 2), kTeeth);
        p.fillRect(QRect(mx + 10, my, 2, 2), kTeeth);
        p.fillRect(QRect(mx + 3,  my + 3, 2, 1), kTeeth);
        p.fillRect(QRect(mx + 7,  my + 3, 2, 1), kTeeth);
        break;
    }
}

void drawArms(QPainter& p, const QRect& cell, const CreechrPose& pose)
{
    const int by = cell.top() + 4 + pose.bodyBob;
    // shoulders sit at the body's side edges, about 2/3 down the body
    const int leftShoulderX  = cell.left() +  9;
    const int rightShoulderX = cell.left() + 38;
    const int shoulderY      = by + 13;

    p.setPen(QPen(kInk, 2));

    // left arm: shoulder → elbow → hand. simple two-segment.
    {
        const int sx = leftShoulderX;
        const int sy = shoulderY;
        const int hx = sx - 2 + pose.leftArm.dx;
        const int hy = sy + pose.leftArm.dy;
        // elbow midway, biased outward so it bends naturally
        const int ex = (sx + hx) / 2 - 1;
        const int ey = (sy + hy) / 2 + 1;
        p.drawLine(sx, sy, ex, ey);
        p.drawLine(ex, ey, hx, hy);
        // hand: little 2x2 dot at the end
        p.fillRect(QRect(hx - 1, hy - 1, 3, 3), kBody);
        p.drawRect(QRect(hx - 1, hy - 1, 2, 2));
    }
    // right arm: same drill, mirrored
    {
        const int sx = rightShoulderX;
        const int sy = shoulderY;
        const int hx = sx + 2 + pose.rightArm.dx;
        const int hy = sy + pose.rightArm.dy;
        const int ex = (sx + hx) / 2 + 1;
        const int ey = (sy + hy) / 2 + 1;
        p.drawLine(sx, sy, ex, ey);
        p.drawLine(ex, ey, hx, hy);
        p.fillRect(QRect(hx - 1, hy - 1, 3, 3), kBody);
        p.drawRect(QRect(hx - 1, hy - 1, 2, 2));
    }
}

void drawLegs(QPainter& p, const QRect& cell, const CreechrPose& pose)
{
    const int hipY = cell.top() + 4 + pose.bodyBob + 20;
    const int leftHipX  = cell.left() + 17;
    const int rightHipX = cell.left() + 30;

    p.setPen(QPen(kInk, 2));

    // left leg: hip → knee → foot
    {
        const int hx = leftHipX;
        const int hy = hipY;
        const int fx = hx + pose.leftLeg.dx;
        const int fy = hy + 14 + pose.leftLeg.dy;
        const int kx = (hx + fx) / 2;
        const int ky = (hy + fy) / 2 + 1;
        p.drawLine(hx, hy, kx, ky);
        p.drawLine(kx, ky, fx, fy);
        // foot: small horizontal stub so he has visible feet
        p.fillRect(QRect(fx - 2, fy - 1, 5, 2), kInk);
    }
    {
        const int hx = rightHipX;
        const int hy = hipY;
        const int fx = hx + pose.rightLeg.dx;
        const int fy = hy + 14 + pose.rightLeg.dy;
        const int kx = (hx + fx) / 2;
        const int ky = (hy + fy) / 2 + 1;
        p.drawLine(hx, hy, kx, ky);
        p.drawLine(kx, ky, fx, fy);
        p.fillRect(QRect(fx - 2, fy - 1, 5, 2), kInk);
    }
}

void drawCreechr(QPainter& p, const QRect& cell, const CreechrPose& pose)
{
    // legs first so the body covers the hip joints, then arms, then
    // head — that order makes the layering look reasonable when arms
    // come forward.
    drawLegs(p, cell, pose);
    drawHead(p, cell, pose);
    drawArms(p, cell, pose);
}

// shorthand factories for common arm/leg states ---

constexpr ArmPose armSide()      { return { 0,  6 }; }
constexpr ArmPose armSwingFwd()  { return { 4,  4 }; }
constexpr ArmPose armSwingBack() { return {-3,  4 }; }
constexpr ArmPose armReachFwd()  { return { 9,  0 }; } // straight forward (grab)
constexpr ArmPose armCarry()     { return { 8,  3 }; } // forward + slight down (carry pose)
constexpr ArmPose armUp()        { return { 0, -6 }; } // climbing

constexpr LegPose legStand()         { return { 0, 0 }; }
constexpr LegPose legFwd()           { return {-3, -1 }; } // foot forward + slightly raised
constexpr LegPose legBack()          { return { 3,  0 }; }
constexpr LegPose legPassing()       { return { 0, -1 }; }
constexpr LegPose legPassingHigh()   { return { 0, -2 }; }
constexpr LegPose legClimb()         { return { 0, -3 }; }

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
    // 8 columns × 12 rows of 48px cells. one row per animation.
    // 384 × 576 atlas. transparent background, draw in.
    constexpr int kCols = 8;
    constexpr int kRows = 12;
    m_pixmap = QPixmap(kCols * kCellW, kRows * kCellH);
    m_pixmap.fill(Qt::transparent);

    QPainter p(&m_pixmap);
    p.setRenderHint(QPainter::Antialiasing, false);

    auto cellRect = [](int col, int row) {
        return QRect(col * kCellW, row * kCellH, kCellW, kCellH);
    };

    // ----- row 0: idle -----
    // single relaxed pose. arms at sides, legs straight, mouth closed.
    {
        CreechrPose pose;
        drawCreechr(p, cellRect(0, 0), pose);
    }

    // ----- row 1: walk_right -----
    // 6-frame leg + arm cycle. arms swing opposite of legs.
    // poses: contact-L, passing-L, recoil-L, contact-R, passing-R, recoil-R
    auto walkPose = [](int frame, bool right) {
        CreechrPose pose;
        pose.facingRight = right;
        // mirror leg/arm dx for left-facing
        const int sign = right ? 1 : -1;
        switch (frame) {
        case 0: // left foot contact, right arm forward
            pose.leftLeg  = { -3 * sign, 0 };
            pose.rightLeg = {  3 * sign, 0 };
            pose.leftArm  = {  4 * sign, 4 };
            pose.rightArm = { -3 * sign, 4 };
            pose.bodyBob = 0;
            break;
        case 1: // passing
            pose.leftLeg  = { 0, -1 };
            pose.rightLeg = { 0,  0 };
            pose.leftArm  = { 2 * sign, 5 };
            pose.rightArm = {-2 * sign, 5 };
            pose.bodyBob = -1;
            break;
        case 2: // left foot recoil
            pose.leftLeg  = {  3 * sign, 0 };
            pose.rightLeg = { -3 * sign, 0 };
            pose.leftArm  = { -3 * sign, 4 };
            pose.rightArm = {  4 * sign, 4 };
            pose.bodyBob = 0;
            break;
        case 3: // right foot contact, left arm forward
            pose.leftLeg  = {  3 * sign, 0 };
            pose.rightLeg = { -3 * sign, 0 };
            pose.leftArm  = { -3 * sign, 4 };
            pose.rightArm = {  4 * sign, 4 };
            pose.bodyBob = 0;
            break;
        case 4: // passing
            pose.leftLeg  = { 0,  0 };
            pose.rightLeg = { 0, -1 };
            pose.leftArm  = {-2 * sign, 5 };
            pose.rightArm = { 2 * sign, 5 };
            pose.bodyBob = -1;
            break;
        case 5: // right foot recoil
            pose.leftLeg  = { -3 * sign, 0 };
            pose.rightLeg = {  3 * sign, 0 };
            pose.leftArm  = {  4 * sign, 4 };
            pose.rightArm = { -3 * sign, 4 };
            pose.bodyBob = 0;
            break;
        }
        return pose;
    };

    for (int i = 0; i < 6; ++i) {
        drawCreechr(p, cellRect(i, 1), walkPose(i, true));
    }
    // ----- row 2: walk_left -----
    for (int i = 0; i < 6; ++i) {
        drawCreechr(p, cellRect(i, 2), walkPose(i, false));
    }

    // ----- row 3: climb_up -----
    // arms reach up, legs scissor. 4 frames.
    for (int i = 0; i < 4; ++i) {
        CreechrPose pose;
        pose.facingRight = true;
        pose.bodyBob = (i % 2 == 0) ? 0 : -1;
        pose.leftArm  = (i < 2) ? armUp() : ArmPose{ -1,  3 };
        pose.rightArm = (i < 2) ? ArmPose{ 1, 3 } : armUp();
        pose.leftLeg  = (i < 2) ? LegPose{ 0, -2 } : LegPose{ -2,  0 };
        pose.rightLeg = (i < 2) ? LegPose{ -2, 0 } : LegPose{  0, -2 };
        drawCreechr(p, cellRect(i, 3), pose);
    }
    // ----- row 4: climb_down -----
    for (int i = 0; i < 4; ++i) {
        CreechrPose pose;
        pose.facingRight = true;
        pose.bodyBob = (i % 2 == 0) ? 0 : 1;
        pose.leftArm  = (i < 2) ? ArmPose{ -1,  4 } : ArmPose{  1,  4 };
        pose.rightArm = (i < 2) ? ArmPose{  1,  4 } : ArmPose{ -1,  4 };
        pose.leftLeg  = (i < 2) ? LegPose{ -2, 1 } : LegPose{  1,  1 };
        pose.rightLeg = (i < 2) ? LegPose{  1, 1 } : LegPose{ -2,  1 };
        drawCreechr(p, cellRect(i, 4), pose);
    }

    // ----- row 5: hang -----
    {
        CreechrPose pose;
        pose.leftArm  = armUp();
        pose.rightArm = armUp();
        pose.leftLeg  = { 0, 2 };
        pose.rightLeg = { 0, 2 };
        drawCreechr(p, cellRect(0, 5), pose);
    }

    // ----- row 6: sleep -----
    {
        CreechrPose pose;
        pose.eyesClosed = true;
        pose.mouth = MouthState::Slack;
        pose.bodyBob = 2;
        pose.leftArm  = { -1, 7 };
        pose.rightArm = {  1, 7 };
        pose.leftLeg  = { -1, 1 };
        pose.rightLeg = {  1, 1 };
        drawCreechr(p, cellRect(0, 6), pose);
    }

    // ----- row 7: wake -----
    // 3 frames: closed, half-open, fully open
    for (int i = 0; i < 3; ++i) {
        CreechrPose pose;
        pose.eyesClosed = (i == 0);
        pose.mouth = (i == 0) ? MouthState::Slack : MouthState::Closed;
        pose.bodyBob = (i == 0) ? 1 : 0;
        drawCreechr(p, cellRect(i, 7), pose);
    }

    // ----- row 8: grab -----
    // 4 frames: arms windup → arms forward + mouth opens → arms forward + mouth wider
    for (int i = 0; i < 4; ++i) {
        CreechrPose pose;
        pose.facingRight = true;
        switch (i) {
        case 0: // anticipation: arms back, mouth tightening
            pose.leftArm  = armSwingBack();
            pose.rightArm = armSwingBack();
            pose.mouth    = MouthState::Closed;
            break;
        case 1: // arms thrust forward, mouth opens small
            pose.leftArm  = { 6, 1 };
            pose.rightArm = { 6, 1 };
            pose.mouth    = MouthState::OpenSmall;
            break;
        case 2: // arms fully extended, mouth wide
            pose.leftArm  = armReachFwd();
            pose.rightArm = armReachFwd();
            pose.mouth    = MouthState::OpenWide;
            break;
        case 3: // hold: mouth closed on the thing, arms still forward
            pose.leftArm  = armReachFwd();
            pose.rightArm = armReachFwd();
            pose.mouth    = MouthState::Closed;
            break;
        }
        drawCreechr(p, cellRect(i, 8), pose);
    }

    // ----- row 9: bite -----
    // 4 frames: closed → wide → closed → wide. quick chomping loop.
    for (int i = 0; i < 4; ++i) {
        CreechrPose pose;
        pose.facingRight = true;
        pose.leftArm  = armReachFwd();
        pose.rightArm = armReachFwd();
        pose.mouth    = (i % 2 == 0) ? MouthState::OpenWide : MouthState::Closed;
        pose.bodyBob  = (i % 2 == 0) ? 0 : -1;
        drawCreechr(p, cellRect(i, 9), pose);
    }

    // ----- row 10: carry_right -----
    // 4-frame walk cycle but with arms held forward (carrying)
    for (int i = 0; i < 4; ++i) {
        CreechrPose pose = walkPose(i < 2 ? i : i + 2, true); // skip middle frames
        pose.leftArm  = armCarry();
        pose.rightArm = armCarry();
        pose.mouth    = MouthState::Closed;
        drawCreechr(p, cellRect(i, 10), pose);
    }
    // ----- row 11: carry_left -----
    for (int i = 0; i < 4; ++i) {
        CreechrPose pose = walkPose(i < 2 ? i : i + 2, false);
        // arms come forward in his FACING direction. for carry_left
        // that means dx is negative.
        pose.leftArm  = { -8, 3 };
        pose.rightArm = { -8, 3 };
        pose.mouth    = MouthState::Closed;
        drawCreechr(p, cellRect(i, 11), pose);
    }

    p.end();

    auto add = [this](const QString& name, std::initializer_list<QPoint> cells,
                       int durMs, bool looping)
    {
        Animation a;
        a.looping = looping;
        a.frameWidth = kCellW;
        a.frameHeight = kCellH;
        for (const QPoint& c : cells) {
            a.frames.push_back({ QRect(c.x() * kCellW, c.y() * kCellH, kCellW, kCellH), durMs });
        }
        m_anims.insert(name, a);
    };

    add("idle",       { {0,0} },                                 1000, true);
    add("walk_right", { {0,1}, {1,1}, {2,1}, {3,1}, {4,1}, {5,1} },110, true);
    add("walk_left",  { {0,2}, {1,2}, {2,2}, {3,2}, {4,2}, {5,2} },110, true);
    add("climb_up",   { {0,3}, {1,3}, {2,3}, {3,3} },             140, true);
    add("climb_down", { {0,4}, {1,4}, {2,4}, {3,4} },             140, true);
    add("hang",       { {0,5} },                                 1000, true);
    add("sleep",      { {0,6} },                                 1000, true);
    add("wake",       { {0,7}, {1,7}, {2,7} },                    180, false);
    add("grab",       { {0,8}, {1,8}, {2,8}, {3,8} },              90, false);
    add("bite",       { {0,9}, {1,9}, {2,9}, {3,9} },              80, true);
    add("carry_right",{ {0,10},{1,10},{2,10},{3,10} },            130, true);
    add("carry_left", { {0,11},{1,11},{2,11},{3,11} },            130, true);

    LOG_INFO(QStringLiteral("sprite_atlas: placeholder atlas built (%1 anims, %2x%3 cells)")
        .arg(m_anims.size()).arg(kCellW).arg(kCellH));
}

} // namespace cr
