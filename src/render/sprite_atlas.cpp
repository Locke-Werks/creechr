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
    // (smoothness pass bumped most cycles up — walks went from 6 frames
    // to 8, climbs/grab/bite/carry from 4 to 6. idle gained a 2-frame
    // breathing bob so he doesnt look like a corpse standing there.)
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
    // 2-frame breathing bob so he visibly inhales / exhales standing there
    for (int i = 0; i < 2; ++i) {
        CreechrPose pose;
        pose.bodyBob = (i == 0) ? 0 : -1;
        drawCreechr(p, cellRect(i, 0), pose);
    }

    // ----- row 1/2: walk_right / walk_left -----
    // 8-frame cycle. arms swing opposite of legs. each frame is a
    // smaller delta than the v0.1 6-frame cycle so the motion reads
    // smoother instead of in big strides.
    // phases: contact, down, passing, high, contact (other foot), ...
    auto walkPose = [](int frame, bool right) {
        CreechrPose pose;
        pose.facingRight = right;
        const int sign = right ? 1 : -1;
        // table of (left foot dx, right foot dx, body bob, swing sign)
        // swing sign: +1 = left arm fwd, -1 = right arm fwd
        struct F { int lf; int rf; int bob; int swing; };
        static const F frames[8] = {
            { -4 * 1,  3 * 1, 0, -1 }, // L contact, R back   — right arm fwd
            { -2 * 1,  2 * 1, 0, -1 }, // L mid,     R lifting
            {  0,      0,     -1, 0 }, // passing                — neutral
            {  2 * 1, -2 * 1, 0, +1 }, // L back, R mid forward — left arm fwd
            {  3 * 1, -4 * 1, 0, +1 }, // R contact, L back
            {  2 * 1, -2 * 1, 0, +1 }, // R mid
            {  0,      0,     -1, 0 }, // passing
            { -2 * 1,  2 * 1, 0, -1 }, // R back, L mid forward
        };
        const F& f = frames[frame];
        pose.leftLeg  = { f.lf * sign, 0 };
        pose.rightLeg = { f.rf * sign, 0 };
        pose.bodyBob  = f.bob;
        // arm swing: 4px forward, 3px back, scaled by sign so it
        // mirrors with facing
        const int fwd  =  4 * sign;
        const int back = -3 * sign;
        if (f.swing > 0) {       // left arm fwd, right arm back
            pose.leftArm  = { fwd,  4 };
            pose.rightArm = { back, 4 };
        } else if (f.swing < 0) { // right arm fwd, left arm back
            pose.leftArm  = { back, 4 };
            pose.rightArm = { fwd,  4 };
        } else {                  // passing — arms near sides
            pose.leftArm  = {  1 * sign, 5 };
            pose.rightArm = { -1 * sign, 5 };
        }
        return pose;
    };

    for (int i = 0; i < 8; ++i) {
        drawCreechr(p, cellRect(i, 1), walkPose(i, true));
    }
    for (int i = 0; i < 8; ++i) {
        drawCreechr(p, cellRect(i, 2), walkPose(i, false));
    }

    // ----- row 3: climb_up (6 frames) -----
    // alternating reach pattern: left arm up + right leg up, then swap.
    // 6 frames smooths the transition into 3 sub-poses per side.
    for (int i = 0; i < 6; ++i) {
        CreechrPose pose;
        pose.facingRight = true;
        const int phase = i % 6;
        // phase 0..2: left side reaching up
        // phase 3..5: right side reaching up
        const bool leftSide = (phase < 3);
        const int sub = phase % 3; // 0 reach, 1 mid, 2 about-to-swap
        pose.bodyBob = (sub == 1) ? -1 : 0;
        if (leftSide) {
            pose.leftArm  = { 0, -7 + sub };
            pose.rightArm = { 1,  3 - sub };
            pose.leftLeg  = { 0,  0 };
            pose.rightLeg = { -2 + sub, -2 };
        } else {
            pose.leftArm  = { 1,  3 - sub };
            pose.rightArm = { 0, -7 + sub };
            pose.leftLeg  = { -2 + sub, -2 };
            pose.rightLeg = { 0, 0 };
        }
        drawCreechr(p, cellRect(i, 3), pose);
    }
    // ----- row 4: climb_down (6 frames) -----
    // mirror of climb_up but with arms reaching DOWN (still pulling on
    // the wall but in the opposite direction)
    for (int i = 0; i < 6; ++i) {
        CreechrPose pose;
        pose.facingRight = true;
        const int phase = i % 6;
        const bool leftSide = (phase < 3);
        const int sub = phase % 3;
        pose.bodyBob = (sub == 1) ? 1 : 0;
        if (leftSide) {
            pose.leftArm  = { -1, 6 - sub };
            pose.rightArm = {  1, 4 + sub };
            pose.leftLeg  = {  0, 1 };
            pose.rightLeg = { -1 + sub, 1 };
        } else {
            pose.leftArm  = {  1, 4 + sub };
            pose.rightArm = { -1, 6 - sub };
            pose.leftLeg  = { -1 + sub, 1 };
            pose.rightLeg = {  0, 1 };
        }
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

    // ----- row 8: grab (6 frames) -----
    // wind-up → cock → thrust mid → thrust full + mouth opens → mouth wide → hold
    for (int i = 0; i < 6; ++i) {
        CreechrPose pose;
        pose.facingRight = true;
        switch (i) {
        case 0: // anticipation: arms slightly back, mouth tightening
            pose.leftArm  = { -2, 4 };
            pose.rightArm = { -2, 4 };
            pose.mouth    = MouthState::Closed;
            pose.bodyBob  = 1;
            break;
        case 1: // deeper windup
            pose.leftArm  = armSwingBack();
            pose.rightArm = armSwingBack();
            pose.mouth    = MouthState::Closed;
            pose.bodyBob  = 1;
            break;
        case 2: // start thrust forward, mouth opens small
            pose.leftArm  = { 4, 2 };
            pose.rightArm = { 4, 2 };
            pose.mouth    = MouthState::OpenSmall;
            break;
        case 3: // arms most of the way out
            pose.leftArm  = { 7, 1 };
            pose.rightArm = { 7, 1 };
            pose.mouth    = MouthState::OpenSmall;
            break;
        case 4: // fully extended, mouth WIDE
            pose.leftArm  = armReachFwd();
            pose.rightArm = armReachFwd();
            pose.mouth    = MouthState::OpenWide;
            break;
        case 5: // hold the catch
            pose.leftArm  = armReachFwd();
            pose.rightArm = armReachFwd();
            pose.mouth    = MouthState::Closed;
            break;
        }
        drawCreechr(p, cellRect(i, 8), pose);
    }

    // ----- row 9: bite (6 frames) -----
    // closed → wide → closed → wide → closed → wide. faster chomp with
    // a body bob each cycle so the bite reads more aggressive.
    for (int i = 0; i < 6; ++i) {
        CreechrPose pose;
        pose.facingRight = true;
        pose.leftArm  = armReachFwd();
        pose.rightArm = armReachFwd();
        const bool open = (i % 2 == 0);
        pose.mouth   = open ? MouthState::OpenWide : MouthState::Closed;
        pose.bodyBob = open ? 0 : -1;
        drawCreechr(p, cellRect(i, 9), pose);
    }

    // ----- row 10: carry_right (6 frames) -----
    // 6-frame walk cycle (subset of row 1) but arms held forward.
    // sample frames 0, 2, 3, 4, 5, 7 of the 8-frame walk for variety.
    {
        const int picks[6] = { 0, 2, 3, 4, 5, 7 };
        for (int i = 0; i < 6; ++i) {
            CreechrPose pose = walkPose(picks[i], true);
            pose.leftArm  = armCarry();
            pose.rightArm = armCarry();
            pose.mouth    = MouthState::Closed;
            drawCreechr(p, cellRect(i, 10), pose);
        }
    }
    // ----- row 11: carry_left (6 frames) -----
    {
        const int picks[6] = { 0, 2, 3, 4, 5, 7 };
        for (int i = 0; i < 6; ++i) {
            CreechrPose pose = walkPose(picks[i], false);
            // arms come forward in his FACING direction. for carry_left
            // that means dx is negative.
            pose.leftArm  = { -8, 3 };
            pose.rightArm = { -8, 3 };
            pose.mouth    = MouthState::Closed;
            drawCreechr(p, cellRect(i, 11), pose);
        }
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

    add("idle",       { {0,0}, {1,0} },                          900, true);
    add("walk_right", { {0,1},{1,1},{2,1},{3,1},{4,1},{5,1},{6,1},{7,1} }, 80, true);
    add("walk_left",  { {0,2},{1,2},{2,2},{3,2},{4,2},{5,2},{6,2},{7,2} }, 80, true);
    add("climb_up",   { {0,3},{1,3},{2,3},{3,3},{4,3},{5,3} },   110, true);
    add("climb_down", { {0,4},{1,4},{2,4},{3,4},{4,4},{5,4} },   110, true);
    add("hang",       { {0,5} },                                1000, true);
    add("sleep",      { {0,6} },                                1000, true);
    add("wake",       { {0,7},{1,7},{2,7} },                     180, false);
    add("grab",       { {0,8},{1,8},{2,8},{3,8},{4,8},{5,8} },    75, false);
    add("bite",       { {0,9},{1,9},{2,9},{3,9},{4,9},{5,9} },    70, true);
    add("carry_right",{ {0,10},{1,10},{2,10},{3,10},{4,10},{5,10} },95, true);
    add("carry_left", { {0,11},{1,11},{2,11},{3,11},{4,11},{5,11} },95, true);

    LOG_INFO(QStringLiteral("sprite_atlas: placeholder atlas built (%1 anims, %2x%3 cells)")
        .arg(m_anims.size()).arg(kCellW).arg(kCellH));
}

} // namespace cr
