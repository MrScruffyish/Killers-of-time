#include "client/BattleView.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

#include "client/Art.h"
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
    std::function<void(core::Pos)> clicked;

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
        // Grass hexes with a little texture; reachable ones lit, rocks grey.
        for (int y = 0; y < battle.height; ++y)
            for (int x = 0; x < battle.width; ++x) {
                const core::Pos h{x, y};
                const QPainterPath path = hexPath(h, 0.97);
                const QRectF box = path.boundingRect();
                p.save();
                p.setClipPath(path);
                p.drawPixmap(box.topLeft(), art::terrainTile(QStringLiteral("grass"), QColor("#5d8a3a"), int(box.width()) + 2,
                                                             art::variantFor(x, y)));
                p.restore();
                if (mine && battle.reachable.contains(h)) p.fillPath(path, QColor(255, 240, 160, 70));
                p.setPen(QPen(QColor(30, 45, 20, 160), 1));
                p.setBrush(Qt::NoBrush);
                p.drawPath(path);
                if (battle.obstacles.contains(h)) {
                    const QPointF c = centre(h);
                    p.setPen(QPen(QColor("#3a3a3a"), 1.5));
                    QRadialGradient g(c - QPointF(r * 0.2, r * 0.3), r);
                    g.setColorAt(0, QColor("#b8b8b0"));
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
        for (const ViewBattleUnit& u : battle.units) {
            const bool active = (u.id == battle.active);
            const bool target = mine && u.side != battle.yourSide && (battle.canShoot || battle.attackable.contains(u.id));
            const core::UnitDef* def = m_content->unit(u.unit);
            art::drawUnitToken(p, centre(u.hex), r * 0.78, sideColor[u.side], def ? def->name : u.unit, u.count, u.stars,
                               active, target);
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        const core::Pos h = hexAt(e->position());
        if (h.x >= 0 && clicked) clicked(h);
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
    m_field->clicked = [this](core::Pos h) { onHexClicked(h); };
    layout->addWidget(m_field, 1);
    auto* buttons = new QHBoxLayout();
    m_defend = new QPushButton(tr("Defend (skip)"), this);
    m_defend->setObjectName("defendButton");
    m_auto = new QPushButton(tr("Auto: let the computer finish this battle"), this);
    m_auto->setObjectName("autoBattleButton");
    m_cast = new QPushButton(tr("Cast spell..."), this);
    m_cast->setObjectName("castSpellButton");
    buttons->addWidget(m_cast);
    buttons->addWidget(m_defend);
    m_wait = new QPushButton(tr("Wait"), this);
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
    connect(m_cast, &QPushButton::clicked, this, &BattleView::chooseSpell);
    connect(m_wait, &QPushButton::clicked, this, [this]() { emit actionRequested({{"action", "wait"}}); });
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
    updateInfo();
    m_field->update();
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
        for (const ViewBattle::Spell& sp : battle.spells) if (sp.id == m_castSpell) enemy = sp.targetsEnemy;
        text += tr("Cast %1: click %2 (or an empty hex to cancel).")
                    .arg(s ? s->name : m_castSpell, enemy ? tr("an enemy stack") : tr("one of your stacks"));
    } else if (mine) {
        text += tr("Your %1 (%2) acts: click a light hex to move, a red-ringed enemy to %3.")
                    .arg(def ? def->name : active->unit)
                    .arg(active->count)
                    .arg(battle.canShoot ? tr("shoot") : tr("attack"));
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
}

void BattleView::chooseSpell() {
    QStringList names;
    QStringList ids;
    for (const ViewBattle::Spell& sp : m_battle.spells) {
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
    updateInfo();
}

void BattleView::onHexClicked(core::Pos hex) {
    if (!m_battle.myMove()) return;
    if (!m_castSpell.isEmpty()) {
        const QString spell = m_castSpell;
        m_castSpell.clear();
        if (const ViewBattleUnit* u = m_battle.unitAt(hex))
            emit actionRequested({{"action", "cast"}, {"spell", spell}, {"target", u->id}});
        updateInfo();
        return;
    }
    if (const ViewBattleUnit* u = m_battle.unitAt(hex)) {
        if (u->side != m_battle.yourSide && (m_battle.canShoot || m_battle.attackable.contains(u->id))) {
            emit actionRequested({{"action", m_battle.canShoot ? "shoot" : "attack"}, {"target", u->id}});
        }
        return;
    }
    if (m_battle.reachable.contains(hex)) emit actionRequested({{"action", "move"}, {"x", hex.x}, {"y", hex.y}});
}

}  // namespace kot::client
