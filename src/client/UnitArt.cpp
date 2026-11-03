// Pictures of the units, drawn in code: one small figure per kind of unit (a
// spearman, an archer, a skeleton, a dragon, ...), chosen from the unit id. Each is
// drawn into a square; art::unitIcon caches them as scaled sprites.

#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <tuple>

#include "client/Art.h"

namespace kot::client::art {

namespace {

const QColor kInk(25, 16, 8);
const QColor kSkin("#f0c8a0");
}  // namespace
const QColor kSkinTone("#f0c8a0");
namespace {
const QColor kSteel("#c8ccd8");
const QColor kWood("#8a5a2b");
const QColor kBone("#ece4cc");

struct Canvas {
    QPainter& p;
    QRectF r;
    QPointF at(double x, double y) const { return {r.left() + r.width() * x, r.top() + r.height() * y}; }
    QRectF box(double x, double y, double w, double h) const {
        return {r.left() + r.width() * x, r.top() + r.height() * y, r.width() * w, r.height() * h};
    }
    double s(double f) const { return r.width() * f; }
    void pen(const QColor& c, double w) { p.setPen(QPen(c, s(w), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); }
    void line(double x1, double y1, double x2, double y2) { p.drawLine(at(x1, y1), at(x2, y2)); }
    void poly(std::initializer_list<std::pair<double, double>> pts) {
        QPolygonF poly;
        for (const auto& [x, y] : pts) poly << at(x, y);
        p.drawPolygon(poly);
    }
};

// Light from the top left: a gradient from lighter to darker across a box.
QLinearGradient lit(const Canvas& c, double x, double y, double w, double h, const QColor& base, int light = 125, int dark = 125) {
    QLinearGradient g(c.at(x, y), c.at(x + w, y + h));
    g.setColorAt(0, base.lighter(light));
    g.setColorAt(0.5, base);
    g.setColorAt(1, base.darker(dark));
    return g;
}

// Eyes with a glint, brows and a small mouth, for faces about `size` wide.
void faceDetails(Canvas& c, double cx, double cy, double size, const QColor& eye = QColor(25, 16, 8), bool mouth = true) {
    c.p.setPen(Qt::NoPen);
    for (const double dx : {-0.2, 0.2}) {
        c.p.setBrush(QColor(250, 248, 240));
        c.p.drawEllipse(QPointF(c.at(cx + dx * size, cy)), c.s(size * 0.11), c.s(size * 0.085));
        c.p.setBrush(eye);
        c.p.drawEllipse(QPointF(c.at(cx + dx * size + size * 0.02, cy)), c.s(size * 0.065), c.s(size * 0.07));
        c.p.setBrush(QColor(255, 255, 255, 220));
        c.p.drawEllipse(QPointF(c.at(cx + dx * size + size * 0.035, cy - size * 0.03)), c.s(size * 0.022), c.s(size * 0.022));
    }
    c.pen(QColor(40, 25, 12, 200), size * 0.06);
    for (const double dx : {-0.2, 0.2}) c.line(cx + dx * size - size * 0.1, cy - size * 0.16, cx + dx * size + size * 0.1, cy - size * 0.19);
    if (mouth) {
        c.pen(QColor(110, 40, 30), size * 0.05);
        c.p.setBrush(Qt::NoBrush);
        c.p.drawArc(QRectF(c.at(cx - size * 0.12, cy + size * 0.18), c.at(cx + size * 0.12, cy + size * 0.32)), 200 * 16, 140 * 16);
    }
}

void wings(Canvas& c, const QColor& colour);
void flames(Canvas& c, double x, double y, double size);

// A four-legged beast seen from the side, facing right: a dog shape (wolves,
// hellhounds) or a bear. `heads` > 1 fans extra heads out above the first.
void quadruped(Canvas& c, const QColor& fur, bool bear, int heads = 1, const QColor& eye = QColor("#ffd84a")) {
    c.pen(kInk, 0.018);
    const auto leg = [&](double x, const QColor& col) {
        const double w = bear ? 0.05 : 0.035;
        c.p.setBrush(lit(c, x - w, 0.6, 2 * w, 0.3, col));
        c.poly({{x - w, 0.6}, {x + w, 0.6}, {x + w * 0.8, 0.8}, {x + w * 1.3, 0.9}, {x - w * 0.7, 0.9}, {x - w * 0.6, 0.8}});
        c.pen(kInk, 0.01);
        for (int k = 0; k < 3; ++k) c.line(x - w * 0.3 + k * w * 0.5, 0.9, x - w * 0.2 + k * w * 0.5, 0.92);  // claws
        c.pen(kInk, 0.018);
    };
    leg(0.25, fur.darker(130));
    leg(0.62, fur.darker(130));
    if (!bear) {
        QPainterPath tail;
        tail.moveTo(c.at(0.18, 0.5));
        tail.cubicTo(c.at(0.06, 0.48), c.at(0.0, 0.36), c.at(0.02, 0.28));
        tail.cubicTo(c.at(0.08, 0.36), c.at(0.12, 0.44), c.at(0.2, 0.46));
        c.p.setBrush(lit(c, 0.0, 0.28, 0.2, 0.24, fur.darker(110)));
        c.p.drawPath(tail);
    }
    QPainterPath body;
    const double top = bear ? 0.32 : 0.4;
    body.moveTo(c.at(0.14, 0.52));
    body.cubicTo(c.at(0.16, top), c.at(0.5, top - 0.02), c.at(0.7, top + 0.02));
    body.cubicTo(c.at(0.8, top + 0.06), c.at(0.78, 0.64), c.at(0.66, 0.65));
    body.cubicTo(c.at(0.5, 0.67), c.at(0.3, 0.66), c.at(0.18, 0.63));
    body.closeSubpath();
    c.p.setBrush(lit(c, 0.14, top, 0.64, 0.66 - top, fur, 130, 130));
    c.p.drawPath(body);
    c.pen(fur.darker(140), 0.008);  // a few strokes of fur
    for (int k = 0; k < 6; ++k) {
        const double fx = 0.22 + k * 0.08;
        c.line(fx, top + 0.06 + (k % 2) * 0.03, fx + 0.03, top + 0.1 + (k % 2) * 0.03);
    }
    c.pen(kInk, 0.018);
    c.p.setBrush(fur.lighter(135));  // pale belly
    c.p.drawChord(c.box(0.3, 0.53, 0.36, 0.14), 180 * 16, 180 * 16);
    leg(0.31, fur);
    leg(0.69, fur);
    for (int h = heads - 1; h >= 0; --h) {
        const double dx = heads == 1 ? 0 : (h - (heads - 1) / 2.0) * 0.1, dy = heads == 1 ? 0 : -std::abs(h - (heads - 1) / 2.0) * -0.06 - 0.08 * (h == 1);
        const auto X = [&](double v) { return v + dx; };
        const auto Y = [&](double v) { return v + dy; };
        QPainterPath head;
        if (bear) {
            head.addEllipse(QRectF(c.at(X(0.66), Y(0.24)), c.at(X(0.9), Y(0.46))));
            head.addEllipse(QRectF(c.at(X(0.84), Y(0.33)), c.at(X(0.98), Y(0.44))));  // snout
        } else {
            head.moveTo(c.at(X(0.66), Y(0.46)));
            head.cubicTo(c.at(X(0.66), Y(0.3)), c.at(X(0.76), Y(0.26)), c.at(X(0.82), Y(0.28)));
            head.lineTo(c.at(X(0.97), Y(0.36)));
            head.lineTo(c.at(X(0.96), Y(0.42)));
            head.cubicTo(c.at(X(0.88), Y(0.46)), c.at(X(0.78), Y(0.5)), c.at(X(0.66), Y(0.46)));
        }
        c.p.setBrush(lit(c, X(0.66), Y(0.24), 0.32, 0.24, fur, 125, 125));
        c.p.drawPath(head.simplified());
        if (bear) {
            c.p.drawEllipse(c.box(X(0.68), Y(0.21), 0.07, 0.07));  // round ears
            c.p.drawEllipse(c.box(X(0.78), Y(0.2), 0.07, 0.07));
        } else {
            c.poly({{X(0.72), Y(0.3)}, {X(0.72), Y(0.16)}, {X(0.79), Y(0.27)}});
            c.poly({{X(0.78), Y(0.28)}, {X(0.81), Y(0.15)}, {X(0.84), Y(0.29)}});
        }
        c.p.setPen(Qt::NoPen);
        c.p.setBrush(eye);
        c.p.drawEllipse(c.box(X(0.8), Y(0.31), 0.035, 0.03));
        c.p.setBrush(QColor(20, 12, 8));
        c.p.drawEllipse(c.box(X(0.95), Y(0.35), 0.025, 0.025));  // nose
        if (!bear) {
            c.pen(QColor(250, 250, 240), 0.01);
            c.line(X(0.88), Y(0.42), X(0.89), Y(0.45));  // a fang
        }
        c.pen(kInk, 0.018);
    }
}

// A dragon: bat wings with finger bones, a long neck, horns, a spiked tail.
// `bones` draws a skeletal one (ragged wings, ribs); `breath` adds fire.
void wyrm(Canvas& c, const QColor& scales, bool bones, bool breath) {
    c.pen(kInk, 0.018);
    // Wings behind: membrane between finger bones.
    for (const double side : {-1.0, 1.0}) {
        const auto X = [&](double f) { return 0.46 + side * f; };
        QPainterPath mem;
        mem.moveTo(c.at(X(0.04), 0.42));
        mem.lineTo(c.at(X(0.26), 0.06));
        mem.quadTo(c.at(X(0.34), 0.2), c.at(X(0.44), 0.18));
        mem.quadTo(c.at(X(0.4), 0.3), c.at(X(0.46), 0.36));
        mem.quadTo(c.at(X(0.32), 0.4), c.at(X(0.3), 0.52));
        mem.quadTo(c.at(X(0.16), 0.46), c.at(X(0.04), 0.52));
        mem.closeSubpath();
        QColor m = bones ? QColor(scales.red(), scales.green(), scales.blue(), 110) : scales.darker(118);
        c.p.setBrush(lit(c, 0.0, 0.06, 1.0, 0.46, m, 125, 125));
        c.p.drawPath(mem);
        c.pen(bones ? kBone : scales.darker(160), 0.018);
        c.line(X(0.04), 0.42, X(0.26), 0.06);
        c.line(X(0.26), 0.06, X(0.44), 0.18);
        c.line(X(0.26), 0.06, X(0.46), 0.36);
        c.line(X(0.26), 0.06, X(0.3), 0.52);
        c.pen(kInk, 0.018);
    }
    // The tail curling to the left with a spade tip.
    QPainterPath tail;
    tail.moveTo(c.at(0.36, 0.7));
    tail.cubicTo(c.at(0.2, 0.82), c.at(0.08, 0.78), c.at(0.06, 0.66));
    tail.lineTo(c.at(0.1, 0.66));
    tail.cubicTo(c.at(0.14, 0.74), c.at(0.24, 0.74), c.at(0.38, 0.62));
    tail.closeSubpath();
    c.p.setBrush(lit(c, 0.06, 0.62, 0.32, 0.2, bones ? kBone : scales));
    c.p.drawPath(tail);
    c.poly({{0.06, 0.66}, {0.0, 0.58}, {0.1, 0.62}, {0.12, 0.66}});
    // Legs with claws.
    for (const double lx : {0.38, 0.56}) {
        c.p.setBrush(lit(c, lx - 0.04, 0.64, 0.1, 0.28, bones ? kBone : scales.darker(110)));
        c.poly({{lx - 0.04, 0.64}, {lx + 0.05, 0.64}, {lx + 0.04, 0.82}, {lx + 0.08, 0.92}, {lx - 0.04, 0.92}, {lx - 0.02, 0.82}});
    }
    if (bones) {
        // A ribcage and spine.
        c.pen(kBone, 0.03);
        c.line(0.34, 0.5, 0.6, 0.46);
        c.pen(kBone, 0.018);
        for (int k = 0; k < 5; ++k) {
            const double rx = 0.36 + k * 0.05;
            c.p.setBrush(Qt::NoBrush);
            c.p.drawArc(c.box(rx - 0.03, 0.47, 0.06, 0.2), 180 * 16, 180 * 16);
        }
        c.pen(kInk, 0.018);
    } else {
        QPainterPath body;
        body.addEllipse(QRectF(c.at(0.3, 0.44), c.at(0.66, 0.72)));
        c.p.setBrush(lit(c, 0.3, 0.44, 0.36, 0.28, scales, 135, 130));
        c.p.drawPath(body);
        c.p.setBrush(lit(c, 0.4, 0.54, 0.18, 0.16, QColor("#e8d8a0")));  // the belly plates
        c.p.drawChord(c.box(0.38, 0.5, 0.22, 0.22), 200 * 16, 140 * 16);
        c.pen(scales.darker(150), 0.008);
        for (int k = 0; k < 4; ++k) c.p.drawArc(c.box(0.34 + k * 0.07, 0.48, 0.07, 0.05), 0, 180 * 16);  // scales
        c.pen(kInk, 0.018);
    }
    // The neck rising to the right and the head.
    QPainterPath neck;
    neck.moveTo(c.at(0.56, 0.5));
    neck.cubicTo(c.at(0.66, 0.42), c.at(0.62, 0.28), c.at(0.7, 0.2));
    neck.lineTo(c.at(0.78, 0.24));
    neck.cubicTo(c.at(0.7, 0.32), c.at(0.74, 0.46), c.at(0.64, 0.56));
    neck.closeSubpath();
    c.p.setBrush(lit(c, 0.56, 0.2, 0.22, 0.36, bones ? kBone : scales));
    c.p.drawPath(neck);
    QPainterPath head;
    head.moveTo(c.at(0.68, 0.2));
    head.cubicTo(c.at(0.72, 0.1), c.at(0.82, 0.1), c.at(0.94, 0.18));
    head.lineTo(c.at(0.96, 0.24));
    head.lineTo(c.at(0.84, 0.26));
    head.lineTo(c.at(0.88, 0.3));
    head.cubicTo(c.at(0.8, 0.32), c.at(0.72, 0.3), c.at(0.68, 0.2));
    c.p.setBrush(lit(c, 0.68, 0.1, 0.28, 0.22, bones ? kBone : scales, 125, 125));
    c.p.drawPath(head);
    c.p.setBrush(lit(c, 0.66, 0.02, 0.1, 0.12, QColor("#ece4cc")));  // horns
    c.poly({{0.72, 0.13}, {0.64, 0.02}, {0.76, 0.11}});
    c.poly({{0.77, 0.12}, {0.74, 0.0}, {0.8, 0.11}});
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(bones ? QColor("#60ffc0") : QColor("#ffe040"));
    c.p.drawEllipse(c.box(0.8, 0.15, 0.04, 0.03));
    if (breath) {
        for (int k = 0; k < 3; ++k) {
            QRadialGradient fire(c.at(0.97 + k * 0.0, 0.28 + k * 0.04), c.s(0.08 - k * 0.015));
            fire.setColorAt(0, QColor(255, 240, 120, 230));
            fire.setColorAt(1, QColor(240, 80, 20, 0));
            c.p.setBrush(fire);
            c.p.drawEllipse(QPointF(c.at(0.97, 0.28 + k * 0.04)), c.s(0.08 - k * 0.015), c.s(0.06 - k * 0.01));
        }
    }
}

// A standing figure: legs, body in `coat`, head; optional helmet.
void figure(Canvas& c, const QColor& coat, bool helmet, double x = 0.5, const QColor& kSkin = art::kSkinTone) {
    const QColor trousers = coat.darker(160), boots("#4a3020");
    c.pen(kInk, 0.022);
    // Legs and boots.
    for (const double side : {-1.0, 1.0}) {
        const double lx = x + side * 0.065 - 0.045;
        c.p.setBrush(lit(c, lx, 0.64, 0.09, 0.22, trousers));
        c.p.drawRoundedRect(c.box(lx, 0.64, 0.09, 0.23), c.s(0.03), c.s(0.03));
        c.p.setBrush(lit(c, lx - 0.01, 0.84, 0.12, 0.09, boots, 140));
        c.p.drawRoundedRect(c.box(lx - 0.012 + (side > 0 ? 0.01 : 0), 0.84, 0.11, 0.09), c.s(0.03), c.s(0.03));
    }
    // The back arm, hanging, with its hand.
    c.p.setBrush(lit(c, x - 0.22, 0.38, 0.09, 0.26, coat.darker(115)));
    c.p.drawRoundedRect(c.box(x - 0.215, 0.38, 0.085, 0.25), c.s(0.04), c.s(0.04));
    c.p.setBrush(lit(c, x - 0.22, 0.6, 0.08, 0.08, kSkin));
    c.p.drawEllipse(c.box(x - 0.215, 0.6, 0.075, 0.075));
    // The tunic: shoulders flaring to a skirt.
    QPainterPath tunic;
    tunic.moveTo(c.at(x - 0.14, 0.37));
    tunic.quadTo(c.at(x, 0.33), c.at(x + 0.14, 0.37));
    tunic.lineTo(c.at(x + 0.17, 0.7));
    tunic.quadTo(c.at(x, 0.74), c.at(x - 0.17, 0.7));
    tunic.closeSubpath();
    c.p.setBrush(lit(c, x - 0.17, 0.33, 0.34, 0.41, coat, 135, 130));
    c.p.drawPath(tunic);
    c.pen(coat.darker(140), 0.012);  // a seam and the hem
    c.line(x, 0.4, x, 0.7);
    c.p.setBrush(Qt::NoBrush);
    c.p.drawArc(QRectF(c.at(x - 0.165, 0.66), c.at(x + 0.165, 0.74)), 200 * 16, 140 * 16);
    // Collar, belt and buckle.
    c.pen(kInk, 0.015);
    c.p.setBrush(coat.lighter(130));
    c.poly({{x - 0.07, 0.355}, {x, 0.43}, {x + 0.07, 0.355}, {x, 0.38}});
    c.p.setBrush(QColor("#5a3a20"));
    c.p.drawRect(c.box(x - 0.155, 0.55, 0.31, 0.045));
    c.p.setBrush(QColor("#e8c04a"));
    c.p.drawRect(c.box(x - 0.025, 0.548, 0.05, 0.05));
    // The front arm reaching forward to hold a weapon at about x + 0.24.
    c.pen(kInk, 0.022);
    QPainterPath arm;
    arm.moveTo(c.at(x + 0.09, 0.38));
    arm.lineTo(c.at(x + 0.17, 0.37));
    arm.lineTo(c.at(x + 0.26, 0.5));
    arm.lineTo(c.at(x + 0.2, 0.55));
    arm.closeSubpath();
    c.p.setBrush(lit(c, x + 0.09, 0.37, 0.17, 0.18, coat.darker(105)));
    c.p.drawPath(arm);
    c.p.setBrush(lit(c, x + 0.2, 0.48, 0.09, 0.09, kSkin));
    c.p.drawEllipse(c.box(x + 0.195, 0.485, 0.085, 0.08));
    // Neck and head.
    c.p.setBrush(kSkin.darker(110));
    c.p.drawRect(c.box(x - 0.035, 0.31, 0.07, 0.06));
    c.p.setBrush(lit(c, x - 0.1, 0.13, 0.2, 0.22, kSkin, 115, 120));
    c.p.drawEllipse(c.box(x - 0.1, 0.13, 0.2, 0.22));
    c.p.setBrush(kSkin.darker(112));  // an ear
    c.p.drawEllipse(c.box(x - 0.115, 0.22, 0.04, 0.06));
    faceDetails(c, x + 0.01, 0.235, 0.2);
    if (helmet) {
        c.pen(kInk, 0.02);
        c.p.setBrush(lit(c, x - 0.12, 0.1, 0.24, 0.12, kSteel, 130, 130));
        c.p.drawChord(c.box(x - 0.12, 0.1, 0.24, 0.2), 0, 180 * 16);
        c.p.setBrush(kSteel.darker(115));
        c.p.drawRect(c.box(x - 0.125, 0.19, 0.25, 0.03));  // the rim
        c.p.drawRect(c.box(x - 0.012, 0.2, 0.024, 0.07));  // the nose guard
        c.pen(QColor(255, 255, 255, 170), 0.015);
        c.p.setBrush(Qt::NoBrush);
        c.p.drawArc(c.box(x - 0.08, 0.12, 0.12, 0.12), 100 * 16, 60 * 16);  // a glint
    } else {
        c.pen(kInk, 0.018);
        c.p.setBrush(lit(c, x - 0.11, 0.11, 0.22, 0.1, QColor("#5a3a1e")));
        QPainterPath hair;
        hair.moveTo(c.at(x - 0.105, 0.22));
        hair.quadTo(c.at(x - 0.11, 0.1), c.at(x, 0.115));
        hair.quadTo(c.at(x + 0.11, 0.1), c.at(x + 0.105, 0.2));
        hair.quadTo(c.at(x + 0.04, 0.16), c.at(x - 0.02, 0.17));
        hair.quadTo(c.at(x - 0.07, 0.18), c.at(x - 0.105, 0.22));
        c.p.drawPath(hair);
    }
}

void spearman(Canvas& c, bool pike) {
    figure(c, pike ? QColor("#3a64b0") : QColor("#8a6a3a"), pike);
    c.pen(kWood.darker(130), 0.04);
    c.line(0.75, pike ? 0.02 : 0.15, 0.75, 0.95);
    c.pen(kInk, 0.025);
    c.p.setBrush(kSteel);
    c.poly({{0.75, pike ? -0.06 : 0.05}, {0.81, pike ? 0.08 : 0.18}, {0.69, pike ? 0.08 : 0.18}});
    c.p.setBrush(kSkin);  // hand on the shaft
    c.p.drawEllipse(c.box(0.69, 0.48, 0.1, 0.1));
}

void archer(Canvas& c, const QColor& coat, int rank, bool elf) {
    figure(c, coat, rank >= 1 && !elf, 0.42);
    if (elf) {  // a pointed hood
        c.pen(kInk, 0.025);
        c.p.setBrush(coat.darker(115));
        c.poly({{0.3, 0.26}, {0.42, 0.04}, {0.54, 0.26}});
    }
    // The bow and an arrow.
    c.pen(kWood.darker(110), 0.045);
    c.p.setBrush(Qt::NoBrush);
    c.p.drawArc(c.box(0.52, 0.18, 0.34, 0.62), -80 * 16, 160 * 16);
    c.pen(QColor("#e8e0d0"), 0.015);
    c.line(0.69, 0.2, 0.69, 0.78);
    c.pen(kInk, 0.02);
    c.line(0.5, 0.49, 0.95, 0.49);
    c.p.setBrush(kSteel);
    c.poly({{0.98, 0.49}, {0.9, 0.45}, {0.9, 0.53}});
    if (rank >= 2) {  // elite: a gold sash
        c.pen(QColor("#ffd84a"), 0.04);
        c.line(0.3, 0.38, 0.54, 0.66);
    }
}

void skull(Canvas& c, double x, double y, double size) {
    c.pen(kInk, 0.025);
    c.p.setBrush(kBone);
    c.p.drawEllipse(c.box(x - size / 2, y - size / 2, size, size * 0.9));
    c.p.drawRect(c.box(x - size * 0.25, y + size * 0.3, size * 0.5, size * 0.25));
    c.p.setBrush(kInk);
    c.p.drawEllipse(c.box(x - size * 0.3, y - size * 0.12, size * 0.22, size * 0.24));
    c.p.drawEllipse(c.box(x + size * 0.08, y - size * 0.12, size * 0.22, size * 0.24));
}

void skull_(Canvas& c) { skull(c, 0.5, 0.22, 0.24); }

void skeleton(Canvas& c, bool warrior) {
    c.pen(kBone, 0.05);
    c.line(0.5, 0.36, 0.5, 0.66);  // spine
    c.line(0.5, 0.66, 0.38, 0.94);
    c.line(0.5, 0.66, 0.62, 0.94);
    c.line(0.32, 0.42, 0.68, 0.42);  // shoulders
    c.pen(kBone, 0.03);
    for (const double y : {0.5, 0.57}) c.line(0.4, y, 0.6, y);  // ribs
    c.pen(kBone, 0.04);
    c.line(0.32, 0.42, 0.26, 0.62);
    c.line(0.68, 0.42, 0.78, 0.3);
    skull(c, 0.5, 0.24, 0.24);
    // A rusty sword; warriors carry a shield too.
    c.pen(kInk, 0.02);
    c.p.setBrush(QColor("#a89c84"));
    c.poly({{0.78, 0.3}, {0.84, 0.0}, {0.88, 0.02}, {0.82, 0.32}});
    if (warrior) {
        c.p.setBrush(QColor("#6a3020"));
        c.p.drawEllipse(c.box(0.12, 0.46, 0.28, 0.34));
        c.p.setBrush(QColor("#a89c84"));
        c.p.drawEllipse(c.box(0.22, 0.58, 0.08, 0.1));
    }
}

void zombie(Canvas& c) {
    const QColor rot("#9ab878");
    figure(c, QColor("#5a6a4a"), false, 0.5, rot);
    c.pen(kInk, 0.02);
    c.p.setBrush(QColor("#5a6a4a").darker(130));  // a torn hem
    c.poly({{0.34, 0.66}, {0.38, 0.76}, {0.42, 0.68}, {0.47, 0.78}, {0.52, 0.68}});
    // Arms stretched forward, hands hanging.
    c.p.setBrush(lit(c, 0.55, 0.38, 0.35, 0.1, rot.darker(110)));
    c.p.drawRoundedRect(c.box(0.56, 0.39, 0.32, 0.07), c.s(0.03), c.s(0.03));
    c.p.drawRoundedRect(c.box(0.54, 0.47, 0.32, 0.07), c.s(0.03), c.s(0.03));
    c.p.setBrush(rot);
    c.p.drawEllipse(c.box(0.86, 0.38, 0.07, 0.08));
    c.p.drawEllipse(c.box(0.84, 0.46, 0.07, 0.08));
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(QColor(60, 80, 40, 160));  // rot spots
    c.p.drawEllipse(c.box(0.42, 0.18, 0.04, 0.03));
    c.p.drawEllipse(c.box(0.6, 0.4, 0.05, 0.03));
}

void wraith(Canvas& c) {
    // A hooded, tattered ghost with clawed hands and cold eyes.
    QPainterPath ghost;
    ghost.moveTo(c.at(0.22, 0.95));
    ghost.cubicTo(c.at(0.14, 0.6), c.at(0.2, 0.12), c.at(0.5, 0.08));
    ghost.cubicTo(c.at(0.8, 0.12), c.at(0.86, 0.6), c.at(0.78, 0.95));
    for (int i = 0; i < 5; ++i) ghost.lineTo(c.at(0.78 - 0.112 * (i + 0.5), i % 2 ? 0.98 : 0.82)), ghost.lineTo(c.at(0.78 - 0.112 * (i + 1), 0.95));
    QLinearGradient g(c.at(0.5, 0.08), c.at(0.5, 0.98));
    g.setColorAt(0, QColor(225, 232, 255, 245));
    g.setColorAt(0.6, QColor(150, 160, 210, 190));
    g.setColorAt(1, QColor(110, 120, 190, 60));
    c.pen(QColor(40, 40, 80, 200), 0.018);
    c.p.setBrush(g);
    c.p.drawPath(ghost);
    c.p.setBrush(QColor(20, 20, 40, 220));  // the dark inside of the hood
    c.p.drawEllipse(c.box(0.36, 0.18, 0.28, 0.24));
    c.p.setPen(Qt::NoPen);
    for (const double ex : {0.43, 0.57}) {
        QRadialGradient eyeGlow(c.at(ex, 0.29), c.s(0.06));
        eyeGlow.setColorAt(0, QColor(120, 230, 255, 255));
        eyeGlow.setColorAt(1, QColor(60, 160, 255, 0));
        c.p.setBrush(eyeGlow);
        c.p.drawEllipse(QPointF(c.at(ex, 0.29)), c.s(0.06), c.s(0.05));
    }
    c.pen(QColor(200, 210, 255, 220), 0.02);  // clawed hands reaching out
    for (const double side : {-1.0, 1.0}) {
        const double hx = 0.5 + side * 0.32;
        for (int k = 0; k < 3; ++k) c.line(hx, 0.5, hx + side * 0.06, 0.44 + k * 0.05);
    }
}

void dragon(Canvas& c, const QColor& bone = kBone) {
    // Bone and ghost dragons are skeletal; the others have scales.
    const bool skeletal = bone == kBone || bone == QColor("#a8d8e8");
    wyrm(c, bone, skeletal, false);
}

void eagle(Canvas& c) {
    wings(c, QColor("#7a5a3a"));
    c.pen(kInk, 0.02);
    // An armoured body, white head with a hooked beak, talons.
    c.p.setBrush(lit(c, 0.36, 0.36, 0.28, 0.44, kSteel, 135, 130));
    c.p.drawEllipse(c.box(0.36, 0.36, 0.28, 0.44));
    c.pen(kSteel.darker(140), 0.01);
    for (int k = 0; k < 3; ++k) c.p.drawArc(c.box(0.38, 0.44 + k * 0.08, 0.24, 0.08), 200 * 16, 140 * 16);
    c.pen(kInk, 0.02);
    c.p.setBrush(lit(c, 0.4, 0.14, 0.22, 0.24, QColor("#f4f0e8"), 115, 120));
    c.p.drawEllipse(c.box(0.4, 0.15, 0.2, 0.22));
    c.p.setBrush(lit(c, 0.5, 0.24, 0.12, 0.1, QColor("#ffc020")));
    QPainterPath beak;
    beak.moveTo(c.at(0.54, 0.25));
    beak.cubicTo(c.at(0.64, 0.24), c.at(0.66, 0.3), c.at(0.6, 0.34));
    beak.lineTo(c.at(0.55, 0.31));
    beak.closeSubpath();
    c.p.drawPath(beak);
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(kInk);
    c.p.drawEllipse(c.box(0.505, 0.22, 0.035, 0.035));
    c.pen(QColor("#e8b020"), 0.025);
    for (const double tx : {0.44, 0.56}) {
        c.line(tx, 0.78, tx, 0.88);
        c.line(tx, 0.88, tx - 0.03, 0.92);
        c.line(tx, 0.88, tx + 0.03, 0.92);
    }
}

void snail(Canvas& c) {
    c.pen(kInk, 0.025);
    c.p.setBrush(QColor("#b0a070"));
    QPainterPath body;
    body.moveTo(c.at(0.05, 0.88));
    body.lineTo(c.at(0.95, 0.88));
    body.quadTo(c.at(0.95, 0.6), c.at(0.82, 0.55));
    body.lineTo(c.at(0.8, 0.7));
    body.lineTo(c.at(0.1, 0.75));
    body.closeSubpath();
    c.p.drawPath(body);
    c.pen(kInk, 0.025);
    c.line(0.86, 0.56, 0.84, 0.36);
    c.line(0.9, 0.58, 0.94, 0.38);
    c.p.setBrush(kSteel);  // an armoured shell with a spike
    c.p.drawEllipse(c.box(0.18, 0.3, 0.52, 0.5));
    c.p.setBrush(Qt::NoBrush);
    c.p.drawEllipse(c.box(0.3, 0.42, 0.28, 0.26));
    c.p.setBrush(QColor("#808898"));
    c.poly({{0.44, 0.3}, {0.48, 0.12}, {0.52, 0.3}});
}

void treant(Canvas& c) {
    const QColor bark("#6a4a28");
    c.pen(kInk, 0.022);
    // Root feet, the trunk body, branch arms.
    c.p.setBrush(lit(c, 0.3, 0.85, 0.4, 0.1, bark.darker(110)));
    c.poly({{0.3, 0.95}, {0.36, 0.84}, {0.44, 0.86}, {0.5, 0.82}, {0.56, 0.86}, {0.64, 0.84}, {0.7, 0.95}});
    QPainterPath trunk;
    trunk.moveTo(c.at(0.37, 0.88));
    trunk.cubicTo(c.at(0.36, 0.7), c.at(0.38, 0.52), c.at(0.4, 0.42));
    trunk.lineTo(c.at(0.6, 0.42));
    trunk.cubicTo(c.at(0.62, 0.52), c.at(0.64, 0.7), c.at(0.63, 0.88));
    trunk.closeSubpath();
    c.p.setBrush(lit(c, 0.36, 0.42, 0.28, 0.46, bark, 130, 130));
    c.p.drawPath(trunk);
    c.pen(bark.darker(150), 0.012);  // bark grooves
    for (const double gx : {0.43, 0.5, 0.57}) {
        QPainterPath g;
        g.moveTo(c.at(gx, 0.46));
        g.cubicTo(c.at(gx - 0.02, 0.6), c.at(gx + 0.02, 0.72), c.at(gx, 0.86));
        c.p.setBrush(Qt::NoBrush);
        c.p.drawPath(g);
    }
    c.pen(bark, 0.055);
    c.line(0.41, 0.52, 0.2, 0.4);
    c.line(0.2, 0.4, 0.1, 0.44);
    c.line(0.2, 0.4, 0.16, 0.3);
    c.line(0.59, 0.52, 0.82, 0.42);
    c.line(0.82, 0.42, 0.92, 0.46);
    c.line(0.82, 0.42, 0.86, 0.32);
    // A face in the bark: glowing eyes under a frown, a knot mouth.
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(QColor("#ffe060"));
    c.p.drawEllipse(c.box(0.43, 0.55, 0.05, 0.04));
    c.p.drawEllipse(c.box(0.52, 0.55, 0.05, 0.04));
    c.pen(bark.darker(170), 0.02);
    c.line(0.42, 0.53, 0.48, 0.55);
    c.line(0.58, 0.53, 0.52, 0.55);
    c.p.setBrush(bark.darker(170));
    c.p.drawEllipse(c.box(0.47, 0.64, 0.06, 0.04));
    // The crown: clusters of leaves, dark behind, light in front.
    c.pen(kInk, 0.018);
    const QColor leaf("#3e8a3a");
    for (const auto& [x, y, r, k] : {std::tuple{0.22, 0.2, 0.17, 0}, std::tuple{0.5, 0.1, 0.2, 0}, std::tuple{0.78, 0.2, 0.17, 0},
                                     std::tuple{0.34, 0.3, 0.17, 1}, std::tuple{0.62, 0.3, 0.17, 1}, std::tuple{0.5, 0.24, 0.15, 2}}) {
        const QColor col = k == 0 ? leaf.darker(115) : k == 1 ? leaf : leaf.lighter(118);
        c.p.setBrush(lit(c, x - r, y - r, 2 * r, 2 * r, col, 130, 125));
        c.p.drawEllipse(QPointF(c.at(x, y)), c.s(r), c.s(r * 0.85));
    }
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(QColor(255, 255, 200, 70));  // sun on the leaves
    c.p.drawEllipse(c.box(0.36, 0.05, 0.16, 0.08));
}

void horseBody(Canvas& c, const QColor& coat, bool head);

void unicorn(Canvas& c) {
    horseBody(c, QColor("#f6f2fa"), true);
    c.pen(kInk, 0.018);
    QLinearGradient horn(c.at(0.86, 0.17), c.at(0.95, -0.02));
    horn.setColorAt(0, QColor("#ffd84a"));
    horn.setColorAt(1, QColor("#fff6c0"));
    c.p.setBrush(horn);
    c.poly({{0.84, 0.16}, {0.95, -0.03}, {0.88, 0.18}});
    c.pen(QColor("#c89a20"), 0.008);
    for (int i = 1; i < 4; ++i) c.line(0.84 + i * 0.025, 0.16 - i * 0.045, 0.86 + i * 0.022, 0.17 - i * 0.04);
    c.pen(kInk, 0.015);
    c.p.setBrush(lit(c, 0.62, 0.14, 0.2, 0.36, QColor("#c080f0")));  // a long violet mane
    QPainterPath mane;
    mane.moveTo(c.at(0.82, 0.15));
    mane.cubicTo(c.at(0.72, 0.22), c.at(0.62, 0.36), c.at(0.6, 0.52));
    mane.lineTo(c.at(0.68, 0.48));
    mane.cubicTo(c.at(0.72, 0.36), c.at(0.78, 0.26), c.at(0.85, 0.19));
    mane.closeSubpath();
    c.p.drawPath(mane);
}

void woodsman(Canvas& c) {
    figure(c, QColor("#4a7a3a"), false, 0.42);
    c.pen(kWood.darker(120), 0.045);
    c.line(0.62, 0.9, 0.78, 0.2);
    c.pen(kInk, 0.025);
    c.p.setBrush(kSteel);
    c.poly({{0.74, 0.18}, {0.92, 0.12}, {0.94, 0.34}, {0.76, 0.32}});
}

void catapult(Canvas& c) {
    // Wheels, a frame, the throwing arm with a stone in its cup.
    c.pen(kInk, 0.025);
    c.p.setBrush(kWood);
    c.poly({{0.1, 0.72}, {0.9, 0.72}, {0.86, 0.8}, {0.14, 0.8}});
    c.poly({{0.3, 0.72}, {0.45, 0.4}, {0.52, 0.4}, {0.4, 0.72}});  // upright
    c.pen(kWood.darker(130), 0.05);
    c.line(0.48, 0.44, 0.88, 0.12);  // the arm
    c.pen(kInk, 0.025);
    c.p.setBrush(kWood.darker(120));
    c.p.drawEllipse(c.box(0.83, 0.05, 0.12, 0.1));
    c.p.setBrush(QColor("#8a8476"));
    c.p.drawEllipse(c.box(0.85, 0.02, 0.09, 0.09));  // the stone
    c.p.setBrush(kWood.darker(140));
    for (const double x : {0.14, 0.62}) {
        c.p.drawEllipse(c.box(x, 0.68, 0.24, 0.24));
        c.p.setBrush(kWood);
        c.p.drawEllipse(c.box(x + 0.08, 0.76, 0.08, 0.08));
        c.p.setBrush(kWood.darker(140));
    }
}

void wings(Canvas& c, const QColor& colour) {
    // Layered feathers: long flight feathers, then shorter coverts.
    for (const double side : {-1.0, 1.0}) {
        const auto X = [&](double f) { return 0.5 + side * f; };
        for (int row = 0; row < 2; ++row) {
            const QColor col = row == 0 ? colour.darker(112) : colour;
            const double len = row == 0 ? 1.0 : 0.72;
            for (int i = 0; i < 5; ++i) {
                const double t = i / 4.0;
                const QPointF root = c.at(X(0.06 + 0.05 * t), 0.42 + 0.06 * t);
                const QPointF tip = c.at(X((0.1 + 0.36 * (1 - t * 0.5)) * len + 0.04), (0.06 + 0.42 * t) * len + (1 - len) * 0.3);
                QPainterPath f;
                const QPointF mid = (root + tip) / 2;
                const QPointF n(-(tip - root).y() * 0.18, (tip - root).x() * 0.18);
                f.moveTo(root);
                f.quadTo(mid + n, tip);
                f.quadTo(mid - n, root);
                c.pen(kInk, 0.012);
                c.p.setBrush(lit(c, 0.0, 0.0, 1.0, 0.6, col, 125, 120));
                c.p.drawPath(f);
                c.pen(col.darker(140), 0.006);
                c.p.drawLine(root, tip);
            }
        }
    }
}

void angel(Canvas& c) {
    wings(c, QColor("#f4f0e8"));
    figure(c, QColor("#e8e0c8"), false);
    c.p.setPen(QPen(QColor("#ffd84a"), c.s(0.03)));
    c.p.setBrush(Qt::NoBrush);
    c.p.drawEllipse(c.box(0.38, 0.04, 0.24, 0.07));  // halo
    c.pen(kInk, 0.025);
    c.p.setBrush(kSteel);
    c.poly({{0.74, 0.62}, {0.78, 0.08}, {0.82, 0.62}});  // a shining sword
}

void horseBody(Canvas& c, const QColor& coat, bool head) {
    const QColor hoof("#2a2018");
    c.pen(kInk, 0.02);
    // Legs (the far ones darker), with knees and hooves.
    const auto leg = [&](double x, double lift, const QColor& col) {
        QPainterPath l;
        l.moveTo(c.at(x - 0.035, 0.62));
        l.lineTo(c.at(x + 0.035, 0.62));
        l.lineTo(c.at(x + 0.025 + lift, 0.78));
        l.lineTo(c.at(x + 0.02, 0.9));
        l.lineTo(c.at(x - 0.02, 0.9));
        l.lineTo(c.at(x - 0.02 + lift, 0.78));
        l.closeSubpath();
        c.p.setBrush(lit(c, x - 0.04, 0.62, 0.08, 0.3, col));
        c.p.drawPath(l);
        c.p.setBrush(hoof);
        c.p.drawRect(c.box(x - 0.03, 0.89, 0.055, 0.04));
    };
    leg(0.27, 0.01, coat.darker(130));
    leg(0.66, -0.01, coat.darker(130));
    // The tail, flowing.
    QPainterPath tail;
    tail.moveTo(c.at(0.15, 0.5));
    tail.cubicTo(c.at(0.02, 0.52), c.at(0.0, 0.72), c.at(0.06, 0.82));
    tail.cubicTo(c.at(0.08, 0.7), c.at(0.12, 0.6), c.at(0.18, 0.56));
    tail.closeSubpath();
    c.p.setBrush(lit(c, 0.0, 0.5, 0.18, 0.32, coat.darker(150)));
    c.p.drawPath(tail);
    // The body, barrel shaped.
    QPainterPath body;
    body.moveTo(c.at(0.15, 0.56));
    body.cubicTo(c.at(0.14, 0.44), c.at(0.3, 0.44), c.at(0.45, 0.46));
    body.cubicTo(c.at(0.62, 0.44), c.at(0.78, 0.44), c.at(0.8, 0.56));
    body.cubicTo(c.at(0.8, 0.68), c.at(0.6, 0.7), c.at(0.45, 0.69));
    body.cubicTo(c.at(0.28, 0.7), c.at(0.15, 0.68), c.at(0.15, 0.56));
    c.p.setBrush(lit(c, 0.14, 0.44, 0.66, 0.26, coat, 130, 130));
    c.p.drawPath(body);
    leg(0.33, 0.015, coat);
    leg(0.72, -0.015, coat);
    if (!head) return;
    // Neck, head with a muzzle, ear, eye and mane.
    QPainterPath neck;
    neck.moveTo(c.at(0.66, 0.5));
    neck.cubicTo(c.at(0.7, 0.36), c.at(0.76, 0.26), c.at(0.82, 0.2));
    neck.lineTo(c.at(0.9, 0.26));
    neck.cubicTo(c.at(0.86, 0.34), c.at(0.82, 0.44), c.at(0.8, 0.56));
    neck.closeSubpath();
    c.p.setBrush(lit(c, 0.66, 0.2, 0.24, 0.36, coat, 125, 125));
    c.p.drawPath(neck);
    QPainterPath skull;
    skull.moveTo(c.at(0.8, 0.2));
    skull.cubicTo(c.at(0.84, 0.13), c.at(0.92, 0.14), c.at(0.98, 0.3));
    skull.cubicTo(c.at(0.99, 0.35), c.at(0.95, 0.37), c.at(0.92, 0.35));
    skull.cubicTo(c.at(0.88, 0.3), c.at(0.84, 0.28), c.at(0.8, 0.2));
    c.p.setBrush(lit(c, 0.8, 0.13, 0.2, 0.24, coat, 120, 125));
    c.p.drawPath(skull);
    c.poly({{0.82, 0.17}, {0.83, 0.08}, {0.86, 0.16}});  // ear
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(QColor(25, 16, 8));
    c.p.drawEllipse(c.box(0.865, 0.2, 0.025, 0.025));
    c.p.drawEllipse(c.box(0.955, 0.31, 0.015, 0.015));  // nostril
    c.pen(kInk, 0.015);
    c.p.setBrush(coat.darker(160));  // the mane
    QPainterPath mane;
    mane.moveTo(c.at(0.81, 0.16));
    mane.cubicTo(c.at(0.74, 0.24), c.at(0.68, 0.36), c.at(0.65, 0.5));
    mane.lineTo(c.at(0.7, 0.48));
    mane.cubicTo(c.at(0.73, 0.36), c.at(0.78, 0.26), c.at(0.84, 0.19));
    mane.closeSubpath();
    c.p.drawPath(mane);
}

void centaur(Canvas& c) {
    horseBody(c, QColor("#8a6038"), false);
    // The man's body rising where a horse's neck would be.
    c.pen(kInk, 0.022);
    QPainterPath torso;
    torso.moveTo(c.at(0.64, 0.56));
    torso.cubicTo(c.at(0.62, 0.42), c.at(0.62, 0.3), c.at(0.66, 0.26));
    torso.lineTo(c.at(0.8, 0.26));
    torso.cubicTo(c.at(0.84, 0.3), c.at(0.84, 0.42), c.at(0.8, 0.56));
    torso.closeSubpath();
    c.p.setBrush(lit(c, 0.62, 0.26, 0.22, 0.3, QColor("#e0b080"), 125, 130));
    c.p.drawPath(torso);
    c.p.setBrush(QColor("#6a4a28"));  // a leather strap with a quiver
    c.poly({{0.64, 0.28}, {0.67, 0.27}, {0.82, 0.48}, {0.79, 0.5}});
    c.p.setBrush(lit(c, 0.6, 0.1, 0.08, 0.2, QColor("#8a5a2b")));
    c.p.drawRoundedRect(c.box(0.6, 0.12, 0.07, 0.2), c.s(0.02), c.s(0.02));
    c.pen(QColor("#e8e0d0"), 0.012);
    for (const double ax : {0.615, 0.635, 0.655}) c.line(ax, 0.12, ax, 0.07);
    c.pen(kInk, 0.02);
    c.p.setBrush(lit(c, 0.64, 0.07, 0.18, 0.2, QColor("#e0b080"), 115, 120));
    c.p.drawEllipse(c.box(0.64, 0.07, 0.18, 0.2));
    faceDetails(c, 0.74, 0.17, 0.17);
    c.p.setBrush(QColor("#4a3020"));  // hair
    c.p.drawChord(c.box(0.64, 0.06, 0.18, 0.12), 0, 180 * 16);
    c.pen(kWood.darker(110), 0.035);  // the bow
    c.p.setBrush(Qt::NoBrush);
    c.p.drawArc(c.box(0.78, 0.08, 0.2, 0.46), -80 * 16, 160 * 16);
    c.pen(QColor("#e8e0d0"), 0.01);
    c.line(0.88, 0.1, 0.88, 0.52);
}

void cavalier(Canvas& c) {
    horseBody(c, QColor("#f0ece4"), true);
    c.pen(kInk, 0.02);
    // A caparison in blue over the horse's back.
    c.p.setBrush(lit(c, 0.3, 0.46, 0.34, 0.2, QColor("#3a64b0")));
    c.poly({{0.3, 0.47}, {0.62, 0.47}, {0.6, 0.66}, {0.32, 0.66}});
    c.p.setBrush(QColor("#ffd84a"));
    c.p.drawEllipse(c.box(0.42, 0.53, 0.07, 0.07));
    // The knight: leg, body in armour, helmet with a plume, a shield.
    c.p.setBrush(lit(c, 0.42, 0.44, 0.08, 0.2, kSteel));
    c.p.drawRoundedRect(c.box(0.43, 0.44, 0.07, 0.2), c.s(0.03), c.s(0.03));
    c.p.setBrush(lit(c, 0.37, 0.22, 0.2, 0.26, kSteel, 135, 130));
    c.p.drawRoundedRect(c.box(0.37, 0.22, 0.2, 0.26), c.s(0.06), c.s(0.06));
    c.pen(kSteel.darker(140), 0.012);
    c.line(0.47, 0.24, 0.47, 0.46);
    c.line(0.38, 0.34, 0.56, 0.34);
    c.pen(kInk, 0.02);
    c.p.setBrush(lit(c, 0.38, 0.04, 0.18, 0.2, kSteel, 135, 130));
    c.p.drawRoundedRect(c.box(0.38, 0.05, 0.17, 0.18), c.s(0.06), c.s(0.06));
    c.p.setBrush(QColor(25, 16, 8));
    c.p.drawRect(c.box(0.44, 0.12, 0.1, 0.02));  // the visor slit
    c.p.setBrush(lit(c, 0.42, -0.04, 0.12, 0.12, QColor("#c03a3a")));  // plume
    QPainterPath plume;
    plume.moveTo(c.at(0.45, 0.06));
    plume.cubicTo(c.at(0.4, -0.02), c.at(0.3, 0.0), c.at(0.26, 0.08));
    plume.cubicTo(c.at(0.32, 0.04), c.at(0.4, 0.05), c.at(0.47, 0.08));
    c.p.drawPath(plume);
    // The lance with a pennant, then the shield in front.
    c.pen(kWood.darker(120), 0.03);
    c.line(0.3, 0.62, 0.99, 0.06);
    c.pen(kInk, 0.012);
    c.p.setBrush(QColor("#3a64b0"));
    c.poly({{0.92, 0.12}, {0.99, 0.15}, {0.9, 0.17}});
    c.pen(kInk, 0.02);
    QPainterPath shield;
    shield.moveTo(c.at(0.3, 0.28));
    shield.lineTo(c.at(0.42, 0.28));
    shield.cubicTo(c.at(0.42, 0.4), c.at(0.4, 0.46), c.at(0.36, 0.5));
    shield.cubicTo(c.at(0.32, 0.46), c.at(0.3, 0.4), c.at(0.3, 0.28));
    c.p.setBrush(lit(c, 0.3, 0.28, 0.12, 0.22, QColor("#3a64b0"), 140, 130));
    c.p.drawPath(shield);
    c.p.setBrush(QColor("#ffd84a"));
    c.poly({{0.36, 0.3}, {0.38, 0.36}, {0.36, 0.44}, {0.34, 0.36}});
}

void wolf(Canvas& c) {
    quadruped(c, QColor("#7a7470"), false);
}

void brute(Canvas& c, const QColor& skin, bool club) {
    // A big, broad figure: trolls, ogres and the like.
    c.pen(kInk, 0.022);
    for (const double lx : {0.3, 0.56}) {  // thick legs, bare feet
        c.p.setBrush(lit(c, lx, 0.68, 0.15, 0.26, skin.darker(115)));
        c.p.drawRoundedRect(c.box(lx, 0.68, 0.15, 0.24), c.s(0.05), c.s(0.05));
        c.p.setBrush(skin.darker(125));
        c.p.drawRoundedRect(c.box(lx - 0.015, 0.89, 0.18, 0.06), c.s(0.03), c.s(0.03));
    }
    // A fur loincloth with a ragged edge.
    QPainterPath cloth;
    cloth.moveTo(c.at(0.26, 0.6));
    cloth.lineTo(c.at(0.74, 0.6));
    for (int i = 0; i <= 6; ++i) cloth.lineTo(c.at(0.74 - i * 0.08, i % 2 ? 0.8 : 0.74));
    cloth.closeSubpath();
    c.p.setBrush(lit(c, 0.26, 0.6, 0.48, 0.2, QColor("#6a4a28")));
    c.p.drawPath(cloth);
    // The torso: broad shoulders, a belly.
    QPainterPath torso;
    torso.moveTo(c.at(0.24, 0.38));
    torso.cubicTo(c.at(0.3, 0.28), c.at(0.7, 0.28), c.at(0.76, 0.38));
    torso.cubicTo(c.at(0.8, 0.5), c.at(0.74, 0.62), c.at(0.5, 0.64));
    torso.cubicTo(c.at(0.26, 0.62), c.at(0.2, 0.5), c.at(0.24, 0.38));
    c.p.setBrush(lit(c, 0.2, 0.28, 0.6, 0.36, skin, 130, 130));
    c.p.drawPath(torso);
    c.pen(skin.darker(140), 0.014);  // chest and belly lines
    c.p.setBrush(Qt::NoBrush);
    c.p.drawArc(c.box(0.32, 0.36, 0.17, 0.1), 200 * 16, 140 * 16);
    c.p.drawArc(c.box(0.51, 0.36, 0.17, 0.1), 200 * 16, 140 * 16);
    c.p.drawArc(c.box(0.38, 0.46, 0.24, 0.14), 200 * 16, 140 * 16);
    // Arms hanging with big fists.
    c.pen(kInk, 0.022);
    for (const double side : {-1.0, 1.0}) {
        const double ax = side < 0 ? 0.1 : 0.76;
        c.p.setBrush(lit(c, ax, 0.36, 0.15, 0.3, skin.darker(105)));
        c.p.drawRoundedRect(c.box(ax, 0.36, 0.14, 0.28), c.s(0.06), c.s(0.06));
        c.p.setBrush(lit(c, ax - 0.01, 0.6, 0.16, 0.12, skin));
        c.p.drawEllipse(c.box(ax - 0.01, 0.6, 0.16, 0.12));
    }
    // The head: low brow, small eyes, teeth.
    c.p.setBrush(lit(c, 0.34, 0.07, 0.32, 0.3, skin, 120, 125));
    c.p.drawEllipse(c.box(0.34, 0.07, 0.32, 0.3));
    c.p.setBrush(skin.darker(115));
    c.p.drawEllipse(c.box(0.31, 0.16, 0.06, 0.08));  // ears
    c.p.drawEllipse(c.box(0.63, 0.16, 0.06, 0.08));
    faceDetails(c, 0.5, 0.2, 0.26, QColor(25, 16, 8), false);
    c.pen(skin.darker(160), 0.03);
    c.line(0.38, 0.15, 0.62, 0.15);  // the heavy brow
    c.pen(kInk, 0.012);
    c.p.setBrush(QColor("#2a1010"));
    c.p.drawChord(c.box(0.42, 0.24, 0.16, 0.08), 180 * 16, 180 * 16);
    c.p.setBrush(QColor("#f4f0e0"));
    c.poly({{0.44, 0.28}, {0.46, 0.25}, {0.48, 0.28}});
    c.poly({{0.52, 0.28}, {0.54, 0.25}, {0.56, 0.28}});
    if (club) {
        c.pen(kInk, 0.02);
        QPainterPath cl;
        cl.moveTo(c.at(0.8, 0.62));
        cl.lineTo(c.at(0.86, 0.62));
        cl.cubicTo(c.at(0.96, 0.3), c.at(1.0, 0.1), c.at(0.94, 0.0));
        cl.cubicTo(c.at(0.86, 0.0), c.at(0.82, 0.2), c.at(0.8, 0.62));
        c.p.setBrush(lit(c, 0.8, 0.0, 0.2, 0.62, kWood));
        c.p.drawPath(cl);
        c.p.setBrush(kWood.darker(140));  // knots
        c.p.drawEllipse(c.box(0.9, 0.12, 0.03, 0.03));
        c.p.drawEllipse(c.box(0.87, 0.3, 0.025, 0.025));
    }
}

void vampire(Canvas& c) {
    // A high-collared cape behind a pale nobleman in black and red.
    c.pen(kInk, 0.022);
    QPainterPath cape;
    cape.moveTo(c.at(0.5, 0.3));
    cape.cubicTo(c.at(0.2, 0.36), c.at(0.12, 0.7), c.at(0.08, 0.95));
    cape.quadTo(c.at(0.3, 0.9), c.at(0.5, 0.95));
    cape.quadTo(c.at(0.7, 0.9), c.at(0.92, 0.95));
    cape.cubicTo(c.at(0.88, 0.7), c.at(0.8, 0.36), c.at(0.5, 0.3));
    c.p.setBrush(lit(c, 0.08, 0.3, 0.84, 0.65, QColor("#1a1020"), 160, 120));
    c.p.drawPath(cape);
    c.p.setBrush(lit(c, 0.2, 0.36, 0.6, 0.55, QColor("#8a1a2a"), 130, 130));  // its red lining
    c.poly({{0.5, 0.36}, {0.26, 0.9}, {0.5, 0.86}, {0.74, 0.9}});
    figure(c, QColor("#2a2030"), false, 0.5, QColor("#e8e0e8"));
    c.pen(kInk, 0.02);
    c.p.setBrush(QColor("#1a1020"));  // the high collar
    c.poly({{0.36, 0.38}, {0.32, 0.18}, {0.42, 0.33}});
    c.poly({{0.64, 0.38}, {0.68, 0.18}, {0.58, 0.33}});
    c.p.setBrush(QColor("#1a1020"));  // slick hair with a widow's peak
    c.poly({{0.39, 0.2}, {0.4, 0.11}, {0.5, 0.1}, {0.6, 0.11}, {0.61, 0.2}, {0.5, 0.16}});
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(QColor("#ff3030"));
    c.p.drawEllipse(c.box(0.445, 0.225, 0.03, 0.025));
    c.p.drawEllipse(c.box(0.525, 0.225, 0.03, 0.025));
    c.p.setBrush(QColor(250, 250, 250));
    c.poly({{0.47, 0.29}, {0.48, 0.32}, {0.49, 0.29}});  // fangs
    c.poly({{0.51, 0.29}, {0.52, 0.32}, {0.53, 0.29}});
}

void robedCaster(Canvas& c, const QColor& robe, bool skull, const QColor& orb) {
    c.pen(kInk, 0.022);
    // The staff behind the hand.
    c.pen(kWood.darker(130), 0.04);
    c.line(0.8, 0.95, 0.79, 0.2);
    c.pen(kWood.lighter(120), 0.012);
    c.line(0.795, 0.9, 0.785, 0.25);
    c.pen(kInk, 0.022);
    // The robe with folds and a wavy hem.
    QPainterPath body;
    body.moveTo(c.at(0.5, 0.29));
    body.cubicTo(c.at(0.36, 0.32), c.at(0.24, 0.6), c.at(0.17, 0.95));
    for (int i = 0; i < 4; ++i) body.quadTo(c.at(0.25 + i * 0.165, 0.99), c.at(0.335 + i * 0.165, 0.95));
    body.cubicTo(c.at(0.76, 0.6), c.at(0.64, 0.32), c.at(0.5, 0.29));
    c.p.setBrush(lit(c, 0.17, 0.29, 0.66, 0.66, robe, 140, 135));
    c.p.drawPath(body);
    c.pen(robe.darker(145), 0.014);
    c.p.setBrush(Qt::NoBrush);
    for (const double fx : {0.38, 0.5, 0.62}) {
        QPainterPath fold;
        fold.moveTo(c.at(fx, 0.5));
        fold.quadTo(c.at(fx + (fx - 0.5) * 0.4 - 0.01, 0.72), c.at(fx + (fx - 0.5) * 0.7, 0.93));
        c.p.drawPath(fold);
    }
    // A sash with a tassel.
    c.pen(kInk, 0.015);
    c.p.setBrush(QColor("#d8b04a"));
    c.poly({{0.37, 0.52}, {0.63, 0.52}, {0.64, 0.56}, {0.36, 0.56}});
    c.p.drawEllipse(c.box(0.42, 0.56, 0.04, 0.08));
    // The back sleeve and the front one reaching the staff.
    c.pen(kInk, 0.02);
    c.p.setBrush(lit(c, 0.24, 0.36, 0.14, 0.3, robe.darker(110)));
    c.poly({{0.38, 0.35}, {0.24, 0.62}, {0.33, 0.66}, {0.42, 0.42}});
    c.p.setBrush(lit(c, 0.27, 0.6, 0.07, 0.07, kSkin));
    c.p.drawEllipse(c.box(0.265, 0.6, 0.07, 0.07));
    c.p.setBrush(lit(c, 0.58, 0.34, 0.24, 0.2, robe.darker(105)));
    c.poly({{0.58, 0.35}, {0.8, 0.45}, {0.78, 0.53}, {0.6, 0.46}});
    c.p.setBrush(lit(c, 0.75, 0.44, 0.09, 0.09, kSkin));
    c.p.drawEllipse(c.box(0.755, 0.445, 0.08, 0.08));
    if (skull) {
        c.p.setBrush(robe.darker(150));  // the hood around the skull
        c.p.drawEllipse(c.box(0.35, 0.08, 0.3, 0.3));
        skull_(c);
    } else {
        c.p.setBrush(robe.darker(125));  // the hood behind the head
        c.p.drawEllipse(c.box(0.35, 0.08, 0.3, 0.3));
        c.p.setBrush(lit(c, 0.39, 0.12, 0.22, 0.24, kSkin, 115, 120));
        c.p.drawEllipse(c.box(0.39, 0.13, 0.22, 0.23));
        faceDetails(c, 0.5, 0.235, 0.2);
        c.p.setBrush(lit(c, 0.35, 0.07, 0.3, 0.14, robe.darker(110), 130, 130));  // the hood's front edge
        c.pen(kInk, 0.02);
        c.p.drawChord(c.box(0.36, 0.08, 0.28, 0.22), 10 * 16, 160 * 16);
    }
    // The orb, glowing.
    QRadialGradient glow(c.at(0.8, 0.17), c.s(0.16));
    glow.setColorAt(0, QColor(orb.red(), orb.green(), orb.blue(), 150));
    glow.setColorAt(1, QColor(orb.red(), orb.green(), orb.blue(), 0));
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(glow);
    c.p.drawEllipse(QPointF(c.at(0.8, 0.17)), c.s(0.16), c.s(0.16));
    QRadialGradient ball(c.at(0.775, 0.145), c.s(0.08));
    ball.setColorAt(0, orb.lighter(170));
    ball.setColorAt(1, orb.darker(120));
    c.pen(kInk, 0.015);
    c.p.setBrush(ball);
    c.p.drawEllipse(c.box(0.735, 0.105, 0.13, 0.13));
}

void dryad(Canvas& c) {
    figure(c, QColor("#5aa84a"), false);
    c.pen(kInk, 0.02);
    for (int i = 0; i < 6; ++i) {  // hair of leaves
        c.p.setBrush(i % 2 ? QColor("#7ac85a") : QColor("#3e8a3a"));
        c.p.drawEllipse(c.box(0.34 + 0.055 * i, 0.08 + (i % 2) * 0.04, 0.1, 0.12));
    }
}

// A short, broad bearded figure (dwarves).
void dwarf(Canvas& c, const QColor& coat, bool helmet) {
    c.pen(kInk, 0.022);
    for (const double lx : {0.34, 0.54}) {  // short legs in big boots
        c.p.setBrush(lit(c, lx, 0.74, 0.12, 0.14, coat.darker(150)));
        c.p.drawRect(c.box(lx, 0.74, 0.12, 0.14));
        c.p.setBrush(lit(c, lx - 0.02, 0.86, 0.16, 0.08, QColor("#4a3020"), 140));
        c.p.drawRoundedRect(c.box(lx - 0.02, 0.86, 0.16, 0.08), c.s(0.03), c.s(0.03));
    }
    // A broad body in mail with a wide belt.
    c.p.setBrush(lit(c, 0.25, 0.44, 0.5, 0.34, coat, 135, 130));
    c.p.drawRoundedRect(c.box(0.25, 0.44, 0.5, 0.34), c.s(0.09), c.s(0.09));
    c.pen(coat.darker(130), 0.008);  // mail rings
    for (int row = 0; row < 4; ++row)
        for (int i = 0; i < 6; ++i) {
            c.p.setBrush(Qt::NoBrush);
            c.p.drawEllipse(c.box(0.29 + i * 0.07 + (row % 2) * 0.035, 0.6 + row * 0.04, 0.035, 0.03));
        }
    c.pen(kInk, 0.015);
    c.p.setBrush(QColor("#4a3020"));
    c.p.drawRect(c.box(0.25, 0.6, 0.5, 0.06));
    c.p.setBrush(lit(c, 0.45, 0.595, 0.1, 0.07, QColor("#e8c04a")));
    c.p.drawRect(c.box(0.45, 0.595, 0.1, 0.07));
    // Arms with gauntlets.
    for (const double ax : {0.17, 0.74}) {
        c.p.setBrush(lit(c, ax, 0.46, 0.1, 0.22, coat.darker(110)));
        c.p.drawRoundedRect(c.box(ax, 0.46, 0.09, 0.2), c.s(0.04), c.s(0.04));
        c.p.setBrush(lit(c, ax - 0.01, 0.64, 0.11, 0.08, kSteel));
        c.p.drawEllipse(c.box(ax - 0.01, 0.64, 0.11, 0.08));
    }
    // The head and a big braided beard.
    c.p.setBrush(lit(c, 0.37, 0.22, 0.26, 0.24, kSkin, 115, 120));
    c.p.drawEllipse(c.box(0.37, 0.22, 0.26, 0.24));
    faceDetails(c, 0.5, 0.32, 0.22, QColor(25, 16, 8), false);
    c.p.setBrush(kSkin.darker(118));  // the nose
    c.p.drawEllipse(c.box(0.475, 0.33, 0.05, 0.05));
    const QColor beard("#c86a2a");
    c.pen(kInk, 0.018);
    QPainterPath b;
    b.moveTo(c.at(0.36, 0.36));
    b.cubicTo(c.at(0.36, 0.5), c.at(0.42, 0.66), c.at(0.5, 0.72));
    b.cubicTo(c.at(0.58, 0.66), c.at(0.64, 0.5), c.at(0.64, 0.36));
    b.cubicTo(c.at(0.58, 0.4), c.at(0.42, 0.4), c.at(0.36, 0.36));
    c.p.setBrush(lit(c, 0.36, 0.36, 0.28, 0.36, beard, 130, 130));
    c.p.drawPath(b);
    c.pen(beard.darker(140), 0.01);
    for (const double bx : {0.44, 0.5, 0.56}) c.line(bx, 0.44, bx + (bx - 0.5) * 0.3, 0.64);
    c.pen(kInk, 0.012);
    c.p.setBrush(QColor("#e8c04a"));  // a braid ring
    c.p.drawRect(c.box(0.475, 0.62, 0.05, 0.03));
    if (helmet) {
        c.pen(kInk, 0.02);
        c.p.setBrush(lit(c, 0.35, 0.16, 0.3, 0.14, kSteel, 135, 130));
        c.p.drawChord(c.box(0.35, 0.16, 0.3, 0.22), 0, 180 * 16);
        c.p.setBrush(kSteel.darker(115));
        c.p.drawRect(c.box(0.345, 0.26, 0.31, 0.03));
        c.p.setBrush(lit(c, 0.24, 0.12, 0.14, 0.16, QColor("#ece4cc")));
        c.poly({{0.36, 0.27}, {0.25, 0.12}, {0.39, 0.22}});
        c.poly({{0.64, 0.27}, {0.75, 0.12}, {0.61, 0.22}});
    } else {
        c.p.setBrush(lit(c, 0.37, 0.19, 0.26, 0.1, beard.darker(110)));
        c.p.drawChord(c.box(0.37, 0.19, 0.26, 0.14), 0, 180 * 16);
    }
}

void axe(Canvas& c, double x) {
    c.pen(kWood.darker(120), 0.04);
    c.line(x, 0.9, x + 0.08, 0.3);
    c.pen(kInk, 0.02);
    c.p.setBrush(kSteel);
    c.poly({{x + 0.06, 0.28}, {x + 0.24, 0.22}, {x + 0.24, 0.44}, {x + 0.08, 0.4}});
}

void golem(Canvas& c, const QColor& stone) {
    c.pen(kInk, 0.022);
    const auto block = [&](double x, double y, double w, double h, int shade) {
        c.p.setBrush(lit(c, x, y, w, h, stone.darker(shade), 130, 135));
        c.p.drawRoundedRect(c.box(x, y, w, h), c.s(0.03), c.s(0.03));
    };
    block(0.3, 0.7, 0.15, 0.24, 110);  // legs
    block(0.55, 0.7, 0.15, 0.24, 110);
    block(0.06, 0.34, 0.17, 0.2, 105);  // shoulders and arms
    block(0.08, 0.53, 0.14, 0.2, 110);
    block(0.77, 0.34, 0.17, 0.2, 105);
    block(0.78, 0.53, 0.14, 0.2, 110);
    block(0.22, 0.3, 0.56, 0.44, 100);  // the body
    block(0.36, 0.06, 0.28, 0.26, 95);  // the head
    // Cracks, moss and a glowing rune on the chest.
    c.pen(stone.darker(170), 0.012);
    c.line(0.3, 0.4, 0.38, 0.5);
    c.line(0.38, 0.5, 0.34, 0.6);
    c.line(0.66, 0.38, 0.72, 0.52);
    c.line(0.84, 0.4, 0.88, 0.48);
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(QColor(90, 130, 60, 170));
    c.p.drawEllipse(c.box(0.24, 0.66, 0.12, 0.05));
    c.p.drawEllipse(c.box(0.62, 0.3, 0.1, 0.04));
    QRadialGradient rune(c.at(0.5, 0.5), c.s(0.12));
    rune.setColorAt(0, QColor(255, 190, 80, 200));
    rune.setColorAt(1, QColor(255, 190, 80, 0));
    c.p.setBrush(rune);
    c.p.drawEllipse(QPointF(c.at(0.5, 0.5)), c.s(0.12), c.s(0.12));
    c.pen(QColor("#ffd070"), 0.02);
    c.line(0.5, 0.42, 0.5, 0.58);
    c.line(0.44, 0.47, 0.56, 0.53);
    c.line(0.56, 0.47, 0.44, 0.53);
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(QColor("#ffb040"));  // eyes
    c.p.drawRoundedRect(c.box(0.41, 0.15, 0.07, 0.04), 2, 2);
    c.p.drawRoundedRect(c.box(0.52, 0.15, 0.07, 0.04), 2, 2);
}

// Green-skinned figures (orcs, goblins).
void orc(Canvas& c, const QColor& skin, const QColor& coat, double scale, bool horns) {
    const double x0 = 0.5 - 0.5 * scale, k = scale;
    const auto X = [&](double x) { return x0 + x * k; };
    const auto Y = [&](double y) { return 1 - (1 - y) * k; };
    const auto B = [&](double x, double y, double w, double h) { return c.box(X(x), Y(y), w * k, h * k); };
    c.pen(kInk, 0.022);
    for (const double lx : {0.35, 0.54}) {  // legs and boots
        c.p.setBrush(lit(c, X(lx), Y(0.7), 0.11 * k, 0.2 * k, coat.darker(150)));
        c.p.drawRect(B(lx, 0.7, 0.11, 0.18));
        c.p.setBrush(QColor("#3a2a1a"));
        c.p.drawRoundedRect(B(lx - 0.015, 0.86, 0.14, 0.08), c.s(0.03), c.s(0.03));
    }
    // A leather jerkin with shoulder spikes.
    c.p.setBrush(lit(c, X(0.28), Y(0.38), 0.44 * k, 0.36 * k, coat, 135, 130));
    c.p.drawRoundedRect(B(0.28, 0.38, 0.44, 0.36), c.s(0.07), c.s(0.07));
    c.pen(coat.darker(150), 0.012);
    c.line(X(0.32), Y(0.5), X(0.68), Y(0.6));
    c.line(X(0.32), Y(0.6), X(0.68), Y(0.5));
    c.pen(kInk, 0.016);
    c.p.setBrush(lit(c, X(0.24), Y(0.36), 0.12 * k, 0.1 * k, kSteel));
    c.p.drawEllipse(B(0.24, 0.36, 0.14, 0.1));
    c.p.drawEllipse(B(0.62, 0.36, 0.14, 0.1));
    for (const double sx : {0.28, 0.7}) c.poly({{X(sx), Y(0.37)}, {X(sx + 0.02), Y(0.3)}, {X(sx + 0.04), Y(0.37)}});
    // Arms (bare, green) with fists.
    for (const double ax : {0.19, 0.72}) {
        c.p.setBrush(lit(c, X(ax), Y(0.44), 0.09 * k, 0.24 * k, skin.darker(105)));
        c.p.drawRoundedRect(B(ax, 0.44, 0.09, 0.22), c.s(0.04), c.s(0.04));
        c.p.setBrush(lit(c, X(ax - 0.01), Y(0.64), 0.11 * k, 0.09 * k, skin));
        c.p.drawEllipse(B(ax - 0.01, 0.64, 0.11, 0.09));
    }
    // The head: pointed ears, a heavy jaw, tusks, red eyes.
    c.p.setBrush(lit(c, X(0.36), Y(0.12), 0.28 * k, 0.28 * k, skin, 120, 125));
    c.poly({{X(0.37), Y(0.22)}, {X(0.22), Y(0.13)}, {X(0.38), Y(0.3)}});
    c.poly({{X(0.63), Y(0.22)}, {X(0.78), Y(0.13)}, {X(0.62), Y(0.3)}});
    c.p.drawEllipse(B(0.36, 0.12, 0.28, 0.28));
    c.p.setBrush(skin.darker(110));
    c.p.drawChord(B(0.38, 0.26, 0.24, 0.13), 180 * 16, 180 * 16);  // the jaw
    faceDetails(c, X(0.5), Y(0.22), 0.24 * k, QColor("#c02010"), false);
    c.pen(skin.darker(160), 0.025 * k);
    c.line(X(0.39), Y(0.18), X(0.61), Y(0.18));
    c.pen(kInk, 0.012);
    c.p.setBrush(QColor("#f4f0e0"));  // tusks
    c.poly({{X(0.43), Y(0.34)}, {X(0.45), Y(0.27)}, {X(0.47), Y(0.34)}});
    c.poly({{X(0.53), Y(0.34)}, {X(0.55), Y(0.27)}, {X(0.57), Y(0.34)}});
    if (horns) {
        c.pen(kInk, 0.02);
        c.p.setBrush(lit(c, X(0.26), Y(0.0), 0.2 * k, 0.14 * k, QColor("#ece4cc")));
        c.poly({{X(0.38), Y(0.14)}, {X(0.28), Y(0.0)}, {X(0.44), Y(0.12)}});
        c.poly({{X(0.62), Y(0.14)}, {X(0.72), Y(0.0)}, {X(0.56), Y(0.12)}});
    }
}

void beastRider(Canvas& c, const QColor& beast, const QColor& riderSkin, const QColor& coat) {
    const bool bear = beast == QColor("#6a4a2a") || beast == QColor("#d8d0c8");
    quadruped(c, beast, bear);
    // The rider on its back: a leg, the body, an arm with a weapon, the head.
    c.pen(kInk, 0.02);
    const double rx = 0.42, top = bear ? 0.02 : 0.1;
    c.p.setBrush(lit(c, rx - 0.02, top + 0.34, 0.08, 0.2, coat.darker(140)));
    c.p.drawRoundedRect(c.box(rx - 0.02, top + 0.36, 0.08, 0.2), c.s(0.03), c.s(0.03));
    c.p.setBrush(lit(c, rx - 0.1, top + 0.16, 0.2, 0.24, coat, 135, 130));
    c.p.drawRoundedRect(c.box(rx - 0.1, top + 0.16, 0.2, 0.24), c.s(0.06), c.s(0.06));
    c.p.setBrush(QColor("#5a3a20"));
    c.p.drawRect(c.box(rx - 0.1, top + 0.32, 0.2, 0.035));
    c.pen(kWood.darker(120), 0.03);  // a spear held high
    c.line(rx + 0.02, top + 0.5, rx + 0.28, top - 0.08);
    c.pen(kInk, 0.015);
    c.p.setBrush(kSteel);
    c.poly({{rx + 0.28, top - 0.12}, {rx + 0.31, top - 0.04}, {rx + 0.26, top - 0.06}});
    c.p.setBrush(lit(c, rx + 0.06, top + 0.2, 0.08, 0.08, riderSkin));
    c.p.drawEllipse(c.box(rx + 0.06, top + 0.2, 0.07, 0.07));
    c.p.setBrush(lit(c, rx - 0.08, top - 0.02, 0.16, 0.18, riderSkin, 115, 120));
    c.p.drawEllipse(c.box(rx - 0.08, top - 0.02, 0.16, 0.18));
    faceDetails(c, rx + 0.01, top + 0.07, 0.15);
    c.p.setBrush(lit(c, rx - 0.09, top - 0.04, 0.18, 0.08, bear ? kSteel : QColor("#4a3020")));
    c.p.drawChord(c.box(rx - 0.09, top - 0.04, 0.18, 0.14), 0, 180 * 16);
}

void cyclops(Canvas& c, bool crown) {
    brute(c, QColor("#b0a070"), false);
    c.pen(kInk, 0.02);
    c.p.setBrush(QColor("#f4f4f0"));
    c.p.drawEllipse(c.box(0.43, 0.15, 0.14, 0.1));  // the one eye
    c.p.setBrush(kInk);
    c.p.drawEllipse(c.box(0.48, 0.17, 0.05, 0.06));
    c.p.setBrush(QColor("#9a968c"));
    c.p.drawEllipse(c.box(0.74, 0.0, 0.22, 0.2));  // a rock to throw
    if (crown) {
        c.p.setBrush(QColor("#ffd84a"));
        c.poly({{0.36, 0.1}, {0.4, 0.0}, {0.45, 0.07}, {0.5, -0.02}, {0.55, 0.07}, {0.6, 0.0}, {0.64, 0.1}});
    }
}

void spider(Canvas& c) {
    const QColor shell("#3a2a2a");
    // Jointed legs, four a side, then the abdomen with a red mark, the head and eyes.
    c.pen(QColor("#1a1010"), 0.03);
    for (int i = 0; i < 4; ++i) {
        const double y = 0.5 + i * 0.05, knee = 0.22 + i * 0.06;
        for (const double side : {-1.0, 1.0}) {
            const auto X = [&](double f) { return 0.5 + side * f; };
            c.line(X(0.1), y, X(0.3), knee);
            c.line(X(0.3), knee, X(0.44 - i * 0.02), 0.62 + i * 0.09);
        }
    }
    c.pen(kInk, 0.02);
    c.p.setBrush(lit(c, 0.28, 0.42, 0.44, 0.46, shell, 150, 130));
    c.p.drawEllipse(c.box(0.28, 0.44, 0.44, 0.44));
    c.p.setBrush(QColor("#c02020"));
    c.poly({{0.5, 0.56}, {0.56, 0.64}, {0.5, 0.72}, {0.44, 0.64}});
    c.pen(shell.lighter(150), 0.008);
    c.p.setBrush(Qt::NoBrush);
    c.p.drawArc(c.box(0.32, 0.47, 0.36, 0.36), 60 * 16, 60 * 16);  // shine
    c.pen(kInk, 0.02);
    c.p.setBrush(lit(c, 0.38, 0.28, 0.24, 0.2, shell, 140, 130));
    c.p.drawEllipse(c.box(0.38, 0.28, 0.24, 0.2));
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(QColor("#ff3030"));
    for (const auto& [x, y] : {std::pair{0.44, 0.33}, std::pair{0.53, 0.33}, std::pair{0.47, 0.37}, std::pair{0.5, 0.37}})
        c.p.drawEllipse(c.box(x, y, 0.035, 0.03));
    c.pen(QColor("#e8e0d0"), 0.015);  // fangs
    c.line(0.46, 0.46, 0.47, 0.5);
    c.line(0.54, 0.46, 0.53, 0.5);
}

void elemental(Canvas& c) {
    // Layers of fire, a bright core with eyes.
    const auto tongue = [&](double x, double top, double w, const QColor& c0, const QColor& c1) {
        QPainterPath f;
        f.moveTo(c.at(x, top));
        f.cubicTo(c.at(x + w * 0.7, top + 0.3), c.at(x + w, 0.7), c.at(x + w * 0.5, 0.95));
        f.lineTo(c.at(x - w * 0.5, 0.95));
        f.cubicTo(c.at(x - w, 0.7), c.at(x - w * 0.7, top + 0.3), c.at(x, top));
        QLinearGradient g(c.at(x, top), c.at(x, 0.95));
        g.setColorAt(0, c0);
        g.setColorAt(1, c1);
        c.p.setBrush(g);
        c.p.drawPath(f);
    };
    c.pen(QColor("#6a1a08"), 0.015);
    tongue(0.3, 0.22, 0.2, QColor("#ffb030"), QColor("#c02a08"));
    tongue(0.72, 0.18, 0.2, QColor("#ffb030"), QColor("#c02a08"));
    tongue(0.5, 0.0, 0.36, QColor("#ffe060"), QColor("#e03a10"));
    c.p.setPen(Qt::NoPen);
    tongue(0.5, 0.3, 0.2, QColor("#fffbe0"), QColor("#ffb020"));
    c.p.setBrush(QColor("#5a1a08"));
    c.p.drawEllipse(c.box(0.41, 0.42, 0.07, 0.06));
    c.p.drawEllipse(c.box(0.52, 0.42, 0.07, 0.06));
    flames(c, 0.16, 0.5, 0.12);
    flames(c, 0.86, 0.46, 0.12);
}

void lizard(Canvas& c, const QColor& scales, int heads) {
    // Basilisks and salamanders (one head) and hydras (many), with a spiny back.
    c.pen(kInk, 0.02);
    for (const double lx : {0.2, 0.56}) {
        c.p.setBrush(lit(c, lx, 0.7, 0.1, 0.22, scales.darker(130)));
        c.poly({{lx, 0.7}, {lx + 0.08, 0.7}, {lx + 0.1, 0.92}, {lx - 0.02, 0.92}});
    }
    QPainterPath tail;
    tail.moveTo(c.at(0.16, 0.62));
    tail.cubicTo(c.at(0.06, 0.66), c.at(0.0, 0.76), c.at(0.02, 0.84));
    tail.cubicTo(c.at(0.08, 0.78), c.at(0.12, 0.72), c.at(0.2, 0.72));
    c.p.setBrush(lit(c, 0.0, 0.62, 0.2, 0.22, scales.darker(110)));
    c.p.drawPath(tail);
    QPainterPath body;
    body.addEllipse(QRectF(c.at(0.12, 0.54), c.at(0.76, 0.82)));
    c.p.setBrush(lit(c, 0.12, 0.54, 0.64, 0.28, scales, 135, 130));
    c.p.drawPath(body);
    c.p.setBrush(lit(c, 0.2, 0.7, 0.5, 0.1, QColor("#e8d8a0")));
    c.p.drawChord(c.box(0.22, 0.64, 0.46, 0.18), 200 * 16, 140 * 16);
    c.p.setBrush(scales.darker(140));  // the spines
    for (int k = 0; k < 6; ++k) {
        const double sx = 0.22 + k * 0.08;
        c.poly({{sx, 0.56}, {sx + 0.03, 0.47}, {sx + 0.06, 0.56}});
    }
    c.pen(scales.darker(150), 0.008);
    for (int row = 0; row < 2; ++row)
        for (int k = 0; k < 6; ++k) c.p.drawArc(c.box(0.2 + k * 0.08 + row * 0.04, 0.6 + row * 0.05, 0.07, 0.05), 180 * 16, 180 * 16);
    c.pen(kInk, 0.02);
    for (const double lx : {0.3, 0.64}) {
        c.p.setBrush(lit(c, lx, 0.7, 0.1, 0.22, scales));
        c.poly({{lx, 0.72}, {lx + 0.08, 0.72}, {lx + 0.1, 0.94}, {lx - 0.02, 0.94}});
    }
    for (int i = 0; i < heads; ++i) {
        const double hx = heads == 1 ? 0.8 : 0.42 + (i - (heads - 1) / 2.0) * 0.15 + 0.12;
        const double hy = heads == 1 ? 0.48 : 0.14 + std::abs(i - (heads - 1) / 2.0) * 0.06;
        QPainterPath neck;
        neck.moveTo(c.at(0.6, 0.6));
        neck.quadTo(c.at((0.6 + hx) / 2 - 0.04, (0.6 + hy) / 2), c.at(hx, hy + 0.08));
        c.pen(kInk, 0.075);
        c.p.setBrush(Qt::NoBrush);
        c.p.drawPath(neck);
        c.pen(scales, 0.055);
        c.p.drawPath(neck);
        c.pen(kInk, 0.018);
        QPainterPath head;
        head.moveTo(c.at(hx - 0.07, hy + 0.08));
        head.cubicTo(c.at(hx - 0.06, hy - 0.01), c.at(hx + 0.08, hy - 0.01), c.at(hx + 0.14, hy + 0.06));
        head.lineTo(c.at(hx + 0.13, hy + 0.11));
        head.cubicTo(c.at(hx + 0.04, hy + 0.13), c.at(hx - 0.04, hy + 0.13), c.at(hx - 0.07, hy + 0.08));
        c.p.setBrush(lit(c, hx - 0.07, hy, 0.21, 0.13, scales.lighter(110), 125, 125));
        c.p.drawPath(head);
        c.p.setPen(Qt::NoPen);
        c.p.setBrush(QColor("#ffe040"));
        c.p.drawEllipse(c.box(hx + 0.02, hy + 0.03, 0.035, 0.03));
        c.p.setBrush(kInk);
        c.p.drawEllipse(c.box(hx + 0.032, hy + 0.035, 0.012, 0.02));
        c.pen(QColor("#d03030"), 0.012);  // a forked tongue
        c.line(hx + 0.13, hy + 0.09, hx + 0.18, hy + 0.1);
    }
}

void minotaur(Canvas& c) {
    brute(c, QColor("#7a4a2a"), false);
    c.pen(kInk, 0.02);
    c.p.setBrush(QColor("#ece4cc"));
    c.poly({{0.36, 0.14}, {0.18, 0.02}, {0.38, 0.2}});
    c.poly({{0.64, 0.14}, {0.82, 0.02}, {0.62, 0.2}});
    c.p.setBrush(kSteel);  // a double axe
    c.pen(kWood.darker(120), 0.04);
    c.line(0.86, 0.92, 0.86, 0.12);
    c.pen(kInk, 0.02);
    c.p.setBrush(kSteel);
    c.poly({{0.86, 0.14}, {0.98, 0.06}, {0.98, 0.34}, {0.86, 0.26}});
    c.poly({{0.86, 0.14}, {0.74, 0.06}, {0.74, 0.34}, {0.86, 0.26}});
}

void swordsman(Canvas& c, const QColor& coat) {
    figure(c, coat, true);
    c.pen(kInk, 0.025);
    c.p.setBrush(kSteel);
    c.poly({{0.72, 0.62}, {0.76, 0.1}, {0.8, 0.62}});
    c.p.setBrush(QColor("#c9a24a"));
    c.p.drawRect(c.box(0.68, 0.6, 0.16, 0.04));
}

// ---- Ashborn: red-skinned, horned fire folk.
const QColor kEmber("#ff8a20");

void horns(Canvas& c, double x, double y, double size, const QColor& colour = QColor("#2a1a14")) {
    c.pen(kInk, 0.02);
    c.p.setBrush(colour);
    c.poly({{x - size * 0.5, y + size * 0.3}, {x - size * 0.9, y - size * 0.6}, {x - size * 0.2, y + size * 0.1}});
    c.poly({{x + size * 0.5, y + size * 0.3}, {x + size * 0.9, y - size * 0.6}, {x + size * 0.2, y + size * 0.1}});
}

void flames(Canvas& c, double x, double y, double size) {
    // A little fire: an orange tongue with a yellow heart.
    c.p.setPen(Qt::NoPen);
    for (const auto& [colour, k] : {std::pair{QColor("#e04010"), 1.0}, std::pair{kEmber, 0.7}, std::pair{QColor("#ffe060"), 0.4}}) {
        QPainterPath f;
        f.moveTo(c.at(x, y));
        f.cubicTo(c.at(x - size * 0.6 * k, y - size * 0.2 * k), c.at(x - size * 0.2 * k, y - size * 0.7 * k), c.at(x, y - size * k));
        f.cubicTo(c.at(x + size * 0.2 * k, y - size * 0.6 * k), c.at(x + size * 0.6 * k, y - size * 0.3 * k), c.at(x, y));
        c.p.setBrush(colour);
        c.p.drawPath(f);
    }
}

void imp(Canvas& c, bool fiend) {
    const QColor skin(fiend ? "#a82a2a" : "#d04a3a");
    wings(c, QColor(fiend ? "#4a1a1a" : "#6a2a2a"));
    c.pen(skin.darker(140), 0.03);  // a tail with a barb
    c.p.setBrush(Qt::NoBrush);
    QPainterPath tail;
    tail.moveTo(c.at(0.55, 0.78));
    tail.cubicTo(c.at(0.8, 0.95), c.at(0.92, 0.75), c.at(0.86, 0.6));
    c.p.drawPath(tail);
    c.pen(kInk, 0.02);
    c.p.setBrush(skin.darker(140));
    c.poly({{0.86, 0.56}, {0.92, 0.64}, {0.8, 0.64}});
    c.p.setBrush(skin.darker(120));
    c.p.drawRect(c.box(0.4, 0.7, 0.07, 0.2));
    c.p.drawRect(c.box(0.53, 0.7, 0.07, 0.2));
    c.p.setBrush(skin);
    c.p.drawEllipse(c.box(0.36, 0.44, 0.28, 0.3));
    c.p.drawEllipse(c.box(0.38, 0.22, 0.24, 0.24));
    horns(c, 0.5, 0.24, 0.12);
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(QColor("#ffe040"));
    c.p.drawEllipse(c.box(0.43, 0.3, 0.04, 0.04));
    c.p.drawEllipse(c.box(0.53, 0.3, 0.04, 0.04));
    c.pen(kInk, 0.02);  // a pitchfork
    c.line(0.24, 0.92, 0.24, 0.28);
    c.p.setBrush(Qt::NoBrush);
    c.line(0.18, 0.28, 0.3, 0.28);
    for (const double x : {0.18, 0.24, 0.3}) c.line(x, 0.28, x, 0.18);
    if (fiend) flames(c, 0.24, 0.17, 0.12);
}

void hellhound(Canvas& c, bool threeHeads) {
    quadruped(c, QColor(threeHeads ? "#4a1a18" : "#6a2a20"), false, threeHeads ? 3 : 1, kEmber);
    flames(c, 0.04, 0.34, 0.2);  // a burning tail tip
    flames(c, 0.5, 0.42, 0.1);   // embers along the back
    flames(c, 0.36, 0.42, 0.08);
}

void witch(Canvas& c, const QColor& robe, bool pyre) {
    robedCaster(c, robe, false, kEmber);
    c.pen(kInk, 0.025);  // a pointed hat
    c.p.setBrush(robe.darker(140));
    c.poly({{0.3, 0.16}, {0.46, -0.04}, {0.56, 0.02}, {0.52, 0.14}, {0.7, 0.16}});
    flames(c, 0.8, 0.18, pyre ? 0.22 : 0.14);
    if (pyre) {
        c.p.setPen(Qt::NoPen);
        c.p.setBrush(QColor("#ffd84a"));
        c.p.drawRect(c.box(0.3, 0.62, 0.4, 0.04));
    }
}

void hornedBrute(Canvas& c, bool overseer) {
    brute(c, QColor(overseer ? "#8a2a20" : "#b04a30"), false);
    horns(c, 0.5, 0.12, 0.18, QColor("#e8dcc0"));
    c.pen(kInk, 0.02);
    if (overseer) {  // a whip and a spiked collar
        c.p.setBrush(kSteel);
        c.p.drawRect(c.box(0.34, 0.34, 0.32, 0.06));
        c.pen(QColor("#3a2a1a"), 0.03);
        c.p.setBrush(Qt::NoBrush);
        QPainterPath whip;
        whip.moveTo(c.at(0.86, 0.5));
        whip.cubicTo(c.at(1.0, 0.2), c.at(0.7, 0.1), c.at(0.95, 0.02));
        c.p.drawPath(whip);
    } else {  // a heavy cleaver
        c.pen(kWood.darker(120), 0.04);
        c.line(0.86, 0.6, 0.86, 0.22);
        c.pen(kInk, 0.02);
        c.p.setBrush(kSteel);
        c.poly({{0.86, 0.22}, {1.0, 0.18}, {1.0, 0.42}, {0.86, 0.38}});
    }
}

void drake(Canvas& c, const QColor& scales) {
    wyrm(c, scales, false, true);
}

void pitLord(Canvas& c) {
    wings(c, QColor("#2a1010"));
    brute(c, QColor("#7a1a18"), false);
    horns(c, 0.5, 0.1, 0.22, QColor("#1a1010"));
    c.pen(kInk, 0.02);
    c.p.setBrush(QColor("#2a1a1a"));
    c.p.drawRoundedRect(c.box(0.22, 0.34, 0.56, 0.2), c.s(0.06), c.s(0.06));  // dark armour
    flames(c, 0.86, 0.36, 0.3);  // a fire in the hand
    flames(c, 0.2, 0.98, 0.16);
    flames(c, 0.8, 0.98, 0.16);
}

// ---- Tidefolk: sailors and sea creatures in blues and teals.
const QColor kSea("#2a7a9a");

void sailor(Canvas& c, const QColor& coat, bool hat) {
    figure(c, coat, false);
    c.pen(kInk, 0.02);
    c.p.setBrush(QColor("#f0f0f0"));  // a striped shirt
    c.p.drawRect(c.box(0.38, 0.4, 0.24, 0.14));
    c.pen(QColor("#2a4a8a"), 0.025);
    for (const double y : {0.44, 0.5}) c.line(0.39, y, 0.61, y);
    c.pen(kInk, 0.02);
    if (hat) {  // a tricorn
        c.p.setBrush(QColor("#1a1a24"));
        c.poly({{0.3, 0.18}, {0.5, 0.04}, {0.7, 0.18}, {0.5, 0.14}});
        c.p.setBrush(QColor("#ffd84a"));
        c.p.drawEllipse(c.box(0.47, 0.08, 0.06, 0.06));
    } else {  // a bandana
        c.p.setBrush(QColor("#c0302a"));
        c.p.drawChord(c.box(0.39, 0.1, 0.22, 0.16), 0, 180 * 16);
    }
    c.p.setBrush(kSteel);  // a curved cutlass
    QPainterPath blade;
    blade.moveTo(c.at(0.72, 0.62));
    blade.quadTo(c.at(0.92, 0.4), c.at(0.84, 0.16));
    blade.quadTo(c.at(0.8, 0.42), c.at(0.68, 0.6));
    c.p.drawPath(blade);
    c.p.setBrush(QColor("#c9a24a"));
    c.p.drawRect(c.box(0.64, 0.6, 0.12, 0.04));
}

void harpooner(Canvas& c, bool master) {
    figure(c, QColor(master ? "#1a5a6a" : "#4a6a7a"), master, 0.42);
    c.pen(kWood.darker(120), 0.035);
    c.line(0.2, 0.8, 0.86, 0.12);  // the harpoon
    c.pen(kInk, 0.02);
    c.p.setBrush(kSteel);
    c.poly({{0.92, 0.06}, {0.8, 0.12}, {0.86, 0.18}});
    c.poly({{0.84, 0.14}, {0.82, 0.24}, {0.78, 0.18}});
    c.pen(QColor("#d8c8a0"), 0.015);  // the rope
    c.p.setBrush(Qt::NoBrush);
    c.p.drawArc(c.box(0.06, 0.6, 0.2, 0.3), 90 * 16, 240 * 16);
}

void triton(Canvas& c, bool guard) {
    const QColor scales(guard ? "#2a6a7a" : "#3a8a8a"), skin("#8ac8c0");
    c.pen(kInk, 0.02);
    // The trident behind, then a scaled fish tail instead of legs.
    c.pen(QColor("#a8822a"), 0.032);
    c.line(0.78, 0.92, 0.78, 0.12);
    c.line(0.7, 0.18, 0.86, 0.18);
    for (const double x : {0.7, 0.78, 0.86}) c.line(x, 0.18, x, 0.05);
    c.pen(kInk, 0.02);
    QPainterPath tail;
    tail.moveTo(c.at(0.36, 0.58));
    tail.cubicTo(c.at(0.3, 0.8), c.at(0.5, 0.86), c.at(0.62, 0.88));
    tail.lineTo(c.at(0.76, 0.76));
    tail.lineTo(c.at(0.74, 0.98));
    tail.lineTo(c.at(0.58, 0.95));
    tail.cubicTo(c.at(0.44, 0.92), c.at(0.6, 0.72), c.at(0.64, 0.58));
    tail.closeSubpath();
    c.p.setBrush(lit(c, 0.3, 0.58, 0.46, 0.4, scales, 135, 130));
    c.p.drawPath(tail);
    c.pen(scales.darker(150), 0.008);
    for (int row = 0; row < 3; ++row)
        for (int k = 0; k < 3; ++k) c.p.drawArc(c.box(0.38 + k * 0.07 + row * 0.03, 0.62 + row * 0.07, 0.07, 0.05), 180 * 16, 180 * 16);
    c.pen(kInk, 0.02);
    // The body, arms and a finned head.
    QPainterPath torso;
    torso.moveTo(c.at(0.34, 0.6));
    torso.cubicTo(c.at(0.3, 0.42), c.at(0.34, 0.34), c.at(0.5, 0.33));
    torso.cubicTo(c.at(0.66, 0.34), c.at(0.7, 0.42), c.at(0.66, 0.6));
    torso.closeSubpath();
    c.p.setBrush(lit(c, 0.3, 0.33, 0.4, 0.27, skin, 130, 130));
    c.p.drawPath(torso);
    c.p.setBrush(lit(c, 0.66, 0.38, 0.14, 0.12, skin.darker(105)));
    c.poly({{0.64, 0.37}, {0.78, 0.44}, {0.76, 0.5}, {0.62, 0.45}});
    c.p.drawEllipse(c.box(0.74, 0.42, 0.07, 0.07));
    c.p.setBrush(lit(c, 0.4, 0.12, 0.2, 0.22, skin, 115, 120));
    c.p.drawEllipse(c.box(0.4, 0.12, 0.2, 0.22));
    c.p.setBrush(lit(c, 0.32, 0.12, 0.1, 0.12, scales));  // ear fins
    c.poly({{0.41, 0.2}, {0.32, 0.14}, {0.4, 0.26}});
    c.poly({{0.59, 0.2}, {0.68, 0.14}, {0.6, 0.26}});
    faceDetails(c, 0.5, 0.22, 0.19, QColor("#1a4a6a"));
    if (guard) {
        c.pen(kInk, 0.018);
        c.p.setBrush(lit(c, 0.38, 0.06, 0.24, 0.12, QColor("#c9a24a")));
        c.p.drawChord(c.box(0.38, 0.06, 0.24, 0.2), 0, 180 * 16);
        // A scallop-shell shield.
        c.p.setBrush(lit(c, 0.1, 0.36, 0.24, 0.32, QColor("#f0d8c0"), 125, 125));
        c.p.drawChord(c.box(0.1, 0.36, 0.24, 0.36), 0, 180 * 16);
        c.pen(QColor("#c8a080"), 0.01);
        for (int k = 0; k < 5; ++k) c.line(0.22, 0.54, 0.12 + k * 0.05, 0.4);
    }
}

void serpent(Canvas& c, const QColor& scales, bool elder) {
    // Coils rising out of the waves.
    c.pen(QColor("#e8f4ff"), 0.025);
    c.p.setBrush(kSea);
    c.p.drawRect(c.box(0.0, 0.78, 1.0, 0.2));
    c.pen(kInk, 0.025);
    c.p.setBrush(scales);
    c.p.drawChord(c.box(0.04, 0.6, 0.3, 0.36), 0, 180 * 16);
    c.p.drawChord(c.box(0.36, 0.56, 0.3, 0.44), 0, 180 * 16);
    QPainterPath neck;
    neck.moveTo(c.at(0.68, 0.8));
    neck.cubicTo(c.at(0.72, 0.4), c.at(0.92, 0.4), c.at(0.78, 0.12));
    neck.lineTo(c.at(0.66, 0.2));
    neck.cubicTo(c.at(0.76, 0.42), c.at(0.6, 0.5), c.at(0.58, 0.8));
    neck.closeSubpath();
    c.p.drawPath(neck);
    c.p.setBrush(scales.lighter(115));
    c.p.drawEllipse(c.box(0.6, 0.06, 0.28, 0.16));  // head
    if (elder) {  // a fin crest
        c.p.setBrush(QColor("#c84a4a"));
        c.poly({{0.66, 0.08}, {0.6, -0.04}, {0.74, 0.04}, {0.76, -0.06}, {0.82, 0.06}});
    }
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(QColor("#ffe040"));
    c.p.drawEllipse(c.box(0.76, 0.1, 0.05, 0.05));
    c.pen(QColor(255, 255, 255, 200), 0.02);
    c.p.setBrush(Qt::NoBrush);
    for (const double x : {0.1, 0.42, 0.62}) c.p.drawArc(c.box(x, 0.76, 0.16, 0.08), 0, 180 * 16);
}

void kraken(Canvas& c) {
    const QColor skin("#8a3a6a");
    c.pen(kInk, 0.025);
    c.p.setBrush(kSea);
    c.p.drawRect(c.box(0.0, 0.8, 1.0, 0.18));
    // Tentacles, then the head.
    c.p.setBrush(skin);
    for (int i = 0; i < 5; ++i) {
        const double x = 0.1 + i * 0.2, bend = (i % 2 ? 0.1 : -0.1);
        QPainterPath t;
        t.moveTo(c.at(x - 0.05, 0.84));
        t.cubicTo(c.at(x - 0.08 + bend, 0.55), c.at(x + bend * 2, 0.4), c.at(x + bend, 0.3));
        t.cubicTo(c.at(x + bend * 2 + 0.04, 0.42), c.at(x + 0.04 + bend, 0.6), c.at(x + 0.05, 0.84));
        t.closeSubpath();
        c.p.drawPath(t);
    }
    c.p.drawEllipse(c.box(0.28, 0.1, 0.44, 0.5));
    c.p.setBrush(skin.lighter(130));
    c.p.drawEllipse(c.box(0.34, 0.14, 0.16, 0.14));
    c.p.setBrush(QColor("#ffe040"));
    c.p.drawEllipse(c.box(0.36, 0.38, 0.1, 0.08));
    c.p.drawEllipse(c.box(0.54, 0.38, 0.1, 0.08));
    c.p.setBrush(kInk);
    c.p.drawRect(c.box(0.4, 0.39, 0.02, 0.06));
    c.p.drawRect(c.box(0.58, 0.39, 0.02, 0.06));
}

}  // namespace

void drawHeroRider(QPainter& p, const QRectF& r, const QColor& owner) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    Canvas c{p, r};
    horseBody(c, QColor("#8a6038"), true);
    c.pen(kInk, 0.02);
    // A saddle cloth and a cape in the owner's colour.
    c.p.setBrush(lit(c, 0.3, 0.46, 0.34, 0.2, owner));
    c.poly({{0.3, 0.47}, {0.62, 0.47}, {0.6, 0.66}, {0.32, 0.66}});
    c.p.setBrush(QColor("#e8c04a"));
    c.p.drawRect(c.box(0.3, 0.62, 0.3, 0.025));
    QPainterPath cape;
    cape.moveTo(c.at(0.42, 0.24));
    cape.cubicTo(c.at(0.3, 0.3), c.at(0.24, 0.42), c.at(0.22, 0.54));
    cape.lineTo(c.at(0.4, 0.5));
    cape.closeSubpath();
    c.p.setBrush(lit(c, 0.22, 0.24, 0.2, 0.3, owner.darker(110)));
    c.p.drawPath(cape);
    // The rider: leg, armoured body, arm, head with a crested helmet.
    c.p.setBrush(lit(c, 0.43, 0.44, 0.07, 0.2, QColor("#4a3020")));
    c.p.drawRoundedRect(c.box(0.43, 0.44, 0.07, 0.2), c.s(0.03), c.s(0.03));
    c.p.setBrush(lit(c, 0.37, 0.22, 0.2, 0.26, kSteel, 135, 130));
    c.p.drawRoundedRect(c.box(0.37, 0.22, 0.2, 0.26), c.s(0.06), c.s(0.06));
    c.p.setBrush(owner);  // a tabard
    c.poly({{0.41, 0.26}, {0.53, 0.26}, {0.52, 0.46}, {0.42, 0.46}});
    c.p.setBrush(lit(c, 0.38, 0.06, 0.18, 0.18, kSkin, 115, 120));
    c.p.drawEllipse(c.box(0.39, 0.07, 0.16, 0.17));
    faceDetails(c, 0.48, 0.16, 0.15);
    c.p.setBrush(lit(c, 0.38, 0.04, 0.18, 0.1, kSteel, 135, 130));
    c.p.drawChord(c.box(0.38, 0.04, 0.18, 0.14), 0, 180 * 16);
    c.p.setBrush(owner);
    c.poly({{0.44, 0.05}, {0.47, -0.04}, {0.5, 0.05}});
    // The banner on a pole.
    c.pen(kWood.darker(130), 0.03);
    c.line(0.62, 0.62, 0.66, -0.02);
    c.pen(kInk, 0.015);
    QPainterPath flag;
    flag.moveTo(c.at(0.66, 0.0));
    flag.cubicTo(c.at(0.78, -0.03), c.at(0.86, 0.04), c.at(0.98, 0.01));
    flag.lineTo(c.at(0.94, 0.1));
    flag.lineTo(c.at(0.98, 0.19));
    flag.cubicTo(c.at(0.86, 0.22), c.at(0.78, 0.15), c.at(0.655, 0.18));
    flag.closeSubpath();
    c.p.setBrush(lit(c, 0.66, -0.03, 0.32, 0.22, owner, 140, 130));
    c.p.drawPath(flag);
    c.p.setBrush(QColor("#ffd84a"));
    c.p.drawEllipse(c.box(0.76, 0.05, 0.06, 0.06));
    p.restore();
}

void drawUnitIcon(QPainter& p, const QRectF& r, const QString& id, int rank) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    Canvas c{p, r};
    if (id == "militia") spearman(c, false);
    else if (id == "pikeman") spearman(c, true);
    else if (id.startsWith("archer")) archer(c, rank >= 2 ? QColor("#8a2a2a") : rank == 1 ? QColor("#3a64b0") : QColor("#7a6040"), rank, false);
    else if (id == "elf_archer") archer(c, QColor("#3e7a3a"), 0, true);
    else if (id == "elf_marksman") archer(c, QColor("#2a5a5a"), 1, true);
    else if (id == "skeleton") skeleton(c, false);
    else if (id == "skeleton_warrior") skeleton(c, true);
    else if (id == "zombie") zombie(c);
    else if (id == "wraith") wraith(c);
    else if (id == "green_dragon") dragon(c, QColor("#6ab85a"));
    else if (id == "emerald_dragon") dragon(c, QColor("#2ad88a"));
    else if (id == "ghost_dragon") dragon(c, QColor("#a8d8e8"));
    else if (id == "archangel") { angel(c); c.pen(kInk, 0.02); c.p.setBrush(QColor("#ffd84a")); c.poly({{0.36, 0.08}, {0.4, 0.0}, {0.46, 0.06}, {0.5, -0.02}, {0.54, 0.06}, {0.6, 0.0}, {0.64, 0.08}}); }
    else if (id == "halberdier") { spearman(c, true); c.pen(kInk, 0.02); c.p.setBrush(kSteel); c.poly({{0.75, 0.1}, {0.92, 0.04}, {0.92, 0.2}, {0.75, 0.18}}); }
    else if (id == "doom_lord") { swordsman(c, QColor("#4a1418")); c.pen(kInk, 0.02); c.p.setBrush(QColor("#ff3030")); c.p.drawEllipse(c.box(0.44, 0.2, 0.04, 0.04)); c.p.drawEllipse(c.box(0.52, 0.2, 0.04, 0.04)); }
    else if (id == "wood_nymph") { wings(c, QColor(180, 240, 200, 160)); dryad(c); }
    else if (id == "centaur_chief") { centaur(c); c.pen(kInk, 0.02); c.p.setBrush(QColor("#ffd84a")); c.poly({{0.56, 0.1}, {0.6, 0.02}, {0.64, 0.08}, {0.68, 0.0}, {0.7, 0.1}}); }
    else if (id == "elder_shaman") robedCaster(c, QColor("#4a3a2a"), false, QColor("#ff60ff"));
    else if (id == "ancient_behemoth") brute(c, QColor("#5a4a3a"), false);
    else if (id == "rune_elder") robedCaster(c, QColor("#3a2a5a"), false, QColor("#c0e8ff"));
    else if (id == "mountain_titan") golem(c, QColor("#6a6a7a"));
    else if (id == "pit_king") { pitLord(c); c.pen(kInk, 0.02); c.p.setBrush(QColor("#ffd84a")); c.poly({{0.4, 0.06}, {0.44, -0.02}, {0.5, 0.04}, {0.56, -0.02}, {0.6, 0.06}}); }
    else if (id == "tide_oracle") robedCaster(c, QColor("#1a4a7a"), false, QColor("#e0ffff"));
    else if (id == "elder_kraken") kraken(c);
    else if (id.contains("dragon")) dragon(c);
    else if (id.contains("eagle")) eagle(c);
    else if (id.contains("snail")) snail(c);
    else if (id == "treant" || id == "ancient_treant") treant(c);
    else if (id == "unicorn") unicorn(c);
    else if (id == "woodsman") woodsman(c);
    else if (id == "catapult") catapult(c);
    else if (id == "swordsman") swordsman(c, QColor("#8a6a3a"));
    else if (id == "crusader") swordsman(c, QColor("#d8d8e0"));
    else if (id == "monk") robedCaster(c, QColor("#8a6a3a"), false, QColor("#fff0a0"));
    else if (id == "cavalier") cavalier(c);
    else if (id == "angel") angel(c);
    else if (id == "dryad") dryad(c);
    else if (id == "centaur") centaur(c);
    else if (id == "ghoul") zombie(c);
    else if (id == "lich") robedCaster(c, QColor("#2a2030"), true, QColor("#60ff90"));
    else if (id == "spectre") wraith(c);
    else if (id == "vampire") vampire(c);
    else if (id == "dread_knight") swordsman(c, QColor("#2a2630"));
    else if (id == "wolf") wolf(c);
    else if (id == "harpy") { wings(c, QColor("#8a6a4a")); figure(c, QColor("#6a5040"), false); }
    else if (id == "troll") brute(c, QColor("#6a8a5a"), true);
    else if (id == "dwarf_warrior") { dwarf(c, QColor("#7a5a3a"), true); axe(c, 0.66); }
    else if (id == "dwarf_veteran") { dwarf(c, QColor("#3a5a8a"), true); axe(c, 0.66); }
    else if (id == "axe_thrower" || id == "axe_master") { dwarf(c, id == "axe_master" ? QColor("#8a2a2a") : QColor("#6a6a3a"), false); axe(c, 0.7); axe(c, 0.08); }
    else if (id == "stone_golem") golem(c, QColor("#9a968c"));
    else if (id == "iron_golem") golem(c, QColor("#7a8090"));
    else if (id == "rune_priest") robedCaster(c, QColor("#5a4a6a"), false, QColor("#80c0ff"));
    else if (id == "bear_rider" || id == "bear_lord") beastRider(c, QColor(id == "bear_lord" ? "#d8d0c8" : "#6a4a2a"), kSkin, QColor("#3a5a8a"));
    else if (id == "earth_titan") golem(c, QColor("#8a7a5a"));
    else if (id == "goblin") orc(c, QColor("#8ab04a"), QColor("#6a5a3a"), 0.8, false);
    else if (id == "hobgoblin") orc(c, QColor("#9a8a3a"), QColor("#7a3a2a"), 0.85, false);
    else if (id == "wolf_rider" || id == "warg_rider") beastRider(c, QColor(id == "warg_rider" ? "#4a4440" : "#7a7470"), QColor("#8ab04a"), QColor("#6a5a3a"));
    else if (id == "orc_warrior") { orc(c, QColor("#5a8a3a"), QColor("#6a4a2a"), 1.0, false); axe(c, 0.68); }
    else if (id == "orc_chieftain") { orc(c, QColor("#4a7a3a"), QColor("#8a2a2a"), 1.0, true); axe(c, 0.68); }
    else if (id == "shaman") robedCaster(c, QColor("#6a5a3a"), false, QColor("#c0ff60"));
    else if (id == "cyclops") cyclops(c, false);
    else if (id == "cyclops_king") cyclops(c, true);
    else if (id == "behemoth") brute(c, QColor("#8a6a4a"), false);
    else if (id == "forest_warden") woodsman(c);
    else if (id == "silver_unicorn") unicorn(c);
    else if (id == "paladin") cavalier(c);
    else if (id == "priest") robedCaster(c, QColor("#e8e0c8"), false, QColor("#fff0a0"));
    else if (id == "vampire_lord") vampire(c);
    else if (id == "arch_lich") robedCaster(c, QColor("#3a1a4a"), true, QColor("#ff60ff"));
    else if (id == "giant_spider") spider(c);
    else if (id == "fire_elemental") elemental(c);
    else if (id == "basilisk") lizard(c, QColor("#6a8a4a"), 1);
    else if (id == "hydra") lizard(c, QColor("#3a7a6a"), 5);
    else if (id == "minotaur") minotaur(c);
    else if (id == "ogre") brute(c, QColor("#c8a070"), true);
    else if (id == "imp") imp(c, false);
    else if (id == "fiend") imp(c, true);
    else if (id == "hellhound") hellhound(c, false);
    else if (id == "cerberus") hellhound(c, true);
    else if (id == "flame_witch") witch(c, QColor("#6a1a2a"), false);
    else if (id == "pyre_witch") witch(c, QColor("#3a0a14"), true);
    else if (id == "horned_brute") hornedBrute(c, false);
    else if (id == "horned_overseer") hornedBrute(c, true);
    else if (id == "ash_drake") drake(c, QColor("#5a4a48"));
    else if (id == "ember_drake") drake(c, QColor("#b04a20"));
    else if (id == "pit_lord") pitLord(c);
    else if (id == "deckhand") sailor(c, QColor("#5a6a8a"), false);
    else if (id == "corsair") sailor(c, QColor("#6a2a3a"), true);
    else if (id == "harpooner") harpooner(c, false);
    else if (id == "harpoon_master") harpooner(c, true);
    else if (id == "tide_priestess") robedCaster(c, QColor("#2a6a9a"), false, QColor("#a0f0ff"));
    else if (id == "triton") triton(c, false);
    else if (id == "triton_guard") triton(c, true);
    else if (id == "sea_serpent") serpent(c, QColor("#3a7a5a"), false);
    else if (id == "elder_serpent") serpent(c, QColor("#2a5a6a"), true);
    else if (id == "kraken") kraken(c);
    else if (id == "salamander") { lizard(c, QColor("#c8502a"), 1); flames(c, 0.06, 0.66, 0.16); }
    else if (id == "sea_witch") robedCaster(c, QColor("#2a5a4a"), false, QColor("#60ffc0"));
    else if (id == "frost_giant") { brute(c, QColor("#a8c8e0"), true); c.pen(kInk, 0.02); c.p.setBrush(QColor("#f4f8ff")); c.p.drawChord(c.box(0.34, 0.04, 0.32, 0.2), 0, 180 * 16); }
    else swordsman(c, QColor::fromHsv(int(qHash(id) % 360), 120, 160));
    p.restore();
}

const QPixmap& unitIcon(const QString& id, int rank, int size) {
    return sprite(QStringLiteral("unit|%1|%2").arg(id).arg(rank), size,
                  [&](QPainter& p, const QRectF& r) { drawUnitIcon(p, r, id, rank); });
}

}  // namespace kot::client::art
