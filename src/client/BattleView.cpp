#include "client/BattleView.h"

#include <QCursor>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QHash>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>
#include <QFontMetrics>
#include <QElapsedTimer>
#include <QTimer>
#include <cmath>

#include "client/Art.h"
#include "client/SpellBook.h"
#include "core/battle/Battle.h"
#include "core/battle/HexGrid.h"
#include "core/content/ContentRegistry.h"

namespace kot::client {

// Paints pointy-top hexes in "odd-r" layout, scaled to fit.
class HexField : public QWidget {
public:
    HexField(const core::ContentRegistry* content, QWidget* parent) : QWidget(parent), m_content(content) {
        setMinimumSize(400, 280);
        setMouseTracking(true);
        m_anim.setInterval(16);
        connect(&m_anim, &QTimer::timeout, this, [this]() {
            double dt = m_clock.restart() / 1000.0;
            m_slide = std::min(1.0, m_slide + dt / 0.28);
            for (Floater& f : m_floaters) f.age += dt;
            m_floaters.removeIf([](const Floater& f) { return f.age > 1.2; });
            for (Ghost& g : m_ghosts) g.age += dt;
            m_ghosts.removeIf([](const Ghost& g) { return g.age > 0.7; });
            // The action being shown, one after the other.
            while (!m_fx.isEmpty()) {
                Fx& f = m_fx.first();
                if (f.type == "move" && f.age == 0) m_fxPos.insert(f.unit, centre(f.hex));
                f.age += dt;
                if (f.age < f.duration) break;
                dt = f.age - f.duration;
                m_fx.removeFirst();
                if (!m_fx.isEmpty()) m_fx.first().age = 0;
            }
            if (m_slide >= 1.0 && m_floaters.isEmpty() && m_ghosts.isEmpty() && m_fx.isEmpty()) m_anim.stop();
            update();
        });
    }
    // A new state of the battle: stacks that moved slide there, losses float up,
    // stacks that were wiped out fade away.
    void setBattleAnimated(const ViewBattle& next) {
        const ViewBattle old = battle;
        battle = next;
        if (!old.present && next.present && !m_pendingEvents.isEmpty()) {
            const QJsonArray pending = m_pendingEvents;
            m_pendingEvents = QJsonArray();
            queueEvents(pending);
        }
        if (!old.present || !next.present) return;
        m_slideFrom.clear();
        for (const ViewBattleUnit& u : old.units) {
            const ViewBattleUnit* now = next.unit(u.id);
            if (!now) {
                m_ghosts.append({u, 0});
                m_floaters.append({centre(u.hex), QStringLiteral("-%1").arg(u.count), QColor("#ff5040"), 0});
                continue;
            }
            if (now->hex != u.hex) m_slideFrom.insert(u.id, centre(u.hex));
            if (now->count < u.count)
                m_floaters.append({centre(now->hex), QStringLiteral("-%1").arg(u.count - now->count), QColor("#ff5040"), 0});
            else if (now->frontHp < u.frontHp && now->count == u.count)
                m_floaters.append({centre(now->hex), tr("hit"), QColor("#ffb060"), 0});
        }
        m_slide = m_slideFrom.isEmpty() ? 1.0 : 0.0;
        if (!m_slideFrom.isEmpty() || !m_floaters.isEmpty() || !m_ghosts.isEmpty()) {
            m_clock.restart();
            m_anim.start();
        }
    }
    // Battle events to show (from the server): arrows fly, attackers lunge, spells
    // sparkle. Positions are taken now, before the new state arrives.
    void queueEvents(const QJsonArray& events) {
        if (!battle.present) {  // the battle screen is not there yet: show them when it is
            for (const QJsonValue& v : events) m_pendingEvents.append(v);
            return;
        }
        for (const ViewBattleUnit& u : battle.units) m_fxPos.insert(u.id, centre(u.hex));
        for (const QJsonValue& v : events) {
            const QJsonObject e = v.toObject();
            Fx f;
            f.type = e["type"].toString();
            f.unit = e["unit"].toInt();
            f.target = e["target"].toInt();
            f.spell = core::SpellId(e["spell"].toInt(-1));
            f.lucky = e["lucky"].toBool();
            f.hex = {e["x"].toInt(), e["y"].toInt()};
            if (f.type == "tower") f.from = centre(core::Pos{e["fromX"].toInt(), e["fromY"].toInt()});
            f.duration = f.type == "shoot" || f.type == "tower" ? 0.45 : f.type == "wall_hit" ? 0.7 : f.type == "engine" ? 0.5 : f.type == "attack" || f.type == "retaliate" ? 0.4
                       : f.type == "cast" ? 0.6 : f.type == "morale" || f.type == "falter" ? 0.7 : f.type == "move" ? 0.05 : 0;
            if (f.duration > 0) m_fx.append(f);
        }
        if (!m_fx.isEmpty()) {
            m_fx.first().age = 0;
            m_clock.restart();
            m_anim.start();
        }
    }
    QPointF fxPos(int id) const {
        if (const ViewBattleUnit* u = battle.unit(id); u && !m_fxPos.contains(id)) return centre(u->hex);
        return m_fxPos.value(id);
    }
    // Where a stack is drawn now (sliding toward its hex, or lunging at a target).
    QPointF drawnAt(const ViewBattleUnit& u) const {
        if (!m_fx.isEmpty()) {
            const Fx& f = m_fx.first();
            if ((f.type == "attack" || f.type == "retaliate") && f.unit == u.id && m_fxPos.contains(f.target)) {
                const QPointF from = centre(u.hex), to = fxPos(f.target);
                return from + (to - from) * (0.35 * std::sin(M_PI * std::clamp(f.age / f.duration, 0.0, 1.0)));
            }
        }
        const QPointF to = centre(u.hex);
        const auto it = m_slideFrom.constFind(u.id);
        if (it == m_slideFrom.cend() || m_slide >= 1.0) return to;
        const double t = 1 - (1 - m_slide) * (1 - m_slide);  // ease out
        return *it + (to - *it) * t;
    }
    ViewBattle battle;
    QColor sideColor[2];
    std::function<void(core::Pos, core::Pos)> clicked;  // hex, and the hex to strike from (or -1,-1)
    std::function<void(core::Pos)> hovered;
    core::Pos hover{-1, -1};
    // What a click would do where the mouse is, and for an attack where from.
    enum class Intent { None, Move, Attack, MoveAttack, Shoot, Blocked };
    Intent intent = Intent::None;
    core::Pos strikeFrom{-1, -1};

    // Works out the intent at `pt`: on an enemy the side the mouse is on picks the
    // hex to attack from (one the stack can reach, or where it stands).
    void updateIntent(QPointF pt) {
        const core::Pos h = hexAt(pt);
        intent = Intent::None;
        strikeFrom = {-1, -1};
        const ViewBattleUnit* me = battle.unit(battle.active);
        if (!battle.myMove() || !me || h.x < 0) return;
        const core::UnitDef* activeDef = m_content->unit(me->unit);
        if (activeDef && activeDef->has(core::Ability::SiegeEngine)) {  // the catapult only shoots walls
            intent = battle.walls.contains(h) ? Intent::Shoot : Intent::Blocked;
            return;
        }
        const ViewBattleUnit* there = battle.unitAt(h);
        if (there && there->side == battle.yourSide) return;  // your own stack: just look
        if (!there) {
            intent = battle.reachable.contains(h) ? Intent::Move : Intent::Blocked;
            return;
        }
        if (battle.shootable.contains(there->id)) {
            intent = Intent::Shoot;
            return;
        }
        if (!battle.attackable.contains(there->id)) {
            intent = Intent::Blocked;
            return;
        }
        const QPointF c = centre(h);
        double best = 1e18;
        for (const core::Pos n : core::hex::neighbours(h)) {
            const bool canStand = n == me->hex || (battle.reachable.contains(n) && !battle.unitAt(n));
            if (!canStand) continue;
            const QPointF dir = centre(n) - c, toMouse = pt - c;
            // Closest in angle to where the mouse points (ties: no movement).
            const double d = std::hypot(dir.x() - toMouse.x() * 2, dir.y() - toMouse.y() * 2) - (n == me->hex ? 0.01 : 0);
            if (d < best) {
                best = d;
                strikeFrom = n;
            }
        }
        intent = strikeFrom == me->hex ? Intent::Attack : Intent::MoveAttack;
    }
    void updatePointer() {
        using art::Pointer;
        const Pointer kind = intent == Intent::Move ? Pointer::Move
                           : intent == Intent::Attack ? Pointer::Attack
                           : intent == Intent::MoveAttack ? Pointer::MoveAttack
                           : intent == Intent::Shoot ? Pointer::Shoot
                           : intent == Intent::Blocked ? Pointer::Blocked : Pointer::Normal;
        static QHash<int, QCursor> cursors;
        auto it = cursors.find(int(kind));
        if (it == cursors.end()) {
            const bool tip = kind == Pointer::Normal;
            it = cursors.insert(int(kind), QCursor(art::pointerImage(kind), tip ? 3 : 16, tip ? 2 : 16));
        }
        setCursor(it.value());
    }

    double radius() const {
        const int w = std::max(1, battle.width), h = std::max(1, battle.height);
        const double byWidth = (width() - 40) / (std::sqrt(3.0) * (w + 0.5));
        const double byHeight = (height() - 40) / (1.5 * (h - 1) + 2.0);
        return std::max(4.0, std::min(byWidth, byHeight));
    }
    // The field is centred in the widget.
    QPointF origin() const {
        const double r = radius();
        const int w = std::max(1, battle.width), h = std::max(1, battle.height);
        const double fw = std::sqrt(3.0) * r * (w + 0.5), fh = 1.5 * r * (h - 1) + 2 * r;
        return {std::max(4.0, (width() - fw) / 2), std::max(4.0, (height() - fh) / 2)};
    }
    QPointF centre(core::Pos p) const {
        const double r = radius();
        const QPointF o = origin();
        return {o.x() + std::sqrt(3.0) * r * (p.x + 0.5 + 0.5 * (p.y & 1)), o.y() + r + 1.5 * r * p.y};
    }
    core::Pos hexAt(QPointF pt) const {
        core::Pos best{-1, -1};
        double bestD = 1e18;
        for (int y = 0; y < battle.height; ++y)
            for (int x = 0; x < battle.width; ++x) {
                const QPointF d = centre({x, y}) - pt;
                const double dist = d.x() * d.x() + d.y() * d.y();
                if (dist < bestD) {
                    bestD = dist;
                    best = {x, y};
                }
            }
        return bestD <= radius() * radius() ? best : core::Pos{-1, -1};
    }

    // The land around the field, after the map tile the battle is on: trees round a
    // forest battle, peaks behind a mountain one, the sea below a shore battle,
    // bushes and stones on open ground; darker than the field so that stands out.
    void drawSurroundings(QPainter& p, const QRectF& field, double r) {
        const QString where = battle.terrain >= 0 && battle.terrain < m_content->terrain().size()
                                  ? m_content->terrain(core::TerrainId(battle.terrain)).key : QStringLiteral("grass");
        static const QHash<QString, QString> groundOf = {{"forest", "grass"}, {"hills", "dirt"}, {"mountains", "dirt"},
                                                         {"road", "dirt"}, {"sea", "sand"}, {"river", "grass"}, {"lava", "dirt"}};
        const QString groundId = groundOf.value(where, where);
        const int tile = std::max(24, int(r * 2.6));
        for (int ty = 0; ty * tile < height(); ++ty)
            for (int tx = 0; tx * tile < width(); ++tx)
                p.drawPixmap(tx * tile, ty * tile, art::terrainTile(groundId, tile, art::variantFor(tx + 3, ty + 9)));
        const bool water = where == "sea" || where == "river";
        if (water) {  // the water along the bottom, with a sandy edge
            for (int ty = int(field.bottom() + r * 0.6) / tile; ty * tile < height() + tile; ++ty)
                for (int tx = 0; tx * tile < width(); ++tx)
                    p.drawPixmap(tx * tile, int(field.bottom() + r * 0.6) + (ty - int(field.bottom() + r * 0.6) / tile) * tile,
                                 art::terrainTile(QStringLiteral("sea"), tile, art::variantFor(tx, ty)));
        }
        // Features in bands above and below the field (and at the sides when there is room).
        QString feature;
        if (where == "forest" || where == "grass" || where == "river" || where == "swamp") feature = QStringLiteral("forest");
        else if (where == "hills" || where == "dirt" || where == "road") feature = QStringLiteral("hills");
        else if (where == "mountains" || where == "snow" || where == "lava") feature = QStringLiteral("mountains");
        const int size = std::max(32, int(r * 3.2));
        const bool dense = where == "forest" || where == "mountains" || where == "hills";
        const auto band = [&](double top, double bottom, double left, double right, int seed) {
            if (bottom - top < size * 0.3 || right - left < size * 0.3) return;
            for (double y = top - size * 0.45; y < bottom - size * 0.3; y += size * 0.42)
                for (double x = left - size * 0.3; x < right; x += size * (dense ? 0.42 : 0.7)) {
                    const int v = art::variantFor(int(x / 7) + seed, int(y / 5) + seed);
                    const double jx = ((v * 37) % 10) / 10.0 * size * 0.2;
                    if (!feature.isEmpty() && (dense || v % 3 == 0)) {
                        p.drawPixmap(QPointF(x + jx, y), art::terrainFeature(feature, size, v));
                    } else {
                        const int deco = art::decorationAt(int(x) / 11 + seed, int(y) / 13);
                        if (deco >= 0) p.drawPixmap(QPointF(x + jx + size * 0.3, y + size * 0.4), art::decoration(groundId, deco, size / 2));
                    }
                }
        };
        band(0, field.top() - r * 0.2, 0, width(), 1);
        if (!water) band(field.bottom() + r * 0.4, height(), 0, width(), 2);
        band(field.top(), field.bottom(), 0, field.left() - r * 0.2, 3);
        band(field.top(), field.bottom(), field.right() + r * 0.2, width(), 4);
        // Darker towards the edges of the widget, so the eye stays on the field.
        QRadialGradient shade(field.center(), std::max(width(), height()) * 0.75);
        shade.setColorAt(0.0, QColor(0, 0, 0, 40));
        shade.setColorAt(1.0, QColor(0, 0, 0, 150));
        p.fillRect(rect(), shade);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter screen(this);
        if (!battle.present) {
            screen.fillRect(rect(), QColor("#1f1810"));
            return;
        }
        // What stays the same all battle (surroundings, ground, hex lines, trees and
        // rocks) is painted once into m_background; each frame draws only the rest.
        const qreal dpr = devicePixelRatioF();
        QList<core::Pos> obstacles(battle.obstacles.cbegin(), battle.obstacles.cend());
        std::sort(obstacles.begin(), obstacles.end(), [](core::Pos a, core::Pos b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
        QString key = QStringLiteral("%1x%2@%3|%4x%5|%6|").arg(width()).arg(height()).arg(dpr).arg(battle.width).arg(battle.height).arg(battle.terrain);
        for (const core::Pos o : obstacles) key += QStringLiteral("%1,%2;").arg(o.x).arg(o.y);
        if (key != m_backgroundKey) {
            m_background = QPixmap(size() * dpr);
            m_background.setDevicePixelRatio(dpr);
            QPainter bp(&m_background);
            paintField(bp, true);
            m_backgroundKey = key;
        }
        screen.drawPixmap(0, 0, m_background);
        paintField(screen, false);
    }

    // The field: `background` = only the parts that do not change, else only the others.
    void paintField(QPainter& p, bool background) {
        if (background) {
            p.fillRect(rect(), QColor("#1f1810"));
            const double r0 = radius();
            const QPointF o = origin();
            const QRectF field(o.x() - r0 * 0.4, o.y() - r0 * 0.4, std::sqrt(3.0) * r0 * (battle.width + 0.5) + r0 * 0.8,
                               1.5 * r0 * (battle.height - 1) + 2 * r0 + r0 * 0.8);
            p.setRenderHint(QPainter::Antialiasing, true);
            drawSurroundings(p, field, r0);
            // A wooden frame around the field.
            p.setPen(QPen(QColor("#c9a24a"), 2));
            p.setBrush(QColor("#3a2a18"));
            p.drawRoundedRect(field.adjusted(-6, -6, 6, 6), 10, 10);
            p.setPen(QPen(QColor(0, 0, 0, 120), 4));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(field.adjusted(-1, -1, 1, 1), 6, 6);
        }
        p.setRenderHint(QPainter::Antialiasing, true);
        const double r = radius();
        const auto hexPath = [&](core::Pos h, double scale) {
            QPainterPath path;
            const QPointF c = centre(h);
            for (int i = 0; i < 6; ++i) {
                const double a = M_PI / 180.0 * (60 * i - 30);
                const QPointF pt(c.x() + r * scale * std::cos(a), c.y() + r * scale * std::sin(a));
                if (i == 0) path.moveTo(pt);
                else path.lineTo(pt);
            }
            path.closeSubpath();
            return path;
        };
        const bool mine = battle.myMove();
        // The ground of the map tile the battle is fought on (forests and hills give
        // their grass or earth; their trees and peaks are the obstacles instead).
        const QString where = battle.terrain >= 0 && battle.terrain < m_content->terrain().size()
                                  ? m_content->terrain(core::TerrainId(battle.terrain)).key : QStringLiteral("grass");
        static const QHash<QString, QString> ground = {{"forest", "grass"}, {"hills", "dirt"}, {"mountains", "dirt"},
                                                       {"road", "dirt"}, {"sea", "sand"}, {"river", "grass"}, {"lava", "dirt"}};
        const QString groundId = ground.value(where, where);
        // Whose range to show: the stack under the mouse if it shoots, else your active shooter.
        const ViewBattleUnit* ranger = battle.unitAt(hover);
        if (!ranger || ranger->range <= 0 || ranger->shots <= 0) {
            ranger = battle.unit(battle.active);
            if (!ranger || !mine || ranger->range <= 0 || !battle.canShoot) ranger = nullptr;
        }
        const bool enemyRange = ranger && ranger->side != battle.yourSide;
        // One continuous ground under the whole field (no tile seams at the hexes),
        // shaped to the hexes; then soft hex lines, lit hexes, small decorations.
        if (background) {
            QPainterPath fieldShape;
            for (int y = 0; y < battle.height; ++y)
                for (int x = 0; x < battle.width; ++x) fieldShape.addPath(hexPath({x, y}, 1.0));
            fieldShape.setFillRule(Qt::WindingFill);
            const QRectF area = fieldShape.boundingRect();
            const int tile = std::max(16, int(r * 2.2));
            p.save();
            p.setClipPath(fieldShape);
            for (double ty = area.top(); ty < area.bottom(); ty += tile)
                for (double tx = area.left(); tx < area.right(); tx += tile) {
                    const int ix = int((tx - area.left()) / tile), iy = int((ty - area.top()) / tile);
                    p.drawPixmap(QPointF(tx, ty), art::terrainTile(groundId, tile, art::variantFor(ix + 31, iy + 17)));
                }
            // A gentle vignette towards the edges of the field.
            QRadialGradient vignette(area.center(), std::max(area.width(), area.height()) * 0.62);
            vignette.setColorAt(0.6, QColor(0, 0, 0, 0));
            vignette.setColorAt(1.0, QColor(0, 0, 0, 90));
            p.fillRect(area, vignette);
            p.restore();
        }
        for (int y = 0; y < battle.height; ++y)
            for (int x = 0; x < battle.width; ++x) {
                const core::Pos h{x, y};
                const QPainterPath path = hexPath(h, 0.97);
                if (!background) {
                    if (mine && battle.reachable.contains(h)) p.fillPath(path, QColor(255, 240, 160, 70));
                    if (ranger && core::hex::distance(ranger->hex, h) <= ranger->range)
                        p.fillPath(path, enemyRange ? QColor(255, 40, 40, 45) : QColor(80, 150, 255, 40));
                    continue;
                }
                if (!battle.obstacles.contains(h) && r >= 14) {
                    const int variant = art::decorationAt(x * 3 + 7, y * 5 + 3);
                    const QPixmap& deco = variant >= 0 ? art::decoration(groundId, variant, int(r * 1.5)) : QPixmap();
                    if (!deco.isNull()) p.drawPixmap(centre(h) - QPointF(r * 0.75, r * 0.75), deco);
                }
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(QColor(255, 250, 220, 45), 1.2));
                p.drawPath(path);
                p.setPen(QPen(QColor(20, 30, 10, 60), 1));
                p.drawPath(hexPath(h, 0.95));
                if (battle.obstacles.contains(h) && (where == "forest" || where == "swamp")) {
                    // A tree clump (forest) or a reedy pool (swamp) instead of a rock.
                    const QPointF c = centre(h);
                    p.setPen(QPen(QColor(20, 30, 10), 1.5));
                    if (where == "forest") {
                        const int size = int(r * 2.3);
                        const QPixmap trees = art::terrainFeature("forest", size, art::variantFor(x, y));
                        if (!trees.isNull()) p.drawPixmap(c - QPointF(size, size * 1.6), trees);
                    } else {
                        p.setBrush(QColor("#3c5a6e"));
                        p.drawEllipse(c, r * 0.6, r * 0.38);
                        p.setPen(QPen(QColor("#9ab060"), 2));
                        for (int i = -2; i <= 2; ++i) p.drawLine(c + QPointF(i * r * 0.18, -r * 0.1), c + QPointF(i * r * 0.22, -r * 0.6));
                    }
                } else if (battle.obstacles.contains(h)) {
                    const QPointF c = centre(h);
                    p.setPen(QPen(QColor("#3a3a3a"), 1.5));
                    QRadialGradient g(c - QPointF(r * 0.2, r * 0.3), r);
                    g.setColorAt(0, where == "snow" ? QColor("#f4f6fa") : QColor("#b8b8b0"));
                    g.setColorAt(1, QColor("#5a5a55"));
                    p.setBrush(g);
                    QPolygonF rock;
                    for (int i = 0; i < 7; ++i) {
                        const double a = 2 * M_PI * i / 7;
                        const double rr = r * (0.55 + 0.15 * ((x * 7 + y * 3 + i) % 3) / 2.0);
                        rock << QPointF(c.x() + rr * std::cos(a), c.y() + rr * 0.75 * std::sin(a));
                    }
                    p.drawPolygon(rock);
                }
            }
        if (background) return;
        // A siege: the wall (cracking as it takes damage, rubble where it fell), the
        // wooden gate, and the towers behind it.
        if (battle.wallLevel > 0) {
            // The wall seen from the side: each segment a block of stone standing up
            // out of its hex (courses of stones, battlements, cracks as it weakens),
            // the gate a timbered door, rubble where a segment fell; drawn top down.
            const double wallH = r * (1.1 + 0.15 * battle.wallLevel);
            for (int y = 0; y < battle.height; ++y) {
                const core::Pos h{core::kWallColumn, y};
                const QPointF c = centre(h);
                const auto it = battle.walls.constFind(h);
                if (it == battle.walls.cend()) {  // rubble
                    p.setPen(QPen(QColor("#3a3a3a"), 1));
                    for (int i = 0; i < 7; ++i) {
                        p.setBrush(QColor("#9a968c").darker(100 + (i % 3) * 12));
                        p.drawEllipse(c + QPointF((i % 4 - 1.5) * r * 0.3, (i / 4 - 0.2) * r * 0.35), r * 0.2, r * 0.14);
                    }
                    continue;
                }
                const double health = battle.wallMaxHp > 0 ? double(it.value()) / battle.wallMaxHp : 1.0;
                const bool gate = h == battle.gate;
                const QRectF front(c.x() - r * 0.88, c.y() + r * 0.45 - wallH, r * 1.76, wallH);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0, 0, 0, 80));
                p.drawEllipse(QPointF(c.x() + r * 0.1, c.y() + r * 0.5), r * 0.95, r * 0.25);
                p.setPen(QPen(QColor("#2b2620"), 1.4));
                QLinearGradient stone(front.topLeft(), front.bottomRight());
                stone.setColorAt(0, QColor("#ddd6c4"));
                stone.setColorAt(1, QColor("#8a8476"));
                p.setBrush(stone);
                p.drawRect(front);
                // The top of the wall walk, slightly lighter, with battlements.
                p.setBrush(QColor("#e8e2d2"));
                p.drawRect(QRectF(front.left(), front.top() - r * 0.12, front.width(), r * 0.12));
                for (int k = 0; k < 4; ++k)
                    p.drawRect(QRectF(front.left() + k * front.width() / 3.5, front.top() - r * 0.34, front.width() / 7, r * 0.24));
                // Courses of stone.
                p.setPen(QPen(QColor(70, 66, 58, 200), 1));
                for (int row = 1; row < 4; ++row) {
                    const double ly = front.top() + row * front.height() / 4;
                    p.drawLine(QPointF(front.left(), ly), QPointF(front.right(), ly));
                    for (int k = 0; k < 3; ++k) {
                        const double lx = front.left() + (k + (row % 2) * 0.5 + 0.25) * front.width() / 3;
                        p.drawLine(QPointF(lx, ly - front.height() / 4), QPointF(lx, ly));
                    }
                }
                if (gate) {  // an arched timber door with iron bands
                    const QRectF door(c.x() - r * 0.5, front.bottom() - wallH * 0.75, r, wallH * 0.75);
                    QPainterPath arch;
                    arch.moveTo(door.bottomLeft());
                    arch.lineTo(door.left(), door.top() + door.width() / 2);
                    arch.arcTo(QRectF(door.left(), door.top(), door.width(), door.width()), 180, -180);
                    arch.lineTo(door.bottomRight());
                    arch.closeSubpath();
                    QLinearGradient wood(door.topLeft(), door.bottomRight());
                    wood.setColorAt(0, QColor("#9a6a34"));
                    wood.setColorAt(1, QColor("#4a3018"));
                    p.setPen(QPen(QColor("#2a1a0c"), 1.4));
                    p.setBrush(wood);
                    p.drawPath(arch);
                    p.setPen(QPen(QColor("#2a1a0c"), 1));
                    for (int k = 1; k < 4; ++k) p.drawLine(QPointF(door.left() + k * door.width() / 4, door.top() + r * 0.15), QPointF(door.left() + k * door.width() / 4, door.bottom()));
                    p.setPen(QPen(QColor("#505058"), std::max(1.5, r * 0.08)));
                    for (const double f : {0.45, 0.8}) p.drawLine(QPointF(door.left() + 2, door.top() + door.height() * f), QPointF(door.right() - 2, door.top() + door.height() * f));
                }
                // Cracks as it weakens.
                p.setPen(QPen(QColor(20, 18, 14), 1.6));
                if (health < 0.67) p.drawPolyline(QPolygonF{QPointF(front.left() + r * 0.4, front.top() + r * 0.1), QPointF(front.left() + r * 0.6, front.center().y()), QPointF(front.left() + r * 0.45, front.bottom() - r * 0.2)});
                if (health < 0.34) p.drawPolyline(QPolygonF{QPointF(front.right() - r * 0.4, front.top() + r * 0.2), QPointF(front.right() - r * 0.7, front.center().y()), QPointF(front.right() - r * 0.5, front.bottom() - r * 0.1)});
            }
            // The towers behind: round, with battlements, a cone roof and a flag.
            for (const core::Pos t : battle.towers) {
                const QPointF c = centre(t);
                const double th = r * 1.9, tw = r * 1.2;
                const QRectF body(c.x() - tw / 2, c.y() + r * 0.45 - th, tw, th);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0, 0, 0, 80));
                p.drawEllipse(QPointF(c.x() + r * 0.1, c.y() + r * 0.5), tw * 0.6, r * 0.22);
                p.setPen(QPen(QColor("#2b2620"), 1.4));
                QLinearGradient stone(body.topLeft(), body.topRight());
                stone.setColorAt(0, QColor("#e0dac8"));
                stone.setColorAt(0.6, QColor("#b8b0a0"));
                stone.setColorAt(1, QColor("#7a7466"));
                p.setBrush(stone);
                p.drawRect(body);
                p.setPen(QPen(QColor(70, 66, 58, 180), 1));
                for (int row = 1; row < 5; ++row) p.drawLine(QPointF(body.left(), body.top() + row * th / 5), QPointF(body.right(), body.top() + row * th / 5));
                p.setPen(QPen(QColor("#2b2620"), 1.4));
                p.setBrush(QColor("#2a2018"));  // an arrow slit
                p.drawRect(QRectF(c.x() - r * 0.05, body.top() + th * 0.35, r * 0.1, th * 0.2));
                p.setBrush(QColor("#c8c0ae"));
                p.drawRect(QRectF(body.left() - r * 0.08, body.top() - r * 0.1, tw + r * 0.16, r * 0.14));
                QPolygonF roof{QPointF(body.left() - r * 0.12, body.top() - r * 0.1), QPointF(c.x(), body.top() - r * 1.0),
                               QPointF(body.right() + r * 0.12, body.top() - r * 0.1)};
                QLinearGradient tile(roof[0], roof[2]);
                tile.setColorAt(0, sideColor[1].lighter(130));
                tile.setColorAt(1, sideColor[1].darker(140));
                p.setBrush(tile);
                p.drawPolygon(roof);
                p.drawLine(QPointF(c.x(), body.top() - r * 1.0), QPointF(c.x(), body.top() - r * 1.4));
                p.setBrush(sideColor[1]);
                p.drawPolygon(QPolygonF{QPointF(c.x(), body.top() - r * 1.4), QPointF(c.x() + r * 0.4, body.top() - r * 1.3), QPointF(c.x(), body.top() - r * 1.2)});
            }
        }
        if (ranger) {  // the edge of the range
            p.setPen(QPen(enemyRange ? QColor(255, 80, 80, 200) : QColor(120, 180, 255, 200), 2, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            for (int y = 0; y < battle.height; ++y)
                for (int x = 0; x < battle.width; ++x)
                    if (core::hex::distance(ranger->hex, {x, y}) == ranger->range) p.drawPath(hexPath({x, y}, 0.9));
        }
        if (intent == Intent::MoveAttack && strikeFrom.x >= 0 && hover.x >= 0) {
            // Where the stack will stand to strike, and the blow.
            p.setPen(QPen(QColor("#ffd84a"), 3));
            p.setBrush(QColor(255, 216, 74, 70));
            p.drawPath(hexPath(strikeFrom, 0.92));
            const QPointF a = centre(strikeFrom), b = centre(hover);
            p.setPen(QPen(QColor("#ffd84a"), 3, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(a, a + (b - a) * 0.55);
        }
        // Back rows first: the figures stand up out of their hexes.
        QList<const ViewBattleUnit*> order;
        for (const ViewBattleUnit& u : battle.units) order << &u;
        std::stable_sort(order.begin(), order.end(), [&](const ViewBattleUnit* a, const ViewBattleUnit* b) { return drawnAt(*a).y() < drawnAt(*b).y(); });
        for (const ViewBattleUnit* up : order) {
            const ViewBattleUnit& u = *up;
            const bool active = (u.id == battle.active);
            const bool target = mine && u.side != battle.yourSide && (battle.shootable.contains(u.id) || battle.attackable.contains(u.id));
            const core::UnitDef* def = m_content->unit(u.unit);
            const QPointF c = drawnAt(u);
            art::drawBattleFigure(p, c, r, sideColor[u.side], art::unitKey(u.unit), def ? def->rank : 0, u.count, u.stars, active, target, u.side == 1);
            if (u.maxHp > 0 && u.frontHp > 0 && u.frontHp < u.maxHp) {
                // The front unit is hurt: a small health bar under the base.
                const QRectF bar(c.x() - r * 0.6, c.y() + r * 0.76, r * 1.2, std::max(3.0, r * 0.12));
                p.setPen(QPen(QColor(20, 12, 4), 1));
                p.setBrush(QColor(40, 20, 20));
                p.drawRect(bar);
                const double f = double(u.frontHp) / u.maxHp;
                p.setPen(Qt::NoPen);
                p.setBrush(f > 0.5 ? QColor("#5ac85a") : f > 0.25 ? QColor("#e0b030") : QColor("#e04030"));
                p.drawRect(QRectF(bar.left() + 1, bar.top() + 1, (bar.width() - 2) * f, bar.height() - 2));
            }
            if (u.range > 0) {
                // A bow badge with the range in hexes (grey when out of arrows).
                const QPointF b(c.x() + (u.side == 1 ? r * 0.62 : -r * 0.62), c.y() + r * 0.1);  // on the base, away from the count
                const double br = std::max(6.0, r * 0.3);
                p.setPen(QPen(QColor("#20180c"), 1.2));
                p.setBrush(u.shots > 0 ? QColor("#2f5d9a") : QColor("#666666"));
                p.drawEllipse(b, br, br);
                p.setPen(QPen(QColor("#f1e4c0"), std::max(1.0, br / 6)));
                p.setBrush(Qt::NoBrush);
                p.drawArc(QRectF(b.x() - br * 0.75, b.y() - br * 0.7, br * 0.9, br * 1.4), -70 * 16, 140 * 16);
                QFont f = p.font();
                f.setPixelSize(std::max(7, int(br * 0.95)));
                f.setBold(true);
                p.setFont(f);
                p.drawText(QRectF(b.x() - br * 0.2, b.y() - br, br * 1.1, br * 2), Qt::AlignCenter, QString::number(u.range));
            }
        }
        // The action being shown.
        if (!m_fx.isEmpty()) {
            const Fx& f = m_fx.first();
            const double t = std::clamp(f.age / f.duration, 0.0, 1.0);
            const QPointF at = fxPos(f.target ? f.target : f.unit);
            if (f.type == "wall_hit") {
                // A stone flies in an arc, then dust where it lands.
                const QPointF from = fxPos(f.unit), to = centre(f.hex);
                if (t < 0.7) {
                    const double k = t / 0.7;
                    const QPointF pos = from + (to - from) * k - QPointF(0, std::sin(M_PI * k) * r * 3);
                    p.setPen(QPen(QColor(30, 30, 30), 1.2));
                    p.setBrush(QColor("#8a8476"));
                    p.drawEllipse(pos, r * 0.22, r * 0.22);
                } else {
                    const double k = (t - 0.7) / 0.3;
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor(200, 190, 170, int(200 * (1 - k))));
                    for (int i = 0; i < 7; ++i) {
                        const double a = i * M_PI * 2 / 7;
                        p.drawEllipse(to + QPointF(std::cos(a), std::sin(a)) * r * (0.3 + 0.7 * k), r * 0.2, r * 0.2);
                    }
                }
            }
            if (f.type == "engine") {
                p.setPen(QPen(QColor(255, 216, 74, int(255 * (1 - t))), 3));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(fxPos(f.unit), r * (0.5 + t), r * (0.5 + t));
            }
            if (f.type == "shoot" || f.type == "tower") {
                const QPointF from = f.type == "tower" ? f.from : fxPos(f.unit), to = fxPos(f.target);
                const QPointF pos = from + (to - from) * std::min(1.0, t / 0.75);
                if (t < 0.75) {
                    const double angle = std::atan2(to.y() - from.y(), to.x() - from.x()) * 180 / M_PI;
                    p.save();
                    p.translate(pos);
                    p.rotate(angle);
                    p.setPen(QPen(QColor(20, 12, 4), 3, Qt::SolidLine, Qt::RoundCap));
                    p.drawLine(QPointF(-r * 0.6, 0), QPointF(r * 0.3, 0));
                    p.setPen(QPen(QColor("#e8d8b0"), 1.6, Qt::SolidLine, Qt::RoundCap));
                    p.drawLine(QPointF(-r * 0.6, 0), QPointF(r * 0.3, 0));
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor("#d8dce8"));
                    p.drawPolygon(QPolygonF{QPointF(r * 0.45, 0), QPointF(r * 0.25, -r * 0.1), QPointF(r * 0.25, r * 0.1)});
                    p.setBrush(QColor("#c03a3a"));
                    p.drawPolygon(QPolygonF{QPointF(-r * 0.6, 0), QPointF(-r * 0.75, -r * 0.12), QPointF(-r * 0.5, 0), QPointF(-r * 0.75, r * 0.12)});
                    p.restore();
                }
            }
            if (((f.type == "shoot" || f.type == "tower") && t >= 0.7) || ((f.type == "attack" || f.type == "retaliate") && t >= 0.35 && t <= 0.85)) {
                // The hit: a bright ring and sparks on the target.
                const double k = (f.type == "shoot" || f.type == "tower") ? (t - 0.7) / 0.3 : (t - 0.35) / 0.5;
                p.setPen(QPen(QColor(255, 255, 255, int(230 * (1 - k))), 3));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(at, r * (0.6 + 0.5 * k), r * (0.6 + 0.5 * k));
                p.setPen(QPen(QColor(255, 210, 90, int(255 * (1 - k))), 2));
                for (int i = 0; i < 6; ++i) {
                    const double a = i * M_PI / 3 + 0.4;
                    p.drawLine(at + QPointF(std::cos(a), std::sin(a)) * r * (0.4 + 0.3 * k),
                               at + QPointF(std::cos(a), std::sin(a)) * r * (0.7 + 0.5 * k));
                }
            }
            if (f.type == "cast") {
                // Sparkles: hot colours for damage, green for healing, blue for the rest.
                const core::SpellDef* s = m_content->spell(f.spell);
                QColor c("#7ab8ff");
                for (const core::SpellEffect& e : s ? s->effects : QList<core::SpellEffect>()) {
                    if (e.type == core::SpellEffect::Type::Damage) c = QColor("#ff8a30");
                    if (e.type == core::SpellEffect::Type::Heal) c = QColor("#7aff8a");
                }
                p.setPen(Qt::NoPen);
                for (int i = 0; i < 10; ++i) {
                    const double a = i * M_PI / 5 + t * 3;
                    const double d = r * (0.2 + 0.9 * t);
                    QColor cc = c;
                    cc.setAlphaF(1 - t * 0.8);
                    p.setBrush(cc);
                    p.drawEllipse(at + QPointF(std::cos(a), std::sin(a)) * d, r * 0.1 * (1.2 - t), r * 0.1 * (1.2 - t));
                }
                QRadialGradient g(at, r * (0.4 + 0.6 * t));
                QColor glow = c;
                glow.setAlphaF(0.5 * (1 - t));
                g.setColorAt(0, glow);
                glow.setAlphaF(0);
                g.setColorAt(1, glow);
                p.setBrush(g);
                p.drawEllipse(at, r * (0.4 + 0.6 * t), r * (0.4 + 0.6 * t));
            }
            if (f.type == "morale" || f.type == "falter" || f.lucky) {
                const QString word = f.type == "morale" ? tr("Morale!") : f.type == "falter" ? tr("Falters...") : tr("Lucky!");
                QFont wf = p.font();
                wf.setPixelSize(std::max(12, int(r * 0.6)));
                wf.setBold(true);
                p.setFont(wf);
                const QPointF wp = fxPos(f.unit) - QPointF(QFontMetrics(wf).horizontalAdvance(word) / 2.0, r * (1.0 + t * 0.5));
                p.setPen(f.type == "falter" ? QColor("#c0c0d0") : QColor("#ffd84a"));
                p.drawText(wp, word);
            }
        }
        // Stacks wiped out fade; losses float up.
        for (const Ghost& g : m_ghosts) {
            const core::UnitDef* def = m_content->unit(g.unit.unit);
            p.setOpacity(std::max(0.0, 1.0 - g.age / 0.7));
            art::drawBattleFigure(p, centre(g.unit.hex) + QPointF(0, r * g.age * 0.4), r, sideColor[g.unit.side], art::unitKey(g.unit.unit),
                                  def ? def->rank : 0, 0, 0, false, false, g.unit.side == 1);
        }
        p.setOpacity(1.0);
        QFont ff = p.font();
        ff.setPixelSize(std::max(12, int(r * 0.7)));
        ff.setBold(true);
        p.setFont(ff);
        for (const Floater& f : m_floaters) {
            const QPointF at = f.at - QPointF(0, r * (0.6 + f.age * 1.2));
            QColor c = f.colour;
            c.setAlphaF(std::max(0.0, 1.0 - f.age / 1.2));
            QPainterPath text;
            text.addText(at - QPointF(QFontMetrics(ff).horizontalAdvance(f.text) / 2.0, 0), ff, f.text);
            p.setPen(QPen(QColor(0, 0, 0, c.alpha()), 3));
            p.setBrush(Qt::NoBrush);
            p.drawPath(text);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawPath(text);
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        const core::Pos h = hexAt(e->position());
        const core::Pos oldFrom = strikeFrom;
        const Intent oldIntent = intent;
        updateIntent(e->position());
        updatePointer();
        if (h == hover && oldFrom == strikeFrom && oldIntent == intent) return;
        if (h != hover) {
            hover = h;
            if (hovered) hovered(h);
        }
        update();
    }
    void leaveEvent(QEvent*) override {
        hover = {-1, -1};
        intent = Intent::None;
        strikeFrom = {-1, -1};
        if (hovered) hovered(hover);
        update();
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        const core::Pos h = hexAt(e->position());
        updateIntent(e->position());
        if (h.x >= 0 && clicked) clicked(h, intent == Intent::MoveAttack || intent == Intent::Attack ? strikeFrom : core::Pos{-1, -1});
    }

private:
    struct Floater {
        QPointF at;
        QString text;
        QColor colour;
        double age;
    };
    struct Ghost {
        ViewBattleUnit unit;
        double age;
    };
    struct Fx {
        QString type;
        core::SpellId spell;
        int unit = 0, target = 0;
        core::Pos hex;
        bool lucky = false;
        double age = 0, duration = 0;
        QPointF from;  // towers: where the arrow comes from
    };
    QList<Fx> m_fx;
    QJsonArray m_pendingEvents;
    QHash<int, QPointF> m_fxPos;
    const core::ContentRegistry* m_content;
    QTimer m_anim;
    QPixmap m_background;  // the unchanging part of the field (see paintEvent)
    QString m_backgroundKey;
    QElapsedTimer m_clock;
    QHash<int, QPointF> m_slideFrom;
    double m_slide = 1.0;
    QList<Floater> m_floaters;
    QList<Ghost> m_ghosts;
};

BattleView::BattleView(const core::ContentRegistry* content, QWidget* parent)
    : QWidget(parent), m_content(content) {
    auto* layout = new QVBoxLayout(this);
    m_info = new QLabel(this);
    m_info->setObjectName("battleInfo");
    layout->addWidget(m_info);
    m_field = new HexField(content, this);
    m_field->setObjectName("battleField");
    m_field->clicked = [this](core::Pos h, core::Pos from) { onHexClicked(h, from); };
    m_field->hovered = [this](core::Pos h) {
        m_hoverUnit = m_battle.unitAt(h) ? m_battle.unitAt(h)->id : 0;
        updateStats();
    };
    auto* middle = new QHBoxLayout();
    middle->addWidget(m_field, 1);
    m_stats = new QLabel(this);
    m_stats->setObjectName("battleStats");
    m_stats->setTextFormat(Qt::RichText);
    m_stats->setWordWrap(true);
    m_stats->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_stats->setFixedWidth(230);
    m_stats->setStyleSheet("QLabel#battleStats { background: rgba(0,0,0,70); border: 1px solid #7a6235; border-radius: 6px; padding: 8px; }");
    middle->addWidget(m_stats);
    layout->addLayout(middle, 1);
    auto* buttons = new QHBoxLayout();
    m_defend = new QPushButton(tr("Defend (D)"), this);
    m_defend->setObjectName("defendButton");
    m_auto = new QPushButton(tr("Finish automatically"), this);
    m_auto->setToolTip(tr("The computer plays your side to the end at once"));
    m_autoPlay = new QPushButton(tr("Auto-play (A)"), this);
    m_autoPlay->setObjectName("autoPlayButton");
    m_autoPlay->setCheckable(true);
    m_autoPlay->setToolTip(tr("The computer plays your stacks move by move, so you can watch; A or this button stops it"));
    m_auto->setObjectName("autoBattleButton");
    m_cast = new QPushButton(tr("Cast spell..."), this);
    m_cast->setObjectName("castSpellButton");
    buttons->addWidget(m_cast);
    buttons->addWidget(m_defend);
    m_wait = new QPushButton(tr("Wait (W)"), this);
    m_wait->setObjectName("waitButton");
    m_wait->setToolTip(tr("Act again at the end of this round"));
    buttons->addWidget(m_wait);
    m_retreat = new QPushButton(tr("Retreat"), this);
    m_retreat->setObjectName("retreatButton");
    m_retreat->setToolTip(tr("Give up the battle: your army is lost, your hero escapes"));
    buttons->addWidget(m_retreat);
    m_surrender = new QPushButton(tr("Surrender"), this);
    m_surrender->setObjectName("surrenderButton");
    m_surrender->setIcon(art::buttonIcon("buy"));
    m_surrender->setIconSize(QSize(22, 22));
    m_surrender->setToolTip(tr("Pay the enemy and end the battle: your hero and your army stay alive"));
    buttons->addWidget(m_surrender);
    connect(m_surrender, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, tr("Surrender"), tr("Pay %1 gold to the enemy and end the battle? Your hero and army stay alive.")
                                                              .arg(m_battle.surrenderCost)) == QMessageBox::Yes)
            emit actionRequested({{"action", "surrender"}});
    });
    m_engine = new QPushButton(this);
    m_engine->setObjectName("buildEngineButton");
    m_engine->setToolTip(tr("A catapult for this battle: it breaks the town's walls, and is gone afterwards"));
    connect(m_engine, &QPushButton::clicked, this, [this]() { emit actionRequested({{"action", "build_engine"}}); });
    m_engine->setIcon(art::buttonIcon("catapult"));
    m_defend->setIcon(art::buttonIcon("defend"));
    m_wait->setIcon(art::buttonIcon("wait"));
    m_retreat->setIcon(art::buttonIcon("retreat"));
    m_auto->setIcon(art::buttonIcon("auto"));
    m_autoPlay->setIcon(art::buttonIcon("auto"));
    m_cast->setIcon(art::buttonIcon("spell"));
    for (QPushButton* b : {m_engine, m_defend, m_wait, m_retreat, m_auto, m_autoPlay, m_cast}) b->setIconSize(QSize(22, 22));
    buttons->addWidget(m_engine);
    buttons->addWidget(m_autoPlay);
    buttons->addWidget(m_auto);
    buttons->addStretch(1);
    layout->addLayout(buttons);
    connect(m_defend, &QPushButton::clicked, this, [this]() { emit actionRequested({{"action", "defend"}}); });
    connect(m_auto, &QPushButton::clicked, this, [this]() { emit actionRequested({{"action", "auto"}}); });
    connect(m_cast, &QPushButton::clicked, this, [this]() { chooseSpell(false); });
    m_unitCast = new QPushButton(tr("Stack's spell..."), this);
    m_unitCast->setObjectName("unitCastButton");
    m_unitCast->setIcon(art::buttonIcon("spell"));
    m_unitCast->setIconSize(QSize(22, 22));
    buttons->insertWidget(1, m_unitCast);
    connect(m_unitCast, &QPushButton::clicked, this, [this]() { chooseSpell(true); });
    connect(m_wait, &QPushButton::clicked, this, [this]() { emit actionRequested({{"action", "wait"}}); });
    connect(m_autoPlay, &QPushButton::toggled, this, [this](bool on) {
        m_autoPlay->setText(on ? tr("Stop auto-play (A)") : tr("Auto-play (A)"));
        if (on) autoStep();
    });
    // Keys: D defend, W wait, A auto-play on/off.
    {
        auto* a = new QShortcut(QKeySequence(Qt::Key_A), this);
        a->setContext(Qt::WindowShortcut);
        connect(a, &QShortcut::activated, this, [this]() {
            if (isVisible()) m_autoPlay->toggle();
        });
    }
    for (const auto& [key, button] : {std::pair{Qt::Key_D, m_defend}, std::pair{Qt::Key_W, m_wait}}) {
        auto* s = new QShortcut(QKeySequence(key), this);
        s->setContext(Qt::WindowShortcut);
        QPushButton* b = button;
        connect(s, &QShortcut::activated, this, [this, b]() {
            if (isVisible() && b->isEnabled()) b->click();
        });
    }
    connect(m_retreat, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, tr("Retreat"), tr("Retreat? Your army will be lost, but your hero escapes."))
            == QMessageBox::Yes)
            emit actionRequested({{"action", "retreat"}});
    });
}

QWidget* BattleView::field() const { return m_field; }

void BattleView::playEvents(const QJsonArray& events) {
    m_field->queueEvents(events);
}

QPoint BattleView::hexCentre(core::Pos hex) const {
    return m_field->centre(hex).toPoint();
}

void BattleView::setBattle(const ViewBattle& battle, const QColor& attacker, const QColor& defender) {
    m_battle = battle;
    m_field->setBattleAnimated(battle);
    m_field->sideColor[0] = attacker;
    m_field->sideColor[1] = defender;
    if (!battle.myMove()) m_castSpell = core::SpellId();
    if (!battle.unit(m_hoverUnit)) m_hoverUnit = 0;
    autoStep();
    updateInfo();
    updateStats();
    m_field->update();
}

void BattleView::battleEnded() {
    // Auto-play never carries over into the next battle.
    m_autoPlay->setChecked(false);
    m_battle = ViewBattle();
    m_field->setBattleAnimated(ViewBattle());
}

void BattleView::autoStep() {
    // Auto-play: when it is your stack's turn, let the battle AI move it, after a
    // pause so the last action can be seen.
    if (!m_autoPlay->isChecked() || !m_battle.myMove() || m_autoPending) return;
    m_autoPending = true;
    QTimer::singleShot(650, this, [this]() {
        m_autoPending = false;
        if (m_autoPlay->isChecked() && m_battle.myMove()) emit actionRequested({{"action", "ai"}});
    });
}

void BattleView::updateStats() {
    // The stack under the mouse, else the one whose turn it is.
    const ViewBattleUnit* u = m_battle.unit(m_hoverUnit);
    if (!u) u = m_battle.unit(m_battle.active);
    const core::UnitDef* d = u ? m_content->unit(u->unit) : nullptr;
    if (!u || !d) {
        m_stats->clear();
        return;
    }
    static const char* ranks[] = {"regular", "advanced", "elite"};
    const QString side = u->side == m_battle.yourSide ? tr("yours") : tr("enemy");
    QString t = tr("<b style='font-size:13pt'>%1</b><br><i>%2, %3</i>%4<hr>")
                    .arg(d->name.toHtmlEscaped(), tr(ranks[std::clamp(d->rank, 0, 2)]), side,
                         u->id == m_battle.active ? tr(" &mdash; <b>acting</b>") : QString());
    t += tr("Units: <b>%1</b> &nbsp; front HP %2 / %3<br>").arg(u->count).arg(u->frontHp).arg(u->maxHp);
    t += tr("Attack <b>%1</b> &nbsp; Defense <b>%2</b><br>").arg(d->attack).arg(d->defense);
    t += tr("Damage <b>%1-%2</b> per unit<br>").arg(d->damageMin).arg(d->damageMax);
    t += tr("Speed <b>%1</b> &nbsp; Initiative <b>%2</b><br>").arg(d->battleSpeed).arg(d->initiative);
    if (d->has(core::Ability::SiegeEngine)) t += tr("Hurls stones at the walls (%1-%2 damage); gone after the battle<br>")
                                                         .arg(m_content->balance().siege.catapultDamageMin).arg(m_content->balance().siege.catapultDamageMax);
    else if (u->range > 0) t += tr("Shoots: range <b>%1</b> hexes, %2 shots left<br>").arg(u->range).arg(u->shots);
    else t += tr("Melee only<br>");
    if (u->stars > 0) t += tr("Veteran: %1<br>").arg(QString(u->stars, QChar(0x2605)));
    if (d->abilities) t += tr("Abilities: %1<br>").arg(core::abilityNames(d->abilities).join(", "));
    if (u->retaliated) t += tr("<i>Has struck back this round</i><br>");
    if (u->range > 0 && u->side != m_battle.yourSide) t += tr("<br><span style='color:#ff8080'>Red hexes: where it can shoot</span>");
    m_stats->setText(t);
}

void BattleView::updateInfo() {
    const ViewBattle& battle = m_battle;
    const bool mine = battle.myMove();
    const ViewBattleUnit* active = battle.unit(battle.active);
    const core::UnitDef* def = active ? m_content->unit(active->unit) : nullptr;
    QString text = tr("<b>Battle, round %1</b> &nbsp; ").arg(battle.round);
    if (!active) {
        text += tr("waiting...");
    } else if (mine && m_castSpell.isValid()) {
        const core::SpellDef* s = m_content->spell(m_castSpell);
        bool enemy = true;
        for (const ViewBattle::Spell& sp : m_castByUnit ? battle.unitSpells : battle.spells)
            if (sp.id == m_castSpell) enemy = sp.targetsEnemy;
        text += tr("Cast %1: click %2 (or an empty hex to cancel).")
                    .arg(s ? s->name : QString(), enemy ? tr("an enemy stack") : tr("one of your stacks"));
    } else if (mine && def && def->has(core::Ability::SiegeEngine)) {
        text += tr("Your catapult: click a wall segment to hurl a stone at it.");
    } else if (mine) {
        text += tr("Your %1 (%2) acts: click a light hex to move, a red-ringed enemy to %3.")
                    .arg(def ? def->name : QString())
                    .arg(active->count)
                    .arg(!battle.shootable.isEmpty() ? tr("shoot (blue hexes: in range)") : tr("attack"));
    } else {
        text += tr("The enemy is acting...");
    }
    if (battle.hasHero) text += tr(" &nbsp; <i>Hero: %1 spell points</i>").arg(battle.heroSpellPoints);
    if (battle.townDefense > 0) text += tr(" &nbsp; <i>Town walls: defenders +%1 defense</i>").arg(battle.townDefense);
    if (battle.wallLevel > 0) text += tr(" &nbsp; <i>Siege: %1 of %2 wall segments stand, %3 towers shoot each round</i>")
                                          .arg(battle.walls.size()).arg(battle.height).arg(battle.towers.size());
    m_info->setText(text);
    m_defend->setEnabled(mine);
    m_wait->setEnabled(mine && battle.canWait);
    m_retreat->setVisible(battle.canRetreat || battle.yourSide == 0);
    m_retreat->setEnabled(mine && battle.canRetreat);
    m_surrender->setVisible(mine && battle.surrenderCost >= 0);
    m_surrender->setText(tr("Surrender (%1 gold)").arg(battle.surrenderCost));
    m_auto->setEnabled(mine);
    {
        QStringList cost;
        const core::ResourceAmounts& c = m_content->balance().siege.engineCost;
        for (const auto [r, amount] : c) cost << QStringLiteral("%1 %2").arg(amount).arg(m_content->resource(r).key);
        m_engine->setText(tr("Build a catapult (%1)").arg(cost.join(", ")));
        m_engine->setVisible(battle.canBuildEngine);
        m_engine->setEnabled(mine);
    }
    bool anyCastable = false;
    for (const ViewBattle::Spell& sp : battle.spells) anyCastable = anyCastable || sp.castable;
    m_cast->setVisible(battle.hasHero);
    m_cast->setEnabled(mine && anyCastable);
    bool unitCastable = false;
    for (const ViewBattle::Spell& sp : battle.unitSpells) unitCastable = unitCastable || sp.castable;
    m_unitCast->setVisible(mine && !battle.unitSpells.isEmpty());
    m_unitCast->setEnabled(unitCastable);
}

void BattleView::chooseSpell(bool unitSpell) {
    QList<SpellBook::Entry> entries;
    for (const ViewBattle::Spell& sp : unitSpell ? m_battle.unitSpells : m_battle.spells)
        entries.append({sp.id, sp.castable, sp.castable ? QString() : tr("Not now (spell points, or already cast this round)")});
    if (entries.isEmpty()) return;
    const core::SpellId chosen = SpellBook::chooseSpell(m_content, entries, unitSpell ? tr("The stack's spells") : tr("Your hero's spell book"), this);
    if (!chosen.isValid()) return;
    m_castSpell = chosen;
    m_castByUnit = unitSpell;
    updateInfo();
}

void BattleView::onHexClicked(core::Pos hex, core::Pos from) {
    if (!m_battle.myMove()) return;
    if (m_castSpell.isValid()) {
        const core::SpellId spell = m_castSpell;
        m_castSpell = core::SpellId();
        if (const ViewBattleUnit* u = m_battle.unitAt(hex))
            emit actionRequested({{"action", m_castByUnit ? "unit_cast" : "cast"}, {"spell", spell.index()}, {"target", u->id}});
        updateInfo();
        return;
    }
    const ViewBattleUnit* active = m_battle.unit(m_battle.active);
    const core::UnitDef* activeDef = active ? m_content->unit(active->unit) : nullptr;
    if (activeDef && activeDef->has(core::Ability::SiegeEngine)) {
        if (m_battle.walls.contains(hex)) emit actionRequested({{"action", "shoot"}, {"x", hex.x}, {"y", hex.y}});
        return;
    }
    if (const ViewBattleUnit* u = m_battle.unitAt(hex)) {
        if (u->side != m_battle.yourSide && m_battle.shootable.contains(u->id))
            emit actionRequested({{"action", "shoot"}, {"target", u->id}});
        else if (u->side != m_battle.yourSide && m_battle.attackable.contains(u->id)) {
            QJsonObject body{{"action", "attack"}, {"target", u->id}};
            if (from.x >= 0) {  // strike from the hex the pointer chose
                body.insert("x", from.x);
                body.insert("y", from.y);
            }
            emit actionRequested(body);
        }
        return;
    }
    if (m_battle.reachable.contains(hex)) emit actionRequested({{"action", "move"}, {"x", hex.x}, {"y", hex.y}});
}

}  // namespace kot::client
