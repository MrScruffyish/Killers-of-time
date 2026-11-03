// Towns drawn in code: the map icon shows how big a town is and how strong its
// walls are (palisade, stone walls, a citadel with four towers), in the stone and
// roof colours of its race; the town screen shows a picture of the town that
// fills up as buildings are added.

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

#include "client/Art.h"

namespace kot::client::art {

namespace {

const QColor kInk(30, 22, 12);

struct Palette {
    QColor stone, roof, wood, window;
};

Palette paletteFor(const QString& race) {
    if (race == "undead") return {QColor("#7a7482"), QColor("#3a3048"), QColor("#4a3a30"), QColor("#9aff9a")};
    if (race == "sylvan") return {QColor("#b8b090"), QColor("#3e7a3a"), QColor("#9a7444"), QColor("#ffe27a")};
    if (race == "dwarves") return {QColor("#9a948a"), QColor("#6a4a2a"), QColor("#7a5a3a"), QColor("#ff9a30")};
    if (race == "ashborn") return {QColor("#5a4a48"), QColor("#2a1a1a"), QColor("#4a2a20"), QColor("#ff6a20")};
    if (race == "tidefolk") return {QColor("#d8d0b8"), QColor("#2a6a8a"), QColor("#8a7a5a"), QColor("#a0f0ff")};
    if (race == "orcs") return {QColor("#b09a7a"), QColor("#8a2a1a"), QColor("#8a6a3a"), QColor("#ffb040")};
    return {QColor("#cfc6b2"), QColor("#a03a2a"), QColor("#8a5a2b"), QColor("#ffd27a")};
}

void house(QPainter& p, const QRectF& r, const Palette& pal, const QColor& roof) {
    p.setPen(QPen(kInk, std::max(1.0, r.width() / 14)));
    p.setBrush(pal.stone.lighter(105));
    p.drawRect(QRectF(r.left(), r.top() + r.height() * 0.4, r.width(), r.height() * 0.6));
    p.setBrush(roof);
    p.drawPolygon(QPolygonF{QPointF(r.left() - r.width() * 0.1, r.top() + r.height() * 0.42), QPointF(r.center().x(), r.top()),
                            QPointF(r.right() + r.width() * 0.1, r.top() + r.height() * 0.42)});
    p.setBrush(pal.window);
    p.drawRect(QRectF(r.center().x() - r.width() * 0.12, r.top() + r.height() * 0.6, r.width() * 0.24, r.height() * 0.2));
}

void battlements(QPainter& p, const QRectF& top, double merlon) {
    for (double x = top.left(); x < top.right() - merlon * 0.5; x += merlon * 1.8)
        p.drawRect(QRectF(x, top.top() - merlon, merlon, merlon));
}

void tower(QPainter& p, const QRectF& r, const Palette& pal, const QColor& roof, bool flag, const QColor& owner) {
    p.setPen(QPen(kInk, std::max(1.0, r.width() / 12)));
    p.setBrush(pal.stone.lighter(108));
    p.drawRect(r);
    p.setBrush(roof);
    p.drawPolygon(QPolygonF{QPointF(r.left() - r.width() * 0.12, r.top()), QPointF(r.center().x(), r.top() - r.width() * 1.0),
                            QPointF(r.right() + r.width() * 0.12, r.top())});
    p.setBrush(pal.window);
    p.drawRect(QRectF(r.center().x() - r.width() * 0.12, r.top() + r.height() * 0.25, r.width() * 0.24, r.height() * 0.15));
    if (flag) {
        const QPointF top(r.center().x(), r.top() - r.width() * 1.0);
        p.drawLine(top, top - QPointF(0, r.width() * 0.6));
        p.setBrush(owner);
        p.drawPolygon(QPolygonF{top - QPointF(0, r.width() * 0.6), top - QPointF(-r.width() * 0.7, r.width() * 0.45),
                                top - QPointF(0, r.width() * 0.3)});
    }
}

}  // namespace

void drawTownIcon(QPainter& p, const QRectF& r, const TownLook& look) {
    // A 2x2 block; the gate is in the bottom left quarter (the town's own tile).
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const Palette pal = paletteFor(look.race);
    const double w = r.width(), h = r.height();
    const auto X = [&](double f) { return r.left() + w * f; };
    const auto Y = [&](double f) { return r.top() + h * f; };
    const int stage = std::clamp(look.size / 3, 0, 3);  // 0 village .. 3 city
    const QColor roof = look.race == "humans" || look.race.isEmpty() ? look.owner : pal.roof;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 70));
    p.drawEllipse(QRectF(X(0.02), Y(0.8), w * 0.96, h * 0.18));
    // Houses behind the walls: more as the town grows.
    static const double hx[] = {0.62, 0.18, 0.74, 0.08, 0.5, 0.3, 0.84};
    static const double hy[] = {0.42, 0.44, 0.3, 0.32, 0.26, 0.24, 0.42};
    for (int i = 0; i < 1 + stage * 2 && i < 7; ++i)
        house(p, QRectF(X(hx[i]), Y(hy[i]), w * 0.16, h * 0.18), pal, i % 2 ? roof.darker(115) : roof);
    // The keep grows taller; a city has a spire.
    const QRectF keep(X(0.36), Y(0.34 - 0.06 * stage), w * 0.3, h * (0.36 + 0.06 * stage));
    p.setPen(QPen(kInk, std::max(1.0, w / 90)));
    p.setBrush(pal.stone);
    p.drawRect(keep);
    battlements(p, keep, w * 0.035);
    p.setBrush(pal.window);
    for (int i = 0; i <= stage; ++i)
        p.drawRoundedRect(QRectF(keep.center().x() - w * 0.025, keep.top() + h * (0.05 + 0.08 * i), w * 0.05, h * 0.05), 2, 2);
    const double flagTop = keep.top() - h * (stage >= 3 ? 0.2 : 0.06);
    if (stage >= 3) {
        p.setBrush(roof);
        p.drawPolygon(QPolygonF{QPointF(keep.left() + w * 0.06, keep.top() - h * 0.04), QPointF(keep.center().x(), flagTop),
                                QPointF(keep.right() - w * 0.06, keep.top() - h * 0.04)});
    }
    p.drawLine(QPointF(keep.center().x(), flagTop), QPointF(keep.center().x(), flagTop - h * 0.1));
    p.setBrush(look.owner);
    p.drawPolygon(QPolygonF{QPointF(keep.center().x(), flagTop - h * 0.1), QPointF(keep.center().x() + w * 0.16, flagTop - h * 0.07),
                            QPointF(keep.center().x(), flagTop - h * 0.03)});
    // The walls: a wooden palisade, stone walls with two towers, or a citadel.
    const double wallTop = look.walls >= 2 ? 0.5 : look.walls == 1 ? 0.58 : 0.66;
    const QRectF gate(X(0.12), Y(0.7), w * 0.2, h * 0.24);
    if (look.walls <= 0) {
        p.setPen(QPen(kInk, std::max(1.0, w / 120)));
        p.setBrush(pal.wood);
        for (double x = 0.04; x < 0.96; x += 0.045) {
            if (x > 0.12 && x < 0.32) continue;  // the gate opening
            const double top = wallTop + 0.02 * std::sin(x * 40);
            p.drawPolygon(QPolygonF{QPointF(X(x), Y(0.94)), QPointF(X(x), Y(top + 0.02)), QPointF(X(x + 0.02), Y(top)),
                                    QPointF(X(x + 0.04), Y(top + 0.02)), QPointF(X(x + 0.04), Y(0.94))});
        }
    } else {
        p.setPen(QPen(kInk, std::max(1.0, w / 90)));
        const QRectF wall(X(0.05), Y(wallTop), w * 0.9, h * (0.94 - wallTop));
        p.setBrush(pal.stone.darker(look.walls >= 2 ? 112 : 104));
        p.drawRect(wall);
        p.setBrush(pal.stone);
        battlements(p, wall, w * 0.04);
        // Stone courses.
        p.setPen(QPen(pal.stone.darker(135), std::max(1.0, w / 160)));
        for (double y = wall.top() + h * 0.07; y < wall.bottom(); y += h * 0.07) p.drawLine(QPointF(wall.left(), y), QPointF(wall.right(), y));
        const double tw = look.walls >= 2 ? 0.17 : 0.15;
        QList<double> towers = {0.0, 1.0 - tw};
        if (look.walls >= 2) towers << 0.34 << 0.62;
        for (const double tx : towers)
            tower(p, QRectF(X(tx), Y(wallTop - (look.walls >= 2 ? 0.14 : 0.08)), w * tw, h * (0.94 - wallTop + (look.walls >= 2 ? 0.14 : 0.08))),
                  pal, roof, look.walls >= 2, look.owner);
    }
    // The gate.
    p.setPen(QPen(kInk, std::max(1.0, w / 90)));
    p.setBrush(QColor("#4a3520"));
    p.drawChord(QRectF(gate.left(), gate.top(), gate.width(), gate.height() * 1.6), 0, 180 * 16);
    p.setPen(QPen(QColor("#2a1d10"), std::max(1.0, w / 120)));
    for (double x = gate.left() + gate.width() * 0.2; x < gate.right(); x += gate.width() * 0.2)
        p.drawLine(QPointF(x, gate.top() + h * 0.03), QPointF(x, gate.bottom()));
    p.restore();
}

const QPixmap& townSprite(const TownLook& look, int size) {
    return sprite(QStringLiteral("town2|%1|%2|%3|%4").arg(look.owner.name(), look.race).arg(look.walls).arg(std::clamp(look.size / 3, 0, 3)), size,
                  [&](QPainter& p, const QRectF& r) { drawTownIcon(p, r, look); });
}

void drawTownScene(QPainter& p, const QRectF& r, const TownLook& look, const QList<SceneBuilding>& buildings) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipRect(r);
    const Palette pal = paletteFor(look.race);
    const double w = r.width(), h = r.height();
    const auto X = [&](double f) { return r.left() + w * f; };
    const auto Y = [&](double f) { return r.top() + h * f; };
    // Sky (dusk for the undead), far hills and the ground.
    QLinearGradient sky(r.topLeft(), r.bottomLeft());
    sky.setColorAt(0, look.race == "undead" ? QColor("#3a2a50") : QColor("#6aa0d8"));
    sky.setColorAt(0.7, look.race == "undead" ? QColor("#8a6a7a") : QColor("#d8e8f0"));
    p.fillRect(r, sky);
    p.setPen(Qt::NoPen);
    p.setBrush(look.race == "undead" ? QColor("#4a4a40") : QColor("#7aa060"));
    QPainterPath hills;
    hills.moveTo(X(0), Y(0.7));
    for (int i = 0; i <= 8; ++i) hills.quadTo(X(i / 8.0 - 0.06), Y(0.5 + 0.06 * ((i * 7) % 3)), X(i / 8.0), Y(0.66));
    hills.lineTo(X(1), Y(1));
    hills.lineTo(X(0), Y(1));
    p.drawPath(hills);
    p.setBrush(look.race == "undead" ? QColor("#5a5a48") : QColor("#5d8a3a"));
    p.drawRect(QRectF(X(0), Y(0.78), w, h * 0.22));
    bool water = false;
    for (const SceneBuilding& b : buildings) water = water || b.kind == SceneBuilding::Kind::Shipyard;
    if (water) {
        p.setBrush(QColor("#3a6ea8"));
        p.drawRect(QRectF(X(0.88), Y(0.74), w * 0.12, h * 0.26));
    }
    // Plots from left to right; the hall in the middle.
    const double unit = h * 0.42;
    QList<SceneBuilding> sorted = buildings;
    std::stable_sort(sorted.begin(), sorted.end(), [](const SceneBuilding& a, const SceneBuilding& b) { return int(a.kind) < int(b.kind); });
    int plot = 0;
    const int plots = std::max(8, int(sorted.size()));
    const auto plotX = [&](int i) {
        // Spread around the centre: 0 centre, then left/right alternately.
        const int side = (i % 2) ? -1 : 1;
        const int step = (i + 1) / 2;
        return 0.47 + side * step * (0.86 / plots);
    };
    const QColor roof = look.race == "humans" ? look.owner : pal.roof;
    for (const SceneBuilding& b : sorted) {
        if (b.kind == SceneBuilding::Kind::Walls || b.kind == SceneBuilding::Kind::Shipyard) continue;
        const double cx = X(plotX(plot++));
        const double base = Y(0.84);
        p.setPen(QPen(kInk, std::max(1.0, unit / 30)));
        switch (b.kind) {
        case SceneBuilding::Kind::Hall: {
            const QRectF hall(cx - unit * 0.7, base - unit * 1.1, unit * 1.4, unit * 1.1);
            p.setBrush(pal.stone);
            p.drawRect(hall);
            p.setBrush(roof);
            p.drawPolygon(QPolygonF{QPointF(hall.left() - unit * 0.1, hall.top()), QPointF(hall.center().x(), hall.top() - unit * 0.6),
                                    QPointF(hall.right() + unit * 0.1, hall.top())});
            p.setBrush(pal.window);
            for (int i = 0; i < 3; ++i) p.drawRoundedRect(QRectF(hall.left() + unit * (0.2 + 0.4 * i), hall.top() + unit * 0.25, unit * 0.18, unit * 0.3), 3, 3);
            p.setBrush(QColor("#4a3520"));
            p.drawChord(QRectF(hall.center().x() - unit * 0.15, base - unit * 0.45, unit * 0.3, unit * 0.9), 0, 180 * 16);
            if (b.level >= 1) {  // a town hall: a clock tower on the roof
                const QRectF ct(hall.center().x() - unit * 0.14, hall.top() - unit * 0.62, unit * 0.28, unit * 0.42);
                p.setBrush(pal.stone.lighter(108));
                p.drawRect(ct);
                p.setBrush(QColor("#f4ecd0"));
                p.drawEllipse(ct.center(), unit * 0.09, unit * 0.09);
                p.drawLine(ct.center(), ct.center() + QPointF(0, -unit * 0.07));
                p.drawLine(ct.center(), ct.center() + QPointF(unit * 0.05, 0));
                if (b.level >= 2) {  // the city hall's golden dome
                    p.setBrush(QColor("#e8c04a"));
                    p.drawChord(QRectF(ct.left() - unit * 0.04, ct.top() - unit * 0.16, ct.width() + unit * 0.08, unit * 0.32), 0, 180 * 16);
                } else {
                    p.setBrush(roof.darker(115));
                    p.drawPolygon(QPolygonF{QPointF(ct.left() - unit * 0.05, ct.top()), QPointF(ct.center().x(), ct.top() - unit * 0.18),
                                            QPointF(ct.right() + unit * 0.05, ct.top())});
                }
            }
            if (b.level >= 2) {  // a city hall: wings, a golden dome and banners
                for (const double side : {-1.0, 1.0}) {
                    const QRectF wing(side < 0 ? hall.left() - unit * 0.45 : hall.right(), base - unit * 0.75, unit * 0.45, unit * 0.75);
                    p.setBrush(pal.stone.darker(104));
                    p.drawRect(wing);
                    p.setBrush(pal.window);
                    p.drawRoundedRect(QRectF(wing.center().x() - unit * 0.08, wing.top() + unit * 0.2, unit * 0.16, unit * 0.26), 3, 3);
                    p.setBrush(roof);
                    p.drawRect(QRectF(wing.left(), wing.top() - unit * 0.08, wing.width(), unit * 0.08));
                    const QPointF pole(wing.center().x(), wing.top() - unit * 0.08);
                    p.drawLine(pole, pole - QPointF(0, unit * 0.45));
                    p.setBrush(look.owner);
                    p.drawPolygon(QPolygonF{pole - QPointF(0, unit * 0.45), pole - QPointF(-unit * 0.22, unit * 0.37), pole - QPointF(0, unit * 0.29)});
                }
            }
            break;
        }
        case SceneBuilding::Kind::Granary: {  // a barn with a round silo and grain sacks
            const QRectF barn(cx - unit * 0.42, base - unit * 0.6, unit * 0.6, unit * 0.6);
            p.setBrush(QColor("#a8402a"));
            p.drawRect(barn);
            p.setBrush(QColor("#7a2a1a"));
            p.drawPolygon(QPolygonF{QPointF(barn.left() - unit * 0.05, barn.top()), QPointF(barn.center().x(), barn.top() - unit * 0.3),
                                    QPointF(barn.right() + unit * 0.05, barn.top())});
            p.setPen(QPen(QColor("#f0e8d8"), std::max(1.0, unit / 30)));
            p.drawLine(barn.bottomLeft() + QPointF(unit * 0.15, 0), barn.topRight() + QPointF(-unit * 0.15, unit * 0.15));
            p.drawLine(barn.bottomRight() + QPointF(-unit * 0.15, 0), barn.topLeft() + QPointF(unit * 0.15, unit * 0.15));
            p.setPen(QPen(kInk, std::max(1.0, unit / 30)));
            const QRectF silo(barn.right(), base - unit * 0.95, unit * 0.26, unit * 0.95);
            p.setBrush(pal.stone.lighter(104));
            p.drawRect(silo);
            p.setBrush(pal.stone.darker(115));
            p.drawChord(QRectF(silo.left(), silo.top() - unit * 0.13, silo.width(), unit * 0.26), 0, 180 * 16);
            p.setBrush(QColor("#e0c070"));
            for (int i = 0; i < 3; ++i) p.drawEllipse(QRectF(barn.left() - unit * 0.12 + i * unit * 0.13, base - unit * 0.16, unit * 0.14, unit * 0.16));
            break;
        }
        case SceneBuilding::Kind::Tavern: {
            house(p, QRectF(cx - unit * 0.4, base - unit * 0.9, unit * 0.8, unit * 0.9), pal, roof.darker(110));
            p.setBrush(QColor("#c9a24a"));  // the sign with a mug
            p.drawRect(QRectF(cx + unit * 0.42, base - unit * 0.62, unit * 0.22, unit * 0.18));
            break;
        }
        case SceneBuilding::Kind::Dwelling: {
            const double hgt = unit * (0.6 + 0.18 * b.level);
            house(p, QRectF(cx - unit * 0.35, base - hgt, unit * 0.7, hgt), pal, roof);
            if (!b.unit.isEmpty()) {  // the unit that lives here, on a sign
                const int s = int(unit * 0.5);
                p.setBrush(QColor(240, 228, 192));
                p.drawEllipse(QPointF(cx, base - hgt - s * 0.2), s * 0.55, s * 0.55);
                p.drawPixmap(QPointF(cx - s / 2.0, base - hgt - s * 0.72), unitIcon(b.unit, b.level, s));
            }
            break;
        }
        case SceneBuilding::Kind::MageGuild: {
            const double hgt = unit * (0.9 + 0.35 * b.level);
            const QRectF t(cx - unit * 0.18, base - hgt, unit * 0.36, hgt);
            p.setBrush(QColor("#6a5a9a"));
            p.drawRect(t);
            p.setBrush(QColor("#3a2a6a"));
            p.drawPolygon(QPolygonF{QPointF(t.left() - unit * 0.06, t.top()), QPointF(t.center().x(), t.top() - unit * 0.5),
                                    QPointF(t.right() + unit * 0.06, t.top())});
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#fff0a0"));
            for (int i = 0; i < b.level; ++i) p.drawEllipse(QPointF(t.center().x(), t.top() + unit * (0.2 + 0.25 * i)), unit * 0.04, unit * 0.04);
            break;
        }
        case SceneBuilding::Kind::Market: {
            for (int i = 0; i < 2; ++i) {
                const double x = cx - unit * 0.5 + i * unit * 0.55;
                p.setBrush(pal.wood);
                p.drawRect(QRectF(x, base - unit * 0.35, unit * 0.45, unit * 0.35));
                p.setBrush(i ? QColor("#d8b030") : QColor("#c03a3a"));  // awning
                p.drawPolygon(QPolygonF{QPointF(x - unit * 0.05, base - unit * 0.35), QPointF(x + unit * 0.05, base - unit * 0.55),
                                        QPointF(x + unit * 0.4, base - unit * 0.55), QPointF(x + unit * 0.5, base - unit * 0.35)});
            }
            break;
        }
        default: {
            house(p, QRectF(cx - unit * 0.3, base - unit * 0.6, unit * 0.6, unit * 0.6), pal, roof.darker(125));
            break;
        }
        }
    }
    // A boat at the shipyard.
    if (water) {
        p.setPen(QPen(kInk, 1.5));
        p.setBrush(pal.wood);
        p.drawPolygon(QPolygonF{QPointF(X(0.89), Y(0.84)), QPointF(X(0.99), Y(0.84)), QPointF(X(0.97), Y(0.92)), QPointF(X(0.91), Y(0.92))});
        p.drawLine(QPointF(X(0.94), Y(0.84)), QPointF(X(0.94), Y(0.66)));
        p.setBrush(QColor("#f0ead8"));
        p.drawPolygon(QPolygonF{QPointF(X(0.94), Y(0.67)), QPointF(X(0.985), Y(0.8)), QPointF(X(0.94), Y(0.8))});
    }
    // Walls in front: a palisade, stone walls, or high walls with towers.
    int walls = 0;
    for (const SceneBuilding& b : buildings) walls += b.kind == SceneBuilding::Kind::Walls;
    p.setPen(QPen(kInk, 1.2));
    if (walls == 0) {
        p.setBrush(pal.wood);
        for (double x = 0.0; x < 0.88; x += 0.018) {
            if (x > 0.42 && x < 0.52) continue;
            p.drawPolygon(QPolygonF{QPointF(X(x), Y(1)), QPointF(X(x), Y(0.88)), QPointF(X(x + 0.008), Y(0.85)), QPointF(X(x + 0.016), Y(0.88)),
                                    QPointF(X(x + 0.016), Y(1))});
        }
    } else {
        const double top = walls >= 3 ? 0.82 : walls >= 2 ? 0.86 : 0.9;
        p.setBrush(pal.stone.darker(108));
        p.drawRect(QRectF(X(0), Y(top), w * 0.88, h * (1 - top)));
        p.setBrush(pal.stone);
        battlements(p, QRectF(X(0), Y(top), w * 0.88, 1), h * 0.035);
        if (walls >= 3) {  // a castle: a great round tower at each end with a banner
            for (const double tx : {0.06, 0.75}) {
                const QRectF t(X(tx), Y(top - 0.16), w * 0.06, h * (1.16 - top));
                p.setBrush(pal.stone.darker(104));
                p.drawRect(t);
                p.setBrush(pal.stone);
                battlements(p, QRectF(t.left() - w * 0.006, t.top(), t.width() + w * 0.012, 1), h * 0.04);
                p.setBrush(QColor("#2a1a10"));
                p.drawRoundedRect(QRectF(t.center().x() - w * 0.006, t.top() + h * 0.08, w * 0.012, h * 0.06), 2, 2);
                const QPointF pole(t.center().x(), t.top() - h * 0.04);
                p.drawLine(pole, pole - QPointF(0, h * 0.14));
                p.setBrush(look.owner);
                p.drawPolygon(QPolygonF{pole - QPointF(0, h * 0.14), pole - QPointF(-w * 0.03, h * 0.11), pole - QPointF(0, h * 0.08)});
            }
        }
        if (walls >= 2)
            for (const double tx : {0.01, 0.4, 0.52, 0.84})
                tower(p, QRectF(X(tx), Y(top - 0.08), w * 0.028, h * (1.08 - top)), pal, roof, false, look.owner);
        p.setBrush(QColor("#4a3520"));
        p.drawChord(QRectF(X(0.445), Y(top + 0.02), w * 0.06, h * 0.25), 0, 180 * 16);
    }
    p.restore();
}

}  // namespace kot::client::art
