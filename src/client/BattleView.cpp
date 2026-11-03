#include "client/BattleView.h"

#include <QCursor>
#include <QHBoxLayout>
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
#include <cmath>

#include "client/Art.h"
#include "core/battle/HexGrid.h"
#include "core/content/ContentRegistry.h"

namespace kot::client {

// Paints pointy-top hexes in "odd-r" layout, scaled to fit.
class HexField : public QWidget {
public:
    HexField(const core::ContentRegistry* content, QWidget* parent) : QWidget(parent), m_content(content) {
        setMinimumSize(400, 280);
        setMouseTracking(true);
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
        const double byWidth = (width() - 8) / (std::sqrt(3.0) * (w + 0.5));
        const double byHeight = (height() - 8) / (1.5 * (h - 1) + 2.0);
        return std::max(4.0, std::min(byWidth, byHeight));
    }
    QPointF centre(core::Pos p) const {
        const double r = radius();
        return {4 + std::sqrt(3.0) * r * (p.x + 0.5 + 0.5 * (p.y & 1)), 4 + r + 1.5 * r * p.y};
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

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#1f1810"));
        if (!battle.present) return;
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
        const QString where = battle.terrain.isEmpty() ? QStringLiteral("grass") : battle.terrain;
        static const QHash<QString, QString> ground = {{"forest", "grass"}, {"hills", "dirt"}, {"mountains", "dirt"},
                                                       {"road", "dirt"}, {"sea", "sand"}, {"river", "grass"}, {"lava", "dirt"}};
        const QString groundId = ground.value(where, where);
        const core::TerrainDef* gt = m_content->terrainType(groundId);
        const core::TerrainDef* wt = m_content->terrainType(where);
        QColor groundColour = gt ? QColor(gt->color) : QColor("#5d8a3a");
        if (where == "forest" && wt) groundColour = QColor(wt->color).lighter(125);
        // Whose range to show: the stack under the mouse if it shoots, else your active shooter.
        const ViewBattleUnit* ranger = battle.unitAt(hover);
        if (!ranger || ranger->range <= 0 || ranger->shots <= 0) {
            ranger = battle.unit(battle.active);
            if (!ranger || !mine || ranger->range <= 0 || !battle.canShoot) ranger = nullptr;
        }
        const bool enemyRange = ranger && ranger->side != battle.yourSide;
        // Grass hexes with a little texture; reachable ones lit, rocks grey.
        for (int y = 0; y < battle.height; ++y)
            for (int x = 0; x < battle.width; ++x) {
                const core::Pos h{x, y};
                const QPainterPath path = hexPath(h, 0.97);
                const QRectF box = path.boundingRect();
                p.save();
                p.setClipPath(path);
                p.drawPixmap(box.topLeft(), art::terrainTile(groundId, groundColour, int(box.width()) + 2, art::variantFor(x, y)));
                p.restore();
                if (mine && battle.reachable.contains(h)) p.fillPath(path, QColor(255, 240, 160, 70));
                if (ranger && core::hex::distance(ranger->hex, h) <= ranger->range)
                    p.fillPath(path, enemyRange ? QColor(255, 40, 40, 45) : QColor(80, 150, 255, 40));
                p.setPen(QPen(QColor(30, 45, 20, 160), 1));
                p.setBrush(Qt::NoBrush);
                p.drawPath(path);
                if (battle.obstacles.contains(h) && (where == "forest" || where == "swamp")) {
                    // A tree (forest) or a reedy pool (swamp) instead of a rock.
                    const QPointF c = centre(h);
                    p.setPen(QPen(QColor(20, 30, 10), 1.5));
                    if (where == "forest") {
                        p.setBrush(QColor(0, 0, 0, 70));
                        p.drawEllipse(c + QPointF(0, r * 0.55), r * 0.7, r * 0.25);
                        p.setBrush(QColor("#5b3a1e"));
                        p.drawRect(QRectF(c.x() - r * 0.1, c.y() + r * 0.2, r * 0.2, r * 0.4));
                        for (int i = 0; i < 3; ++i) {  // three layers of a fir, widest at the bottom
                            const double tw = r * (1.5 - 0.35 * i), base = c.y() + r * (0.3 - 0.42 * i);
                            p.setBrush(QColor("#3f8a36").lighter(100 + 10 * i));
                            p.drawPolygon(QPolygonF{QPointF(c.x(), base - r * 0.75), QPointF(c.x() + tw * 0.5, base),
                                                    QPointF(c.x() - tw * 0.5, base)});
                        }
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
        for (const ViewBattleUnit& u : battle.units) {
            const bool active = (u.id == battle.active);
            const bool target = mine && u.side != battle.yourSide && (battle.shootable.contains(u.id) || battle.attackable.contains(u.id));
            const core::UnitDef* def = m_content->unit(u.unit);
            const QPointF c = centre(u.hex);
            art::drawUnitToken(p, c, r * 0.78, sideColor[u.side], def ? def->name : u.unit, u.count, u.stars, active, target,
                               u.unit, def ? def->rank : 0);
            if (u.range > 0) {
                // A bow badge with the range in hexes (grey when out of arrows).
                const QPointF b(c.x() + r * 0.62, c.y() - r * 0.55);
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
    const core::ContentRegistry* m_content;
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
    m_auto = new QPushButton(tr("Auto: let the computer finish this battle"), this);
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
    buttons->addWidget(m_auto);
    buttons->addStretch(1);
    layout->addLayout(buttons);
    connect(m_defend, &QPushButton::clicked, this, [this]() { emit actionRequested({{"action", "defend"}}); });
    connect(m_auto, &QPushButton::clicked, this, [this]() { emit actionRequested({{"action", "auto"}}); });
    connect(m_cast, &QPushButton::clicked, this, [this]() { chooseSpell(false); });
    m_unitCast = new QPushButton(tr("Stack's spell..."), this);
    m_unitCast->setObjectName("unitCastButton");
    buttons->insertWidget(1, m_unitCast);
    connect(m_unitCast, &QPushButton::clicked, this, [this]() { chooseSpell(true); });
    connect(m_wait, &QPushButton::clicked, this, [this]() { emit actionRequested({{"action", "wait"}}); });
    // Keys: D defend, W wait.
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

QPoint BattleView::hexCentre(core::Pos hex) const {
    return m_field->centre(hex).toPoint();
}

void BattleView::setBattle(const ViewBattle& battle, const QColor& attacker, const QColor& defender) {
    m_battle = battle;
    m_field->battle = battle;
    m_field->sideColor[0] = attacker;
    m_field->sideColor[1] = defender;
    if (!battle.myMove()) m_castSpell.clear();
    if (!battle.unit(m_hoverUnit)) m_hoverUnit = 0;
    updateInfo();
    updateStats();
    m_field->update();
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
    if (u->range > 0) t += tr("Shoots: range <b>%1</b> hexes, %2 shots left<br>").arg(u->range).arg(u->shots);
    else t += tr("Melee only<br>");
    if (u->stars > 0) t += tr("Veteran: %1<br>").arg(QString(u->stars, QChar(0x2605)));
    if (!d->abilities.isEmpty()) t += tr("Abilities: %1<br>").arg(d->abilities.join(", ").replace('_', ' '));
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
    } else if (mine && !m_castSpell.isEmpty()) {
        const core::SpellDef* s = m_content->spell(m_castSpell);
        bool enemy = true;
        for (const ViewBattle::Spell& sp : m_castByUnit ? battle.unitSpells : battle.spells)
            if (sp.id == m_castSpell) enemy = sp.targetsEnemy;
        text += tr("Cast %1: click %2 (or an empty hex to cancel).")
                    .arg(s ? s->name : m_castSpell, enemy ? tr("an enemy stack") : tr("one of your stacks"));
    } else if (mine) {
        text += tr("Your %1 (%2) acts: click a light hex to move, a red-ringed enemy to %3.")
                    .arg(def ? def->name : active->unit)
                    .arg(active->count)
                    .arg(!battle.shootable.isEmpty() ? tr("shoot (blue hexes: in range)") : tr("attack"));
    } else {
        text += tr("The enemy is acting...");
    }
    if (battle.hasHero) text += tr(" &nbsp; <i>Hero: %1 spell points</i>").arg(battle.heroSpellPoints);
    if (battle.townDefense > 0) text += tr(" &nbsp; <i>Town walls: defenders +%1 defense</i>").arg(battle.townDefense);
    m_info->setText(text);
    m_defend->setEnabled(mine);
    m_wait->setEnabled(mine && battle.canWait);
    m_retreat->setVisible(battle.yourSide == 0);
    m_retreat->setEnabled(mine && battle.canRetreat);
    m_auto->setEnabled(mine);
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
    QStringList names;
    QStringList ids;
    for (const ViewBattle::Spell& sp : unitSpell ? m_battle.unitSpells : m_battle.spells) {
        if (!sp.castable) continue;
        const core::SpellDef* s = m_content->spell(sp.id);
        names << tr("%1 (%2 SP)").arg(s ? s->name : sp.id).arg(sp.cost);
        ids << sp.id;
    }
    if (names.isEmpty()) return;
    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, tr("Cast spell"), tr("Spell:"), names, 0, false, &ok);
    if (!ok) return;
    m_castSpell = ids[names.indexOf(chosen)];
    m_castByUnit = unitSpell;
    updateInfo();
}

void BattleView::onHexClicked(core::Pos hex, core::Pos from) {
    if (!m_battle.myMove()) return;
    if (!m_castSpell.isEmpty()) {
        const QString spell = m_castSpell;
        m_castSpell.clear();
        if (const ViewBattleUnit* u = m_battle.unitAt(hex))
            emit actionRequested({{"action", m_castByUnit ? "unit_cast" : "cast"}, {"spell", spell}, {"target", u->id}});
        updateInfo();
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
