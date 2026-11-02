#include "core/battle/Battle.h"

#include <QJsonArray>
#include <QQueue>
#include <algorithm>
#include <cmath>

#include "core/content/ContentRegistry.h"

namespace kot::core {

Battle::Battle(const GameState& state, const ContentRegistry& content, ArmyId attacker, ArmyId defender,
               std::uint64_t seed)
    : m_content(content), m_rng(seed) {
    const ArmyId ids[2] = {attacker, defender};
    for (int side = 0; side < 2; ++side) {
        m_army[side] = ids[side];
        const Army* army = state.army(ids[side]);
        if (!army) continue;
        m_owner[side] = army->owner;
        m_hero[side] = army->hero;
        if (const Hero* h = state.hero(army->hero)) {
            m_attackBonus[side] = h->trait(QStringLiteral("attack"));
            m_defenseBonus[side] = h->trait(QStringLiteral("defense"));
        }
        // Up to 7 stacks over the start column, from the middle outwards.
        static const int rows[] = {5, 3, 7, 1, 9, 0, 10};
        int placed = 0;
        for (int i = 0; i < army->stacks.size(); ++i) {
            const UnitStack& s = army->stacks[i];
            const UnitDef* def = content.unit(s.unitTypeId());
            if (!def || s.isEliminated()) continue;
            BattleUnit u;
            u.id = static_cast<int>(m_units.size()) + 1;
            u.side = side;
            u.armyIndex = i;
            u.def = def;
            u.stack = s;
            u.pos = Pos{side == 0 ? 0 : hex::kWidth - 1, rows[placed++ % 7]};
            u.shotsLeft = def->shots;
            m_units.append(u);
        }
    }
    checkOver();
    if (!isOver()) startRound(nullptr);
}

const BattleUnit* Battle::unit(int id) const {
    for (const BattleUnit& u : m_units)
        if (u.id == id) return &u;
    return nullptr;
}

BattleUnit* Battle::mutableUnit(int id) {
    for (BattleUnit& u : m_units)
        if (u.id == id) return &u;
    return nullptr;
}

const BattleUnit* Battle::unitAt(Pos h) const {
    for (const BattleUnit& u : m_units)
        if (u.alive() && u.pos == h) return &u;
    return nullptr;
}

const BattleUnit* Battle::activeUnit() const {
    if (isOver() || m_orderPos >= m_order.size()) return nullptr;
    return unit(m_order[m_orderPos]);
}

bool Battle::sideAlive(int side) const {
    for (const BattleUnit& u : m_units)
        if (u.side == side && u.alive()) return true;
    return false;
}

QHash<Pos, int> Battle::reachable(const BattleUnit& who) const {
    QHash<Pos, int> steps;
    steps.insert(who.pos, 0);
    QQueue<Pos> queue;
    queue.enqueue(who.pos);
    const int speed = std::max(1, who.def->battleSpeed);
    while (!queue.isEmpty()) {
        const Pos p = queue.dequeue();
        const int s = steps.value(p);
        if (s >= speed) continue;
        for (const Pos n : hex::neighbours(p)) {
            if (steps.contains(n) || unitAt(n)) continue;
            steps.insert(n, s + 1);
            queue.enqueue(n);
        }
    }
    return steps;
}

bool Battle::canShoot(const BattleUnit& who) const {
    if (who.def->range <= 0 || who.shotsLeft <= 0) return false;
    for (const BattleUnit& u : m_units)
        if (u.alive() && u.side != who.side && hex::adjacent(u.pos, who.pos)) return false;
    return true;
}

bool Battle::attackHex(const BattleUnit& attacker, const BattleUnit& target, Pos* from) const {
    if (hex::adjacent(attacker.pos, target.pos)) {
        *from = attacker.pos;
        return true;
    }
    const QHash<Pos, int> reach = reachable(attacker);
    bool found = false;
    int best = 0;
    for (const Pos n : hex::neighbours(target.pos)) {
        const auto it = reach.constFind(n);
        if (it == reach.cend()) continue;
        if (!found || it.value() < best) {
            found = true;
            best = it.value();
            *from = n;
        }
    }
    return found;
}

void Battle::strike(BattleUnit& from, BattleUnit& to, BattleEvent::Type type, QList<BattleEvent>* events) {
    const BattleBalance& bb = m_content.balance().battle;
    const int diff = (from.def->attack + m_attackBonus[from.side]) - (to.def->defense + m_defenseBonus[to.side]);
    double mult = 1.0;
    if (diff > 0) mult += std::min(diff * bb.attackPercentPerPoint, bb.maxAttackPercent) / 100.0;
    if (diff < 0) mult -= std::min(-diff * bb.defensePercentPerPoint, bb.maxDefensePercent) / 100.0;
    const int roll = m_rng.nextInt(from.def->damageMin, std::max(from.def->damageMin, from.def->damageMax));
    const int damage = std::max(1, static_cast<int>(std::lround(from.stack.count() * roll * mult)));
    const UnitStack::DamageResult r = to.stack.applyDamage(damage);
    if (events) {
        BattleEvent e;
        e.type = type;
        e.unit = from.id;
        e.target = to.id;
        e.from = from.pos;
        e.to = to.pos;
        e.damage = r.damageDealt;
        e.killed = r.unitsKilled;
        events->append(e);
    }
}

QString Battle::act(const BattleAction& action, QList<BattleEvent>* events) {
    if (isOver()) return QStringLiteral("the battle is over");
    BattleUnit* me = mutableUnit(m_order[m_orderPos]);
    BattleUnit* target = action.target ? mutableUnit(action.target) : nullptr;

    switch (action.type) {
    case BattleAction::Type::Move: {
        if (!action.hasHex) return QStringLiteral("no hex given");
        const QHash<Pos, int> reach = reachable(*me);
        if (!reach.contains(action.hex) || action.hex == me->pos) return QStringLiteral("cannot move there");
        if (events) events->append({BattleEvent::Type::Move, me->id, 0, me->pos, action.hex, 0, 0});
        me->pos = action.hex;
        break;
    }
    case BattleAction::Type::Attack: {
        if (!target || !target->alive() || target->side == me->side) return QStringLiteral("no enemy to attack");
        Pos from;
        if (action.hasHex) {
            if (!hex::adjacent(action.hex, target->pos)) return QStringLiteral("that hex is not next to the target");
            if (action.hex != me->pos && !reachable(*me).contains(action.hex)) return QStringLiteral("cannot reach that hex");
            from = action.hex;
        } else if (!attackHex(*me, *target, &from)) {
            return QStringLiteral("cannot reach that enemy");
        }
        if (from != me->pos) {
            if (events) events->append({BattleEvent::Type::Move, me->id, 0, me->pos, from, 0, 0});
            me->pos = from;
        }
        strike(*me, *target, BattleEvent::Type::Attack, events);
        if (target->alive() && !target->retaliated) {
            target->retaliated = true;
            strike(*target, *me, BattleEvent::Type::Retaliate, events);
        }
        break;
    }
    case BattleAction::Type::Shoot: {
        if (!target || !target->alive() || target->side == me->side) return QStringLiteral("no enemy to shoot");
        if (!canShoot(*me)) return QStringLiteral("this stack cannot shoot now");
        --me->shotsLeft;
        strike(*me, *target, BattleEvent::Type::Shoot, events);
        break;
    }
    case BattleAction::Type::Defend:
        if (events) events->append({BattleEvent::Type::Defend, me->id, 0, me->pos, me->pos, 0, 0});
        break;
    }
    checkOver();
    if (!isOver()) advance(events);
    return {};
}

void Battle::startRound(QList<BattleEvent>* events) {
    ++m_round;
    if (m_round > m_content.balance().battle.maxRounds) {
        m_round = m_content.balance().battle.maxRounds;
        m_outcome = Outcome::Draw;
        return;
    }
    if (events) events->append({BattleEvent::Type::NewRound, 0, 0, {}, {}, m_round, 0});
    m_order.clear();
    QList<const BattleUnit*> alive;
    for (BattleUnit& u : m_units) {
        u.retaliated = false;
        if (u.alive()) alive.append(&u);
    }
    std::stable_sort(alive.begin(), alive.end(), [](const BattleUnit* a, const BattleUnit* b) {
        if (a->def->initiative != b->def->initiative) return a->def->initiative > b->def->initiative;
        if (a->def->battleSpeed != b->def->battleSpeed) return a->def->battleSpeed > b->def->battleSpeed;
        if (a->side != b->side) return a->side < b->side;
        return a->id < b->id;
    });
    for (const BattleUnit* u : alive) m_order.append(u->id);
    m_orderPos = 0;
}

void Battle::advance(QList<BattleEvent>* events) {
    do {
        ++m_orderPos;
        if (m_orderPos >= m_order.size()) {
            startRound(events);
            if (isOver()) return;
        }
    } while (!unit(m_order[m_orderPos])->alive());
}

void Battle::checkOver() {
    const bool a = sideAlive(0), d = sideAlive(1);
    if (a && d) return;
    m_outcome = a ? Outcome::AttackerWon : Outcome::DefenderWon;
}

QJsonObject battleEventToJson(const BattleEvent& e) {
    static const char* names[] = {"move", "attack", "shoot", "retaliate", "defend", "round"};
    QJsonObject o;
    o.insert("type", names[static_cast<int>(e.type)]);
    if (e.type == BattleEvent::Type::NewRound) {
        o.insert("round", e.damage);
        return o;
    }
    o.insert("unit", e.unit);
    if (e.target) o.insert("target", e.target);
    if (e.type == BattleEvent::Type::Move) {
        o.insert("x", e.to.x);
        o.insert("y", e.to.y);
    }
    if (e.target) {
        o.insert("damage", e.damage);
        o.insert("killed", e.killed);
    }
    return o;
}

QJsonObject battleToJson(const Battle& b) {
    QJsonObject o;
    o.insert("width", hex::kWidth);
    o.insert("height", hex::kHeight);
    o.insert("round", b.round());
    const BattleUnit* active = b.activeUnit();
    o.insert("active", active ? active->id : 0);
    QJsonArray sides;
    for (int s = 0; s < 2; ++s)
        sides.append(QJsonObject{{"owner", static_cast<int>(b.owner(s).value())}, {"hero", static_cast<int>(b.hero(s).value())}});
    o.insert("sides", sides);
    QJsonArray units;
    for (const BattleUnit& u : b.units()) {
        if (!u.alive()) continue;
        QJsonObject j;
        j.insert("id", u.id);
        j.insert("side", u.side);
        j.insert("unit", u.def->id);
        j.insert("count", u.stack.count());
        j.insert("frontHp", u.stack.frontUnitHp());
        j.insert("maxHp", u.stack.maxHp());
        j.insert("x", u.pos.x);
        j.insert("y", u.pos.y);
        j.insert("shots", u.shotsLeft);
        j.insert("retaliated", u.retaliated);
        units.append(j);
    }
    o.insert("units", units);
    if (active) {
        QJsonArray reach;
        const QHash<Pos, int> r = b.reachable(*active);
        QList<Pos> hexes = r.keys();
        std::sort(hexes.begin(), hexes.end());
        for (const Pos p : hexes)
            if (p != active->pos) reach.append(QJsonArray{p.x, p.y});
        o.insert("reachable", reach);
        o.insert("canShoot", b.canShoot(*active));
        QJsonArray attackable;
        for (const BattleUnit& u : b.units()) {
            Pos from;
            if (u.alive() && u.side != active->side && b.attackHex(*active, u, &from)) attackable.append(u.id);
        }
        o.insert("attackable", attackable);
    }
    return o;
}

}  // namespace kot::core
