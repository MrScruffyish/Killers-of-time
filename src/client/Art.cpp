#include "client/Art.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
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

QPixmap makeTile(const QString& id, const QColor& base, int s, int variant) {
    QPixmap pm(s, s);
    pm.fill(base);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    Dice d(qHash(id) + variant * 7919);
    // Soft light from the top left.
    QLinearGradient shade(0, 0, s, s);
    shade.setColorAt(0, QColor(255, 255, 255, 18));
    shade.setColorAt(1, QColor(0, 0, 0, 22));
    p.fillRect(pm.rect(), shade);
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
    } else if (id == "forest") {
        speckles(p, s, d, base.darker(130), 4, 0.07);
        for (int i = 0; i < 3; ++i) {  // small conifers
            const double x = s * (0.2 + 0.3 * i) + (d.next() - 0.5) * s * 0.12;
            const double y = s * (0.35 + 0.35 * d.next());
            const double h = s * 0.42, w = s * 0.2;
            QPainterPath tree;
            tree.moveTo(x, y - h * 0.6);
            tree.lineTo(x + w, y + h * 0.3);
            tree.lineTo(x - w, y + h * 0.3);
            tree.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(base.darker(150));
            p.drawPath(tree.translated(s * 0.03, s * 0.03));
            p.setBrush(base.lighter(125));
            p.drawPath(tree);
            p.setBrush(QColor("#5b3a1e"));
            p.drawRect(QRectF(x - s * 0.025, y + h * 0.3, s * 0.05, s * 0.08));
        }
    } else if (id == "hills") {
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 2; ++i) {
            const QRectF hill(s * (0.05 + 0.45 * i), s * (0.35 + 0.2 * d.next()), s * 0.55, s * 0.5);
            p.setBrush(base.lighter(118));
            p.drawChord(hill, 0, 180 * 16);
            p.setBrush(base.darker(118));
            p.drawChord(hill.adjusted(hill.width() * 0.5, 0, 0, 0), 0, 90 * 16);
        }
    } else if (id == "mountains") {
        p.setPen(Qt::NoPen);
        const double peak = s * (0.08 + 0.1 * d.next());
        QPainterPath m;
        m.moveTo(s * 0.05, s * 0.92);
        m.lineTo(s * 0.5, peak);
        m.lineTo(s * 0.95, s * 0.92);
        m.closeSubpath();
        p.setBrush(base.darker(125));
        p.drawPath(m);
        QPainterPath lit;
        lit.moveTo(s * 0.05, s * 0.92);
        lit.lineTo(s * 0.5, peak);
        lit.lineTo(s * 0.45, s * 0.92);
        lit.closeSubpath();
        p.setBrush(base.lighter(125));
        p.drawPath(lit);
        QPainterPath cap;  // snow cap
        cap.moveTo(s * 0.5, peak);
        cap.lineTo(s * 0.62, peak + s * 0.2);
        cap.lineTo(s * 0.5, peak + s * 0.15);
        cap.lineTo(s * 0.38, peak + s * 0.2);
        cap.closeSubpath();
        p.setBrush(QColor("#f4f6f8"));
        p.drawPath(cap);
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
const QPixmap& mineSprite(const QString& resource, const QColor& resourceColor, const QColor& owner, int size) {
    return sprite(QStringLiteral("mine|%1|%2").arg(resource, owner.name()), size, [&](QPainter& p, const QRectF& r) {
        drawMine(p, r, resourceColor, owner);
        drawResourceIcon(p, QRectF(r.left() + r.width() * 0.58, r.top() - r.height() * 0.02, r.width() * 0.42, r.height() * 0.42),
                         resource, resourceColor);
    });
}
const QPixmap& dwellingSprite(const QColor& owner, int size) {
    return sprite(QStringLiteral("dwelling|") + owner.name(), size, [&](QPainter& p, const QRectF& r) { drawDwelling(p, r, owner); });
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
                   int stars, bool active, bool target) {
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
    if (rad >= 10) {
        QFont f = p.font();
        f.setPixelSize(std::max(7, int(rad * 0.36)));
        f.setBold(true);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRectF(c.x() - rad, c.y() - rad * 0.7, 2 * rad, rad * 0.8), Qt::AlignCenter, name.left(8));
        // The count on a small plate.
        const QRectF plate(c.x() - rad * 0.55, c.y() + rad * 0.15, rad * 1.1, rad * 0.55);
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
