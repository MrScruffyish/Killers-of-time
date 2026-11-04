// A picture for every spell: a medallion in its school's colour with what the
// spell does drawn on it (a flame, a bolt, a wave, a skull...). Drawn big through
// art::sprite and scaled.

#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <tuple>

#include "client/Art.h"

namespace kot::client::art {

namespace {

const QColor kInk(30, 20, 10);

QColor schoolColour(const QString& school) {
    if (school == "air") return QColor("#7ac0f0");
    if (school == "water") return QColor("#3a7ad0");
    if (school == "earth") return QColor("#a07a40");
    if (school == "nature") return QColor("#4aa840");
    if (school == "dark") return QColor("#8a3ab8");
    if (school == "fire") return QColor("#e05a20");
    return QColor("#d8b048");
}

struct Pen {
    QPainter& p;
    QRectF r;
    QPointF P(double x, double y) const { return {r.left() + r.width() * x, r.top() + r.height() * y}; }
    double W(double f) const { return r.width() * f; }
    QPen ink(double f, const QColor& c = kInk) const { return QPen(c, std::max(1.0, W(f)), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin); }
};

QBrush glow(const QPointF& c, double rad, const QColor& col) {
    QRadialGradient g(c - QPointF(rad * 0.3, rad * 0.35), rad * 1.2);
    g.setColorAt(0, col.lighter(175));
    g.setColorAt(0.55, col);
    g.setColorAt(1, col.darker(160));
    return g;
}

void sparkle(QPainter& p, const QPointF& c, double s, const QColor& col = QColor(255, 255, 255, 235)) {
    QPainterPath path;
    path.moveTo(c.x(), c.y() - s);
    path.quadTo(c, QPointF(c.x() + s, c.y()));
    path.quadTo(c, QPointF(c.x(), c.y() + s));
    path.quadTo(c, QPointF(c.x() - s, c.y()));
    path.quadTo(c, QPointF(c.x(), c.y() - s));
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    p.drawPath(path);
    p.restore();
}

QPainterPath flamePath(const Pen& d, double cx, double base, double h, double wd) {
    QPainterPath f;
    f.moveTo(d.P(cx, base));
    f.cubicTo(d.P(cx - wd, base - 0.02), d.P(cx - wd * 0.9, base - h * 0.55), d.P(cx - wd * 0.15, base - h));
    f.cubicTo(d.P(cx - wd * 0.1, base - h * 0.65), d.P(cx + wd * 0.35, base - h * 0.7), d.P(cx + wd * 0.3, base - h * 0.85));
    f.cubicTo(d.P(cx + wd * 1.0, base - h * 0.5), d.P(cx + wd * 0.9, base - 0.02), d.P(cx, base));
    return f;
}

void flame(const Pen& d, double cx, double base, double h, double wd) {
    QLinearGradient g(d.P(cx, base), d.P(cx, base - h));
    g.setColorAt(0, QColor("#ffe070"));
    g.setColorAt(0.5, QColor("#ff8a20"));
    g.setColorAt(1, QColor("#d02a10"));
    d.p.setPen(d.ink(0.02, QColor("#5a1a08")));
    d.p.setBrush(g);
    d.p.drawPath(flamePath(d, cx, base, h, wd));
    d.p.setPen(Qt::NoPen);
    d.p.setBrush(QColor(255, 250, 200, 220));
    d.p.drawPath(flamePath(d, cx, base - 0.01, h * 0.45, wd * 0.45));
}

void bolt(const Pen& d, const QList<QPointF>& pts, double wd, const QColor& c) {
    QPolygonF poly;
    for (const QPointF& q : pts) poly << d.P(q.x(), q.y());
    d.p.setBrush(Qt::NoBrush);
    d.p.setPen(QPen(QColor(c.red(), c.green(), c.blue(), 90), d.W(wd * 3), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    d.p.drawPolyline(poly);
    d.p.setPen(d.ink(wd * 1.6, kInk));
    d.p.drawPolyline(poly);
    d.p.setPen(QPen(c, d.W(wd), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    d.p.drawPolyline(poly);
    d.p.setPen(QPen(Qt::white, d.W(wd * 0.35), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    d.p.drawPolyline(poly);
}

void cloud(const Pen& d, double cx, double cy, double s, const QColor& c) {
    QPainterPath path;
    for (const auto& [x, y, r] : {std::tuple{-0.16, 0.03, 0.12}, std::tuple{0.0, -0.05, 0.15}, std::tuple{0.17, 0.02, 0.12}, std::tuple{0.05, 0.07, 0.12}})
        path.addEllipse(d.P(cx + x * s, cy + y * s), d.W(r * s), d.W(r * s));
    path = path.simplified();
    d.p.setPen(d.ink(0.02));
    d.p.setBrush(glow(d.P(cx, cy), d.W(0.2 * s), c));
    d.p.drawPath(path);
}

void drop(const Pen& d, double cx, double cy, double s, const QColor& c) {
    QPainterPath path;
    path.moveTo(d.P(cx, cy - 0.2 * s));
    path.cubicTo(d.P(cx + 0.15 * s, cy), d.P(cx + 0.14 * s, cy + 0.16 * s), d.P(cx, cy + 0.16 * s));
    path.cubicTo(d.P(cx - 0.14 * s, cy + 0.16 * s), d.P(cx - 0.15 * s, cy), d.P(cx, cy - 0.2 * s));
    d.p.setPen(d.ink(0.02));
    d.p.setBrush(glow(d.P(cx, cy + 0.04 * s), d.W(0.14 * s), c));
    d.p.drawPath(path);
    sparkle(d.p, d.P(cx - 0.05 * s, cy + 0.02 * s), d.W(0.04 * s));
}

void skull(const Pen& d, double cx, double cy, double s, const QColor& bone) {
    QPainterPath head;
    head.addEllipse(d.P(cx, cy), d.W(0.17 * s), d.W(0.16 * s));
    head.addRoundedRect(QRectF(d.P(cx - 0.09 * s, cy + 0.06 * s), d.P(cx + 0.09 * s, cy + 0.2 * s)), d.W(0.03), d.W(0.03));
    d.p.setPen(d.ink(0.02));
    d.p.setBrush(glow(d.P(cx, cy), d.W(0.17 * s), bone));
    d.p.drawPath(head.simplified());
    d.p.setBrush(QColor("#1a0a1a"));
    d.p.drawEllipse(d.P(cx - 0.065 * s, cy + 0.01 * s), d.W(0.045 * s), d.W(0.05 * s));
    d.p.drawEllipse(d.P(cx + 0.065 * s, cy + 0.01 * s), d.W(0.045 * s), d.W(0.05 * s));
    d.p.drawPolygon(QPolygonF{d.P(cx, cy + 0.07 * s), d.P(cx - 0.025 * s, cy + 0.11 * s), d.P(cx + 0.025 * s, cy + 0.11 * s)});
    d.p.setPen(d.ink(0.012));
    for (const double x : {-0.05, 0.0, 0.05}) d.p.drawLine(d.P(cx + x * s, cy + 0.14 * s), d.P(cx + x * s, cy + 0.2 * s));
}

void arrow(const Pen& d, QPointF from, QPointF to, double wd, const QColor& c) {
    const QPointF a = d.P(from.x(), from.y()), b = d.P(to.x(), to.y());
    const QPointF u = (b - a) / std::hypot(b.x() - a.x(), b.y() - a.y());
    const QPointF n(-u.y(), u.x());
    d.p.setPen(d.ink(wd * 1.8));
    d.p.drawLine(a, b - u * d.W(0.08));
    d.p.setPen(QPen(c.lighter(130), d.W(wd), Qt::SolidLine, Qt::RoundCap));
    d.p.drawLine(a, b - u * d.W(0.08));
    d.p.setPen(d.ink(0.015));
    d.p.setBrush(glow(b, d.W(0.08), c));
    d.p.drawPolygon(QPolygonF{b, b - u * d.W(0.16) + n * d.W(0.07), b - u * d.W(0.11), b - u * d.W(0.16) - n * d.W(0.07)});
    d.p.setBrush(c.lighter(150));
    for (const double off : {0.0, 0.06})
        d.p.drawPolygon(QPolygonF{a + u * d.W(off), a + u * d.W(off + 0.05) + n * d.W(0.06), a + u * d.W(off + 0.09),
                                  a + u * d.W(off + 0.05) - n * d.W(0.06)});
}

void leaf(const Pen& d, QPointF base, double angle, double len, const QColor& c) {
    d.p.save();
    d.p.translate(d.P(base.x(), base.y()));
    d.p.rotate(angle);
    QPainterPath l;
    l.moveTo(0, 0);
    l.quadTo(d.W(len * 0.45), -d.W(len * 0.5), 0, -d.W(len));
    l.quadTo(-d.W(len * 0.45), -d.W(len * 0.5), 0, 0);
    d.p.setPen(d.ink(0.015, c.darker(250)));
    d.p.setBrush(glow(QPointF(0, -d.W(len / 2)), d.W(len / 2), c));
    d.p.drawPath(l);
    d.p.setPen(QPen(c.darker(170), d.W(0.008)));
    d.p.drawLine(QPointF(0, 0), QPointF(0, -d.W(len * 0.85)));
    d.p.restore();
}

void rock(const Pen& d, double cx, double cy, double s, int seed, const QColor& c = QColor("#8a7a68")) {
    QPolygonF poly;
    for (int i = 0; i < 7; ++i) {
        const double a = M_PI * 2 * i / 7 + seed;
        const double rr = s * (0.8 + 0.2 * (((i + 2) * (seed + 5) * 7) % 10) / 10.0);
        poly << d.P(cx + std::cos(a) * rr, cy + std::sin(a) * rr * 0.85);
    }
    d.p.setPen(d.ink(0.018));
    d.p.setBrush(glow(d.P(cx, cy), d.W(s), c));
    d.p.drawPolygon(poly);
}

void shieldShape(const Pen& d, const QColor& c, bool lines) {
    QPainterPath sh;
    sh.moveTo(d.P(0.5, 0.2));
    sh.lineTo(d.P(0.76, 0.3));
    sh.quadTo(d.P(0.76, 0.62), d.P(0.5, 0.82));
    sh.quadTo(d.P(0.24, 0.62), d.P(0.24, 0.3));
    sh.closeSubpath();
    d.p.setPen(d.ink(0.025));
    d.p.setBrush(glow(d.P(0.5, 0.45), d.W(0.3), c));
    d.p.drawPath(sh);
    if (lines) {
        d.p.save();
        d.p.setClipPath(sh);
        d.p.setPen(QPen(c.darker(170), d.W(0.012)));
        for (double y = 0.3; y < 0.85; y += 0.07)
            d.p.drawLine(d.P(0.2, y + 0.02 * std::sin(y * 30)), d.P(0.8, y - 0.02 * std::sin(y * 25)));
        d.p.restore();
    }
}

void motif(const Pen& d, const QString& id, const QColor& school) {
    QPainter& p = d.p;
    if (id == "magic_arrow") {
        arrow(d, {0.22, 0.78}, {0.8, 0.2}, 0.035, QColor("#a0e0ff"));
        sparkle(p, d.P(0.78, 0.22), d.W(0.08));
    } else if (id == "fire_bolt") {
        p.save();
        p.translate(d.P(0.5, 0.5));
        p.rotate(45);
        p.translate(-d.P(0.5, 0.5));
        flame(d, 0.5, 0.84, 0.66, 0.13);
        p.restore();
    } else if (id == "fireball") {
        flame(d, 0.5, 0.7, 0.5, 0.26);
        p.setPen(d.ink(0.02, QColor("#5a1a08")));
        p.setBrush(glow(d.P(0.5, 0.6), d.W(0.17), QColor("#ffb030")));
        p.drawEllipse(d.P(0.5, 0.62), d.W(0.16), d.W(0.16));
    } else if (id == "inferno") {
        flame(d, 0.3, 0.8, 0.45, 0.15);
        flame(d, 0.7, 0.8, 0.45, 0.15);
        flame(d, 0.5, 0.82, 0.64, 0.2);
    } else if (id == "meteor_shower") {
        for (const auto& [x, y, s] : {std::tuple{0.35, 0.35, 0.09}, std::tuple{0.68, 0.5, 0.11}, std::tuple{0.4, 0.7, 0.08}}) {
            p.setPen(QPen(QColor(255, 160, 60, 170), d.W(s * 0.9), Qt::SolidLine, Qt::RoundCap));
            p.drawLine(d.P(x - 0.18, y - 0.2), d.P(x, y));
            rock(d, x, y, s, int(x * 10), QColor("#c0603a"));
        }
    } else if (id == "ash_cloud") {
        cloud(d, 0.5, 0.42, 1.2, QColor("#7a7470"));
        for (const double x : {0.36, 0.5, 0.64}) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(60, 54, 50, 200));
            p.drawEllipse(d.P(x, 0.68 + 0.04 * std::sin(x * 20)), d.W(0.025), d.W(0.025));
        }
    } else if (id == "battle_fury") {
        flame(d, 0.5, 0.86, 0.72, 0.24);
        p.setPen(d.ink(0.07));
        p.drawLine(d.P(0.3, 0.75), d.P(0.7, 0.3));
        p.setPen(QPen(QColor("#e8ecf4"), d.W(0.04), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(d.P(0.3, 0.75), d.P(0.7, 0.3));
    } else if (id == "lightning_bolt") {
        bolt(d, {{0.62, 0.14}, {0.4, 0.46}, {0.58, 0.48}, {0.36, 0.86}}, 0.05, QColor("#fff070"));
    } else if (id == "chain_lightning") {
        bolt(d, {{0.5, 0.14}, {0.38, 0.4}, {0.52, 0.44}, {0.3, 0.84}}, 0.04, QColor("#fff070"));
        bolt(d, {{0.52, 0.44}, {0.72, 0.6}, {0.62, 0.66}, {0.8, 0.82}}, 0.03, QColor("#fff070"));
    } else if (id == "gust") {
        p.setBrush(Qt::NoBrush);
        for (const auto& [y, len] : {std::pair{0.36, 0.5}, std::pair{0.52, 0.6}, std::pair{0.68, 0.42}}) {
            QPainterPath w;
            w.moveTo(d.P(0.2, y));
            w.cubicTo(d.P(0.2 + len * 0.5, y - 0.05), d.P(0.2 + len, y + 0.02), d.P(0.18 + len, y - 0.1));
            p.setPen(d.ink(0.06));
            p.drawPath(w);
            p.setPen(QPen(QColor("#f0faff"), d.W(0.035), Qt::SolidLine, Qt::RoundCap));
            p.drawPath(w);
        }
    } else if (id == "haste" || id == "haste_mass" || id == "swift_march") {
        // A winged boot; many: chevrons; march: a road arrow.
        if (id == "haste_mass") {
            for (const double x : {0.3, 0.48, 0.66}) {
                p.setPen(d.ink(0.07));
                p.drawPolyline(QPolygonF{d.P(x, 0.3), d.P(x + 0.12, 0.5), d.P(x, 0.7)});
                p.setPen(QPen(QColor("#f0faff"), d.W(0.04), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.drawPolyline(QPolygonF{d.P(x, 0.3), d.P(x + 0.12, 0.5), d.P(x, 0.7)});
            }
        } else {
            QPainterPath boot;
            boot.moveTo(d.P(0.32, 0.3));
            boot.lineTo(d.P(0.5, 0.3));
            boot.lineTo(d.P(0.52, 0.62));
            boot.cubicTo(d.P(0.68, 0.64), d.P(0.74, 0.7), d.P(0.72, 0.78));
            boot.lineTo(d.P(0.32, 0.78));
            boot.closeSubpath();
            p.setPen(d.ink(0.02));
            p.setBrush(glow(d.P(0.45, 0.5), d.W(0.25), QColor("#8a5a2c")));
            p.drawPath(boot);
            if (id == "haste") {
                QPainterPath wing;
                wing.moveTo(d.P(0.5, 0.42));
                wing.cubicTo(d.P(0.6, 0.2), d.P(0.82, 0.2), d.P(0.86, 0.3));
                wing.cubicTo(d.P(0.76, 0.36), d.P(0.66, 0.44), d.P(0.5, 0.5));
                p.setBrush(glow(d.P(0.7, 0.32), d.W(0.15), QColor("#f4f0e8")));
                p.drawPath(wing);
            } else {
                arrow(d, {0.18, 0.88}, {0.86, 0.88}, 0.025, QColor("#ffd84a"));
            }
        }
    } else if (id == "ice_bolt") {
        const QPolygonF ice{d.P(0.28, 0.76), d.P(0.74, 0.18), d.P(0.4, 0.82)};
        p.setPen(d.ink(0.02, QColor("#1a3a5a")));
        p.setBrush(glow(d.P(0.5, 0.5), d.W(0.3), QColor("#c0ecff")));
        p.drawPolygon(ice);
        p.setPen(QPen(Qt::white, d.W(0.012)));
        p.drawLine(d.P(0.33, 0.76), d.P(0.7, 0.24));
        sparkle(p, d.P(0.7, 0.24), d.W(0.07));
    } else if (id == "frost_ring") {
        for (int i = 0; i < 6; ++i) {
            const double a = M_PI / 3 * i;
            const QPointF c = d.P(0.5 + 0.24 * std::cos(a), 0.5 + 0.24 * std::sin(a));
            p.setPen(QPen(QColor("#e8f8ff"), d.W(0.02), Qt::SolidLine, Qt::RoundCap));
            for (int k = 0; k < 3; ++k) {
                const double b = M_PI / 3 * k;
                p.drawLine(c - QPointF(std::cos(b), std::sin(b)) * d.W(0.07), c + QPointF(std::cos(b), std::sin(b)) * d.W(0.07));
            }
        }
        p.setPen(QPen(QColor(220, 245, 255, 180), d.W(0.025)));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(d.P(0.5, 0.5), d.W(0.24), d.W(0.24));
    } else if (id == "tidal_wave") {
        QPainterPath wave;
        wave.moveTo(d.P(0.14, 0.78));
        wave.cubicTo(d.P(0.24, 0.4), d.P(0.56, 0.18), d.P(0.78, 0.3));
        wave.cubicTo(d.P(0.62, 0.32), d.P(0.56, 0.46), d.P(0.66, 0.52));
        wave.cubicTo(d.P(0.76, 0.56), d.P(0.84, 0.6), d.P(0.86, 0.78));
        wave.closeSubpath();
        p.setPen(d.ink(0.02, QColor("#0a2a4a")));
        p.setBrush(glow(d.P(0.4, 0.5), d.W(0.35), QColor("#3a9ae0")));
        p.drawPath(wave);
        p.setPen(QPen(QColor("#f0faff"), d.W(0.03), Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(d.P(0.5, 0.24), d.P(0.8, 0.5)), 30 * 16, 120 * 16);
    } else if (id == "bless") {
        for (int i = 0; i < 12; ++i) {
            const double a = M_PI / 6 * i;
            p.setPen(QPen(QColor(255, 240, 170, 200), d.W(0.025), Qt::SolidLine, Qt::RoundCap));
            p.drawLine(d.P(0.5 + 0.16 * std::cos(a), 0.5 + 0.16 * std::sin(a)), d.P(0.5 + 0.3 * std::cos(a), 0.5 + 0.3 * std::sin(a)));
        }
        p.setPen(d.ink(0.02));
        p.setBrush(glow(d.P(0.5, 0.5), d.W(0.14), QColor("#ffe27a")));
        p.drawEllipse(d.P(0.5, 0.5), d.W(0.13), d.W(0.13));
    } else if (id == "cure") {
        drop(d, 0.5, 0.48, 1.6, QColor("#4a9af0"));
        p.setPen(QPen(Qt::white, d.W(0.05), Qt::SolidLine, Qt::FlatCap));
        p.drawLine(d.P(0.5, 0.44), d.P(0.5, 0.64));
        p.drawLine(d.P(0.4, 0.54), d.P(0.6, 0.54));
    } else if (id == "far_sight") {
        QPainterPath eye;
        eye.moveTo(d.P(0.16, 0.5));
        eye.quadTo(d.P(0.5, 0.18), d.P(0.84, 0.5));
        eye.quadTo(d.P(0.5, 0.82), d.P(0.16, 0.5));
        p.setPen(d.ink(0.025));
        p.setBrush(QColor("#f4f0e0"));
        p.drawPath(eye);
        p.setBrush(glow(d.P(0.5, 0.5), d.W(0.13), QColor("#3a8ae0")));
        p.drawEllipse(d.P(0.5, 0.5), d.W(0.13), d.W(0.13));
        p.setBrush(Qt::black);
        p.drawEllipse(d.P(0.5, 0.5), d.W(0.05), d.W(0.05));
        sparkle(p, d.P(0.45, 0.45), d.W(0.04));
    } else if (id == "curse" || id == "weakness") {
        if (id == "curse") {
            skull(d, 0.5, 0.46, 1.3, QColor("#d8d0e0"));
        } else {  // a broken sword
            p.setPen(d.ink(0.07));
            p.drawLine(d.P(0.3, 0.8), d.P(0.48, 0.52));
            p.drawLine(d.P(0.54, 0.44), d.P(0.72, 0.2));
            p.setPen(QPen(QColor("#c8d0dc"), d.W(0.045), Qt::SolidLine, Qt::FlatCap));
            p.drawLine(d.P(0.3, 0.8), d.P(0.48, 0.52));
            p.drawLine(d.P(0.54, 0.44), d.P(0.72, 0.2));
            p.setPen(d.ink(0.03));
            p.drawLine(d.P(0.22, 0.66), d.P(0.4, 0.78));
        }
        p.setPen(QPen(QColor(200, 120, 255, 170), d.W(0.02), Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(d.P(0.5, 0.5), d.W(0.32), d.W(0.32));
    } else if (id == "death_touch" || id == "soul_reap") {
        if (id == "soul_reap") {
            p.setPen(d.ink(0.05));
            p.drawLine(d.P(0.3, 0.86), d.P(0.62, 0.18));
            p.setPen(QPen(QColor("#6a4a2a"), d.W(0.03), Qt::SolidLine, Qt::RoundCap));
            p.drawLine(d.P(0.3, 0.86), d.P(0.62, 0.18));
            QPainterPath blade;
            blade.moveTo(d.P(0.62, 0.18));
            blade.cubicTo(d.P(0.4, 0.12), d.P(0.22, 0.24), d.P(0.18, 0.4));
            blade.cubicTo(d.P(0.3, 0.3), d.P(0.46, 0.26), d.P(0.6, 0.26));
            p.setPen(d.ink(0.02));
            p.setBrush(glow(d.P(0.4, 0.24), d.W(0.2), QColor("#c8d0dc")));
            p.drawPath(blade);
            p.setBrush(QColor(170, 255, 200, 160));
            p.setPen(Qt::NoPen);
            p.drawEllipse(d.P(0.66, 0.66), d.W(0.08), d.W(0.1));
        } else {
            p.setPen(d.ink(0.05));
            for (int k = 0; k < 4; ++k) {
                const double x = 0.36 + 0.09 * k;
                p.drawLine(d.P(x, 0.62), d.P(x - 0.02, 0.26 + 0.04 * std::abs(k - 1.5)));
            }
            p.drawLine(d.P(0.32, 0.66), d.P(0.24, 0.48));
            p.setPen(QPen(QColor("#e8e0d0"), d.W(0.03), Qt::SolidLine, Qt::RoundCap));
            for (int k = 0; k < 4; ++k) {
                const double x = 0.36 + 0.09 * k;
                p.drawLine(d.P(x, 0.62), d.P(x - 0.02, 0.26 + 0.04 * std::abs(k - 1.5)));
            }
            p.drawLine(d.P(0.32, 0.66), d.P(0.24, 0.48));
            p.setPen(d.ink(0.02));
            p.setBrush(glow(d.P(0.48, 0.72), d.W(0.14), QColor("#e8e0d0")));
            p.drawRoundedRect(QRectF(d.P(0.32, 0.6), d.P(0.66, 0.82)), d.W(0.05), d.W(0.05));
        }
    } else if (id == "drain_life") {
        QPainterPath heart;
        heart.moveTo(d.P(0.5, 0.74));
        heart.cubicTo(d.P(0.2, 0.54), d.P(0.18, 0.26), d.P(0.36, 0.26));
        heart.cubicTo(d.P(0.44, 0.26), d.P(0.5, 0.32), d.P(0.5, 0.38));
        heart.cubicTo(d.P(0.5, 0.32), d.P(0.56, 0.26), d.P(0.64, 0.26));
        heart.cubicTo(d.P(0.82, 0.26), d.P(0.8, 0.54), d.P(0.5, 0.74));
        p.setPen(d.ink(0.02));
        p.setBrush(glow(d.P(0.5, 0.45), d.W(0.25), QColor("#c02040")));
        p.drawPath(heart);
        for (const double x : {0.4, 0.6}) drop(d, x, 0.84, 0.4, QColor("#c02040"));
    } else if (id == "plague_cloud") {
        cloud(d, 0.5, 0.42, 1.2, QColor("#6aa040"));
        skull(d, 0.5, 0.62, 0.55, QColor("#d8e8a0"));
    } else if (id == "barkskin" || id == "stone_skin") {
        shieldShape(d, id == "barkskin" ? QColor("#8a5a2c") : QColor("#9a948a"), true);
        if (id == "barkskin") leaf(d, {0.64, 0.34}, 40, 0.18, QColor("#5ab83a"));
        else sparkle(p, d.P(0.4, 0.36), d.W(0.05));
    } else if (id == "entangle" || id == "thorn_storm") {
        p.setBrush(Qt::NoBrush);
        for (int k = 0; k < 3; ++k) {
            QPainterPath vine;
            vine.moveTo(d.P(0.2 + 0.2 * k, 0.86));
            vine.cubicTo(d.P(0.1 + 0.25 * k, 0.6), d.P(0.5 + 0.1 * k, 0.5), d.P(0.3 + 0.2 * k, 0.2));
            p.setPen(d.ink(0.05, QColor("#1a3a10")));
            p.drawPath(vine);
            p.setPen(QPen(id == "entangle" ? QColor("#4a9a30") : QColor("#7a5a2a"), d.W(0.028), Qt::SolidLine, Qt::RoundCap));
            p.drawPath(vine);
            if (id == "thorn_storm")
                for (double t = 0.15; t < 1; t += 0.2) {
                    const QPointF at = vine.pointAtPercent(t);
                    p.setPen(d.ink(0.01));
                    p.setBrush(QColor("#d8c090"));
                    p.drawPolygon(QPolygonF{at, at + QPointF(d.W(0.05), -d.W(0.02)), at + QPointF(0, d.W(0.025))});
                    p.setBrush(Qt::NoBrush);
                }
            else
                leaf(d, {(vine.pointAtPercent(0.5).x() - d.r.left()) / d.r.width(), (vine.pointAtPercent(0.5).y() - d.r.top()) / d.r.height()},
                     50 - 40 * k, 0.12, QColor("#5ab83a"));
        }
    } else if (id == "regrowth") {
        p.setPen(d.ink(0.045, QColor("#1a3a10")));
        p.drawLine(d.P(0.5, 0.82), d.P(0.5, 0.44));
        p.setPen(QPen(QColor("#5aa83a"), d.W(0.025), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(d.P(0.5, 0.82), d.P(0.5, 0.44));
        leaf(d, {0.5, 0.5}, -50, 0.28, QColor("#5ac83a"));
        leaf(d, {0.5, 0.58}, 55, 0.24, QColor("#4ab83a"));
        p.setPen(d.ink(0.02));
        p.setBrush(glow(d.P(0.5, 0.84), d.W(0.15), QColor("#7a5a30")));
        p.drawChord(QRectF(d.P(0.3, 0.76), d.P(0.7, 0.94)), 0, 180 * 16);
        sparkle(p, d.P(0.66, 0.3), d.W(0.05));
    } else if (id == "stampede") {
        for (const auto& [x, y] : {std::pair{0.32, 0.72}, std::pair{0.52, 0.5}, std::pair{0.7, 0.28}}) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(40, 26, 12, 220));
            p.drawChord(QRectF(d.P(x - 0.1, y - 0.08), d.P(x + 0.1, y + 0.12)), 0, 180 * 16);
            p.drawRect(QRectF(d.P(x - 0.1, y + 0.02), d.P(x - 0.05, y + 0.1)));
            p.drawRect(QRectF(d.P(x + 0.05, y + 0.02), d.P(x + 0.1, y + 0.1)));
        }
        p.setPen(QPen(QColor(240, 220, 170, 160), d.W(0.02), Qt::DashLine));
        p.drawLine(d.P(0.16, 0.88), d.P(0.86, 0.18));
    } else if (id == "rock_fall") {
        rock(d, 0.36, 0.34, 0.13, 1);
        rock(d, 0.62, 0.52, 0.16, 2);
        rock(d, 0.38, 0.72, 0.11, 3);
        p.setPen(QPen(QColor(240, 230, 210, 150), d.W(0.02), Qt::SolidLine, Qt::RoundCap));
        for (const double x : {0.3, 0.56}) p.drawLine(d.P(x, 0.1), d.P(x, 0.2));
    } else if (id == "slow") {  // an hourglass in a snail's shell spiral
        p.setPen(d.ink(0.03));
        p.setBrush(glow(d.P(0.45, 0.48), d.W(0.24), QColor("#b08a50")));
        p.drawEllipse(d.P(0.48, 0.46), d.W(0.24), d.W(0.24));
        p.setPen(QPen(QColor("#5a3a18"), d.W(0.025)));
        p.setBrush(Qt::NoBrush);
        QPainterPath spiral;
        for (int i = 0; i <= 40; ++i) {
            const double a = i * 0.4, rr = 0.2 * i / 40.0;
            const QPointF q = d.P(0.48 + rr * std::cos(a), 0.46 + rr * std::sin(a));
            if (i == 0) spiral.moveTo(q);
            else spiral.lineTo(q);
        }
        p.drawPath(spiral);
        p.setPen(d.ink(0.02));
        p.setBrush(glow(d.P(0.4, 0.76), d.W(0.2), QColor("#c8b080")));
        p.drawRoundedRect(QRectF(d.P(0.16, 0.68), d.P(0.86, 0.8)), d.W(0.05), d.W(0.05));
    } else if (id == "war_cry") {
        QPainterPath horn;
        horn.moveTo(d.P(0.2, 0.62));
        horn.cubicTo(d.P(0.3, 0.7), d.P(0.56, 0.6), d.P(0.7, 0.3));
        horn.lineTo(d.P(0.84, 0.38));
        horn.cubicTo(d.P(0.7, 0.72), d.P(0.38, 0.8), d.P(0.2, 0.68));
        horn.closeSubpath();
        p.setPen(d.ink(0.02));
        p.setBrush(glow(d.P(0.5, 0.55), d.W(0.3), QColor("#e8d8b0")));
        p.drawPath(horn);
        p.setBrush(QColor("#c8a030"));
        p.drawEllipse(QRectF(d.P(0.68, 0.28), d.P(0.86, 0.4)));
        p.setPen(QPen(QColor(255, 240, 200, 200), d.W(0.025), Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        for (const double rr : {0.1, 0.18}) p.drawArc(QRectF(d.P(0.78 - rr, 0.32 - rr), d.P(0.78 + rr, 0.32 + rr)), 0, 80 * 16);
    } else {
        sparkle(p, d.P(0.5, 0.5), d.W(0.25), school.lighter(170));
    }
}

}  // namespace

void drawSpellIcon(QPainter& p, const QRectF& r, const QString& id, const QString& school) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor c = schoolColour(school);
    // The medallion: a gold rim and a deep disc in the school's colour.
    const QPointF centre = r.center();
    const double rad = r.width() * 0.48;
    QConicalGradient rim(centre, 135);
    rim.setColorAt(0, QColor("#fff0a8"));
    rim.setColorAt(0.5, QColor("#8a6418"));
    rim.setColorAt(1, QColor("#fff0a8"));
    p.setPen(QPen(kInk, r.width() / 60));
    p.setBrush(rim);
    p.drawEllipse(centre, rad, rad);
    QRadialGradient disc(centre - QPointF(rad * 0.2, rad * 0.25), rad * 1.1);
    disc.setColorAt(0, c.lighter(130));
    disc.setColorAt(0.7, c.darker(150));
    disc.setColorAt(1, c.darker(260));
    p.setBrush(disc);
    p.drawEllipse(centre, rad * 0.86, rad * 0.86);
    Pen d{p, r};
    motif(d, id, c);
    p.restore();
}

const QPixmap& spellIcon(const QString& id, const QString& school, int size) {
    return sprite(QStringLiteral("spell|%1|%2").arg(id, school), size, [&](QPainter& p, const QRectF& r) { drawSpellIcon(p, r, id, school); });
}

}  // namespace kot::client::art
