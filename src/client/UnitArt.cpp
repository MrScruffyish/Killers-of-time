// Pictures of the units, drawn in code: one small figure per kind of unit (a
// spearman, an archer, a skeleton, a dragon, ...), chosen from the unit id. Each is
// drawn into a square; art::unitIcon caches them as scaled sprites.

#include <QPainter>
#include <QPainterPath>
#include <cmath>

#include "client/Art.h"

namespace kot::client::art {

namespace {

const QColor kInk(25, 16, 8);
const QColor kSkin("#f0c8a0");
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

// A standing figure: legs, body in `coat`, head; optional helmet.
void figure(Canvas& c, const QColor& coat, bool helmet, double x = 0.5) {
    c.pen(kInk, 0.03);
    c.p.setBrush(coat.darker(130));
    c.p.drawRect(c.box(x - 0.11, 0.66, 0.09, 0.26));  // legs
    c.p.drawRect(c.box(x + 0.02, 0.66, 0.09, 0.26));
    c.p.setBrush(coat);
    c.p.drawRoundedRect(c.box(x - 0.15, 0.36, 0.3, 0.34), c.s(0.06), c.s(0.06));
    c.p.setBrush(kSkin);
    c.p.drawEllipse(c.box(x - 0.1, 0.14, 0.2, 0.22));
    if (helmet) {
        c.p.setBrush(kSteel);
        c.p.drawChord(c.box(x - 0.12, 0.11, 0.24, 0.2), 0, 180 * 16);
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
    figure(c, QColor("#5a6a4a"), false);
    c.pen(kInk, 0.03);
    c.p.setBrush(QColor("#9ab878"));  // green face over the head
    c.p.drawEllipse(c.box(0.4, 0.14, 0.2, 0.22));
    c.pen(QColor("#9ab878"), 0.07);  // arms stretched forward
    c.line(0.6, 0.42, 0.9, 0.38);
    c.line(0.58, 0.5, 0.88, 0.48);
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(kInk);
    c.p.drawEllipse(c.box(0.44, 0.22, 0.04, 0.04));
    c.p.drawEllipse(c.box(0.52, 0.22, 0.04, 0.04));
}

void wraith(Canvas& c) {
    QPainterPath ghost;
    ghost.moveTo(c.at(0.2, 0.95));
    ghost.quadTo(c.at(0.15, 0.2), c.at(0.5, 0.1));
    ghost.quadTo(c.at(0.85, 0.2), c.at(0.8, 0.95));
    for (int i = 0; i < 4; ++i) ghost.quadTo(c.at(0.8 - 0.15 * i - 0.075, 0.82), c.at(0.8 - 0.15 * (i + 1), 0.95));
    QLinearGradient g(c.at(0.5, 0.1), c.at(0.5, 0.95));
    g.setColorAt(0, QColor(220, 230, 255, 240));
    g.setColorAt(1, QColor(120, 130, 190, 120));
    c.pen(QColor(40, 40, 80), 0.025);
    c.p.setBrush(g);
    c.p.drawPath(ghost);
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(QColor("#30c0ff"));
    c.p.drawEllipse(c.box(0.36, 0.3, 0.1, 0.08));
    c.p.drawEllipse(c.box(0.54, 0.3, 0.1, 0.08));
}

void dragon(Canvas& c, const QColor& bone = kBone) {
    // Bony wings, a long skull head.
    c.pen(kInk, 0.025);
    c.p.setBrush(QColor(bone.red(), bone.green(), bone.blue(), 170));
    c.poly({{0.45, 0.45}, {0.05, 0.05}, {0.15, 0.35}, {0.0, 0.4}, {0.2, 0.55}});
    c.poly({{0.55, 0.45}, {0.95, 0.05}, {0.85, 0.35}, {1.0, 0.4}, {0.8, 0.55}});
    c.pen(bone, 0.05);
    c.line(0.3, 0.9, 0.5, 0.5);
    c.line(0.7, 0.9, 0.5, 0.5);
    c.pen(kInk, 0.025);
    c.p.setBrush(bone);
    c.p.drawEllipse(c.box(0.38, 0.42, 0.24, 0.3));  // ribcage
    c.poly({{0.4, 0.3}, {0.6, 0.3}, {0.56, 0.5}, {0.44, 0.5}});  // neck
    c.poly({{0.36, 0.12}, {0.64, 0.12}, {0.6, 0.34}, {0.5, 0.42}, {0.4, 0.34}});  // head
    c.p.setBrush(QColor("#ff4020"));
    c.p.drawEllipse(c.box(0.42, 0.18, 0.06, 0.06));
    c.p.drawEllipse(c.box(0.52, 0.18, 0.06, 0.06));
}

void eagle(Canvas& c) {
    c.pen(kInk, 0.025);
    c.p.setBrush(QColor("#7a5a3a"));
    c.poly({{0.5, 0.45}, {0.02, 0.2}, {0.15, 0.45}, {0.05, 0.5}, {0.4, 0.62}});
    c.poly({{0.5, 0.45}, {0.98, 0.2}, {0.85, 0.45}, {0.95, 0.5}, {0.6, 0.62}});
    c.p.setBrush(kSteel);  // armoured body
    c.p.drawEllipse(c.box(0.36, 0.36, 0.28, 0.44));
    c.p.setBrush(QColor("#f4f0e8"));
    c.p.drawEllipse(c.box(0.4, 0.16, 0.2, 0.22));
    c.p.setBrush(QColor("#ffc020"));
    c.poly({{0.5, 0.26}, {0.6, 0.3}, {0.5, 0.36}});
    c.p.setBrush(kInk);
    c.p.drawEllipse(c.box(0.45, 0.22, 0.04, 0.04));
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
    c.pen(kInk, 0.025);
    c.p.setBrush(QColor("#6a4a28"));
    c.poly({{0.38, 0.95}, {0.42, 0.45}, {0.58, 0.45}, {0.62, 0.95}, {0.54, 0.85}, {0.46, 0.85}});
    c.pen(QColor("#6a4a28"), 0.06);  // branch arms
    c.line(0.42, 0.55, 0.15, 0.4);
    c.line(0.58, 0.55, 0.85, 0.42);
    c.pen(kInk, 0.025);
    c.p.setBrush(QColor("#3e8a3a"));
    c.p.drawEllipse(c.box(0.18, 0.02, 0.64, 0.5));
    c.p.setBrush(QColor("#5aa84a"));
    c.p.drawEllipse(c.box(0.3, 0.08, 0.3, 0.24));
    c.p.setBrush(QColor("#ffe060"));
    c.p.drawEllipse(c.box(0.44, 0.56, 0.05, 0.05));
    c.p.drawEllipse(c.box(0.52, 0.56, 0.05, 0.05));
}

void unicorn(Canvas& c) {
    c.pen(kInk, 0.025);
    c.p.setBrush(QColor("#f6f2fa"));
    c.p.drawEllipse(c.box(0.15, 0.4, 0.6, 0.3));  // body
    c.poly({{0.62, 0.48}, {0.74, 0.18}, {0.9, 0.24}, {0.76, 0.56}});  // neck and head
    c.pen(QColor("#d8d0e8"), 0.05);
    for (const double x : {0.24, 0.34, 0.56, 0.66}) c.line(x, 0.66, x, 0.94);
    c.pen(kInk, 0.02);
    c.p.setBrush(QColor("#ffd84a"));  // the horn
    c.poly({{0.8, 0.2}, {0.92, 0.0}, {0.85, 0.22}});
    c.p.setBrush(QColor("#c080f0"));  // mane
    c.poly({{0.7, 0.2}, {0.62, 0.42}, {0.66, 0.46}, {0.75, 0.24}});
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
    c.pen(kInk, 0.02);
    c.p.setBrush(colour);
    c.poly({{0.42, 0.42}, {0.05, 0.12}, {0.1, 0.38}, {0.02, 0.45}, {0.36, 0.6}});
    c.poly({{0.58, 0.42}, {0.95, 0.12}, {0.9, 0.38}, {0.98, 0.45}, {0.64, 0.6}});
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

void horseBody(Canvas& c, const QColor& coat) {
    c.pen(QPen(coat.darker(170), c.s(0.04)).color(), 0.045);
    for (const double x : {0.2, 0.3, 0.62, 0.72}) c.line(x, 0.66, x, 0.94);
    c.pen(kInk, 0.025);
    c.p.setBrush(coat);
    c.p.drawEllipse(c.box(0.12, 0.48, 0.68, 0.26));
    c.p.setBrush(coat.darker(140));
    c.p.drawEllipse(c.box(0.04, 0.5, 0.12, 0.22));  // tail
}

void centaur(Canvas& c) {
    horseBody(c, QColor("#8a6038"));
    c.pen(kInk, 0.025);
    c.p.setBrush(QColor("#e0b080"));  // the man's body on the horse
    c.p.drawRoundedRect(c.box(0.6, 0.24, 0.18, 0.32), c.s(0.05), c.s(0.05));
    c.p.drawEllipse(c.box(0.6, 0.06, 0.18, 0.2));
    c.pen(kWood.darker(110), 0.04);
    c.p.setBrush(Qt::NoBrush);
    c.p.drawArc(c.box(0.74, 0.12, 0.24, 0.5), -80 * 16, 160 * 16);
}

void cavalier(Canvas& c) {
    horseBody(c, QColor("#f0ece4"));
    c.pen(kInk, 0.025);
    c.p.setBrush(QColor("#3a64b0"));
    c.p.drawRoundedRect(c.box(0.38, 0.22, 0.2, 0.32), c.s(0.05), c.s(0.05));
    c.p.setBrush(kSteel);
    c.p.drawEllipse(c.box(0.39, 0.04, 0.18, 0.2));
    c.pen(kWood.darker(120), 0.035);
    c.line(0.3, 0.6, 0.98, 0.1);  // lance
}

void wolf(Canvas& c) {
    const QColor fur("#7a7470");
    c.pen(fur.darker(160), 0.045);
    for (const double x : {0.25, 0.35, 0.62, 0.72}) c.line(x, 0.62, x, 0.9);
    c.pen(kInk, 0.025);
    c.p.setBrush(fur);
    c.p.drawEllipse(c.box(0.15, 0.42, 0.62, 0.26));
    c.poly({{0.68, 0.5}, {0.98, 0.42}, {0.9, 0.3}, {0.74, 0.24}, {0.7, 0.14}, {0.62, 0.3}});  // head and ears
    c.poly({{0.18, 0.5}, {0.0, 0.32}, {0.06, 0.52}});  // tail
    c.p.setBrush(QColor("#ffd84a"));
    c.p.drawEllipse(c.box(0.78, 0.32, 0.04, 0.04));
}

void brute(Canvas& c, const QColor& skin, bool club) {
    // A big, broad figure: trolls and ogres.
    c.pen(kInk, 0.025);
    c.p.setBrush(skin.darker(120));
    c.p.drawRect(c.box(0.3, 0.7, 0.14, 0.24));
    c.p.drawRect(c.box(0.56, 0.7, 0.14, 0.24));
    c.p.setBrush(QColor("#6a4a28"));
    c.p.drawRoundedRect(c.box(0.22, 0.34, 0.56, 0.42), c.s(0.1), c.s(0.1));
    c.p.setBrush(skin);
    c.p.drawEllipse(c.box(0.34, 0.08, 0.32, 0.3));
    c.p.drawEllipse(c.box(0.1, 0.38, 0.16, 0.3));
    c.p.drawEllipse(c.box(0.74, 0.38, 0.16, 0.3));
    c.p.setBrush(kInk);
    c.p.drawEllipse(c.box(0.42, 0.18, 0.04, 0.04));
    c.p.drawEllipse(c.box(0.54, 0.18, 0.04, 0.04));
    if (club) {
        c.p.setBrush(kWood);
        c.poly({{0.82, 0.5}, {0.98, 0.02}, {0.9, 0.0}, {0.78, 0.48}});
    }
}

void vampire(Canvas& c) {
    c.pen(kInk, 0.025);
    c.p.setBrush(QColor("#1a1020"));  // cape
    c.poly({{0.5, 0.3}, {0.1, 0.95}, {0.9, 0.95}});
    c.p.setBrush(QColor("#8a1a2a"));
    c.poly({{0.5, 0.36}, {0.3, 0.92}, {0.7, 0.92}});
    c.p.setBrush(QColor("#e8e0e8"));
    c.p.drawEllipse(c.box(0.38, 0.1, 0.24, 0.26));
    c.p.setBrush(QColor("#1a1020"));
    c.poly({{0.36, 0.2}, {0.5, 0.06}, {0.64, 0.2}, {0.5, 0.14}});  // hair
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(QColor("#ff3030"));
    c.p.drawEllipse(c.box(0.43, 0.2, 0.04, 0.04));
    c.p.drawEllipse(c.box(0.53, 0.2, 0.04, 0.04));
}

void robedCaster(Canvas& c, const QColor& robe, bool skull, const QColor& orb) {
    c.pen(kInk, 0.025);
    c.p.setBrush(robe);
    c.poly({{0.5, 0.28}, {0.18, 0.95}, {0.82, 0.95}});
    if (skull) skull_(c);
    else {
        c.p.setBrush(kSkin);
        c.p.drawEllipse(c.box(0.39, 0.12, 0.22, 0.24));
        c.p.setBrush(robe.darker(120));
        c.p.drawChord(c.box(0.36, 0.08, 0.28, 0.24), 0, 180 * 16);  // hood
    }
    c.pen(kWood.darker(120), 0.04);
    c.line(0.8, 0.95, 0.8, 0.2);  // staff
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(orb);
    c.p.drawEllipse(c.box(0.73, 0.1, 0.14, 0.14));
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
    c.pen(kInk, 0.025);
    c.p.setBrush(coat.darker(130));
    c.p.drawRect(c.box(0.34, 0.74, 0.12, 0.2));
    c.p.drawRect(c.box(0.54, 0.74, 0.12, 0.2));
    c.p.setBrush(coat);
    c.p.drawRoundedRect(c.box(0.26, 0.44, 0.48, 0.34), c.s(0.08), c.s(0.08));
    c.p.setBrush(kSkin);
    c.p.drawEllipse(c.box(0.37, 0.24, 0.26, 0.24));
    c.p.setBrush(QColor("#c86a2a"));  // the beard
    c.poly({{0.37, 0.38}, {0.63, 0.38}, {0.56, 0.66}, {0.5, 0.7}, {0.44, 0.66}});
    if (helmet) {
        c.p.setBrush(kSteel);
        c.p.drawChord(c.box(0.35, 0.18, 0.3, 0.22), 0, 180 * 16);
        c.p.setBrush(QColor("#ece4cc"));
        c.poly({{0.36, 0.28}, {0.26, 0.14}, {0.38, 0.22}});
        c.poly({{0.64, 0.28}, {0.74, 0.14}, {0.62, 0.22}});
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
    c.pen(kInk, 0.025);
    c.p.setBrush(stone);
    c.p.drawRect(c.box(0.3, 0.7, 0.14, 0.24));
    c.p.drawRect(c.box(0.56, 0.7, 0.14, 0.24));
    c.p.drawRect(c.box(0.24, 0.32, 0.52, 0.42));
    c.p.drawRect(c.box(0.08, 0.34, 0.16, 0.36));
    c.p.drawRect(c.box(0.76, 0.34, 0.16, 0.36));
    c.p.setBrush(stone.lighter(115));
    c.p.drawRect(c.box(0.36, 0.08, 0.28, 0.24));
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(QColor("#ffb040"));
    c.p.drawRect(c.box(0.41, 0.16, 0.06, 0.04));
    c.p.drawRect(c.box(0.53, 0.16, 0.06, 0.04));
    c.pen(stone.darker(150), 0.015);  // cracks and runes
    c.line(0.3, 0.42, 0.42, 0.56);
    c.line(0.58, 0.4, 0.68, 0.6);
}

// Green-skinned figures (orcs, goblins).
void orc(Canvas& c, const QColor& skin, const QColor& coat, double scale, bool horns) {
    const double x0 = 0.5 - 0.5 * scale, k = scale;
    const auto B = [&](double x, double y, double w, double h) { return c.box(x0 + x * k, 1 - (1 - y) * k, w * k, h * k); };
    c.pen(kInk, 0.025);
    c.p.setBrush(coat.darker(130));
    c.p.drawRect(B(0.36, 0.7, 0.1, 0.24));
    c.p.drawRect(B(0.54, 0.7, 0.1, 0.24));
    c.p.setBrush(coat);
    c.p.drawRoundedRect(B(0.3, 0.38, 0.4, 0.36), c.s(0.06), c.s(0.06));
    c.p.setBrush(skin);
    c.p.drawEllipse(B(0.36, 0.12, 0.28, 0.28));
    c.poly({{x0 + 0.36 * k, 1 - (1 - 0.22) * k}, {x0 + 0.24 * k, 1 - (1 - 0.14) * k}, {x0 + 0.38 * k, 1 - (1 - 0.28) * k}});  // ears
    c.poly({{x0 + 0.64 * k, 1 - (1 - 0.22) * k}, {x0 + 0.76 * k, 1 - (1 - 0.14) * k}, {x0 + 0.62 * k, 1 - (1 - 0.28) * k}});
    c.p.setBrush(QColor("#f4f0e0"));  // tusks
    c.p.drawEllipse(B(0.43, 0.33, 0.04, 0.05));
    c.p.drawEllipse(B(0.53, 0.33, 0.04, 0.05));
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(QColor("#ff3020"));
    c.p.drawEllipse(B(0.43, 0.22, 0.04, 0.04));
    c.p.drawEllipse(B(0.53, 0.22, 0.04, 0.04));
    if (horns) {
        c.pen(kInk, 0.02);
        c.p.setBrush(QColor("#ece4cc"));
        c.poly({{x0 + 0.38 * k, 1 - (1 - 0.14) * k}, {x0 + 0.28 * k, 1 - (1 - 0.0) * k}, {x0 + 0.44 * k, 1 - (1 - 0.12) * k}});
        c.poly({{x0 + 0.62 * k, 1 - (1 - 0.14) * k}, {x0 + 0.72 * k, 1 - (1 - 0.0) * k}, {x0 + 0.56 * k, 1 - (1 - 0.12) * k}});
    }
}

void beastRider(Canvas& c, const QColor& beast, const QColor& riderSkin, const QColor& coat) {
    c.pen(beast.darker(170), 0.045);
    for (const double x : {0.25, 0.35, 0.6, 0.7}) c.line(x, 0.66, x, 0.92);
    c.pen(kInk, 0.025);
    c.p.setBrush(beast);
    c.p.drawEllipse(c.box(0.15, 0.5, 0.62, 0.24));
    c.p.drawEllipse(c.box(0.7, 0.42, 0.22, 0.2));  // head
    c.p.setBrush(coat);
    c.p.drawRoundedRect(c.box(0.38, 0.24, 0.2, 0.3), c.s(0.05), c.s(0.05));
    c.p.setBrush(riderSkin);
    c.p.drawEllipse(c.box(0.39, 0.06, 0.18, 0.2));
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
    c.pen(QColor("#2a2020"), 0.035);
    for (int i = 0; i < 4; ++i) {
        const double y = 0.42 + i * 0.1;
        c.line(0.4, 0.55, 0.06, y - 0.12);
        c.line(0.06, y - 0.12, 0.02, y + 0.12);
        c.line(0.6, 0.55, 0.94, y - 0.12);
        c.line(0.94, y - 0.12, 0.98, y + 0.12);
    }
    c.pen(kInk, 0.025);
    c.p.setBrush(QColor("#3a2a2a"));
    c.p.drawEllipse(c.box(0.3, 0.45, 0.4, 0.4));
    c.p.drawEllipse(c.box(0.38, 0.28, 0.24, 0.22));
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(QColor("#ff3030"));
    for (const double x : {0.43, 0.48, 0.52, 0.57}) c.p.drawEllipse(c.box(x - 0.02, 0.34, 0.04, 0.04));
}

void elemental(Canvas& c) {
    QPainterPath flame;
    flame.moveTo(c.at(0.5, 0.02));
    flame.quadTo(c.at(0.86, 0.4), c.at(0.74, 0.95));
    flame.lineTo(c.at(0.26, 0.95));
    flame.quadTo(c.at(0.14, 0.4), c.at(0.5, 0.02));
    QLinearGradient g(c.at(0.5, 0.0), c.at(0.5, 1.0));
    g.setColorAt(0, QColor("#ffe060"));
    g.setColorAt(1, QColor("#e03a10"));
    c.pen(QColor("#6a1a08"), 0.02);
    c.p.setBrush(g);
    c.p.drawPath(flame);
    c.p.setPen(Qt::NoPen);
    c.p.setBrush(QColor("#fff8d0"));
    c.p.drawEllipse(c.box(0.4, 0.42, 0.07, 0.07));
    c.p.drawEllipse(c.box(0.53, 0.42, 0.07, 0.07));
}

void lizard(Canvas& c, const QColor& scales, int heads) {
    // Basilisks (one head) and hydras (many).
    c.pen(kInk, 0.025);
    c.p.setBrush(scales);
    c.p.drawEllipse(c.box(0.12, 0.55, 0.62, 0.28));
    c.poly({{0.14, 0.66}, {0.0, 0.8}, {0.18, 0.74}});  // tail
    c.pen(scales.darker(150), 0.04);
    for (const double x : {0.22, 0.34, 0.54, 0.66}) c.line(x, 0.78, x, 0.94);
    c.pen(kInk, 0.025);
    for (int i = 0; i < heads; ++i) {
        const double hx = heads == 1 ? 0.78 : 0.5 + (i - (heads - 1) / 2.0) * 0.16;
        const double hy = heads == 1 ? 0.5 : 0.2 + (i % 2) * 0.08;
        c.pen(scales, 0.07);
        c.line(0.6, 0.6, hx, hy + 0.1);
        c.pen(kInk, 0.025);
        c.p.setBrush(scales.lighter(110));
        c.p.drawEllipse(c.box(hx - 0.08, hy, 0.18, 0.12));
        c.p.setBrush(QColor("#ffe040"));
        c.p.drawEllipse(c.box(hx + 0.02, hy + 0.03, 0.04, 0.04));
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

}  // namespace

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
    else swordsman(c, QColor::fromHsv(int(qHash(id) % 360), 120, 160));
    p.restore();
}

const QPixmap& unitIcon(const QString& id, int rank, int size) {
    return sprite(QStringLiteral("unit|%1|%2").arg(id).arg(rank), size,
                  [&](QPainter& p, const QRectF& r) { drawUnitIcon(p, r, id, rank); });
}

}  // namespace kot::client::art
