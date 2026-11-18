// Icons drawn in code at full size: resource symbols, the button icons and the
// special mines (sawmill, quarry, crystal cave). Everything goes through
// the export (a 512 px master with light, shade, outline and shadow) and is
// scaled down, so the drawings can carry detail.

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

#include "artgen/ArtGen.h"

namespace kot::artgen {

namespace {

const QColor kInk(36, 24, 10);

QPen inkPen(double w, const QColor& c = kInk) { return QPen(c, std::max(1.0, w), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin); }

// Lit from the top left: lighter there, darker at the bottom right.
QBrush shine(const QRectF& r, const QColor& c, int lift = 145, int drop = 145) {
    QLinearGradient g(r.topLeft(), r.bottomRight());
    g.setColorAt(0, c.lighter(lift));
    g.setColorAt(1, c.darker(drop));
    return g;
}

// A round thing lit from the top left.
QBrush ball(const QPointF& c, double rad, const QColor& col) {
    QRadialGradient g(c - QPointF(rad * 0.35, rad * 0.4), rad * 1.3);
    g.setColorAt(0, col.lighter(170));
    g.setColorAt(0.5, col);
    g.setColorAt(1, col.darker(170));
    return g;
}

// A four-pointed sparkle.
void glint(QPainter& p, const QPointF& c, double s) {
    QPainterPath path;
    path.moveTo(c.x(), c.y() - s);
    path.quadTo(c, QPointF(c.x() + s, c.y()));
    path.quadTo(c, QPointF(c.x(), c.y() + s));
    path.quadTo(c, QPointF(c.x() - s, c.y()));
    path.quadTo(c, QPointF(c.x(), c.y() - s));
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 235));
    p.drawPath(path);
    p.restore();
}

void groundShadow(QPainter& p, const QRectF& r, double top = 0.8) {
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 70));
    p.drawEllipse(QRectF(r.left() + r.width() * 0.06, r.top() + r.height() * top, r.width() * 0.88, r.height() * 0.18));
}

// A six-sided crystal: two lit faces and a point.
void prism(QPainter& p, const QPointF& base, double angle, double len, double half, const QColor& c, double penW) {
    const QPointF u(std::sin(angle), -std::cos(angle)), n(std::cos(angle), std::sin(angle));
    const QPointF bl = base - n * half, br = base + n * half, top = base + u * len;
    const QPointF tl = bl + u * len, tr = br + u * len, tip = base + u * (len + half * 1.5);
    p.setPen(inkPen(penW, c.darker(320)));
    p.setBrush(c.lighter(135));
    p.drawPolygon(QPolygonF{bl, tl, tip, top, base});
    p.setBrush(c.darker(118));
    p.drawPolygon(QPolygonF{base, top, tip, tr, br});
    p.setPen(QPen(QColor(255, 255, 255, 150), penW * 0.8, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(bl + n * half * 0.35 + u * len * 0.1, bl + n * half * 0.35 + u * len * 0.8);
}

// A log seen end on: a cut face with rings, lit bark round it.
void logEnd(QPainter& p, const QPointF& c, double rad, double penW) {
    p.setPen(inkPen(penW, QColor("#3a2210")));
    p.setBrush(ball(c, rad, QColor("#7a4a22")));
    p.drawEllipse(c, rad, rad);
    QRadialGradient face(c - QPointF(rad * 0.2, rad * 0.2), rad);
    face.setColorAt(0, QColor("#f4d098"));
    face.setColorAt(1, QColor("#c08848"));
    p.setPen(Qt::NoPen);
    p.setBrush(face);
    p.drawEllipse(c, rad * 0.8, rad * 0.8);
    p.setPen(QPen(QColor(150, 92, 40, 210), penW * 0.5));
    p.setBrush(Qt::NoBrush);
    for (int k = 1; k <= 3; ++k) p.drawEllipse(c + QPointF(rad * 0.04 * k, 0), rad * 0.2 * k, rad * 0.19 * k);
    p.setPen(QPen(QColor(110, 60, 24, 220), penW * 0.6, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(c, c + QPointF(rad * 0.55, -rad * 0.3));  // a drying crack
}

void ownerFlag(QPainter& p, const QPointF& foot, double h, const QColor& owner, double penW) {
    p.setPen(inkPen(penW * 1.4, QColor("#3b2a17")));
    p.drawLine(foot, foot - QPointF(0, h));
    QPainterPath cloth;
    const QPointF top = foot - QPointF(0, h);
    cloth.moveTo(top);
    cloth.cubicTo(top + QPointF(h * 0.25, -h * 0.08), top + QPointF(h * 0.35, h * 0.12), top + QPointF(h * 0.55, h * 0.06));
    cloth.lineTo(top + QPointF(h * 0.5, h * 0.3));
    cloth.cubicTo(top + QPointF(h * 0.32, h * 0.38), top + QPointF(h * 0.2, h * 0.2), top + QPointF(0, h * 0.34));
    cloth.closeSubpath();
    p.setPen(inkPen(penW));
    QLinearGradient flag(cloth.boundingRect().topLeft(), cloth.boundingRect().bottomRight());
    flag.setColorAt(0, towardsWhite(owner, 135));  // linear in the owner colour (ownerSprite)
    flag.setColorAt(1, owner.darker(135));
    p.setBrush(flag);
    p.drawPath(cloth);
}

// --- resources ---------------------------------------------------------------

void drawGold(QPainter& p, const QRectF& r) {
    const double w = r.width(), h = r.height();
    const auto X = [&](double f) { return r.left() + w * f; };
    const auto Y = [&](double f) { return r.top() + h * f; };
    const QColor gold("#f2c230");
    const double pw = w / 48;
    // A stack of coins behind, seen from the side.
    for (int i = 0; i < 5; ++i) {
        const QRectF top(X(0.46), Y(0.66 - i * 0.085), w * 0.48, h * 0.17);
        QPainterPath edge;
        edge.addEllipse(top.translated(0, h * 0.06));
        QPainterPath band;
        band.addRect(QRectF(top.left(), top.center().y(), top.width(), h * 0.06));
        edge = edge.united(band);
        p.setPen(inkPen(pw));
        QLinearGradient side(top.topLeft(), top.topRight());
        side.setColorAt(0, gold.darker(105));
        side.setColorAt(0.35, gold.lighter(120));
        side.setColorAt(1, gold.darker(170));
        p.setBrush(side);
        p.drawPath(edge);
        p.setPen(QPen(gold.darker(190), pw * 0.5));  // milled edge
        for (double x = 0.08; x < 0.95; x += 0.09) {
            const double ex = top.left() + top.width() * x;
            const double dy = std::sqrt(std::max(0.0, 1 - std::pow(2 * x - 1, 2))) * top.height() / 2;
            p.drawLine(QPointF(ex, top.center().y() + dy + h * 0.01), QPointF(ex, top.center().y() + dy + h * 0.05));
        }
        p.setPen(inkPen(pw));
        p.setBrush(shine(top, gold, 135, 125));
        p.drawEllipse(top);
        p.setPen(QPen(gold.darker(150), pw * 0.6));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(top.adjusted(w * 0.05, h * 0.025, -w * 0.05, -h * 0.025));
    }
    // A big coin in front, face on, with a crown.
    const QPointF c(X(0.36), Y(0.6));
    const double rad = w * 0.3;
    p.setPen(inkPen(pw * 1.1));
    p.setBrush(QColor(gold.darker(160)));
    p.drawEllipse(c + QPointF(w * 0.025, h * 0.02), rad, rad);  // its thickness
    p.setBrush(ball(c, rad, gold));
    p.drawEllipse(c, rad, rad);
    p.setPen(QPen(gold.darker(150), pw * 0.9));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, rad * 0.82, rad * 0.82);
    p.setPen(QPen(gold.darker(140), pw * 0.4));
    for (int i = 0; i < 36; ++i) {  // beads round the rim
        const double a = M_PI * 2 * i / 36;
        p.drawPoint(c + QPointF(std::cos(a), std::sin(a)) * rad * 0.91);
    }
    const double cw = rad * 0.9, ch = rad * 0.62;
    const QPointF b = c + QPointF(-cw / 2, ch * 0.45);
    QPolygonF crown{b,
                    b + QPointF(0, -ch),
                    b + QPointF(cw * 0.25, -ch * 0.45),
                    b + QPointF(cw * 0.5, -ch * 1.15),
                    b + QPointF(cw * 0.75, -ch * 0.45),
                    b + QPointF(cw, -ch),
                    b + QPointF(cw, 0)};
    p.setPen(inkPen(pw * 0.8, gold.darker(210)));
    p.setBrush(shine(crown.boundingRect(), gold.darker(112), 125, 130));
    p.drawPolygon(crown);
    p.setBrush(QColor("#c8322a"));
    p.drawEllipse(b + QPointF(cw * 0.5, -ch * 0.25), rad * 0.07, rad * 0.07);
    glint(p, c + QPointF(-rad * 0.5, -rad * 0.55), rad * 0.28);
}

void drawWood(QPainter& p, const QRectF& r) {
    const double w = r.width(), h = r.height();
    const double pw = w / 50;
    struct Log {
        QPointF c;
        double rad;
    };
    const Log logs[] = {{{r.left() + w * 0.36, r.top() + h * 0.72}, w * 0.18},
                        {{r.left() + w * 0.72, r.top() + h * 0.72}, w * 0.18},
                        {{r.left() + w * 0.54, r.top() + h * 0.42}, w * 0.18}};
    const QPointF back(-w * 0.24, -h * 0.17);  // logs run back to the upper left
    // Bark first (the ends are all in front).
    for (const Log& l : logs) {
        const QPointF d = back / std::hypot(back.x(), back.y());
        const QPointF n(-d.y(), d.x());
        QPainterPath body;
        body.addEllipse(l.c + back, l.rad, l.rad);
        QPainterPath side;
        side.addPolygon(QPolygonF{l.c + n * l.rad, l.c + back + n * l.rad, l.c + back - n * l.rad, l.c - n * l.rad});
        body = body.united(side);
        QLinearGradient bark(l.c + back + n * l.rad, l.c + back - n * l.rad);
        bark.setColorAt(0, QColor("#4a2c12"));
        bark.setColorAt(0.55, QColor("#8a5a2c"));
        bark.setColorAt(1, QColor("#a8763e"));
        p.setPen(inkPen(pw, QColor("#2e1a08")));
        p.setBrush(bark);
        p.drawPath(body);
        p.setPen(QPen(QColor(46, 26, 10, 190), pw * 0.55, Qt::SolidLine, Qt::RoundCap));
        for (int i = 0; i < 7; ++i) {  // bark furrows along the log
            const double off = -0.75 + 0.25 * i;
            const QPointF s = l.c + n * l.rad * off + back * (0.15 + 0.1 * (i % 3));
            p.drawLine(s, s + back * (0.3 + 0.08 * (i % 2)));
        }
    }
    for (const Log& l : logs) logEnd(p, l.c, l.rad, pw);
    // A twig with a leaf on top.
    const QPointF tw(r.left() + w * 0.62, r.top() + h * 0.24);
    p.setPen(inkPen(pw * 1.2, QColor("#4a2c12")));
    p.drawLine(tw, tw + QPointF(w * 0.16, -h * 0.1));
    QPainterPath leaf;
    const QPointF ls = tw + QPointF(w * 0.12, -h * 0.07);
    leaf.moveTo(ls);
    leaf.quadTo(ls + QPointF(w * 0.06, -h * 0.14), ls + QPointF(w * 0.18, -h * 0.12));
    leaf.quadTo(ls + QPointF(w * 0.12, h * 0.0), ls);
    p.setPen(inkPen(pw * 0.8, QColor("#1e3a10")));
    p.setBrush(shine(leaf.boundingRect(), QColor("#5aa03a")));
    p.drawPath(leaf);
}

void drawStone(QPainter& p, const QRectF& r) {
    const double w = r.width(), h = r.height();
    const double pw = w / 50;
    const auto block = [&](double x, double y, double bw, double bh, int seed) {
        const QRectF front(r.left() + w * x, r.top() + h * y, w * bw, h * bh);
        const QPointF d(w * 0.12, -h * 0.09);
        const QColor base = QColor::fromHsv(35 + seed * 6, 22 + seed * 6, 178 - seed * 6);
        p.setPen(inkPen(pw, QColor("#2a2620")));
        p.setBrush(shine(front, base, 115, 130));
        p.drawRect(front);
        QPolygonF top{front.topLeft(), front.topLeft() + d, front.topRight() + d, front.topRight()};
        p.setBrush(base.lighter(128));
        p.drawPolygon(top);
        QPolygonF side{front.topRight(), front.topRight() + d, front.bottomRight() + d, front.bottomRight()};
        p.setBrush(base.darker(140));
        p.drawPolygon(side);
        // Chisel marks and a chipped corner.
        p.setPen(QPen(base.darker(165), pw * 0.45, Qt::SolidLine, Qt::RoundCap));
        for (int i = 0; i < 9; ++i) {
            const double fx = 0.12 + 0.09 * i, fy = 0.25 + 0.5 * (((i + seed) * 37) % 10) / 10.0;
            const QPointF s(front.left() + front.width() * fx, front.top() + front.height() * fy);
            p.drawLine(s, s + QPointF(front.width() * 0.04, front.height() * 0.08));
        }
        p.setPen(QPen(base.lighter(150), pw * 0.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(front.topLeft() + QPointF(pw, pw), front.topRight() + QPointF(-pw, pw));
        p.setPen(inkPen(pw * 0.7, QColor("#2a2620")));
        p.setBrush(base.darker(115));
        p.drawPolygon(QPolygonF{front.bottomLeft(), front.bottomLeft() + QPointF(front.width() * 0.14, 0),
                                front.bottomLeft() + QPointF(0, -front.height() * 0.2)});
    };
    block(0.04, 0.56, 0.42, 0.32, 0);
    block(0.44, 0.58, 0.42, 0.3, 2);
    block(0.22, 0.27, 0.44, 0.3, 1);
}

void drawGrain(QPainter& p, const QRectF& r) {
    const double w = r.width(), h = r.height();
    const double pw = w / 60;
    const QPointF tie(r.left() + w * 0.5, r.top() + h * 0.6);
    const QColor straw("#d8a640"), ear("#e8c050");
    constexpr int kStalks = 13;
    for (int i = 0; i < kStalks; ++i) {
        const double a = -0.55 + 1.1 * i / (kStalks - 1);
        const QPointF up(std::sin(a), -std::cos(a));
        const QPointF top = tie + up * h * (0.38 + 0.04 * ((i * 5) % 3));
        const QPointF bottom = tie + QPointF(-std::sin(a) * w * 0.28, h * 0.34);
        p.setPen(inkPen(pw * 2.2, QColor("#6a4a14")));
        p.drawLine(bottom, top);
        p.setPen(QPen(i % 2 ? straw : straw.lighter(115), pw * 1.3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(bottom, top);
        // The ear: a plump seed head with awns.
        p.save();
        p.translate(top);
        p.rotate(a * 180 / M_PI);
        p.setPen(QPen(QColor("#a07820"), pw * 0.5, Qt::SolidLine, Qt::RoundCap));
        for (int k = 0; k < 4; ++k) {
            const double y = -h * (0.03 + 0.035 * k);
            p.drawLine(QPointF(-w * 0.02, y), QPointF(-w * 0.07, y - h * 0.07));
            p.drawLine(QPointF(w * 0.02, y), QPointF(w * 0.07, y - h * 0.07));
        }
        p.setPen(inkPen(pw * 0.8, QColor("#6a4a14")));
        for (int k = 0; k < 4; ++k) {
            const QRectF seed(-w * 0.032, -h * (0.06 + 0.035 * k), w * 0.064, h * 0.05);
            p.setBrush(shine(seed, ear, 130, 125));
            p.drawEllipse(seed);
        }
        p.restore();
    }
    // The band round the middle.
    const QRectF band(tie.x() - w * 0.13, tie.y() - h * 0.04, w * 0.26, h * 0.08);
    p.setPen(inkPen(pw * 1.2));
    p.setBrush(shine(band, QColor("#9a3a22")));
    p.drawRoundedRect(band, h * 0.03, h * 0.03);
    p.drawEllipse(QPointF(band.center().x(), band.center().y()), w * 0.035, h * 0.04);
}

void drawGem(QPainter& p, const QRectF& r, const QColor& c) {
    const double w = r.width(), h = r.height();
    const auto P = [&](double x, double y) { return QPointF(r.left() + w * x, r.top() + h * y); };
    const double pw = std::max(1.0, w / 50);
    const QPointF g[] = {P(0.04, 0.38), P(0.27, 0.38), P(0.5, 0.38), P(0.73, 0.38), P(0.96, 0.38)};  // girdle
    const QPointF t[] = {P(0.22, 0.12), P(0.4, 0.06), P(0.6, 0.06), P(0.78, 0.12)};                  // crown top
    const QPointF tip = P(0.5, 0.96);
    p.setPen(inkPen(pw * 0.7, c.darker(300)));
    // Pavilion: darker to the right.
    for (int i = 0; i < 4; ++i) {
        p.setBrush(c.darker(100 + 22 * i));
        p.drawPolygon(QPolygonF{g[i], g[i + 1], tip});
    }
    p.setBrush(c.lighter(115));
    p.drawPolygon(QPolygonF{g[1], P(0.5, 0.58), tip});
    // Crown facets.
    p.setBrush(c.lighter(150));
    p.drawPolygon(QPolygonF{g[0], t[0], g[1]});
    p.setBrush(c.lighter(125));
    p.drawPolygon(QPolygonF{t[0], t[1], g[1]});
    p.setBrush(c.lighter(170));
    p.drawPolygon(QPolygonF{t[1], t[2], g[2], g[1]});  // the table
    p.setBrush(c.lighter(105));
    p.drawPolygon(QPolygonF{t[2], t[3], g[3], g[2]});
    p.setBrush(c.darker(120));
    p.drawPolygon(QPolygonF{t[3], g[4], g[3]});
    p.setPen(inkPen(pw * 1.1, c.darker(320)));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(QPolygonF{g[0], t[0], t[1], t[2], t[3], g[4], tip});
    glint(p, P(0.38, 0.18), w * 0.12);
}

void drawSulphur(QPainter& p, const QRectF& r, const QColor& c) {
    const double w = r.width(), h = r.height();
    const auto P = [&](double x, double y) { return QPointF(r.left() + w * x, r.top() + h * y); };
    const double pw = w / 60;
    // A faint fume rising.
    p.setPen(QPen(QColor(230, 230, 180, 120), w * 0.04, Qt::SolidLine, Qt::RoundCap));
    QPainterPath fume;
    fume.moveTo(P(0.6, 0.3));
    fume.cubicTo(P(0.5, 0.2), P(0.72, 0.12), P(0.6, 0.0));
    p.drawPath(fume);
    // The rock it grows from.
    QPainterPath rock;
    rock.moveTo(P(0.04, 0.9));
    rock.cubicTo(P(0.06, 0.66), P(0.3, 0.58), P(0.5, 0.62));
    rock.cubicTo(P(0.7, 0.56), P(0.96, 0.64), P(0.96, 0.9));
    rock.closeSubpath();
    p.setPen(inkPen(pw * 1.2));
    p.setBrush(shine(rock.boundingRect(), QColor("#7a6a58")));
    p.drawPath(rock);
    prism(p, P(0.28, 0.72), -0.5, h * 0.32, w * 0.07, c, pw);
    prism(p, P(0.74, 0.72), 0.45, h * 0.36, w * 0.075, c, pw);
    prism(p, P(0.5, 0.7), 0.05, h * 0.5, w * 0.1, c, pw);
    prism(p, P(0.4, 0.8), -0.15, h * 0.22, w * 0.06, c.lighter(110), pw);
    prism(p, P(0.62, 0.82), 0.2, h * 0.2, w * 0.055, c.lighter(110), pw);
    glint(p, P(0.46, 0.3), w * 0.09);
}

void drawCoal(QPainter& p, const QRectF& r) {
    const double w = r.width(), h = r.height();
    const double pw = w / 55;
    struct Lump {
        double x, y, rad;
        int seed;
    };
    const Lump lumps[] = {{0.26, 0.7, 0.2, 1}, {0.72, 0.72, 0.19, 2}, {0.5, 0.78, 0.17, 3}, {0.38, 0.45, 0.19, 4}, {0.64, 0.46, 0.17, 5}};
    for (const Lump& l : lumps) {
        const QPointF c(r.left() + w * l.x, r.top() + h * l.y);
        QPolygonF poly;
        for (int i = 0; i < 7; ++i) {
            const double a = M_PI * 2 * i / 7 + l.seed;
            const double rr = l.rad * w * (0.78 + 0.22 * (((i + 1) * (l.seed + 3) * 7) % 10) / 10.0);
            poly << c + QPointF(std::cos(a) * rr, std::sin(a) * rr * 0.86);
        }
        p.setPen(inkPen(pw, QColor("#08080a")));
        p.setBrush(shine(poly.boundingRect(), QColor("#2c2c34"), 150, 160));
        p.drawPolygon(poly);
        // Bluish facets catching the light.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(140, 150, 180, 150));
        p.drawPolygon(QPolygonF{poly[4 % poly.size()], c + QPointF(-l.rad * w * 0.1, -l.rad * w * 0.15), poly[5 % poly.size()]});
        p.setBrush(QColor(200, 210, 235, 110));
        p.drawPolygon(QPolygonF{c + QPointF(-l.rad * w * 0.5, -l.rad * w * 0.25), c + QPointF(-l.rad * w * 0.15, -l.rad * w * 0.5),
                                c + QPointF(-l.rad * w * 0.05, -l.rad * w * 0.15)});
    }
    // One lump still glowing.
    const QPointF ember(r.left() + w * 0.52, r.top() + h * 0.8);
    QRadialGradient g(ember, w * 0.08);
    g.setColorAt(0, QColor(255, 210, 90, 240));
    g.setColorAt(0.5, QColor(240, 90, 20, 180));
    g.setColorAt(1, QColor(240, 60, 10, 0));
    p.setBrush(g);
    p.drawEllipse(ember, w * 0.08, w * 0.06);
}

void drawMercury(QPainter& p, const QRectF& r, const QColor& c) {
    const double w = r.width(), h = r.height();
    const auto P = [&](double x, double y) { return QPointF(r.left() + w * x, r.top() + h * y); };
    const double pw = w / 50;
    const QPointF centre = P(0.5, 0.64);
    const double rad = w * 0.3;
    QPainterPath glass;
    glass.addEllipse(centre, rad, rad);
    QPainterPath neck;
    neck.addRect(QRectF(P(0.4, 0.14), P(0.6, 0.42)));
    glass = glass.united(neck);
    // The silver inside, with a bright meniscus.
    p.save();
    p.setClipPath(glass);
    QLinearGradient silver(P(0.2, 0.55), P(0.8, 0.95));
    silver.setColorAt(0, QColor("#f4f6fc"));
    silver.setColorAt(0.4, c.lighter(120));
    silver.setColorAt(1, QColor("#4a4e68"));
    p.setPen(Qt::NoPen);
    p.setBrush(silver);
    p.drawRect(QRectF(P(0.0, 0.58), P(1.0, 1.0)));
    p.setBrush(QColor(255, 255, 255, 170));
    p.drawEllipse(QRectF(P(0.24, 0.56), P(0.76, 0.62)));
    p.restore();
    // The glass.
    p.setPen(inkPen(pw, QColor("#20283a")));
    p.setBrush(QColor(200, 225, 255, 70));
    p.drawPath(glass);
    p.setPen(QPen(QColor(255, 255, 255, 200), pw * 1.4, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(QRectF(centre.x() - rad * 0.75, centre.y() - rad * 0.75, rad * 1.5, rad * 1.5), 110 * 16, 60 * 16);
    p.drawLine(P(0.44, 0.2), P(0.44, 0.36));
    // Lip and cork.
    p.setPen(inkPen(pw, QColor("#20283a")));
    p.setBrush(QColor(210, 230, 255, 120));
    p.drawRoundedRect(QRectF(P(0.36, 0.12), P(0.64, 0.17)), w * 0.02, w * 0.02);
    const QRectF cork(P(0.42, 0.02), P(0.58, 0.13));
    p.setBrush(shine(cork, QColor("#b07a48")));
    p.drawRoundedRect(cork, w * 0.02, w * 0.02);
    // A drop beside it.
    p.setBrush(ball(P(0.86, 0.88), w * 0.05, c));
    p.drawEllipse(P(0.86, 0.88), w * 0.05, w * 0.04);
    glint(p, centre + QPointF(-rad * 0.45, rad * 0.1), w * 0.07);
}

void drawPapyrus(QPainter& p, const QRectF& r) {
    const double w = r.width(), h = r.height();
    const auto P = [&](double x, double y) { return QPointF(r.left() + w * x, r.top() + h * y); };
    const double pw = w / 55;
    const QColor paper("#e8d4a0");
    // The sheet, a little wavy.
    QPainterPath sheet;
    sheet.moveTo(P(0.18, 0.2));
    sheet.cubicTo(P(0.4, 0.16), P(0.6, 0.24), P(0.82, 0.2));
    sheet.lineTo(P(0.82, 0.8));
    sheet.cubicTo(P(0.6, 0.84), P(0.4, 0.76), P(0.18, 0.8));
    sheet.closeSubpath();
    p.setPen(inkPen(pw, QColor("#4a3214")));
    QLinearGradient sg(P(0.18, 0.2), P(0.82, 0.8));
    sg.setColorAt(0, paper.lighter(112));
    sg.setColorAt(1, paper.darker(118));
    p.setBrush(sg);
    p.drawPath(sheet);
    // Writing: rows of little marks.
    p.setPen(QPen(QColor(70, 44, 20, 200), pw * 0.7, Qt::SolidLine, Qt::RoundCap));
    for (int row = 0; row < 5; ++row) {
        const double y = 0.3 + row * 0.09;
        double x = 0.25;
        int k = row * 3;
        while (x < 0.74) {
            const double len = 0.03 + 0.03 * ((k * 7) % 4) / 3.0;
            p.drawLine(P(x, y + 0.01 * std::sin(x * 20)), P(std::min(0.75, x + len), y + 0.01 * std::sin((x + len) * 20)));
            x += len + 0.025;
            ++k;
        }
    }
    // The rolls at each end.
    for (const double x : {0.08, 0.78}) {
        const QRectF roll(P(x, 0.12), P(x + 0.14, 0.88));
        QLinearGradient cyl(roll.topLeft(), roll.topRight());
        cyl.setColorAt(0, paper.darker(150));
        cyl.setColorAt(0.35, paper.lighter(115));
        cyl.setColorAt(1, paper.darker(160));
        p.setPen(inkPen(pw, QColor("#4a3214")));
        p.setBrush(cyl);
        p.drawRoundedRect(roll, w * 0.05, w * 0.03);
        p.setBrush(QColor("#7a5428"));
        p.drawEllipse(QRectF(roll.left() + roll.width() * 0.15, roll.top() - h * 0.03, roll.width() * 0.7, h * 0.06));
        p.drawEllipse(QRectF(roll.left() + roll.width() * 0.15, roll.bottom() - h * 0.03, roll.width() * 0.7, h * 0.06));
    }
    // A wax seal with a ribbon.
    const QPointF seal = P(0.5, 0.82);
    p.setPen(inkPen(pw * 0.8, QColor("#4a0a0a")));
    p.setBrush(QColor("#8a1a1a"));
    p.drawPolygon(QPolygonF{seal + QPointF(-w * 0.04, 0), seal + QPointF(-w * 0.07, h * 0.15), seal + QPointF(-w * 0.03, h * 0.12)});
    p.drawPolygon(QPolygonF{seal + QPointF(w * 0.04, 0), seal + QPointF(w * 0.07, h * 0.15), seal + QPointF(w * 0.03, h * 0.12)});
    p.setBrush(ball(seal, w * 0.07, QColor("#c42a22")));
    p.drawEllipse(seal, w * 0.07, w * 0.07);
    p.setPen(QPen(QColor("#6a0a0a"), pw * 0.7));
    p.drawEllipse(seal, w * 0.04, w * 0.04);
}

}  // namespace

void drawResourceIcon(QPainter& p, const QRectF& r, const QString& id, const QColor& c) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    if (id == "gold") drawGold(p, r);
    else if (id == "wood") drawWood(p, r);
    else if (id == "stone") drawStone(p, r);
    else if (id == "grain") drawGrain(p, r);
    else if (id == "gems") drawGem(p, r.adjusted(r.width() * 0.04, r.height() * 0.06, -r.width() * 0.04, -r.height() * 0.02), c);
    else if (id == "sulphur") drawSulphur(p, r, c);
    else if (id == "coal") drawCoal(p, r);
    else if (id == "mercury") drawMercury(p, r, c);
    else if (id == "papyrus") drawPapyrus(p, r);
    else {
        const QPointF centre = r.center();
        const double rad = r.width() * 0.38;
        p.setPen(inkPen(r.width() / 40));
        p.setBrush(ball(centre, rad, c));
        p.drawEllipse(centre, rad, rad);
        glint(p, centre - QPointF(rad * 0.4, rad * 0.4), rad * 0.3);
    }
    p.restore();
}

// --- special mines -----------------------------------------------------------

void drawSawmill(QPainter& p, const QRectF& r, const QColor& owner) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    groundShadow(p, r);
    const double w = r.width(), h = r.height();
    const auto P = [&](double x, double y) { return QPointF(r.left() + w * x, r.top() + h * y); };
    const double pw = w / 70;
    const QColor plank("#9a6a38"), roof("#6a4630");
    // The mill house: vertical planks, a door and a shutter.
    const QRectF house(P(0.06, 0.42), P(0.54, 0.9));
    p.setPen(inkPen(pw));
    p.setBrush(shine(house, plank, 125, 135));
    p.drawRect(house);
    p.setPen(QPen(plank.darker(170), pw * 0.6));
    for (double x = house.left() + w * 0.045; x < house.right(); x += w * 0.045) p.drawLine(QPointF(x, house.top()), QPointF(x, house.bottom()));
    p.setPen(QPen(plank.darker(140), pw * 0.4));
    for (double x = house.left() + w * 0.02; x < house.right(); x += w * 0.09) p.drawPoint(QPointF(x, house.top() + h * 0.06));
    p.setPen(inkPen(pw));
    p.setBrush(QColor("#2a1a0c"));
    p.drawRect(QRectF(P(0.12, 0.62), P(0.26, 0.9)));
    p.setBrush(QColor("#ffd27a"));
    const QRectF win(P(0.36, 0.56), P(0.46, 0.66));
    p.drawRect(win);
    p.drawLine(QPointF(win.center().x(), win.top()), QPointF(win.center().x(), win.bottom()));
    p.drawLine(QPointF(win.left(), win.center().y()), QPointF(win.right(), win.center().y()));
    // The roof with rows of shingles.
    const QPolygonF roofShape{P(0.0, 0.45), P(0.3, 0.16), P(0.6, 0.45)};
    p.setBrush(shine(roofShape.boundingRect(), roof, 135, 140));
    p.drawPolygon(roofShape);
    p.setPen(QPen(roof.darker(170), pw * 0.6));
    for (int i = 1; i < 4; ++i) {
        const double y = 0.16 + 0.29 * i / 4, half = 0.3 * i / 4;
        p.drawLine(P(0.3 - half, y), P(0.3 + half, y));
        for (double x = 0.3 - half + 0.04; x < 0.3 + half; x += 0.06) p.drawLine(P(x, y), P(x + 0.01, y + 0.07));
    }
    // The saw shed: posts, a lean-to roof, a bench with a log and a big saw.
    p.setPen(inkPen(pw));
    p.setBrush(shine(QRectF(P(0.54, 0.5), P(0.96, 0.9)), plank.darker(115)));
    p.drawRect(QRectF(P(0.88, 0.5), P(0.92, 0.9)));
    const QPolygonF lean{P(0.52, 0.44), P(0.98, 0.52), P(0.98, 0.56), P(0.52, 0.5)};
    p.setBrush(shine(lean.boundingRect(), roof));
    p.drawPolygon(lean);
    const QRectF bench(P(0.56, 0.72), P(0.96, 0.78));
    p.setBrush(shine(bench, plank.darker(110)));
    p.drawRect(bench);
    p.drawRect(QRectF(P(0.58, 0.78), P(0.61, 0.9)));
    p.drawRect(QRectF(P(0.91, 0.78), P(0.94, 0.9)));
    const QRectF log(P(0.6, 0.64), P(0.94, 0.72));
    QLinearGradient bark(log.topLeft(), log.bottomLeft());
    bark.setColorAt(0, QColor("#a8763e"));
    bark.setColorAt(1, QColor("#4a2c12"));
    p.setBrush(bark);
    p.drawRoundedRect(log, h * 0.03, h * 0.04);
    p.setBrush(QColor("#e8bc80"));
    p.drawEllipse(QRectF(log.right() - w * 0.03, log.top(), w * 0.04, log.height()));
    // The saw blade: steel with teeth and a shine.
    const QPointF hub = P(0.74, 0.64);
    const double sr = w * 0.13;
    QPolygonF saw;
    for (int i = 0; i < 32; ++i) {
        const double a = M_PI * 2 * i / 32, rad = (i % 2 ? 0.86 : 1.0) * sr;
        saw << hub + QPointF(std::cos(a) * rad, std::sin(a) * rad);
    }
    p.setPen(inkPen(pw * 0.9, QColor("#20242c")));
    QRadialGradient steel(hub - QPointF(sr * 0.3, sr * 0.3), sr * 1.4);
    steel.setColorAt(0, QColor("#f4f6fa"));
    steel.setColorAt(0.6, QColor("#a8b0c0"));
    steel.setColorAt(1, QColor("#5a6070"));
    p.setBrush(steel);
    p.drawPolygon(saw);
    p.setPen(QPen(QColor(255, 255, 255, 170), pw * 0.8, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(hub.x() - sr * 0.6, hub.y() - sr * 0.6, sr * 1.2, sr * 1.2), 100 * 16, 70 * 16);
    p.setPen(inkPen(pw * 0.8, QColor("#20242c")));
    p.setBrush(QColor("#6a7080"));
    p.drawEllipse(hub, sr * 0.2, sr * 0.2);
    // Sawdust, and a pile of logs in front.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(232, 196, 130, 200));
    p.drawEllipse(QRectF(P(0.62, 0.86), P(0.84, 0.94)));
    logEnd(p, P(0.12, 0.86), w * 0.075, pw);
    logEnd(p, P(0.27, 0.87), w * 0.075, pw);
    logEnd(p, P(0.195, 0.75), w * 0.075, pw);
    ownerFlag(p, P(0.3, 0.18), h * 0.2, owner, pw);
    p.restore();
}

void drawQuarry(QPainter& p, const QRectF& r, const QColor& owner) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    groundShadow(p, r);
    const double w = r.width(), h = r.height();
    const auto P = [&](double x, double y) { return QPointF(r.left() + w * x, r.top() + h * y); };
    const double pw = w / 70;
    const QColor rock("#a49c8c");
    // The hill the stone is cut from.
    QPainterPath hill;
    hill.moveTo(P(0.02, 0.9));
    hill.cubicTo(P(0.04, 0.5), P(0.2, 0.26), P(0.46, 0.22));
    hill.cubicTo(P(0.72, 0.18), P(0.94, 0.4), P(0.98, 0.9));
    hill.closeSubpath();
    p.setPen(inkPen(pw));
    p.setBrush(shine(hill.boundingRect(), QColor("#8a8070"), 125, 150));
    p.drawPath(hill);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(96, 130, 64, 190));  // grass on the top
    p.drawEllipse(QRectF(P(0.28, 0.2), P(0.62, 0.28)));
    // Three cut terraces stepping down, each with a lit top and a face of blocks.
    for (int i = 0; i < 3; ++i) {
        const double top = 0.36 + 0.17 * i, left = 0.24 - 0.04 * i, right = 0.74 + 0.04 * i;
        const QRectF face(P(left, top + 0.04), P(right, top + 0.17));
        p.setPen(inkPen(pw * 0.8));
        p.setBrush(rock.lighter(130));
        p.drawPolygon(QPolygonF{P(left - 0.02, top + 0.04), P(left + 0.03, top), P(right - 0.03, top), P(right + 0.02, top + 0.04)});
        p.setBrush(shine(face, rock, 112, 130));
        p.drawRect(face);
        p.setPen(QPen(rock.darker(160), pw * 0.55));
        p.drawLine(QPointF(face.left(), face.center().y()), QPointF(face.right(), face.center().y()));
        for (int k = 0; k < 8; ++k) {
            const double x = face.left() + face.width() * (k + (i % 2) * 0.5) / 7.5;
            if (x >= face.right() - 1) continue;
            const double y0 = k % 2 ? face.top() : face.center().y();
            p.drawLine(QPointF(x, y0), QPointF(x, y0 + face.height() / 2));
        }
    }
    // A wooden crane on the top lifting a block.
    p.setPen(inkPen(pw * 1.3, QColor("#3a2410")));
    p.drawLine(P(0.66, 0.36), P(0.72, 0.1));
    p.drawLine(P(0.8, 0.38), P(0.72, 0.1));
    p.drawLine(P(0.72, 0.1), P(0.98, 0.2));
    p.setPen(inkPen(pw * 0.5, QColor("#3a2a1a")));
    p.drawLine(P(0.96, 0.2), P(0.96, 0.42));
    p.setPen(inkPen(pw * 0.8));
    const QRectF hung(P(0.91, 0.42), P(1.01, 0.5));
    p.setBrush(shine(hung, rock.lighter(110)));
    p.drawRect(hung);
    // Cut blocks and a pickaxe on the ground.
    for (const QRectF& b : {QRectF(P(0.02, 0.8), P(0.18, 0.92)), QRectF(P(0.06, 0.7), P(0.17, 0.8)), QRectF(P(0.82, 0.82), P(0.98, 0.93))}) {
        p.setBrush(shine(b, rock.lighter(115)));
        p.drawRect(b);
        p.setBrush(rock.lighter(140));
        p.drawPolygon(QPolygonF{b.topLeft(), b.topLeft() + QPointF(w * 0.03, -h * 0.025), b.topRight() + QPointF(w * 0.03, -h * 0.025), b.topRight()});
    }
    p.setPen(inkPen(pw * 2, QColor("#5a3a18")));
    p.drawLine(P(0.24, 0.94), P(0.38, 0.76));
    p.setPen(inkPen(pw * 1.8, QColor("#6a7080")));
    QPainterPath pick;
    pick.moveTo(P(0.28, 0.72));
    pick.quadTo(P(0.36, 0.72), P(0.46, 0.8));
    p.drawPath(pick);
    ownerFlag(p, P(0.4, 0.23), h * 0.22, owner, pw);
    p.restore();
}

void drawCrystalCave(QPainter& p, const QRectF& r, const QColor& crystal, const QColor& owner) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    groundShadow(p, r);
    const double w = r.width(), h = r.height();
    const auto P = [&](double x, double y) { return QPointF(r.left() + w * x, r.top() + h * y); };
    const double pw = w / 70;
    // A dark rocky outcrop.
    QPainterPath rock;
    rock.moveTo(P(0.02, 0.9));
    rock.cubicTo(P(0.0, 0.5), P(0.18, 0.24), P(0.38, 0.22));
    rock.lineTo(P(0.46, 0.14));
    rock.lineTo(P(0.56, 0.2));
    rock.cubicTo(P(0.82, 0.22), P(1.0, 0.5), P(0.98, 0.9));
    rock.closeSubpath();
    p.setPen(inkPen(pw));
    p.setBrush(shine(rock.boundingRect(), QColor("#6a5e72"), 135, 160));
    p.drawPath(rock);
    p.setPen(QPen(QColor("#3a3040"), pw * 0.7, Qt::SolidLine, Qt::RoundCap));
    p.drawPolyline(QPolygonF{P(0.14, 0.5), P(0.22, 0.44), P(0.26, 0.48)});
    p.drawPolyline(QPolygonF{P(0.74, 0.36), P(0.82, 0.44), P(0.88, 0.42)});
    p.drawLine(P(0.6, 0.28), P(0.64, 0.34));
    // The cave mouth, glowing from inside.
    QPainterPath mouth;
    mouth.moveTo(P(0.28, 0.9));
    mouth.cubicTo(P(0.28, 0.52), P(0.38, 0.42), P(0.5, 0.42));
    mouth.cubicTo(P(0.62, 0.42), P(0.72, 0.52), P(0.72, 0.9));
    mouth.closeSubpath();
    p.setPen(inkPen(pw * 1.1));
    p.setBrush(QColor("#120a16"));
    p.drawPath(mouth);
    p.save();
    p.setClipPath(mouth);
    QRadialGradient inner(P(0.5, 0.82), w * 0.3);
    inner.setColorAt(0, QColor(crystal.red(), crystal.green(), crystal.blue(), 190));
    inner.setColorAt(1, QColor(crystal.red(), crystal.green(), crystal.blue(), 0));
    p.setPen(Qt::NoPen);
    p.setBrush(inner);
    p.drawRect(QRectF(P(0.2, 0.4), P(0.8, 0.95)));
    prism(p, P(0.42, 0.9), -0.3, h * 0.16, w * 0.04, crystal, pw * 0.7);
    prism(p, P(0.54, 0.9), 0.15, h * 0.22, w * 0.05, crystal, pw * 0.7);
    p.restore();
    // Clusters outside, each with a soft halo.
    const auto cluster = [&](QPointF base, double s) {
        QRadialGradient halo(base - QPointF(0, h * 0.1 * s), w * 0.2 * s);
        halo.setColorAt(0, QColor(crystal.red(), crystal.green(), crystal.blue(), 120));
        halo.setColorAt(1, QColor(crystal.red(), crystal.green(), crystal.blue(), 0));
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(base - QPointF(0, h * 0.1 * s), w * 0.2 * s, w * 0.2 * s);
        prism(p, base + QPointF(-w * 0.05 * s, 0), -0.55, h * 0.18 * s, w * 0.04 * s, crystal, pw * 0.8);
        prism(p, base + QPointF(w * 0.06 * s, 0), 0.5, h * 0.16 * s, w * 0.04 * s, crystal, pw * 0.8);
        prism(p, base, 0.0, h * 0.3 * s, w * 0.055 * s, crystal, pw * 0.8);
    };
    cluster(P(0.84, 0.9), 1.0);
    cluster(P(0.15, 0.9), 0.75);
    glint(p, P(0.82, 0.58), w * 0.06);
    glint(p, P(0.52, 0.66), w * 0.04);
    ownerFlag(p, P(0.46, 0.16), h * 0.2, owner, pw);
    p.restore();
}

// --- button icons ------------------------------------------------------------

void drawButtonIcon(QPainter& p, const QRectF& box, const QString& name) {
    // Drawn in a 48 unit box.
    {
        p.translate(box.topLeft());
        p.scale(box.width() / 48.0, box.height() / 48.0);
        const QPen ink(kInk, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(ink);
        const QColor gold("#f2c230"), wood("#a0703a"), steel("#c8ced8");
        const auto lit = [](const QRectF& r, const QColor& c) { return shine(r, c, 150, 150); };
        if (name == "build") {  // a hammer: grained handle, steel head
            const QPolygonF handle{QPointF(8, 41), QPointF(12, 45), QPointF(31, 25), QPointF(27, 21)};
            p.setBrush(lit(handle.boundingRect(), wood));
            p.drawPolygon(handle);
            p.setPen(QPen(wood.darker(170), 0.7));
            p.drawLine(QPointF(11, 41.5), QPointF(28, 24));
            p.setPen(ink);
            const QPolygonF head{QPointF(20, 12), QPointF(32, 4), QPointF(44, 17), QPointF(34, 27), QPointF(25, 20)};
            p.setBrush(lit(head.boundingRect(), steel));
            p.drawPolygon(head);
            p.setPen(QPen(QColor(255, 255, 255, 200), 1.2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(24, 12), QPointF(31, 7));
            glint(p, QPointF(33, 8), 3.5);
        } else if (name == "buy") {  // a stack of coins
            for (int i = 0; i < 4; ++i) {
                const QRectF c(7 + (i % 2) * 2, 32 - i * 7.5, 32, 11);
                p.setBrush(gold.darker(150));
                p.drawRoundedRect(c.translated(0, 3), 16, 5.5);
                p.setBrush(lit(c, gold));
                p.drawEllipse(c);
                p.setPen(QPen(gold.darker(160), 0.8));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(c.adjusted(4, 2, -4, -2));
                p.setPen(ink);
            }
            glint(p, QPointF(16, 11), 3.5);
        } else if (name == "wagon") {  // a covered wagon
            QPainterPath cover;
            cover.moveTo(8, 24);
            cover.cubicTo(8, 6, 40, 6, 40, 24);
            cover.closeSubpath();
            p.setBrush(lit(cover.boundingRect(), QColor("#ece2c8")));
            p.drawPath(cover);
            p.setPen(QPen(QColor("#a89878"), 0.9));
            for (const double x : {16.0, 24.0, 32.0}) p.drawLine(QPointF(x, 11.5), QPointF(x, 24));
            p.setPen(ink);
            const QRectF bed(4, 23, 40, 9);
            p.setBrush(lit(bed, wood));
            p.drawRect(bed);
            for (const double x : {13.0, 35.0}) {
                p.setBrush(lit(QRectF(x - 7, 29, 14, 14), QColor("#6a4420")));
                p.drawEllipse(QPointF(x, 36), 7, 7);
                p.setBrush(QColor("#c8a060"));
                p.drawEllipse(QPointF(x, 36), 2, 2);
                p.drawLine(QPointF(x - 5, 36), QPointF(x + 5, 36));
                p.drawLine(QPointF(x, 31), QPointF(x, 41));
            }
        } else if (name == "end" || name == "wait") {  // an hourglass
            p.setBrush(lit(QRectF(8, 3, 32, 6), wood));
            p.drawRoundedRect(QRectF(8, 3, 32, 6), 2, 2);
            p.drawRoundedRect(QRectF(8, 39, 32, 6), 2, 2);
            p.drawLine(QPointF(11, 9), QPointF(11, 39));
            p.drawLine(QPointF(37, 9), QPointF(37, 39));
            const QPolygonF glass{QPointF(14, 9), QPointF(34, 9), QPointF(25.5, 24), QPointF(34, 39), QPointF(14, 39), QPointF(22.5, 24)};
            p.setBrush(QColor(220, 235, 250, 150));
            p.drawPolygon(glass);
            p.setPen(Qt::NoPen);
            p.setBrush(lit(QRectF(15, 28, 18, 10), QColor("#e8b850")));
            p.drawPolygon(QPolygonF{QPointF(16, 37.5), QPointF(32, 37.5), QPointF(24, 30)});
            p.drawPolygon(QPolygonF{QPointF(18.5, 14), QPointF(29.5, 14), QPointF(24, 22)});
            p.setPen(QPen(QColor("#e8b850"), 1.2));
            p.drawLine(QPointF(24, 22), QPointF(24, 30));
            p.setPen(QPen(QColor(255, 255, 255, 200), 1.2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(16.5, 11), QPointF(21, 19));
        } else if (name == "spell") {  // a wand with a star and sparks
            const QPolygonF wand{QPointF(6, 42), QPointF(9, 45), QPointF(29, 23), QPointF(26, 20)};
            p.setBrush(lit(wand.boundingRect(), QColor("#5a3a6a")));
            p.drawPolygon(wand);
            p.setBrush(gold);
            p.drawPolygon(QPolygonF{QPointF(9, 39), QPointF(12, 42), QPointF(14, 40), QPointF(11, 37)});
            QRadialGradient halo(QPointF(33, 15), 15);
            halo.setColorAt(0, QColor(170, 220, 255, 200));
            halo.setColorAt(1, QColor(170, 220, 255, 0));
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            p.drawEllipse(QPointF(33, 15), 15, 15);
            QPolygonF star;
            for (int i = 0; i < 10; ++i) {
                const double a = M_PI / 5 * i - M_PI / 2, rad = i % 2 ? 4.5 : 11;
                star << QPointF(33 + rad * std::cos(a), 15 + rad * std::sin(a));
            }
            p.setPen(ink);
            p.setBrush(lit(star.boundingRect(), QColor("#9ad0ff")));
            p.drawPolygon(star);
            glint(p, QPointF(44, 30), 3);
            glint(p, QPointF(20, 8), 2.5);
        } else if (name == "market") {  // scales
            p.setBrush(lit(QRectF(22, 6, 4, 34), wood));
            p.drawRect(QRectF(22.5, 6, 3, 34));
            p.drawRoundedRect(QRectF(13, 40, 22, 5), 2, 2);
            p.setBrush(lit(QRectF(6, 12, 36, 4), gold));
            p.drawRoundedRect(QRectF(6, 12, 36, 4), 2, 2);
            p.setPen(QPen(kInk, 1));
            for (const double x : {10.0, 38.0}) {
                p.drawLine(QPointF(x, 15), QPointF(x - 7, 26));
                p.drawLine(QPointF(x, 15), QPointF(x + 7, 26));
            }
            p.setPen(ink);
            p.setBrush(lit(QRectF(2, 24, 16, 9), gold));
            p.drawChord(QRectF(2, 19, 16, 13), 180 * 16, 180 * 16);
            p.drawChord(QRectF(30, 19, 16, 13), 180 * 16, 180 * 16);
            p.setBrush(gold.lighter(110));
            p.drawEllipse(QPointF(24, 6), 3, 3);
        } else if (name == "defend") {  // a shield with a cross
            QPainterPath sh;
            sh.moveTo(24, 3);
            sh.lineTo(41, 9);
            sh.quadTo(41, 34, 24, 45);
            sh.quadTo(7, 34, 7, 9);
            sh.closeSubpath();
            p.setBrush(lit(sh.boundingRect(), QColor("#3a64b0")));
            p.drawPath(sh);
            p.setPen(QPen(gold, 4, Qt::SolidLine, Qt::FlatCap));
            p.drawLine(QPointF(24, 7), QPointF(24, 41));
            p.drawLine(QPointF(10, 19), QPointF(38, 19));
            p.setPen(QPen(gold.darker(150), 2));
            p.setBrush(Qt::NoBrush);
            QPainterPath rim;
            rim.moveTo(24, 6);
            rim.lineTo(38.5, 11);
            rim.quadTo(38.5, 32.5, 24, 42);
            rim.quadTo(9.5, 32.5, 9.5, 11);
            rim.closeSubpath();
            p.drawPath(rim);
            glint(p, QPointF(15, 12), 3.5);
        } else if (name == "auto") {  // a gear
            QPolygonF gear;
            for (int i = 0; i < 24; ++i) {
                const double a = M_PI / 12 * (i + 0.5), rad = (i / 2) % 2 ? 16 : 21;
                gear << QPointF(24 + rad * std::cos(a), 24 + rad * std::sin(a));
            }
            p.setBrush(lit(gear.boundingRect(), steel));
            p.drawPolygon(gear);
            p.setPen(QPen(steel.darker(150), 1));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(24, 24), 11, 11);
            p.setPen(ink);
            p.setBrush(QColor("#2b2118"));
            p.drawEllipse(QPointF(24, 24), 5, 5);
            glint(p, QPointF(15, 14), 3);
        } else if (name == "retreat") {  // a white flag
            p.setPen(QPen(kInk, 3, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(11, 45), QPointF(11, 4));
            p.setPen(ink);
            QPainterPath flag;
            flag.moveTo(11, 6);
            flag.cubicTo(20, 2, 28, 12, 41, 7);
            flag.cubicTo(38, 14, 39, 20, 42, 26);
            flag.cubicTo(30, 31, 22, 21, 11, 26);
            flag.closeSubpath();
            p.setBrush(lit(flag.boundingRect(), QColor("#f4f0e8")));
            p.drawPath(flag);
            p.setBrush(gold);
            p.drawEllipse(QPointF(11, 4), 2.5, 2.5);
        } else if (name == "catapult") {
            drawUnitIcon(p, QRectF(0, 0, 48, 48), QStringLiteral("catapult"), 0);
        } else if (name == "town") {
            drawTownIcon(p, QRectF(0, 4, 48, 44), TownLook{QColor("#c03a3a"), QStringLiteral("humans"), 1, 6});
        } else if (name == "army") {
            drawUnitIcon(p, QRectF(2, 0, 44, 48), QStringLiteral("pikeman"), 0);
        } else if (name == "tavern") {  // a foaming tankard
            const QRectF mug(9, 14, 24, 30);
            p.setBrush(lit(mug, wood));
            p.drawRoundedRect(mug, 3, 3);
            p.setPen(QPen(wood.darker(170), 1));
            for (const double x : {15.0, 21.0, 27.0}) p.drawLine(QPointF(x, 16), QPointF(x, 42));
            p.setPen(ink);
            p.setBrush(lit(QRectF(8, 19, 26, 4), steel));
            p.drawRect(QRectF(8, 19, 26, 3.5));
            p.drawRect(QRectF(8, 36, 26, 3.5));
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(kInk, 5, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(QRectF(26, 20, 15, 16), -90 * 16, 180 * 16);
            p.setPen(QPen(wood, 2.5, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(QRectF(26, 20, 15, 16), -90 * 16, 180 * 16);
            p.setPen(ink);
            QPainterPath foam;
            foam.moveTo(7, 16);
            foam.cubicTo(5, 8, 13, 5, 17, 9);
            foam.cubicTo(20, 3, 30, 4, 30, 10);
            foam.cubicTo(36, 9, 37, 17, 34, 17);
            foam.closeSubpath();
            p.setBrush(lit(foam.boundingRect(), QColor("#fbf8f0")));
            p.drawPath(foam);
        }
    }
}

}  // namespace kot::artgen

// --- artifacts ---------------------------------------------------------------

namespace kot::artgen {

namespace {

void drawBlade(QPainter& p, const QRectF& r, bool great) {
    const auto P = [&](double x, double y) { return QPointF(r.left() + r.width() * x, r.top() + r.height() * y); };
    const double pw = r.width() / 45;
    const QColor steel = great ? QColor("#e0c8f0") : QColor("#c8d0dc");
    p.save();
    p.translate(r.center());
    p.rotate(-45);
    p.translate(-r.center());
    const QPolygonF blade{P(0.46, 0.62), P(0.46, 0.12), P(0.5, 0.02), P(0.54, 0.12), P(0.54, 0.62)};
    p.setPen(inkPen(pw));
    QLinearGradient g(P(0.46, 0), P(0.54, 0));
    g.setColorAt(0, steel.lighter(130));
    g.setColorAt(0.5, steel);
    g.setColorAt(1, steel.darker(150));
    p.setBrush(g);
    p.drawPolygon(blade);
    p.setPen(QPen(steel.darker(170), pw * 0.6));
    p.drawLine(P(0.5, 0.1), P(0.5, 0.6));
    p.setPen(inkPen(pw));
    const QRectF guard(P(0.3, 0.62), P(0.7, 0.68));
    p.setBrush(shine(guard, QColor("#d8a838")));
    p.drawRoundedRect(guard, pw * 2, pw * 2);
    const QRectF grip(P(0.465, 0.68), P(0.535, 0.88));
    p.setBrush(shine(grip, great ? QColor("#5a1a2a") : QColor("#6a4020")));
    p.drawRect(grip);
    p.setBrush(ball(P(0.5, 0.91), r.width() * 0.05, QColor("#d8a838")));
    p.drawEllipse(P(0.5, 0.91), r.width() * 0.05, r.width() * 0.05);
    if (great) {
        p.setBrush(ball(P(0.5, 0.65), r.width() * 0.035, QColor("#d02030")));
        p.drawEllipse(P(0.5, 0.65), r.width() * 0.035, r.width() * 0.035);
    }
    p.restore();
}

void drawAxe(QPainter& p, const QRectF& r) {
    const auto P = [&](double x, double y) { return QPointF(r.left() + r.width() * x, r.top() + r.height() * y); };
    const double pw = r.width() / 45;
    p.setPen(inkPen(pw * 3, QColor("#3a2410")));
    p.drawLine(P(0.25, 0.92), P(0.66, 0.12));
    p.setPen(QPen(QColor("#8a5a2c"), pw * 1.6, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(P(0.25, 0.92), P(0.66, 0.12));
    QPainterPath head;
    head.moveTo(P(0.56, 0.2));
    head.cubicTo(P(0.7, 0.02), P(0.96, 0.12), P(0.94, 0.3));
    head.cubicTo(P(0.88, 0.42), P(0.74, 0.46), P(0.62, 0.36));
    head.closeSubpath();
    p.setPen(inkPen(pw));
    p.setBrush(shine(head.boundingRect(), QColor("#b8c0cc")));
    p.drawPath(head);
    p.setPen(QPen(QColor(255, 255, 255, 200), pw, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(QRectF(P(0.7, 0.1), P(0.95, 0.4)), 20 * 16, 70 * 16);
    p.setPen(inkPen(pw));
    p.setBrush(shine(QRectF(P(0.55, 0.22), P(0.68, 0.32)), QColor("#d8a838")));
    p.drawEllipse(QRectF(P(0.56, 0.2), P(0.68, 0.32)));
}

void drawStaff(QPainter& p, const QRectF& r, const QColor& gem) {
    const auto P = [&](double x, double y) { return QPointF(r.left() + r.width() * x, r.top() + r.height() * y); };
    const double pw = r.width() / 45;
    p.setPen(inkPen(pw * 3, QColor("#3a2410")));
    p.drawLine(P(0.3, 0.95), P(0.62, 0.26));
    p.setPen(QPen(QColor("#9a6a3a"), pw * 1.6, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(P(0.3, 0.95), P(0.62, 0.26));
    QRadialGradient halo(P(0.66, 0.18), r.width() * 0.25);
    halo.setColorAt(0, QColor(gem.red(), gem.green(), gem.blue(), 170));
    halo.setColorAt(1, QColor(gem.red(), gem.green(), gem.blue(), 0));
    p.setPen(Qt::NoPen);
    p.setBrush(halo);
    p.drawEllipse(P(0.66, 0.18), r.width() * 0.25, r.width() * 0.25);
    p.setPen(inkPen(pw, QColor("#5a3a10")));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(P(0.52, 0.06), P(0.8, 0.34)), 200 * 16, 220 * 16);
    p.setPen(inkPen(pw));
    p.setBrush(ball(P(0.66, 0.19), r.width() * 0.09, gem));
    p.drawEllipse(P(0.66, 0.19), r.width() * 0.09, r.width() * 0.09);
    glint(p, P(0.63, 0.15), r.width() * 0.05);
}

void drawShield(QPainter& p, const QRectF& r, bool tall) {
    const auto P = [&](double x, double y) { return QPointF(r.left() + r.width() * x, r.top() + r.height() * y); };
    const double pw = r.width() / 40;
    QPainterPath sh;
    if (tall) {
        sh.moveTo(P(0.2, 0.06));
        sh.lineTo(P(0.8, 0.06));
        sh.lineTo(P(0.8, 0.7));
        sh.quadTo(P(0.8, 0.86), P(0.5, 0.96));
        sh.quadTo(P(0.2, 0.86), P(0.2, 0.7));
        sh.closeSubpath();
    } else {
        sh.addEllipse(QRectF(P(0.1, 0.1), P(0.9, 0.9)));
    }
    p.setPen(inkPen(pw));
    p.setBrush(shine(sh.boundingRect(), tall ? QColor("#8a96a8") : QColor("#9a6a38")));
    p.drawPath(sh);
    p.setPen(QPen(tall ? QColor("#4a5468") : QColor("#5a3a18"), pw * 0.7));
    p.setBrush(Qt::NoBrush);
    if (tall) {
        p.drawPath(QTransform().translate(r.center().x(), r.center().y()).scale(0.82, 0.86).translate(-r.center().x(), -r.center().y()).map(sh));
        p.setPen(inkPen(pw * 2.5, QColor("#c8a030")));
        p.drawLine(P(0.5, 0.16), P(0.5, 0.82));
        p.drawLine(P(0.3, 0.38), P(0.7, 0.38));
    } else {
        for (const double x : {0.32, 0.5, 0.68}) p.drawLine(P(x, 0.13), P(x, 0.87));  // planks
        p.setPen(inkPen(pw * 1.4, QColor("#5a5a62")));
        p.drawEllipse(QRectF(P(0.13, 0.13), P(0.87, 0.87)));
        p.setPen(inkPen(pw));
        p.setBrush(ball(P(0.5, 0.5), r.width() * 0.12, QColor("#a8b0bc")));
        p.drawEllipse(P(0.5, 0.5), r.width() * 0.12, r.width() * 0.12);
    }
    glint(p, P(0.32, 0.26), r.width() * 0.07);
}

void drawHeadgear(QPainter& p, const QRectF& r, const QString& id) {
    const auto P = [&](double x, double y) { return QPointF(r.left() + r.width() * x, r.top() + r.height() * y); };
    const double pw = r.width() / 42;
    p.setPen(inkPen(pw));
    if (id.contains("crown")) {
        const QColor gold("#f0c030");
        const QPolygonF crown{P(0.12, 0.78), P(0.1, 0.3), P(0.3, 0.52), P(0.5, 0.18), P(0.7, 0.52), P(0.9, 0.3), P(0.88, 0.78)};
        p.setBrush(shine(crown.boundingRect(), gold));
        p.drawPolygon(crown);
        const QRectF band(P(0.1, 0.66), P(0.9, 0.82));
        p.setBrush(shine(band, gold.darker(110)));
        p.drawRect(band);
        int k = 0;
        for (const double x : {0.25, 0.5, 0.75}) {
            const QColor gem = k++ == 1 ? QColor("#d02030") : id.contains("command") ? QColor("#2a8ae0") : QColor("#2aa060");
            p.setBrush(ball(P(x, 0.74), r.width() * 0.05, gem));
            p.drawEllipse(P(x, 0.74), r.width() * 0.05, r.width() * 0.05);
        }
        for (const QPointF tip : {P(0.1, 0.3), P(0.5, 0.18), P(0.9, 0.3)}) {
            p.setBrush(ball(tip, r.width() * 0.04, gold.lighter(120)));
            p.drawEllipse(tip, r.width() * 0.04, r.width() * 0.04);
        }
        glint(p, P(0.34, 0.6), r.width() * 0.06);
    } else if (id.contains("hat")) {
        QPainterPath hat;
        hat.moveTo(P(0.3, 0.74));
        hat.cubicTo(P(0.4, 0.5), P(0.5, 0.2), P(0.78, 0.04));
        hat.cubicTo(P(0.66, 0.3), P(0.68, 0.52), P(0.72, 0.74));
        hat.closeSubpath();
        p.setBrush(shine(hat.boundingRect(), QColor("#3a3aa0")));
        p.drawPath(hat);
        p.setBrush(shine(QRectF(P(0.06, 0.68), P(0.94, 0.88)), QColor("#2e2e88")));
        p.drawEllipse(QRectF(P(0.06, 0.68), P(0.94, 0.88)));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#ffe27a"));
        for (const QPointF s : {P(0.5, 0.5), P(0.58, 0.32), P(0.45, 0.64)}) glint(p, s, r.width() * 0.05);
    } else if (id.contains("eye")) {  // a circlet with an eye stone
        p.setBrush(Qt::NoBrush);
        p.setPen(inkPen(pw * 4, QColor("#3a2a10")));
        p.drawArc(QRectF(P(0.1, 0.3), P(0.9, 0.8)), 0, 180 * 16);
        p.setPen(inkPen(pw * 2.4, QColor("#d8b048")));
        p.drawArc(QRectF(P(0.1, 0.3), P(0.9, 0.8)), 0, 180 * 16);
        p.setPen(inkPen(pw));
        p.setBrush(QColor("#f4f0e0"));
        p.drawEllipse(QRectF(P(0.32, 0.2), P(0.68, 0.42)));
        p.setBrush(ball(P(0.5, 0.31), r.width() * 0.08, QColor("#d8a020")));
        p.drawEllipse(P(0.5, 0.31), r.width() * 0.08, r.width() * 0.08);
        p.setBrush(Qt::black);
        p.drawEllipse(P(0.5, 0.31), r.width() * 0.025, r.width() * 0.06);
    } else {  // a helm with a nose guard
        QPainterPath helm;
        helm.moveTo(P(0.16, 0.8));
        helm.cubicTo(P(0.12, 0.2), P(0.88, 0.2), P(0.84, 0.8));
        helm.closeSubpath();
        p.setBrush(shine(helm.boundingRect(), QColor("#a8b0bc")));
        p.drawPath(helm);
        p.setBrush(shine(QRectF(P(0.14, 0.66), P(0.86, 0.8)), QColor("#8a6a3a")));
        p.drawRect(QRectF(P(0.14, 0.66), P(0.86, 0.8)));
        p.setBrush(QColor("#8a929e"));
        p.drawRect(QRectF(P(0.46, 0.5), P(0.54, 0.94)));
        p.setPen(inkPen(pw * 0.6, QColor("#4a5260")));
        p.drawLine(P(0.5, 0.26), P(0.5, 0.5));
        glint(p, P(0.34, 0.4), r.width() * 0.07);
    }
}

void drawNecklace(QPainter& p, const QRectF& r, const QColor& gem) {
    const auto P = [&](double x, double y) { return QPointF(r.left() + r.width() * x, r.top() + r.height() * y); };
    const double pw = r.width() / 45;
    p.setPen(QPen(QColor("#d8b048"), pw * 1.6, Qt::DotLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(P(0.16, -0.1), P(0.84, 0.66)), 180 * 16, 180 * 16);
    const QPointF c = P(0.5, 0.68);
    const double rad = r.width() * 0.2;
    p.setPen(inkPen(pw));
    p.setBrush(ball(c, rad * 1.2, QColor("#d8a838")));
    p.drawEllipse(c, rad * 1.2, rad * 1.25);
    p.setBrush(ball(c, rad * 0.85, gem));
    p.drawEllipse(c, rad * 0.85, rad * 0.9);
    glint(p, c - QPointF(rad * 0.35, rad * 0.4), rad * 0.4);
}

void drawArmour(QPainter& p, const QRectF& r, bool scales) {
    const auto P = [&](double x, double y) { return QPointF(r.left() + r.width() * x, r.top() + r.height() * y); };
    const double pw = r.width() / 45;
    const QPolygonF shirt{P(0.3, 0.1),  P(0.42, 0.16), P(0.58, 0.16), P(0.7, 0.1), P(0.94, 0.26), P(0.84, 0.46),
                          P(0.74, 0.4), P(0.76, 0.92), P(0.24, 0.92), P(0.26, 0.4), P(0.16, 0.46), P(0.06, 0.26)};
    const QColor c = scales ? QColor("#3a8a5a") : QColor("#a8b0bc");
    p.setPen(inkPen(pw));
    p.setBrush(shine(shirt.boundingRect(), c));
    p.drawPolygon(shirt);
    p.save();
    QPainterPath clip;
    clip.addPolygon(shirt);
    p.setClipPath(clip);
    p.setPen(QPen(c.darker(170), pw * 0.6));
    p.setBrush(Qt::NoBrush);
    const double step = r.width() * 0.07;
    for (int row = 0; row * step < r.height(); ++row)
        for (double x = r.left() + (row % 2) * step / 2; x < r.right(); x += step)
            p.drawArc(QRectF(x, r.top() + row * step * 0.7, step, step), 180 * 16, 180 * 16);
    p.restore();
    p.setPen(inkPen(pw));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(shirt);
    p.setBrush(shine(QRectF(P(0.26, 0.66), P(0.74, 0.72)), QColor("#6a4020")));
    p.drawRect(QRectF(P(0.25, 0.66), P(0.75, 0.72)));
    glint(p, P(0.36, 0.3), r.width() * 0.07);
}

void drawBoots(QPainter& p, const QRectF& r, bool winged) {
    const auto P = [&](double x, double y) { return QPointF(r.left() + r.width() * x, r.top() + r.height() * y); };
    const double pw = r.width() / 45;
    for (const double dx : {0.0, 0.26}) {
        QPainterPath boot;
        boot.moveTo(P(0.18 + dx, 0.2));
        boot.lineTo(P(0.42 + dx, 0.2));
        boot.lineTo(P(0.44 + dx, 0.7));
        boot.cubicTo(P(0.62 + dx, 0.72), P(0.7 + dx, 0.78), P(0.68 + dx, 0.88));
        boot.lineTo(P(0.18 + dx, 0.88));
        boot.closeSubpath();
        p.setPen(inkPen(pw));
        p.setBrush(shine(boot.boundingRect(), QColor("#7a4a22")));
        p.drawPath(boot);
        p.setBrush(QColor("#4a2a10"));
        p.drawRect(QRectF(P(0.17 + dx, 0.84), P(0.69 + dx, 0.9)));
        p.setBrush(shine(QRectF(P(0.16 + dx, 0.16), P(0.44 + dx, 0.26)), QColor("#a06a38")));
        p.drawRoundedRect(QRectF(P(0.16 + dx, 0.16), P(0.44 + dx, 0.26)), pw, pw);
        if (winged) {
            QPainterPath wing;
            const QPointF w0 = P(0.42 + dx, 0.36);
            wing.moveTo(w0);
            wing.cubicTo(w0 + QPointF(r.width() * 0.1, -r.height() * 0.24), w0 + QPointF(r.width() * 0.28, -r.height() * 0.22),
                         w0 + QPointF(r.width() * 0.3, -r.height() * 0.12));
            wing.cubicTo(w0 + QPointF(r.width() * 0.2, -r.height() * 0.06), w0 + QPointF(r.width() * 0.12, 0), w0 + QPointF(0, r.height() * 0.06));
            p.setBrush(shine(wing.boundingRect(), QColor("#f4f0e8")));
            p.drawPath(wing);
        }
    }
}

void drawRing(QPainter& p, const QRectF& r, const QColor& gem) {
    const auto P = [&](double x, double y) { return QPointF(r.left() + r.width() * x, r.top() + r.height() * y); };
    const double pw = r.width() / 45;
    p.setPen(inkPen(pw * 6, QColor("#3a2a10")));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(P(0.22, 0.36), P(0.78, 0.9)));
    QConicalGradient band(P(0.5, 0.63), 120);
    band.setColorAt(0, QColor("#fff0a0"));
    band.setColorAt(0.5, QColor("#a87a18"));
    band.setColorAt(1, QColor("#fff0a0"));
    p.setPen(QPen(QBrush(band), pw * 3.6));
    p.drawEllipse(QRectF(P(0.22, 0.36), P(0.78, 0.9)));
    p.setPen(inkPen(pw));
    p.setBrush(shine(QRectF(P(0.38, 0.28), P(0.62, 0.42)), QColor("#d8a838")));
    p.drawRoundedRect(QRectF(P(0.37, 0.28), P(0.63, 0.42)), pw * 2, pw * 2);
    drawGem(p, QRectF(P(0.33, 0.06), P(0.67, 0.36)), gem);
}

void drawMiscArtifact(QPainter& p, const QRectF& r, const QString& id) {
    const auto P = [&](double x, double y) { return QPointF(r.left() + r.width() * x, r.top() + r.height() * y); };
    const double pw = r.width() / 45;
    p.setPen(inkPen(pw));
    if (id.contains("spyglass")) {
        p.save();
        p.translate(r.center());
        p.rotate(-30);
        p.translate(-r.center());
        const QRectF parts[] = {QRectF(P(0.06, 0.4), P(0.36, 0.6)), QRectF(P(0.34, 0.42), P(0.64, 0.58)), QRectF(P(0.62, 0.44), P(0.94, 0.56))};
        const QColor cols[] = {QColor("#8a5a2c"), QColor("#d8a838"), QColor("#8a5a2c")};
        for (int i = 0; i < 3; ++i) {
            QLinearGradient g(parts[i].topLeft(), parts[i].bottomLeft());
            g.setColorAt(0, cols[i].lighter(140));
            g.setColorAt(1, cols[i].darker(150));
            p.setBrush(g);
            p.drawRoundedRect(parts[i], pw, pw);
        }
        p.setBrush(QColor(180, 220, 255));
        p.drawEllipse(QRectF(P(0.03, 0.39), P(0.09, 0.61)));
        p.restore();
    } else if (id.contains("clover")) {
        for (int i = 0; i < 4; ++i) {
            const double a = M_PI / 2 * i + M_PI / 4;
            const QPointF c = P(0.5, 0.42) + QPointF(std::cos(a), std::sin(a)) * r.width() * 0.17;
            p.setBrush(ball(c, r.width() * 0.18, QColor("#3aa040")));
            p.drawEllipse(c, r.width() * 0.17, r.width() * 0.17);
        }
        p.setPen(inkPen(pw * 2, QColor("#2a6a20")));
        p.drawLine(P(0.5, 0.45), P(0.6, 0.94));
    } else if (id.contains("banner") || id.contains("standard")) {
        p.setPen(inkPen(pw * 2.4, QColor("#3a2410")));
        p.drawLine(P(0.22, 0.96), P(0.22, 0.04));
        p.setPen(inkPen(pw));
        QPainterPath flag;
        flag.moveTo(P(0.22, 0.1));
        flag.cubicTo(P(0.5, 0.04), P(0.6, 0.16), P(0.86, 0.1));
        flag.lineTo(P(0.86, 0.56));
        flag.lineTo(P(0.68, 0.48));
        flag.lineTo(P(0.54, 0.62));
        flag.cubicTo(P(0.44, 0.56), P(0.32, 0.6), P(0.22, 0.58));
        flag.closeSubpath();
        p.setBrush(shine(flag.boundingRect(), id.contains("war") ? QColor("#c0302a") : QColor("#2a5ab0")));
        p.drawPath(flag);
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            const double a = M_PI / 5 * i - M_PI / 2, rad = (i % 2 ? 0.05 : 0.12) * r.width();
            star << P(0.52, 0.32) + QPointF(rad * std::cos(a), rad * std::sin(a));
        }
        p.setBrush(QColor("#f0c030"));
        p.drawPolygon(star);
        p.setBrush(ball(P(0.22, 0.04), r.width() * 0.04, QColor("#f0c030")));
        p.drawEllipse(P(0.22, 0.04), r.width() * 0.04, r.width() * 0.04);
    } else if (id.contains("orb")) {
        p.setBrush(shine(QRectF(P(0.24, 0.76), P(0.76, 0.94)), QColor("#8a5a2c")));
        p.drawRoundedRect(QRectF(P(0.24, 0.76), P(0.76, 0.94)), pw * 2, pw * 2);
        p.setBrush(ball(P(0.5, 0.46), r.width() * 0.32, QColor("#6a4ac8")));
        p.drawEllipse(P(0.5, 0.46), r.width() * 0.32, r.width() * 0.32);
        p.setPen(QPen(QColor(220, 200, 255, 160), pw));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(P(0.5, 0.46), r.width() * 0.16, r.width() * 0.08);
        glint(p, P(0.38, 0.32), r.width() * 0.08);
    } else if (id.contains("horseshoe")) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#2a2a30"), r.width() * 0.17, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(QRectF(P(0.2, 0.14), P(0.8, 0.8)), -30 * 16, 240 * 16);
        p.setPen(QPen(QColor("#a8b0bc"), r.width() * 0.12, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(QRectF(P(0.2, 0.14), P(0.8, 0.8)), -28 * 16, 236 * 16);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#2a2a30"));
        for (int i = 0; i < 6; ++i) {
            const double a = (-10 + 40 * i) * M_PI / 180;
            p.drawEllipse(P(0.5, 0.47) + QPointF(std::cos(a), -std::sin(a)) * r.width() * 0.3, r.width() * 0.018, r.width() * 0.018);
        }
    } else if (id.contains("heart")) {
        QPainterPath heart;
        heart.moveTo(P(0.5, 0.9));
        heart.cubicTo(P(0.1, 0.6), P(0.06, 0.2), P(0.32, 0.18));
        heart.cubicTo(P(0.44, 0.18), P(0.5, 0.28), P(0.5, 0.34));
        heart.cubicTo(P(0.5, 0.28), P(0.56, 0.18), P(0.68, 0.18));
        heart.cubicTo(P(0.94, 0.2), P(0.9, 0.6), P(0.5, 0.9));
        p.setBrush(shine(heart.boundingRect(), QColor("#3a8a3a")));
        p.drawPath(heart);
        p.setPen(QPen(QColor("#1a4a1a"), pw));
        p.drawLine(P(0.4, 0.4), P(0.48, 0.6));
        p.drawLine(P(0.62, 0.36), P(0.56, 0.56));
        glint(p, P(0.3, 0.32), r.width() * 0.06);
    } else if (id.contains("string") || id.contains("bow")) {
        p.setBrush(Qt::NoBrush);
        p.setPen(inkPen(pw * 4, QColor("#3a2410")));
        p.drawArc(QRectF(P(0.1, 0.06), P(0.7, 0.94)), -70 * 16, 140 * 16);
        p.setPen(QPen(QColor("#a06a38"), pw * 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(P(0.1, 0.06), P(0.7, 0.94)), -70 * 16, 140 * 16);
        p.setPen(QPen(QColor("#f4f0e0"), pw * 1.2));
        const QPointF top = P(0.4, 0.06) + QPointF(r.width() * 0.3 * std::cos(70 * M_PI / 180) * 0 + r.width() * 0.1, r.height() * 0.02);
        p.drawLine(top, QPointF(top.x(), r.top() + r.height() * 0.92));
        glint(p, P(0.6, 0.3), r.width() * 0.06);
    } else if (id.contains("stone")) {
        drawGem(p, QRectF(P(0.14, 0.18), P(0.86, 0.86)), QColor("#3a8ae0"));
    } else {
        drawGem(p, QRectF(P(0.14, 0.18), P(0.86, 0.86)), QColor("#d8a838"));
    }
}

}  // namespace

void drawArtifactIcon(QPainter& p, const QRectF& r, const QString& id, const QString& slot) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    if (slot == "weapon") {
        if (id.contains("axe")) drawAxe(p, r);
        else if (id.contains("staff")) drawStaff(p, r, QColor("#6ab8ff"));
        else drawBlade(p, r, id.contains("dragon"));
    } else if (slot == "shield") drawShield(p, r, id.contains("tower"));
    else if (slot == "head") drawHeadgear(p, r, id);
    else if (slot == "neck") drawNecklace(p, r, id.contains("mana") ? QColor("#3a6ae0") : id.contains("courage") ? QColor("#e0602a") : QColor("#d02040"));
    else if (slot == "body") drawArmour(p, r, id.contains("scale") || id.contains("dragon"));
    else if (slot == "feet") drawBoots(p, r, id.contains("wing"));
    else if (slot == "ring") drawRing(p, r, id.contains("sage") ? QColor("#8a4ae0") : id.contains("vigor") ? QColor("#d02040") : QColor("#2aa060"));
    else drawMiscArtifact(p, r, id);
    p.restore();
}


}  // namespace kot::artgen

// --- map sites -------------------------------------------------------------------

namespace kot::artgen {

void drawSite(QPainter& p, const QRectF& r, const QString& site, bool glowing) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    groundShadow(p, r, 0.78);
    const double w = r.width(), h = r.height();
    const auto P = [&](double x, double y) { return QPointF(r.left() + w * x, r.top() + h * y); };
    const double pw = w / 60;
    const QColor stone("#a8a090");
    const auto halo = [&](QPointF c, double rad, const QColor& col) {
        if (!glowing) return;
        QRadialGradient g(c, rad);
        g.setColorAt(0, QColor(col.red(), col.green(), col.blue(), 170));
        g.setColorAt(1, QColor(col.red(), col.green(), col.blue(), 0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(c, rad, rad);
    };
    p.setPen(inkPen(pw));
    if (site == "quest_hut") {  // a small round hut of wood and thatch, a scroll on a post: the hermit's wish
        halo(P(0.72, 0.36), w * 0.24, QColor("#ffe08a"));
        const QRectF wall(P(0.16, 0.5), P(0.62, 0.88));
        p.setBrush(shine(wall, QColor("#8a6a44")));
        p.drawRect(wall);
        p.setPen(inkPen(pw * 0.6, QColor(60, 40, 20)));
        for (double y = 0.58; y < 0.88; y += 0.08) p.drawLine(P(0.16, y), P(0.62, y));
        p.setPen(inkPen(pw));
        const QPolygonF thatch{P(0.08, 0.54), P(0.39, 0.22), P(0.7, 0.54)};
        p.setBrush(shine(thatch.boundingRect(), QColor("#c8a050")));
        p.drawPolygon(thatch);
        p.setBrush(QColor("#2a1a0c"));
        p.drawRoundedRect(QRectF(P(0.32, 0.66), P(0.46, 0.88)), w * 0.02, w * 0.02);  // the door
        p.setBrush(QColor("#6a4a2a"));
        p.drawRect(QRectF(P(0.71, 0.4), P(0.75, 0.88)));  // the post
        const QRectF scroll(P(0.62, 0.22), P(0.86, 0.46));
        p.setBrush(shine(scroll, QColor("#f4e6c0")));
        p.drawRoundedRect(scroll, w * 0.02, w * 0.02);
        p.setPen(inkPen(pw * 0.5, QColor(90, 60, 30)));
        for (int i = 0; i < 3; ++i) p.drawLine(P(0.66, 0.28 + 0.05 * i), P(0.82, 0.28 + 0.05 * i));
        p.setPen(inkPen(pw));
        if (!glowing) {  // done: the scroll is crossed out
            p.setPen(inkPen(pw * 1.2, QColor("#a02020")));
            p.drawLine(P(0.63, 0.24), P(0.85, 0.44));
        }
    } else if (site == "shrine") {  // a stone altar under a little roof, a glowing book on it
        halo(P(0.5, 0.5), w * 0.42, QColor("#a0c8ff"));
        p.setBrush(shine(QRectF(P(0.2, 0.74), P(0.8, 0.9)), stone));
        p.drawRect(QRectF(P(0.16, 0.8), P(0.84, 0.9)));
        for (const double x : {0.22, 0.7}) {
            p.setBrush(shine(QRectF(P(x, 0.34), P(x + 0.08, 0.8)), stone.lighter(110)));
            p.drawRect(QRectF(P(x, 0.34), P(x + 0.08, 0.8)));
        }
        const QPolygonF roof{P(0.1, 0.36), P(0.5, 0.08), P(0.9, 0.36)};
        p.setBrush(shine(roof.boundingRect(), QColor("#5a6aa8")));
        p.drawPolygon(roof);
        p.setBrush(shine(QRectF(P(0.36, 0.58), P(0.64, 0.8)), stone));
        p.drawRect(QRectF(P(0.36, 0.6), P(0.64, 0.8)));
        p.setBrush(QColor("#f4ecd0"));
        p.drawPolygon(QPolygonF{P(0.34, 0.6), P(0.5, 0.54), P(0.66, 0.6), P(0.5, 0.64)});
        p.setPen(QPen(QColor("#5a4a30"), pw * 0.6));
        p.drawLine(P(0.5, 0.54), P(0.5, 0.64));
        if (glowing) glint(p, P(0.5, 0.46), w * 0.08);
    } else if (site == "magic_well") {  // a round stone well with glowing water
        halo(P(0.5, 0.56), w * 0.38, QColor("#60e0ff"));
        const QRectF rim(P(0.2, 0.5), P(0.8, 0.66));
        p.setBrush(shine(QRectF(P(0.2, 0.56), P(0.8, 0.88)), stone));
        p.drawRect(QRectF(P(0.2, 0.58), P(0.8, 0.86)));
        p.drawEllipse(QRectF(P(0.2, 0.78), P(0.8, 0.92)));
        p.setPen(QPen(stone.darker(150), pw * 0.6));
        for (const double y : {0.66, 0.74})
            for (double x = 0.26 + (y > 0.7 ? 0.06 : 0); x < 0.8; x += 0.12) p.drawLine(P(x, y), P(x, y + 0.08));
        p.setPen(inkPen(pw));
        p.setBrush(shine(rim, stone.lighter(115)));
        p.drawEllipse(rim);
        p.setBrush(glowing ? QColor("#5ad8ff") : QColor("#3a5a6a"));
        p.drawEllipse(rim.adjusted(w * 0.05, h * 0.025, -w * 0.05, -h * 0.025));
        p.setPen(inkPen(pw * 1.4, QColor("#4a3018")));
        p.drawLine(P(0.24, 0.56), P(0.24, 0.2));
        p.drawLine(P(0.76, 0.56), P(0.76, 0.2));
        p.drawLine(P(0.2, 0.22), P(0.8, 0.22));
        if (glowing) glint(p, P(0.42, 0.56), w * 0.06);
    } else if (site == "obelisk") {  // a tall stone needle with an eye
        halo(P(0.5, 0.3), w * 0.3, QColor("#ffe27a"));
        p.setBrush(shine(QRectF(P(0.24, 0.8), P(0.76, 0.92)), stone));
        p.drawRect(QRectF(P(0.24, 0.82), P(0.76, 0.92)));
        const QPolygonF needle{P(0.38, 0.82), P(0.42, 0.14), P(0.5, 0.04), P(0.58, 0.14), P(0.62, 0.82)};
        p.setBrush(shine(needle.boundingRect(), QColor("#8a8478")));
        p.drawPolygon(needle);
        p.setBrush(QColor("#f4f0e0"));
        p.drawEllipse(QRectF(P(0.43, 0.26), P(0.57, 0.34)));
        p.setBrush(glowing ? QColor("#ffd84a") : QColor("#6a6050"));
        p.drawEllipse(P(0.5, 0.3), w * 0.03, w * 0.03);
        p.setPen(QPen(QColor("#4a4438"), pw * 0.6));
        for (const double y : {0.46, 0.56, 0.66}) p.drawLine(P(0.46, y), P(0.54, y + 0.04));
    } else if (site == "arena") {  // a ring of fence with crossed swords and a banner
        halo(P(0.5, 0.6), w * 0.36, QColor("#ff9a5a"));
        p.setBrush(shine(QRectF(P(0.08, 0.5), P(0.92, 0.9)), QColor("#c8a868")));
        p.drawEllipse(QRectF(P(0.08, 0.5), P(0.92, 0.9)));
        p.setPen(inkPen(pw * 1.2, QColor("#4a3018")));
        for (int i = 0; i < 9; ++i) {
            const double a = M_PI * (0.05 + 0.9 * i / 8.0);
            const QPointF base = P(0.5 - 0.42 * std::cos(a), 0.7 + 0.2 * std::sin(a));
            p.drawLine(base, base - QPointF(0, h * 0.12));
        }
        p.drawLine(P(0.1, 0.62), P(0.9, 0.62));
        p.setPen(inkPen(pw * 2.4));
        p.drawLine(P(0.34, 0.2), P(0.66, 0.52));
        p.drawLine(P(0.66, 0.2), P(0.34, 0.52));
        p.setPen(QPen(QColor("#d8dce8"), pw * 1.3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(P(0.34, 0.2), P(0.62, 0.48));
        p.drawLine(P(0.66, 0.2), P(0.38, 0.48));
        p.setPen(inkPen(pw));
        p.setBrush(QColor("#d8a838"));
        p.drawRect(QRectF(P(0.36, 0.42), P(0.42, 0.46)));
        p.drawRect(QRectF(P(0.58, 0.42), P(0.64, 0.46)));
    } else {  // learning stone: a standing stone with glowing runes
        halo(P(0.5, 0.45), w * 0.36, QColor("#9aff9a"));
        QPainterPath stoneShape;
        stoneShape.moveTo(P(0.28, 0.86));
        stoneShape.cubicTo(P(0.24, 0.5), P(0.3, 0.12), P(0.5, 0.1));
        stoneShape.cubicTo(P(0.7, 0.12), P(0.76, 0.5), P(0.72, 0.86));
        stoneShape.closeSubpath();
        p.setBrush(shine(stoneShape.boundingRect(), QColor("#7a7a70")));
        p.drawPath(stoneShape);
        p.setPen(QPen(glowing ? QColor("#a8ffa0") : QColor("#4a4a40"), pw * 1.4, Qt::SolidLine, Qt::RoundCap));
        p.drawPolyline(QPolygonF{P(0.44, 0.26), P(0.5, 0.34), P(0.56, 0.26)});
        p.drawLine(P(0.5, 0.4), P(0.5, 0.56));
        p.drawPolyline(QPolygonF{P(0.42, 0.62), P(0.5, 0.7), P(0.58, 0.62)});
        p.drawLine(P(0.44, 0.48), P(0.56, 0.48));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(90, 130, 60, 200));
        p.drawEllipse(QRectF(P(0.2, 0.82), P(0.4, 0.9)));
        p.drawEllipse(QRectF(P(0.6, 0.83), P(0.8, 0.9)));
    }
    p.restore();
}


}  // namespace kot::artgen
