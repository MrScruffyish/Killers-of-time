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

void dragon(Canvas& c) {
    // Bony wings, a long skull head.
    c.pen(kInk, 0.025);
    c.p.setBrush(QColor(236, 228, 204, 170));
    c.poly({{0.45, 0.45}, {0.05, 0.05}, {0.15, 0.35}, {0.0, 0.4}, {0.2, 0.55}});
    c.poly({{0.55, 0.45}, {0.95, 0.05}, {0.85, 0.35}, {1.0, 0.4}, {0.8, 0.55}});
    c.pen(kBone, 0.05);
    c.line(0.3, 0.9, 0.5, 0.5);
    c.line(0.7, 0.9, 0.5, 0.5);
    c.pen(kInk, 0.025);
    c.p.setBrush(kBone);
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
    else if (id.contains("dragon")) dragon(c);
    else if (id.contains("eagle")) eagle(c);
    else if (id.contains("snail")) snail(c);
    else if (id == "treant") treant(c);
    else if (id == "unicorn") unicorn(c);
    else if (id == "woodsman") woodsman(c);
    else if (id == "catapult") catapult(c);
    else swordsman(c, QColor::fromHsv(int(qHash(id) % 360), 120, 160));
    p.restore();
}

const QPixmap& unitIcon(const QString& id, int rank, int size) {
    return sprite(QStringLiteral("unit|%1|%2").arg(id).arg(rank), size,
                  [&](QPainter& p, const QRectF& r) { drawUnitIcon(p, r, id, rank); });
}

}  // namespace kot::client::art
