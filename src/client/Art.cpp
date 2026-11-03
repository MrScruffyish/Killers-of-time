#include "client/Art.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace kot::client::art {

namespace {

// A small deterministic random sequence (art only; the game rules never use it).
struct Dice {
    quint32 state;
    explicit Dice(quint32 seed) : state(seed * 2654435761u + 1) {}
    double next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (state % 10000) / 10000.0;
    }
};

void grassTufts(QPainter& p, int s, Dice& d, const QColor& c, int count) {
    p.setPen(QPen(c.darker(135), std::max(1.0, s / 24.0)));
    for (int i = 0; i < count; ++i) {
        const double x = d.next() * s, y = d.next() * s, h = s * (0.08 + 0.06 * d.next());
        p.drawLine(QPointF(x, y), QPointF(x - h * 0.3, y - h));
        p.drawLine(QPointF(x, y), QPointF(x + h * 0.3, y - h));
    }
}

void speckles(QPainter& p, int s, Dice& d, const QColor& c, int count, double size) {
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    for (int i = 0; i < count; ++i) p.drawEllipse(QPointF(d.next() * s, d.next() * s), s * size, s * size);
}

}  // namespace

QColor groundColor(const QString& id, const QColor& base) {
    // Under the peaks: rocky earth, so the mountains' grey does not show as squares.
    if (id == "mountains") return QColor("#8e8a6c");
    return base;
}

namespace {

QPixmap makeTile(const QString& id, const QColor& dataColour, int s, int variant) {
    const QColor base = groundColor(id, dataColour);
    QPixmap pm(s, s);
    pm.fill(base);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    Dice d(qHash(id) + variant * 7919);
    if (s < 8) return pm;

    if (id == "grass") {
        speckles(p, s, d, base.lighter(115), 5, 0.05);
        grassTufts(p, s, d, base, 4);
    } else if (id == "dirt" || id == "road") {
        speckles(p, s, d, base.darker(120), id == "road" ? 3 : 7, 0.04);
        speckles(p, s, d, base.lighter(115), 4, 0.03);
    } else if (id == "sand") {
        p.setPen(QPen(base.darker(112), std::max(1.0, s / 20.0)));
        for (int i = 0; i < 3; ++i) {
            const double y = s * (0.25 + 0.25 * i) + d.next() * s * 0.08;
            QPainterPath dune;
            dune.moveTo(0, y);
            dune.quadTo(s * 0.5, y - s * 0.12, s, y);
            p.drawPath(dune);
        }
    } else if (id == "forest") {  // the forest floor; the trees are terrainFeature
        speckles(p, s, d, base.darker(125), 6, 0.07);
        speckles(p, s, d, base.lighter(118), 5, 0.04);
        grassTufts(p, s, d, base.lighter(115), 3);
    } else if (id == "hills") {
        speckles(p, s, d, base.darker(112), 5, 0.06);
        grassTufts(p, s, d, base, 4);
    } else if (id == "mountains") {  // scree; the peaks are terrainFeature
        speckles(p, s, d, base.darker(125), 7, 0.05);
        speckles(p, s, d, base.lighter(125), 6, 0.035);
    } else if (id == "swamp") {
        speckles(p, s, d, QColor("#3c5a6e"), 3, 0.09);
        grassTufts(p, s, d, base.lighter(130), 5);
    } else if (id == "snow") {
        speckles(p, s, d, QColor("#d0dbe6"), 6, 0.05);
    } else if (id == "sea" || id == "river") {
        p.setPen(QPen(base.lighter(135), std::max(1.0, s / 22.0)));
        for (int i = 0; i < 2; ++i) {
            const double x = d.next() * s * 0.6, y = s * (0.3 + 0.4 * i) + d.next() * s * 0.1;
            QPainterPath wave;
            wave.moveTo(x, y);
            wave.quadTo(x + s * 0.1, y - s * 0.08, x + s * 0.2, y);
            wave.quadTo(x + s * 0.3, y + s * 0.08, x + s * 0.4, y);
            p.drawPath(wave);
        }
    } else if (id == "lava") {
        speckles(p, s, d, QColor("#ffb000"), 4, 0.07);
        speckles(p, s, d, base.darker(150), 4, 0.06);
    } else {
        speckles(p, s, d, base.darker(115), 5, 0.04);
    }
    return pm;
}

void shadow(QPainter& p, const QRectF& r) {
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 70));
    p.drawEllipse(QRectF(r.left() + r.width() * 0.1, r.bottom() - r.height() * 0.18, r.width() * 0.8, r.height() * 0.2));
}

// A small symbol for a resource: coin, logs, stone blocks, gem, or a coloured orb.
void drawResourceIcon(QPainter& p, const QRectF& r, const QString& id, const QColor& c) {
    p.save();
    const double w = r.width(), h = r.height();
    p.setPen(QPen(QColor(30, 20, 8), std::max(1.0, w / 14)));
    if (id == "gold") {
        p.setBrush(QColor("#ffd700"));
        p.drawEllipse(r.adjusted(w * 0.1, h * 0.1, -w * 0.1, -h * 0.1));
        p.setBrush(QColor("#e8b400"));
        p.drawEllipse(r.adjusted(w * 0.28, h * 0.28, -w * 0.28, -h * 0.28));
    } else if (id == "wood") {
        p.setBrush(QColor("#8b5a2b"));
        for (int i = 0; i < 3; ++i) {
            const QRectF log(r.left() + w * (i == 2 ? 0.25 : 0.05 + 0.4 * i), r.top() + h * (i == 2 ? 0.15 : 0.5), w * 0.5, h * 0.32);
            p.drawRoundedRect(log, h * 0.15, h * 0.15);
            p.setBrush(QColor("#d9a066"));
            p.drawEllipse(QRectF(log.right() - h * 0.3, log.top(), h * 0.3, log.height()));
            p.setBrush(QColor("#8b5a2b"));
        }
    } else if (id == "stone") {
        p.setBrush(QColor("#b0aca4"));
        p.drawRect(QRectF(r.left() + w * 0.05, r.top() + h * 0.5, w * 0.45, h * 0.38));
        p.drawRect(QRectF(r.left() + w * 0.5, r.top() + h * 0.5, w * 0.45, h * 0.38));
        p.setBrush(QColor("#c8c4bc"));
        p.drawRect(QRectF(r.left() + w * 0.27, r.top() + h * 0.12, w * 0.45, h * 0.38));
    } else if (id == "gems") {
        p.setBrush(c);
        p.drawPolygon(QPolygonF{QPointF(r.left() + w * 0.2, r.top() + h * 0.3), QPointF(r.left() + w * 0.8, r.top() + h * 0.3),
                                QPointF(r.right() - w * 0.05, r.top() + h * 0.45), QPointF(r.center().x(), r.bottom() - h * 0.05),
                                QPointF(r.left() + w * 0.05, r.top() + h * 0.45)});
    } else {
        QRadialGradient g(r.center() - QPointF(w * 0.12, h * 0.12), w * 0.5);
        g.setColorAt(0, c.lighter(160));
        g.setColorAt(1, c.darker(130));
        p.setBrush(g);
        p.drawEllipse(r.adjusted(w * 0.12, h * 0.12, -w * 0.12, -h * 0.12));
    }
    p.restore();
}

}  // namespace

int variantFor(int x, int y) {
    quint32 h = quint32(x) * 73856093u ^ quint32(y) * 19349663u;
    return int((h >> 7) % 4);
}

const QPixmap& terrainTile(const QString& id, const QColor& base, int size, int variant) {
    // Drawn once big, then scaled down: the same look at every zoom step.
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(id, base.name()).arg(size).arg(variant);
    auto it = cache.find(key);
    if (it == cache.end()) {
        constexpr int kMaster = 128;
        QPixmap pm = makeTile(id, base, kMaster, variant);
        if (size != kMaster) pm = pm.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        it = cache.insert(key, pm);
    }
    return it.value();
}

int decorationAt(int x, int y) {
    const quint32 h = (quint32(x) * 2654435761u) ^ (quint32(y) * 40503u + 0x9e37u);
    return (h >> 9) % 6 == 0 ? int((h >> 13) % 4) : -1;
}

const QPixmap& decoration(const QString& id, int variant, int size) {
    static const QPixmap none;
    const auto draw = [&](QPainter& p, const QRectF& r) {
        const double w = r.width(), h = r.height();
        const auto X = [&](double f) { return r.left() + w * f; };
        const auto Y = [&](double f) { return r.top() + h * f; };
        const QPen ink(QColor(30, 22, 12), std::max(1.0, w / 60));
        Dice d(qHash(id) + variant * 31);
        const double ox = 0.15 + d.next() * 0.5, oy = 0.35 + d.next() * 0.4;  // where in the tile
        const auto stone = [&](double x, double y, double s, const QColor& c) {
            p.setPen(ink);
            p.setBrush(c);
            p.drawEllipse(QRectF(X(x), Y(y), w * s, h * s * 0.7));
            p.setPen(Qt::NoPen);
            p.setBrush(c.lighter(130));
            p.drawEllipse(QRectF(X(x + s * 0.2), Y(y + s * 0.1), w * s * 0.35, h * s * 0.2));
        };
        if (id == "grass" || id == "road") {
            if (variant % 2 == 0) {  // flowers
                for (int i = 0; i < 4; ++i) {
                    const double x = ox + d.next() * 0.3, y = oy + d.next() * 0.2;
                    p.setPen(QPen(QColor("#3e6a2a"), std::max(1.0, w / 50)));
                    p.drawLine(QPointF(X(x), Y(y)), QPointF(X(x), Y(y + 0.1)));
                    p.setPen(Qt::NoPen);
                    p.setBrush(i % 3 == 0 ? QColor("#ffe060") : i % 3 == 1 ? QColor("#ff8ab0") : QColor("#f4f4ff"));
                    p.drawEllipse(QPointF(X(x), Y(y)), w * 0.035, w * 0.035);
                }
            } else if (variant == 1) {  // a bush
                p.setPen(ink);
                p.setBrush(QColor("#4a8a3a"));
                p.drawEllipse(QRectF(X(ox), Y(oy), w * 0.3, h * 0.22));
                p.setBrush(QColor("#5aa04a"));
                p.drawEllipse(QRectF(X(ox + 0.06), Y(oy - 0.06), w * 0.18, h * 0.16));
            } else {
                stone(ox, oy, 0.18, QColor("#9a968c"));
            }
        } else if (id == "dirt" || id == "hills") {
            stone(ox, oy, 0.2, QColor("#a8987a"));
            if (variant > 1) stone(ox + 0.18, oy + 0.08, 0.12, QColor("#8a7a60"));
        } else if (id == "sand") {
            if (variant % 2) {  // a cactus
                p.setPen(ink);
                p.setBrush(QColor("#5a9a4a"));
                p.drawRoundedRect(QRectF(X(ox + 0.1), Y(oy - 0.2), w * 0.08, h * 0.32), w * 0.04, w * 0.04);
                p.drawRoundedRect(QRectF(X(ox), Y(oy - 0.08), w * 0.06, h * 0.14), w * 0.03, w * 0.03);
                p.drawRoundedRect(QRectF(X(ox + 0.2), Y(oy - 0.14), w * 0.06, h * 0.12), w * 0.03, w * 0.03);
            } else {
                stone(ox, oy, 0.16, QColor("#d8c08a"));
            }
        } else if (id == "snow") {
            stone(ox, oy, 0.2, QColor("#c8ccd8"));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#ffffff"));
            p.drawEllipse(QRectF(X(ox + 0.03), Y(oy - 0.01), w * 0.14, h * 0.06));
        } else if (id == "swamp") {
            p.setPen(QPen(QColor("#6a8a3a"), std::max(1.0, w / 40)));
            for (int i = 0; i < 4; ++i) {
                const double x = ox + i * 0.06;
                p.drawLine(QPointF(X(x), Y(oy + 0.12)), QPointF(X(x - 0.03 + d.next() * 0.06), Y(oy - 0.1)));
            }
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#6a4a28"));
            p.drawRoundedRect(QRectF(X(ox + 0.05), Y(oy - 0.14), w * 0.04, h * 0.08), w * 0.02, w * 0.02);  // a cattail
        } else if (id == "forest") {
            p.setPen(ink);  // mushrooms
            for (int i = 0; i < 2; ++i) {
                const double x = ox + i * 0.12, y = oy + i * 0.06;
                p.setBrush(QColor("#f0e8d8"));
                p.drawRect(QRectF(X(x + 0.025), Y(y), w * 0.03, h * 0.07));
                p.setBrush(i ? QColor("#c03a2a") : QColor("#a87a4a"));
                p.drawChord(QRectF(X(x), Y(y - 0.05), w * 0.08, h * 0.1), 0, 180 * 16);
            }
        } else if (id == "sea") {
            if (variant == 3) {  // a rock in the waves
                stone(ox, oy, 0.18, QColor("#6a6a70"));
                p.setPen(QPen(QColor(255, 255, 255, 150), std::max(1.0, w / 50)));
                p.setBrush(Qt::NoBrush);
                p.drawArc(QRectF(X(ox - 0.04), Y(oy + 0.04), w * 0.26, h * 0.12), 200 * 16, 140 * 16);
            }
        }
    };
    static const QStringList kinds = {"grass", "road", "dirt", "hills", "sand", "snow", "swamp", "forest", "sea"};
    if (!kinds.contains(id) || (id == "sea" && variant != 3)) return none;
    return sprite(QStringLiteral("decor|%1|%2").arg(id).arg(variant), size, draw);
}

const QPixmap& fogTexture() {
    static QPixmap tex;
    if (!tex.isNull()) return tex;
    constexpr int kSize = 256;
    tex = QPixmap(kSize, kSize);
    tex.fill(QColor(10, 9, 16));
    QPainter p(&tex);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    Dice d(4242);
    for (int i = 0; i < 70; ++i) {
        const double x = d.next() * kSize, y = d.next() * kSize, rad = 14 + d.next() * 46;
        const int shade = 18 + int(d.next() * 22);
        QRadialGradient g(0, 0, rad);
        g.setColorAt(0, QColor(shade, shade, shade + 14, 70));
        g.setColorAt(1, QColor(shade, shade, shade + 14, 0));
        p.setBrush(g);
        // Drawn at every wrap-around position, so the texture tiles without seams.
        for (const int ox : {-kSize, 0, kSize})
            for (const int oy : {-kSize, 0, kSize}) {
                p.save();
                p.translate(x + ox, y + oy);
                p.drawEllipse(QPointF(0, 0), rad, rad * 0.7);
                p.restore();
            }
    }
    return tex;
}

namespace {
// The outline every feature gets.
QPen featureInk(double s) { return QPen(QColor(18, 22, 12, 220), std::max(1.0, s / 64.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin); }

void drawConifer(QPainter& p, double x, double y, double h, const QColor& c, double s) {
    // Three tiers on a trunk, lit from the left.
    const double w = h * 0.36;
    p.setPen(featureInk(s));
    p.setBrush(QColor("#5b3a1e"));
    p.drawRect(QRectF(x - h * 0.04, y - h * 0.14, h * 0.08, h * 0.14));
    for (int tier = 0; tier < 3; ++tier) {
        const double top = y - h * (1.0 - tier * 0.22), bottom = y - h * (0.42 - tier * 0.15), half = w * (0.55 + tier * 0.22);
        QPainterPath t;
        t.moveTo(x, top);
        t.lineTo(x + half, bottom);
        t.quadTo(x, bottom + h * 0.06, x - half, bottom);
        t.closeSubpath();
        p.setPen(featureInk(s));
        p.setBrush(c.darker(100 + tier * 6));
        p.drawPath(t);
        QPainterPath lit;  // the sunny half
        lit.moveTo(x, top);
        lit.lineTo(x - half, bottom);
        lit.quadTo(x - half * 0.4, bottom + h * 0.04, x - half * 0.05, bottom + h * 0.02);
        lit.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(c.lighter(128));
        p.drawPath(lit);
    }
}

void drawLeafTree(QPainter& p, double x, double y, double h, const QColor& c, double s) {
    p.setPen(featureInk(s));
    p.setBrush(QColor("#6a4424"));
    p.drawRect(QRectF(x - h * 0.05, y - h * 0.4, h * 0.1, h * 0.4));
    const double r = h * 0.3;
    p.setBrush(c);
    p.drawEllipse(QPointF(x, y - h * 0.62), r * 1.1, r);
    p.setPen(Qt::NoPen);
    p.setBrush(c.lighter(130));
    p.drawEllipse(QPointF(x - r * 0.35, y - h * 0.72), r * 0.5, r * 0.4);
}
}  // namespace

const QPixmap& terrainFeature(const QString& id, const QColor& base, int size, int variant) {
    static const QPixmap none;
    if (id != "forest" && id != "hills" && id != "mountains") return none;
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(id, base.name()).arg(size).arg(variant);
    auto it = cache.find(key);
    if (it != cache.end()) return it.value();
    // A canvas of 2x2 tiles; the feature's own tile is the bottom middle (s/2..3s/2, s..2s).
    constexpr int s = 128;
    QPixmap pm(2 * s, 2 * s);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        Dice d(qHash(id) * 31 + variant * 104729 + 7);
        const double left = s * 0.5, top = s;
        if (id == "forest") {
            struct Tree { double x, y, h; bool leaf; };
            QList<Tree> trees;
            const int n = 4 + variant % 3;
            for (int i = 0; i < n; ++i)
                trees.append({left + s * (0.12 + 0.76 * d.next()), top + s * (0.3 + 0.62 * d.next()), s * (0.55 + 0.35 * d.next()),
                              variant == 3 && d.next() < 0.5});
            std::sort(trees.begin(), trees.end(), [](const Tree& a, const Tree& b) { return a.y < b.y; });
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 55));
            for (const Tree& t : trees) p.drawEllipse(QPointF(t.x + t.h * 0.08, t.y), t.h * 0.22, t.h * 0.07);
            for (const Tree& t : trees) {
                const QColor c = base.lighter(105 + int(d.next() * 30));
                if (t.leaf) drawLeafTree(p, t.x, t.y, t.h * 0.9, QColor("#4f8a32").lighter(95 + int(d.next() * 20)), s);
                else drawConifer(p, t.x, t.y, t.h, c, s);
            }
        } else if (id == "hills") {
            const int n = 2 + variant % 2;
            for (int i = 0; i < n; ++i) {
                const double w = s * (0.55 + 0.3 * d.next()), h = s * (0.32 + 0.2 * d.next());
                const double cx = left + s * (0.25 + 0.5 * (n == 1 ? 0.5 : double(i) / (n - 1))) + (d.next() - 0.5) * s * 0.1;
                const double by = top + s * (0.62 + 0.3 * (i % 2 ? d.next() : 1 - d.next()));
                QPainterPath mound;
                mound.moveTo(cx - w / 2, by);
                mound.cubicTo(cx - w * 0.3, by - h * 1.1, cx + w * 0.25, by - h * 1.15, cx + w / 2, by);
                mound.closeSubpath();
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0, 0, 0, 50));
                p.drawEllipse(QPointF(cx + w * 0.06, by), w * 0.5, s * 0.05);
                QLinearGradient g(cx - w / 2, by - h, cx + w / 2, by);
                g.setColorAt(0, base.lighter(135));
                g.setColorAt(0.55, base);
                g.setColorAt(1, base.darker(135));
                p.setPen(featureInk(s));
                p.setBrush(g);
                p.drawPath(mound);
                p.setPen(QPen(base.darker(140), s / 64.0));  // a few tufts on it
                for (int k = 0; k < 3; ++k) {
                    const double tx = cx - w * 0.25 + d.next() * w * 0.4, ty = by - h * (0.25 + 0.4 * d.next());
                    p.drawLine(QPointF(tx, ty), QPointF(tx - s * 0.02, ty - s * 0.05));
                    p.drawLine(QPointF(tx, ty), QPointF(tx + s * 0.02, ty - s * 0.05));
                }
            }
        } else {  // mountains: one big peak and a smaller one, rocky faces and snow
            // Behind: a smaller peak to one side; in front: the main one and a low spur.
            for (int i = 2; i >= 0; --i) {
                const bool main = i == 0;
                const double side = (variant % 2 ? -1 : 1);
                const double w = s * (main ? 1.2 : i == 1 ? 0.8 : 0.6), h = s * (main ? 1.1 + 0.2 * d.next() : i == 1 ? 0.85 : 0.45);
                const double cx = left + s * 0.5 + (main ? (d.next() - 0.5) * s * 0.16 : side * s * (i == 1 ? 0.36 : -0.38));
                const double by = top + s * (main ? 0.96 : i == 1 ? 0.7 : 1.0);
                const double apex = cx + (d.next() - 0.5) * w * 0.2;
                QPolygonF body{QPointF(cx - w / 2, by), QPointF(cx - w * 0.3, by - h * 0.45), QPointF(apex - w * 0.08, by - h * 0.9),
                               QPointF(apex, by - h), QPointF(apex + w * 0.1, by - h * 0.86), QPointF(cx + w * 0.32, by - h * 0.5),
                               QPointF(cx + w / 2, by)};
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0, 0, 0, 60));
                p.drawEllipse(QPointF(cx + w * 0.08, by), w * 0.52, s * 0.06);
                p.setPen(featureInk(s));
                p.setBrush(base.darker(130));
                p.drawPolygon(body);
                // The lit west face: from the apex down to the left foot and back along a ridge.
                QPolygonF lit{QPointF(apex, by - h), QPointF(apex - w * 0.08, by - h * 0.9), QPointF(cx - w * 0.3, by - h * 0.45),
                              QPointF(cx - w / 2, by), QPointF(cx - w * 0.02, by), QPointF(apex - w * 0.04, by - h * 0.5)};
                p.setPen(Qt::NoPen);
                QLinearGradient g(cx - w / 2, by - h, cx, by);
                g.setColorAt(0, base.lighter(150));
                g.setColorAt(1, base.lighter(108));
                p.setBrush(g);
                p.drawPolygon(lit);
                p.setPen(QPen(base.darker(165), s / 70.0, Qt::SolidLine, Qt::RoundCap));  // cracks
                for (int k = 0; k < 3; ++k) {
                    const double t = 0.35 + 0.18 * k;
                    const QPointF a(apex + w * (0.05 + 0.1 * k), by - h * (1 - t));
                    p.drawLine(a, a + QPointF(w * 0.06, h * 0.16));
                }
                // The snow cap with a ragged lower edge.
                const double snow = h * (main ? 0.3 : i == 1 ? 0.22 : 0.0);
                if (snow <= 0) continue;
                QPolygonF cap{QPointF(apex, by - h), QPointF(apex + w * 0.1, by - h * 0.86)};
                const double capL = apex - w * 0.08 - (w * 0.22) * (snow / h - 0.1), capR = apex + w * 0.1 + w * 0.12 * (snow / h);
                for (int k = 0; k <= 4; ++k) {
                    const double x = capR + (capL - capR) * k / 4.0;
                    cap << QPointF(x, by - h + snow * (k % 2 ? 0.75 : 1.0));
                }
                cap << QPointF(apex - w * 0.08, by - h * 0.9);
                p.setPen(featureInk(s));
                p.setBrush(QColor("#f2f5fa"));
                p.drawPolygon(cap);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor("#c8d4e4"));
                p.drawPolygon(QPolygonF{QPointF(apex, by - h), QPointF(apex + w * 0.1, by - h * 0.86), QPointF(capR, by - h + snow * 0.9),
                                        QPointF(apex + w * 0.02, by - h + snow * 0.6)});
            }
        }
    }
    it = cache.insert(key, pm.scaled(2 * size, 2 * size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    return it.value();
}

int terrainLayer(const QString& id) {
    static const QHash<QString, int> layers = {{"sea", 0}, {"river", 1}, {"lava", 2}, {"swamp", 3}, {"sand", 4},
                                               {"dirt", 5}, {"road", 6}, {"grass", 7}, {"snow", 8}, {"forest", 9},
                                               {"hills", 10}, {"mountains", 11}};
    return layers.value(id, 5);
}

const QPixmap& terrainEdge(const QColor& colour, int side, int size, bool foam) {
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(colour.name()).arg(side).arg(size).arg(foam);
    auto it = cache.find(key);
    if (it != cache.end()) return it.value();
    // Drawn for the left side, then turned.
    constexpr int kMaster = 128;
    QPixmap pm(kMaster, kMaster);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        QPainterPath band;
        band.moveTo(0, 0);
        const double depth = foam ? kMaster * 0.16 : kMaster * 0.32;
        for (int i = 0; i <= 8; ++i) {
            const double y = kMaster * i / 8.0;
            const double wobble = std::sin(i * 1.7 + colour.hue() * 0.1) * depth * 0.25;
            band.lineTo(depth + wobble, y);
        }
        band.lineTo(0, kMaster);
        band.closeSubpath();
        QLinearGradient g(0, 0, depth * 1.25, 0);
        if (foam) {
            g.setColorAt(0, QColor(250, 250, 240, 170));
            g.setColorAt(0.5, QColor(220, 235, 245, 90));
            g.setColorAt(1, QColor(220, 235, 245, 0));
        } else {
            QColor c = colour;
            c.setAlpha(210);
            g.setColorAt(0, c);
            c.setAlpha(120);
            g.setColorAt(0.55, c);
            c.setAlpha(0);
            g.setColorAt(1, c);
        }
        p.setBrush(g);
        p.drawPath(band);
    }
    QTransform turn;
    turn.rotate(90 * side);
    pm = pm.transformed(turn).scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    it = cache.insert(key, pm);
    return it.value();
}

int nextZoom(int tile, int direction) {
    const int n = int(std::size(kZoomSteps));
    int index = 0;
    for (int i = 0; i < n; ++i)
        if (kZoomSteps[i] <= tile) index = i;
    return kZoomSteps[std::clamp(index + direction, 0, n - 1)];
}

const QPixmap& sprite(const QString& key, int size, const std::function<void(QPainter&, const QRectF&)>& draw) {
    static QHash<QString, QPixmap> cache;
    const QString full = key + QLatin1Char('|') + QString::number(size);
    auto it = cache.find(full);
    if (it != cache.end()) return it.value();
    constexpr int kMaster = 256;
    QPixmap art(kMaster, kMaster);
    art.fill(Qt::transparent);
    {
        QPainter p(&art);
        p.setRenderHint(QPainter::Antialiasing, true);
        draw(p, QRectF(kMaster * 0.06, kMaster * 0.06, kMaster * 0.88, kMaster * 0.88));
    }
    // A dark outline around the shape so it stands out from the terrain.
    QPixmap mask = art;
    {
        QPainter p(&mask);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(mask.rect(), QColor(20, 12, 4, 230));
    }
    QPixmap out(kMaster, kMaster);
    out.fill(Qt::transparent);
    {
        QPainter p(&out);
        const int w = kMaster / 48;
        for (int dy = -w; dy <= w; dy += w)
            for (int dx = -w; dx <= w; dx += w)
                if (dx || dy) p.drawPixmap(dx, dy, mask);
        p.drawPixmap(0, 0, art);
    }
    it = cache.insert(full, out.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    return it.value();
}

const QPixmap& townSprite(const QColor& owner, int size) {
    return sprite(QStringLiteral("town|") + owner.name(), size, [&](QPainter& p, const QRectF& r) { drawTown(p, r, owner); });
}
namespace {
void ownerFlag(QPainter& p, const QRectF& r, const QColor& owner) {
    const double w = r.width(), h = r.height();
    p.setPen(QPen(QColor("#2b2b2b"), std::max(1.5, w / 12)));
    p.drawLine(QPointF(r.left() + w * 0.12, r.top() + h * 0.6), QPointF(r.left() + w * 0.12, r.top() + h * 0.04));
    p.setPen(QPen(QColor("#2b2b2b"), std::max(1.0, w / 30)));
    p.setBrush(owner);
    p.drawRect(QRectF(r.left() + w * 0.12, r.top() + h * 0.04, w * 0.24, h * 0.16));
}

// A sawmill: a log house with a big saw wheel and a pile of logs.
void drawSawmill(QPainter& p, const QRectF& r, const QColor& owner) {
    const double w = r.width(), h = r.height();
    const auto X = [&](double f) { return r.left() + w * f; };
    const auto Y = [&](double f) { return r.top() + h * f; };
    p.setPen(QPen(QColor(30, 20, 8), std::max(1.0, w / 30)));
    p.setBrush(QColor("#8a5a2b"));
    p.drawRect(QRectF(X(0.2), Y(0.42), w * 0.5, h * 0.48));
    p.setBrush(QColor("#6a4020"));
    p.drawPolygon(QPolygonF{QPointF(X(0.14), Y(0.44)), QPointF(X(0.45), Y(0.18)), QPointF(X(0.76), Y(0.44))});
    p.setPen(QPen(QColor("#5a3a18"), std::max(1.0, w / 60)));
    for (double y = 0.52; y < 0.9; y += 0.09) p.drawLine(QPointF(X(0.2), Y(y)), QPointF(X(0.7), Y(y)));
    p.setPen(QPen(QColor(30, 20, 8), std::max(1.0, w / 30)));
    p.setBrush(QColor("#d8dce8"));  // the saw wheel with teeth
    QPolygonF saw;
    for (int i = 0; i < 24; ++i) {
        const double a = M_PI * 2 * i / 24, rad = (i % 2 ? 0.16 : 0.2) * w;
        saw << QPointF(X(0.78) + std::cos(a) * rad, Y(0.62) + std::sin(a) * rad);
    }
    p.drawPolygon(saw);
    p.setBrush(QColor("#808898"));
    p.drawEllipse(QPointF(X(0.78), Y(0.62)), w * 0.04, w * 0.04);
    p.setBrush(QColor("#a87a4a"));  // logs
    for (int i = 0; i < 3; ++i) p.drawEllipse(QRectF(X(0.0 + 0.1 * i), Y(0.8 - 0.06 * (i % 2)), w * 0.13, h * 0.13));
    ownerFlag(p, r, owner);
}

// A quarry: a cut rock face with blocks and a pickaxe.
void drawQuarry(QPainter& p, const QRectF& r, const QColor& owner) {
    const double w = r.width(), h = r.height();
    const auto X = [&](double f) { return r.left() + w * f; };
    const auto Y = [&](double f) { return r.top() + h * f; };
    p.setPen(QPen(QColor(30, 30, 30), std::max(1.0, w / 30)));
    p.setBrush(QColor("#9a968c"));
    p.drawPolygon(QPolygonF{QPointF(X(0.05), Y(0.9)), QPointF(X(0.2), Y(0.3)), QPointF(X(0.55), Y(0.15)), QPointF(X(0.9), Y(0.35)),
                            QPointF(X(0.98), Y(0.9))});
    p.setBrush(QColor("#c8c4bc"));  // the cut face
    p.drawPolygon(QPolygonF{QPointF(X(0.3), Y(0.9)), QPointF(X(0.35), Y(0.42)), QPointF(X(0.7), Y(0.42)), QPointF(X(0.75), Y(0.9))});
    p.setPen(QPen(QColor(90, 88, 80), std::max(1.0, w / 60)));
    for (double y = 0.55; y < 0.9; y += 0.12) p.drawLine(QPointF(X(0.33), Y(y)), QPointF(X(0.73), Y(y)));
    p.setPen(QPen(QColor(30, 30, 30), std::max(1.0, w / 30)));
    p.setBrush(QColor("#b0aca4"));
    p.drawRect(QRectF(X(0.76), Y(0.74), w * 0.18, h * 0.14));
    p.drawRect(QRectF(X(0.8), Y(0.6), w * 0.14, h * 0.14));
    p.setPen(QPen(QColor("#6a4a28"), std::max(1.5, w / 20)));  // pickaxe
    p.drawLine(QPointF(X(0.12), Y(0.86)), QPointF(X(0.28), Y(0.6)));
    p.setPen(QPen(QColor("#808898"), std::max(1.5, w / 22)));
    p.drawLine(QPointF(X(0.18), Y(0.56)), QPointF(X(0.36), Y(0.66)));
    ownerFlag(p, r, owner);
}

// A crystal cave: a dark cave mouth with glowing crystals.
void drawCrystalCave(QPainter& p, const QRectF& r, const QColor& crystal, const QColor& owner) {
    const double w = r.width(), h = r.height();
    const auto X = [&](double f) { return r.left() + w * f; };
    const auto Y = [&](double f) { return r.top() + h * f; };
    p.setPen(QPen(QColor(30, 20, 30), std::max(1.0, w / 30)));
    p.setBrush(QColor("#6a5a70"));
    p.drawChord(QRectF(X(0.05), Y(0.25), w * 0.9, h * 1.2), 0, 180 * 16);
    p.setBrush(QColor("#140c18"));
    p.drawChord(QRectF(X(0.3), Y(0.5), w * 0.4, h * 0.8), 0, 180 * 16);
    QRadialGradient glow(QPointF(X(0.5), Y(0.75)), w * 0.3);
    glow.setColorAt(0, QColor(crystal.red(), crystal.green(), crystal.blue(), 150));
    glow.setColorAt(1, QColor(crystal.red(), crystal.green(), crystal.blue(), 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(QPointF(X(0.5), Y(0.75)), w * 0.3, h * 0.2);
    p.setPen(QPen(QColor(40, 10, 40), std::max(1.0, w / 40)));
    for (const auto& [x, s] : {std::pair{0.62, 1.0}, std::pair{0.78, 0.8}, std::pair{0.88, 0.6}}) {
        p.setBrush(crystal.lighter(115));
        p.drawPolygon(QPolygonF{QPointF(X(x - 0.05 * s), Y(0.9)), QPointF(X(x), Y(0.9 - 0.4 * s)), QPointF(X(x + 0.05 * s), Y(0.9))});
    }
    ownerFlag(p, r, owner);
}
}  // namespace

const QPixmap& mineSprite(const QString& resource, const QColor& resourceColor, const QColor& owner, int size) {
    return sprite(QStringLiteral("mine|%1|%2").arg(resource, owner.name()), size, [&](QPainter& p, const QRectF& r) {
        if (resource == "wood") return drawSawmill(p, r, owner);
        if (resource == "stone") return drawQuarry(p, r, owner);
        if (resource == "gems") return drawCrystalCave(p, r, resourceColor, owner);
        drawMine(p, r, resourceColor, owner);
        drawResourceIcon(p, QRectF(r.left() + r.width() * 0.58, r.top() - r.height() * 0.02, r.width() * 0.42, r.height() * 0.42),
                         resource, resourceColor);
    });
}
const QPixmap& dwellingSprite(const QColor& owner, int size) {
    return sprite(QStringLiteral("dwelling|") + owner.name(), size, [&](QPainter& p, const QRectF& r) { drawDwelling(p, r, owner); });
}
const QPixmap& dwellingSprite(const QColor& owner, const QString& unitId, int size) {
    return sprite(QStringLiteral("dwelling|%1|%2").arg(owner.name(), unitId), size, [&](QPainter& p, const QRectF& r) {
        drawDwelling(p, r.adjusted(0, r.height() * 0.08, -r.width() * 0.2, 0), owner);
        // A round sign with the unit that lives there.
        const QRectF sign(r.left() + r.width() * 0.52, r.top(), r.width() * 0.48, r.height() * 0.48);
        p.setPen(QPen(QColor(30, 20, 8), std::max(1.0, r.width() / 40)));
        p.setBrush(QColor(240, 228, 192));
        p.drawEllipse(sign);
        drawUnitIcon(p, sign.adjusted(sign.width() * 0.08, sign.height() * 0.06, -sign.width() * 0.08, -sign.height() * 0.06), unitId, 0);
    });
}
const QPixmap& heroSprite(const QColor& owner, bool inBoat, int size) {
    return sprite(QStringLiteral("hero|%1|%2").arg(owner.name()).arg(inBoat), size,
                  [&](QPainter& p, const QRectF& r) { drawHero(p, r, owner, false, inBoat); });
}
const QPixmap& monsterSprite(int size) {
    return sprite(QStringLiteral("monster"), size, [&](QPainter& p, const QRectF& r) { drawMonster(p, r); });
}
const QPixmap& chestSprite(const QColor& resource, int size) {
    return sprite(QStringLiteral("chest|") + resource.name(), size, [&](QPainter& p, const QRectF& r) { drawChest(p, r, resource); });
}
const QPixmap& artifactSprite(int size) {
    return sprite(QStringLiteral("artifact"), size, [&](QPainter& p, const QRectF& r) { drawArtifact(p, r); });
}

QColor resourceColor(const QString& id) {
    static const QMap<QString, QColor> colors = {
        {"gold", "#ffd700"}, {"wood", "#8b5a2b"}, {"stone", "#aaaaaa"}, {"grain", "#e6c35c"},
        {"gems", "#ff4fd8"}, {"sulphur", "#e8e800"}, {"coal", "#333333"}, {"mercury", "#a0a0ff"},
        {"papyrus", "#d2b48c"}};
    return colors.value(id, QColor::fromHsv(static_cast<int>(qHash(id) % 360), 200, 220));
}

const QPixmap& resourceIcon(const QString& id, int size) {
    return sprite(QStringLiteral("res|") + id, size, [&](QPainter& p, const QRectF& r) { drawResourceIcon(p, r, id, resourceColor(id)); });
}

QIcon buttonIcon(const QString& name) {
    static QHash<QString, QIcon> cache;
    if (cache.contains(name)) return cache.value(name);
    constexpr int s = 48;
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPen ink(QColor(25, 16, 8), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(ink);
    const QColor gold("#ffd84a"), wood("#a0703a"), steel("#d8dce8");
    if (name == "build") {  // a hammer
        p.setBrush(wood);
        p.drawPolygon(QPolygonF{QPointF(10, 40), QPointF(14, 44), QPointF(32, 24), QPointF(28, 20)});
        p.setBrush(steel);
        p.drawPolygon(QPolygonF{QPointF(22, 10), QPointF(34, 6), QPointF(42, 18), QPointF(32, 26), QPointF(26, 20)});
    } else if (name == "buy") {  // a stack of coins
        p.setBrush(gold);
        for (int i = 0; i < 3; ++i) p.drawEllipse(QRectF(8 + i * 3, 30 - i * 9, 30, 12));
    } else if (name == "wagon") {
        p.setBrush(wood);
        p.drawRect(QRectF(6, 16, 34, 16));
        p.setBrush(QColor("#5a3a18"));
        p.drawEllipse(QPointF(14, 36), 6, 6);
        p.drawEllipse(QPointF(34, 36), 6, 6);
    } else if (name == "end" || name == "wait") {  // an hourglass
        p.setBrush(wood);
        p.drawRect(QRectF(10, 4, 28, 5));
        p.drawRect(QRectF(10, 39, 28, 5));
        p.setBrush(QColor("#e8e0c8"));
        p.drawPolygon(QPolygonF{QPointF(13, 9), QPointF(35, 9), QPointF(24, 24), QPointF(35, 39), QPointF(13, 39), QPointF(24, 24)});
        p.setPen(Qt::NoPen);
        p.setBrush(gold);
        p.drawPolygon(QPolygonF{QPointF(17, 37), QPointF(31, 37), QPointF(24, 29)});
    } else if (name == "spell") {  // a wand with a star
        p.setBrush(wood);
        p.drawPolygon(QPolygonF{QPointF(8, 42), QPointF(11, 45), QPointF(30, 22), QPointF(27, 19)});
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            const double a = M_PI / 5 * i - M_PI / 2, rad = i % 2 ? 5 : 12;
            star << QPointF(34 + rad * std::cos(a), 14 + rad * std::sin(a));
        }
        p.setBrush(QColor("#9ad0ff"));
        p.drawPolygon(star);
    } else if (name == "market") {  // scales
        p.drawLine(QPointF(24, 6), QPointF(24, 40));
        p.drawLine(QPointF(8, 14), QPointF(40, 14));
        p.drawLine(QPointF(14, 42), QPointF(34, 42));
        p.setBrush(gold);
        p.drawChord(QRectF(2, 18, 16, 14), 180 * 16, 180 * 16);
        p.drawChord(QRectF(30, 18, 16, 14), 180 * 16, 180 * 16);
    } else if (name == "defend") {  // a shield
        p.setBrush(QColor("#3a64b0"));
        QPainterPath sh;
        sh.moveTo(24, 4);
        sh.lineTo(40, 10);
        sh.quadTo(40, 34, 24, 44);
        sh.quadTo(8, 34, 8, 10);
        sh.closeSubpath();
        p.drawPath(sh);
        p.setPen(QPen(gold, 3));
        p.drawLine(QPointF(24, 10), QPointF(24, 38));
        p.drawLine(QPointF(13, 20), QPointF(35, 20));
    } else if (name == "auto") {  // a gear
        p.setBrush(steel);
        QPolygonF gear;
        for (int i = 0; i < 16; ++i) {
            const double a = M_PI / 8 * i, rad = i % 2 ? 16 : 21;
            gear << QPointF(24 + rad * std::cos(a), 24 + rad * std::sin(a));
        }
        p.drawPolygon(gear);
        p.setBrush(QColor("#2b2118"));
        p.drawEllipse(QPointF(24, 24), 6, 6);
    } else if (name == "retreat") {  // a white flag
        p.drawLine(QPointF(12, 44), QPointF(12, 4));
        p.setBrush(QColor("#f4f0e8"));
        p.drawPolygon(QPolygonF{QPointF(12, 6), QPointF(40, 10), QPointF(34, 18), QPointF(40, 26), QPointF(12, 24)});
    } else if (name == "catapult") {
        drawUnitIcon(p, QRectF(2, 2, 44, 44), QStringLiteral("catapult"), 0);
    } else if (name == "town") {
        drawTown(p, QRectF(2, 6, 44, 40), QColor("#c03a3a"));
    } else if (name == "army") {
        drawUnitIcon(p, QRectF(4, 2, 40, 44), QStringLiteral("pikeman"), 0);
    } else if (name == "tavern") {  // a mug
        p.setBrush(QColor("#d8a020"));
        p.drawRoundedRect(QRectF(10, 14, 22, 28), 4, 4);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(26, 20, 14, 14), -90 * 16, 180 * 16);
        p.setBrush(QColor("#f8f4e8"));
        p.drawEllipse(QRectF(8, 8, 26, 10));
    }
    p.end();
    const QIcon icon(pm);
    cache.insert(name, icon);
    return icon;
}

QPixmap pointerImage(Pointer kind) {
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPen outline(QColor(20, 12, 4), 2.2);
    const auto foot = [&](double x, double y, double angle, const QColor& c) {
        p.save();
        p.translate(x, y);
        p.rotate(angle);
        p.setPen(outline);
        p.setBrush(c);
        p.drawEllipse(QRectF(-3.2, -6, 6.4, 9));    // sole
        p.drawEllipse(QRectF(-2.6, 3.6, 5.2, 4.4));  // heel
        p.restore();
    };
    switch (kind) {
    case Pointer::Move:
    case Pointer::MoveLater: {
        const QColor c = kind == Pointer::Move ? QColor("#ffffff") : QColor("#ff7060");
        foot(10, 21, -12, c);
        foot(21, 11, 12, c);
        break;
    }
    case Pointer::MoveAttack: {  // a sword with footsteps under it: walk there, then strike
        foot(8, 25, -12, QColor("#ffffff"));
        foot(15, 20, 12, QColor("#ffffff"));
        p.save();
        p.translate(21, 13);
        p.rotate(40);
        p.setPen(outline);
        p.setBrush(QColor("#e8e8f0"));
        p.drawRect(QRectF(-2, -12, 4, 17));
        p.setBrush(QColor("#c9a24a"));
        p.drawRect(QRectF(-5.5, 4, 11, 3));
        p.drawRect(QRectF(-1.5, 7, 3, 5));
        p.restore();
        break;
    }
    case Pointer::Shoot: {  // a bow with an arrow
        p.setPen(QPen(QColor(20, 12, 4), 4));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(4, 4, 20, 24), 40 * 16, 190 * 16);
        p.setPen(QPen(QColor("#a06a2a"), 2.2));
        p.drawArc(QRectF(4, 4, 20, 24), 40 * 16, 190 * 16);
        p.setPen(QPen(QColor(240, 230, 210), 1));
        p.drawLine(QPointF(20, 6), QPointF(9, 26));
        p.setPen(QPen(QColor(20, 12, 4), 2.4));
        p.drawLine(QPointF(8, 24), QPointF(28, 4));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#e8e8f0"));
        p.drawPolygon(QPolygonF{QPointF(30, 2), QPointF(23, 5), QPointF(27, 9)});
        break;
    }
    case Pointer::Attack: {  // two crossed swords
        for (const int side : {-1, 1}) {
            p.save();
            p.translate(16, 16);
            p.rotate(45 * side);
            p.setPen(outline);
            p.setBrush(QColor("#e8e8f0"));
            p.drawRect(QRectF(-2, -14, 4, 20));
            p.setBrush(QColor("#c9a24a"));
            p.drawRect(QRectF(-6, 5, 12, 3));
            p.drawRect(QRectF(-1.5, 8, 3, 6));
            p.restore();
        }
        break;
    }
    case Pointer::Enter: {  // a little gate
        p.setPen(outline);
        p.setBrush(QColor("#d8cdb4"));
        p.drawRect(QRectF(5, 8, 22, 20));
        p.setBrush(QColor("#4a3520"));
        p.drawChord(QRectF(11, 14, 10, 24), 0, 180 * 16);
        p.setBrush(QColor("#c03030"));
        p.drawPolygon(QPolygonF{QPointF(16, 1), QPointF(26, 8), QPointF(6, 8)});
        break;
    }
    case Pointer::Select: {  // a pointing gauntlet: an arrow with a gold ring
        p.setPen(outline);
        p.setBrush(QColor("#ffd84a"));
        p.drawPolygon(QPolygonF{QPointF(3, 2), QPointF(3, 25), QPointF(9, 19), QPointF(14, 29), QPointF(18, 27),
                                QPointF(13, 17), QPointF(21, 17)});
        break;
    }
    case Pointer::Blocked: {
        p.setPen(QPen(QColor(20, 12, 4), 6));
        p.drawEllipse(QRectF(6, 6, 20, 20));
        p.drawLine(QPointF(9, 23), QPointF(23, 9));
        p.setPen(QPen(QColor("#e04040"), 3));
        p.drawEllipse(QRectF(6, 6, 20, 20));
        p.drawLine(QPointF(9, 23), QPointF(23, 9));
        break;
    }
    case Pointer::Normal:
        p.setPen(outline);
        p.setBrush(QColor("#f6e7c1"));
        p.drawPolygon(QPolygonF{QPointF(3, 2), QPointF(3, 25), QPointF(9, 19), QPointF(14, 29), QPointF(18, 27),
                                QPointF(13, 17), QPointF(21, 17)});
        break;
    }
    return pm;
}

void drawTown(QPainter& p, const QRectF& r, const QColor& owner) {
    // A castle filling a 2x2 block; the gate is in the bottom left quarter (the town's tile).
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const double w = r.width(), h = r.height();
    const auto X = [&](double f) { return r.left() + w * f; };
    const auto Y = [&](double f) { return r.top() + h * f; };
    const QColor stone("#cfc6b2"), dark("#3a3328");
    const QPen pen(dark, std::max(1.0, w / 90));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 70));
    p.drawEllipse(QRectF(X(0.02), Y(0.78), w * 0.96, h * 0.2));
    p.setPen(pen);
    // Curtain wall with battlements.
    const QRectF wall(X(0.06), Y(0.52), w * 0.88, h * 0.4);
    p.setBrush(stone.darker(105));
    p.drawRect(wall);
    p.setBrush(stone);
    for (double x = wall.left(); x < wall.right() - w * 0.02; x += w * 0.08)
        p.drawRect(QRectF(x, wall.top() - h * 0.04, w * 0.045, h * 0.045));
    // The keep in the middle back.
    const QRectF keep(X(0.36), Y(0.2), w * 0.36, h * 0.5);
    p.setBrush(stone.lighter(106));
    p.drawRect(keep);
    for (double x = keep.left(); x < keep.right() - w * 0.02; x += w * 0.07)
        p.drawRect(QRectF(x, keep.top() - h * 0.04, w * 0.04, h * 0.04));
    p.setBrush(QColor("#2a2018"));
    for (const double fx : {0.44, 0.58}) p.drawRoundedRect(QRectF(X(fx), Y(0.3), w * 0.05, h * 0.09), w * 0.02, w * 0.02);
    // Banner on the keep in the owner's colour.
    p.drawLine(QPointF(X(0.54), keep.top() - h * 0.04), QPointF(X(0.54), Y(0.0)));
    p.setBrush(owner);
    p.drawPolygon(QPolygonF{QPointF(X(0.54), Y(0.0)), QPointF(X(0.74), Y(0.045)), QPointF(X(0.54), Y(0.1))});
    // Corner towers with roofs in the owner's colour.
    for (const double fx : {0.0, 0.8}) {
        const QRectF tower(X(fx), Y(0.38), w * 0.2, h * 0.56);
        p.setBrush(stone.lighter(110));
        p.drawRect(tower);
        p.setBrush(owner);
        p.drawPolygon(QPolygonF{QPointF(tower.left() - w * 0.02, tower.top()), QPointF(tower.center().x(), tower.top() - h * 0.2),
                                QPointF(tower.right() + w * 0.02, tower.top())});
        p.setBrush(QColor("#2a2018"));
        p.drawRect(QRectF(tower.center().x() - w * 0.015, Y(0.55), w * 0.03, h * 0.07));
    }
    // The gate (bottom left quarter) with a road out.
    const QRectF gate(X(0.12), Y(0.66), w * 0.2, h * 0.28);
    p.setBrush(QColor("#4a3520"));
    p.drawChord(QRectF(gate.left(), gate.top(), gate.width(), gate.height() * 1.6), 0, 180 * 16);
    p.setPen(QPen(QColor("#2a1d10"), std::max(1.0, w / 120)));
    for (double x = gate.left() + gate.width() * 0.2; x < gate.right(); x += gate.width() * 0.2)
        p.drawLine(QPointF(x, gate.top() + h * 0.03), QPointF(x, gate.bottom()));
    p.restore();
}

void drawMine(QPainter& p, const QRectF& r, const QColor& resource, const QColor& owner) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    shadow(p, r);
    const double w = r.width(), h = r.height();
    p.setPen(QPen(QColor("#2b2b2b"), std::max(1.0, w / 18)));
    p.setBrush(QColor("#7a6a55"));  // rock heap
    p.drawChord(QRectF(r.left() + w * 0.05, r.top() + h * 0.25, w * 0.9, h * 1.2), 0, 180 * 16);
    p.setBrush(QColor("#1d1610"));  // entrance
    p.drawChord(QRectF(r.left() + w * 0.33, r.top() + h * 0.5, w * 0.34, h * 0.7), 0, 180 * 16);
    p.setBrush(resource);  // the resource it gives
    p.drawEllipse(QRectF(r.left() + w * 0.66, r.top() + h * 0.08, w * 0.28, h * 0.28));
    p.setPen(QPen(owner, std::max(1.5, w / 10)));  // owner flag
    p.drawLine(QPointF(r.left() + w * 0.15, r.top() + h * 0.55), QPointF(r.left() + w * 0.15, r.top() + h * 0.05));
    p.setPen(Qt::NoPen);
    p.setBrush(owner);
    p.drawRect(QRectF(r.left() + w * 0.15, r.top() + h * 0.05, w * 0.22, h * 0.15));
    p.restore();
}

void drawDwelling(QPainter& p, const QRectF& r, const QColor& owner) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    shadow(p, r);
    const double w = r.width(), h = r.height();
    p.setPen(QPen(QColor("#3a2a18"), std::max(1.0, w / 18)));
    p.setBrush(QColor("#a8864f"));  // hut
    p.drawRect(QRectF(r.left() + w * 0.2, r.top() + h * 0.5, w * 0.6, h * 0.4));
    QPainterPath roof;
    roof.moveTo(r.left() + w * 0.1, r.top() + h * 0.52);
    roof.lineTo(r.center().x(), r.top() + h * 0.12);
    roof.lineTo(r.right() - w * 0.1, r.top() + h * 0.52);
    roof.closeSubpath();
    p.setBrush(owner);
    p.drawPath(roof);
    p.setBrush(QColor("#2a1d10"));
    p.drawRect(QRectF(r.center().x() - w * 0.08, r.top() + h * 0.65, w * 0.16, h * 0.25));
    p.restore();
}

void drawHero(QPainter& p, const QRectF& r, const QColor& owner, bool selected, bool inBoat) {
    // A rider on a horse with a banner in the owner's colour (or in a boat).
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const double w = r.width(), h = r.height();
    const auto X = [&](double f) { return r.left() + w * f; };
    const auto Y = [&](double f) { return r.top() + h * f; };
    const QPen pen(QColor("#20150a"), std::max(1.0, w / 40));
    if (selected) {
        p.setPen(QPen(QColor("#ffd84a"), std::max(2.0, w / 14)));
        p.setBrush(QColor(255, 216, 74, 60));
        p.drawEllipse(QRectF(X(0.04), Y(0.76), w * 0.92, h * 0.22));
    }
    p.setPen(pen);
    if (inBoat) {
        QPainterPath hull;
        hull.moveTo(X(0.0), Y(0.66));
        hull.lineTo(X(1.0), Y(0.66));
        hull.lineTo(X(0.82), Y(0.98));
        hull.lineTo(X(0.18), Y(0.98));
        hull.closeSubpath();
        p.setBrush(QColor("#8a5a2b"));
        p.drawPath(hull);
    } else {
        if (!selected) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 70));
            p.drawEllipse(QRectF(X(0.12), Y(0.84), w * 0.76, h * 0.14));
            p.setPen(pen);
        }
        // Horse: legs, body, neck and head.
        const QColor horse("#8a6038");
        p.setPen(QPen(horse.darker(170), std::max(1.5, w / 22), Qt::SolidLine, Qt::RoundCap));
        for (const double fx : {0.3, 0.38, 0.66, 0.74}) p.drawLine(QPointF(X(fx), Y(0.66)), QPointF(X(fx + (fx < 0.5 ? -0.03 : 0.03)), Y(0.9)));
        p.setPen(pen);
        p.setBrush(horse);
        p.drawEllipse(QRectF(X(0.22), Y(0.5), w * 0.6, h * 0.24));
        QPainterPath neck;
        neck.moveTo(X(0.7), Y(0.56));
        neck.lineTo(X(0.84), Y(0.34));
        neck.lineTo(X(0.96), Y(0.42));
        neck.lineTo(X(0.82), Y(0.62));
        neck.closeSubpath();
        p.drawPath(neck);
        p.setBrush(horse.darker(150));  // tail
        p.drawEllipse(QRectF(X(0.14), Y(0.52), w * 0.12, h * 0.2));
        // Saddle cloth in the owner's colour.
        p.setBrush(owner);
        p.drawRect(QRectF(X(0.4), Y(0.5), w * 0.22, h * 0.16));
    }
    // Rider: body, head and helmet.
    p.setBrush(owner.darker(115));
    p.drawRoundedRect(QRectF(X(0.42), Y(0.28), w * 0.18, h * 0.26), w * 0.05, w * 0.05);
    p.setBrush(QColor("#f0c8a0"));
    p.drawEllipse(QRectF(X(0.44), Y(0.14), w * 0.14, h * 0.14));
    p.setBrush(QColor("#b8b8c8"));
    p.drawChord(QRectF(X(0.43), Y(0.12), w * 0.16, h * 0.12), 0, 180 * 16);
    // Lance with a pennant.
    p.setPen(QPen(QColor("#3b2a17"), std::max(1.5, w / 28)));
    p.drawLine(QPointF(X(0.36), Y(0.7)), QPointF(X(0.36), Y(0.0)));
    p.setPen(pen);
    p.setBrush(owner);
    p.drawPolygon(QPolygonF{QPointF(X(0.37), Y(0.01)), QPointF(X(0.06), Y(0.08)), QPointF(X(0.2), Y(0.12)), QPointF(X(0.06), Y(0.18)),
                            QPointF(X(0.37), Y(0.2))});
    p.restore();
}

void drawMonster(QPainter& p, const QRectF& r) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    shadow(p, r);
    const double w = r.width(), h = r.height();
    p.setPen(QPen(QColor("#2a0a0a"), std::max(1.0, w / 16)));
    p.setBrush(QColor("#8b1a1a"));
    p.drawEllipse(QRectF(r.left() + w * 0.12, r.top() + h * 0.2, w * 0.76, h * 0.7));
    p.setBrush(QColor("#e8d9a8"));  // horns
    for (const double side : {-1.0, 1.0}) {
        QPainterPath horn;
        const double x = r.center().x() + side * w * 0.22;
        horn.moveTo(x - w * 0.07, r.top() + h * 0.3);
        horn.lineTo(x + side * w * 0.12, r.top() + h * 0.02);
        horn.lineTo(x + w * 0.07, r.top() + h * 0.3);
        horn.closeSubpath();
        p.drawPath(horn);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#ffe14a"));  // eyes
    p.drawEllipse(QPointF(r.center().x() - w * 0.13, r.top() + h * 0.5), w * 0.07, w * 0.07);
    p.drawEllipse(QPointF(r.center().x() + w * 0.13, r.top() + h * 0.5), w * 0.07, w * 0.07);
    p.restore();
}

void drawChest(QPainter& p, const QRectF& r, const QColor& resource) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    shadow(p, r);
    const double w = r.width(), h = r.height();
    const QRectF box(r.left() + w * 0.15, r.top() + h * 0.4, w * 0.7, h * 0.45);
    p.setPen(QPen(QColor("#3a2410"), std::max(1.0, w / 16)));
    p.setBrush(QColor("#8a5a2b"));
    p.drawRect(box);
    p.setBrush(resource);  // what is inside, spilling out
    p.drawChord(QRectF(box.left(), box.top() - h * 0.22, box.width(), h * 0.44), 0, 180 * 16);
    p.setBrush(QColor("#d4a017"));
    p.drawRect(QRectF(box.center().x() - w * 0.05, box.top(), w * 0.1, h * 0.14));
    p.restore();
}

void drawArtifact(QPainter& p, const QRectF& r) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPointF c = r.center();
    QRadialGradient glow(c, r.width() * 0.55);
    glow.setColorAt(0, QColor(255, 235, 140, 200));
    glow.setColorAt(1, QColor(255, 235, 140, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(c, r.width() * 0.55, r.height() * 0.55);
    QPolygonF star;
    for (int i = 0; i < 10; ++i) {
        const double a = M_PI / 5 * i - M_PI / 2;
        const double rad = (i % 2 ? 0.18 : 0.42) * r.width();
        star << QPointF(c.x() + rad * std::cos(a), c.y() + rad * std::sin(a));
    }
    p.setPen(QPen(QColor("#4a1a6a"), std::max(1.0, r.width() / 18)));
    p.setBrush(QColor("#c070f0"));
    p.drawPolygon(star);
    p.restore();
}

void drawUnitToken(QPainter& p, const QPointF& c, double rad, const QColor& side, const QString& name, int count,
                   int stars, bool active, bool target, const QString& unitId, int rank) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 80));
    p.drawEllipse(c + QPointF(rad * 0.08, rad * 0.12), rad, rad);
    QRadialGradient g(c - QPointF(rad * 0.3, rad * 0.3), rad * 1.4);
    g.setColorAt(0, side.lighter(150));
    g.setColorAt(1, side.darker(140));
    p.setBrush(g);
    p.setPen(QPen(active ? QColor("#ffd84a") : target ? QColor("#ff3030") : QColor("#20180c"), active || target ? 3 : 1.5));
    p.drawEllipse(c, rad, rad);
    p.setPen(QPen(QColor(255, 255, 255, 90), 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, rad * 0.8, rad * 0.8);
    if (!unitId.isEmpty()) {
        // The unit's picture inside the shield.
        const int size = std::max(8, int(rad * 1.5));
        p.drawPixmap(QPointF(c.x() - size / 2.0, c.y() - rad * 0.92), unitIcon(unitId, rank, size));
    }
    if (rad >= 10) {
        QFont f = p.font();
        f.setPixelSize(std::max(7, int(rad * 0.36)));
        f.setBold(true);
        p.setFont(f);
        if (unitId.isEmpty()) {
            p.setPen(Qt::white);
            p.drawText(QRectF(c.x() - rad, c.y() - rad * 0.7, 2 * rad, rad * 0.8), Qt::AlignCenter, name.left(8));
        }
        // Rank chevrons (advanced one, elite two) on the left.
        p.setPen(QPen(QColor("#ffd84a"), std::max(1.5, rad / 9)));
        for (int i = 0; i < rank; ++i) {
            const double y = c.y() + rad * (0.05 - 0.22 * i);
            p.drawLine(QPointF(c.x() - rad * 0.92, y - rad * 0.1), QPointF(c.x() - rad * 0.78, y));
            p.drawLine(QPointF(c.x() - rad * 0.78, y), QPointF(c.x() - rad * 0.64, y - rad * 0.1));
        }
        // The count on a small plate.
        const QRectF plate(c.x() - rad * 0.55, c.y() + rad * 0.42, rad * 1.1, rad * 0.5);
        p.setPen(QPen(QColor("#20180c"), 1));
        p.setBrush(QColor("#f1e4c0"));
        p.drawRoundedRect(plate, 3, 3);
        p.setPen(QColor("#20180c"));
        p.drawText(plate, Qt::AlignCenter, QString::number(count));
        if (stars > 0) {
            p.setPen(QColor("#ffd84a"));
            p.drawText(QRectF(c.x() - rad, c.y() - rad * 1.15, 2 * rad, rad * 0.5), Qt::AlignCenter, QString(stars, QChar(0x2605)));
        }
    }
    p.restore();
}

// Small arrow images for the style sheet (it cannot draw them itself).
QString arrowFile(bool up) {
    const QString dir = QDir::temp().filePath(QStringLiteral("kot_art"));
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(up ? QStringLiteral("up.png") : QStringLiteral("down.png"));
    if (!QFile::exists(path)) {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#e0c070"));
        QPolygonF tri;
        if (up) tri << QPointF(2, 15) << QPointF(18, 15) << QPointF(10, 5);
        else tri << QPointF(2, 5) << QPointF(18, 5) << QPointF(10, 15);
        p.drawPolygon(tri);
        p.end();
        pm.save(path);
    }
    return path;
}

QString styleSheet() {
    QString css = QStringLiteral(R"(
QWidget { background-color: #2b2118; color: #eadfc8; font-size: 10pt; }
QLabel { background: transparent; }
QPushButton {
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6b4a2b, stop:1 #4a3119);
    border: 1px solid #c9a24a; border-radius: 5px; padding: 5px 10px; color: #f6e7c1;
}
QPushButton:hover { background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #85603a, stop:1 #5b3d20); }
QPushButton:pressed { background-color: #3a2614; }
QPushButton:disabled { color: #8a7b63; border-color: #5e5030; background-color: #3a2c1e; }
QPushButton#endTurnButton { font-weight: bold; padding: 8px 10px; }
QPushButton#levelUpButton {
    font-weight: bold; color: #2b1a08; border: 2px solid #fff0b0;
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffe27a, stop:1 #d8a020);
}
QPushButton#endTurnButton[ready="true"] {
    font-size: 12pt; color: #2b1a08; border: 2px solid #fff0b0;
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffd75a, stop:1 #c98a12);
}
QPushButton#endTurnButton[ready="true"][pulse="true"] {
    border: 2px solid #ffffff;
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fff09a, stop:1 #e8a820);
}
QLineEdit, QSpinBox, QComboBox, QPlainTextEdit, QListWidget, QTableWidget {
    background-color: #1d1610; border: 1px solid #7a6235; border-radius: 3px; color: #eadfc8;
    selection-background-color: #8a6a2a;
}
QComboBox QAbstractItemView { background-color: #1d1610; color: #eadfc8; }
QTabWidget::pane { border: 1px solid #7a6235; }
QTabBar::tab { background: #3a2c1e; border: 1px solid #7a6235; padding: 5px 12px; color: #d8c8a8; }
QTabBar::tab:selected { background: #5b4025; color: #ffe9b0; }
QHeaderView::section { background-color: #3a2c1e; color: #f0d898; border: 1px solid #5e4a2a; padding: 3px; }
QScrollBar:vertical, QScrollBar:horizontal { background: #1d1610; }
QScrollBar::handle { background: #6b4a2b; border-radius: 4px; }
QToolTip { background-color: #f1e4c0; color: #20180c; border: 1px solid #7a6235; }
QComboBox { min-width: 7em; padding: 2px 22px 2px 6px; }
QComboBox::drop-down { width: 18px; border-left: 1px solid #7a6235; }
QComboBox::down-arrow { image: url(%DOWN%); width: 10px; height: 10px; }
QSpinBox { padding-right: 18px; min-width: 3em; }
QSpinBox::up-button, QSpinBox::down-button { width: 16px; background: #4a3119; border-left: 1px solid #7a6235; }
QSpinBox::up-arrow { image: url(%UP%); width: 8px; height: 8px; }
QSpinBox::down-arrow { image: url(%DOWN%); width: 8px; height: 8px; }
QCheckBox { spacing: 6px; }
QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid #c9a24a; border-radius: 3px; background: #1d1610; }
QCheckBox::indicator:checked { background: qradialgradient(cx:0.5, cy:0.5, radius:0.6, fx:0.5, fy:0.5,
                               stop:0 #ffe07a, stop:0.55 #c9a24a, stop:0.6 #1d1610); }
QCheckBox::indicator:disabled { border-color: #5e5030; }
QDialog, QMessageBox { background-color: #2b2118; }
)");
    css.replace(QStringLiteral("%UP%"), arrowFile(true));
    css.replace(QStringLiteral("%DOWN%"), arrowFile(false));
    return css;
}

}  // namespace kot::client::art
