#include "core/battle/Battle.h"

#include <QJsonArray>
#include <QQueue>
#include <QSet>
#include <algorithm>
#include <cmath>

#include "core/content/ContentRegistry.h"
#include "core/magic/Spells.h"
#include "core/rules/Veterancy.h"

namespace kot::core {

const Town* retreatTown(const GameState& state, const Army& army) {
    const Town* best = nullptr;
    int bestDist = 0;
    for (const Town& t : state.towns()) {
        if (t.owner != army.owner || t.pos == army.pos) continue;
        bool taken = false;
        for (const Army& a : state.armies()) taken = taken || (a.id != army.id && a.hasHero() && a.pos == t.pos);
        if (taken) continue;
        const int d = std::abs(t.pos.x - army.pos.x) + std::abs(t.pos.y - army.pos.y);
        if (!best || d < bestDist) {
            best = &t;
            bestDist = d;
        }
    }
    return best;
}

Battle::Battle(const GameState& state, const ContentRegistry& content, ArmyId attacker, ArmyId defender,
               std::uint64_t seed)
    : m_content(content), m_rng(seed) {
    const ArmyId ids[2] = {attacker, defender};
    for (int side = 0; side < 2; ++side) {
        m_army[side] = ids[side];
        const Army* army = state.army(ids[side]);
        if (!army) continue;
        m_owner[side] = army->owner;
        m_hasRefuge[side] = retreatTown(state, *army) != nullptr;
        if (const Player* pl = state.player(army->owner)) m_schoolPercent[side] = content.race(pl->race).spellSchoolPercent;
        m_hero[side] = army->hero;
        if (const Hero* h = state.hero(army->hero)) {
            m_attackBonus[side] = h->trait(content.known().attack);
            m_defenseBonus[side] = h->trait(content.known().defense);
            m_heroCopy[side] = *h;
            m_morale[side] = h->trait(content.known().morale);
            m_luck[side] = h->trait(content.known().luck);
        }
        const int hpPercent = (m_heroCopy[side].id.isValid() ? m_heroCopy[side].trait(content.known().hitPoints) : 0)
                            * content.balance().battle.hpPercentPerTraitLevel;
        // Up to 10 stacks over the start column, from the middle outwards.
        static const int rows[] = {5, 3, 7, 1, 9, 0, 10, 2, 8, 4, 6};
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
            u.baseMaxHp = s.maxHp();
            // The hero's Hit Points: more HP per unit, same number of units.
            const int maxHp = effectiveMaxHp(s.maxHp(), HpBonus{0, hpPercent / 100.0});
            u.stack = UnitStack::fromTotalHp(s.unitTypeId(), s.totalHp() * maxHp / std::max(1, s.maxHp()), maxHp);
            u.stack.setExperience(s.experience());
            u.stars = veteranStars(s.experience(), content);
            for (const char* stat : {"attack", "defense", "initiative", "battleSpeed", "morale"})
                if (const int b = veteranBonus(u.stars, QString::fromLatin1(stat), content)) u.veteran.insert(QString::fromLatin1(stat), b);
            u.pos = Pos{side == 0 ? 0 : hex::kWidth - 1, rows[placed++ % 11]};
            u.shotsLeft = def->shots;
            u.spellPoints = def->spellPoints;  // full at the start of every battle
            m_units.append(u);
        }
    }
    // The ground the battle is fought on (for the picture only): the defender's tile.
    if (const Army* d = state.army(defender)) {
        m_terrain = state.map().terrainAt(d->pos);
        for (const Town& t : state.towns()) m_defenderInTown = m_defenderInTown || t.pos == d->pos;
    }
    // Town walls: a defender standing in a town gets its buildings' defense.
    if (const Army* d = state.army(defender)) {
        for (const Town& t : state.towns()) {
            if (t.pos != d->pos) continue;
            for (const BuildingTypeId b : t.buildings)
                if (const BuildingDef* def = content.building(b))
                    for (const BuildingEffect& e : def->effects)
                        if (e.type == BuildingEffect::Type::Defense) m_townDefense += e.amount;
        }
        // Dwarves: their walls hold better.
        if (m_townDefense > 0)
            if (const Player* pl = state.player(d->owner)) m_townDefense += content.race(pl->race).wallDefenseBonus;
        m_defenseBonus[1] += m_townDefense;
        // The walls on the field: one level per defense building.
        for (const Town& t : state.towns()) {
            if (t.pos != d->pos) continue;
            for (const BuildingTypeId b : t.buildings)
                if (const BuildingDef* def = content.building(b))
                    for (const BuildingEffect& e : def->effects) m_wallLevel += e.type == BuildingEffect::Type::Defense;
        }
        if (m_wallLevel > 0) {
            for (int y = 0; y < hex::kHeight; ++y) m_wallHp.insert(Pos{kWallColumn, y}, wallMaxHp());
            m_gate = Pos{kWallColumn, hex::kHeight / 2};
            m_towers = {Pos{kWallColumn + 1, 1}, Pos{kWallColumn + 1, hex::kHeight - 2}};
            if (m_wallLevel >= 2) m_towers.append(Pos{kWallColumn + 1, hex::kHeight / 2 - 2});
            if (m_wallLevel >= 3) m_towers.append(Pos{kWallColumn + 1, hex::kHeight / 2 + 2});  // a castle
        }
    }
    // Rocks in the middle columns (in front of the walls in a siege).
    for (int i = 0, tries = 0; i < content.balance().battle.obstacles && tries < 100; ++tries) {
        const Pos p{m_rng.nextInt(3, m_wallLevel > 0 ? kWallColumn - 2 : hex::kWidth - 4), m_rng.nextInt(0, hex::kHeight - 1)};
        if (m_obstacles.contains(p)) continue;
        m_obstacles.append(p);
        ++i;
    }
    checkOver();
    if (!isOver()) {
        startRound(nullptr);
        if (!isOver()) settle(nullptr);
    }
}

bool Battle::chance(int percent) {
    return percent > 0 && m_rng.nextInt(1, 100) <= percent;
}

int BattleUnit::modifier(const QString& stat) const {
    int total = veteran.value(stat);
    for (const StatModifier& m : modifiers)
        if (m.stat == stat) total += m.amount;
    return total;
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
        if (u.side == side && u.alive() && !u.def->has(Ability::SiegeEngine)) return true;  // a catapult alone does not fight on
    return false;
}

QString Battle::whyNoRetreat(int side) const {
    if (isOver()) return QStringLiteral("the battle is over");
    if (!m_hero[side].isValid()) return QStringLiteral("only a hero can retreat");
    if (side == 1 && m_defenderInTown) return QStringLiteral("a town's defenders cannot run away");
    if (!m_hasRefuge[side]) return QStringLiteral("there is no town of yours to flee to");
    return {};
}

QString Battle::whyNoSurrender(int side) const {
    if (const QString why = whyNoRetreat(side); !why.isEmpty()) return why;
    if (!m_owner[1 - side].isValid()) return QStringLiteral("monsters do not take surrender");
    return {};
}

int Battle::surrenderCost(int side) const {
    long long gold = 0;
    for (const BattleUnit& u : m_units)
        if (u.side == side && u.alive() && u.armyIndex >= 0) gold += static_cast<long long>(u.stack.count()) * u.def->cost.value(m_content.currency());
    return static_cast<int>(gold * m_content.balance().battle.surrenderCostPercent / 100);
}

int Battle::wallMaxHp() const {
    return m_content.balance().siege.wallHpPerLevel * std::max(1, m_wallLevel);
}

bool Battle::blockedFor(Pos hex, const BattleUnit& who) const {
    if (m_obstacles.contains(hex)) return true;
    if (m_wallHp.contains(hex) && !(who.side == 1 && hex == m_gate)) return true;
    const BattleUnit* u = unitAt(hex);
    return u && u->id != who.id;
}

QString Battle::whyNoSiegeEngine() const {
    if (m_wallLevel <= 0) return QStringLiteral("there are no walls to break");
    if (m_engineBuilt) return QStringLiteral("a catapult is already here");
    if (isOver()) return QStringLiteral("the battle is over");
    if (!m_content.unitByKey(QStringLiteral("catapult"))) return QStringLiteral("no catapult in the game data");
    return {};
}

QString Battle::buildSiegeEngine(QList<BattleEvent>* events) {
    const QString why = whyNoSiegeEngine();
    if (!why.isEmpty()) return why;
    BattleUnit u;
    u.id = static_cast<int>(m_units.size()) + 1;
    u.side = 0;
    u.armyIndex = -1;  // not from the army: gone after the battle
    u.def = m_content.unitByKey(QStringLiteral("catapult"));
    u.stack = UnitStack(u.def->id, 1, u.def->maxHp);
    u.baseMaxHp = u.def->maxHp;
    // At the back of the attacker's side, on a free hex.
    for (int y : {5, 4, 6, 3, 7, 2, 8, 1, 9, 0, 10})
        for (int x = 0; x < 3; ++x)
            if (!blocked(Pos{x, y}) && u.pos == Pos{}) u.pos = Pos{x, y};
    m_units.append(u);
    m_order.append(u.id);  // it shoots this round already
    m_engineBuilt = true;
    if (events) events->append({BattleEvent::Type::Engine, u.id, 0, u.pos, u.pos, 0, 0, {}});
    return {};
}

void Battle::towersShoot(QList<BattleEvent>* events) {
    // Each tower shoots the most dangerous attacking stack.
    const int damage = m_content.balance().siege.towerDamagePerLevel * m_wallLevel;
    for (const Pos tower : m_towers) {
        BattleUnit* best = nullptr;
        double worst = -1;
        for (BattleUnit& u : m_units) {
            if (u.side != 0 || !u.alive()) continue;
            const double danger = u.stack.count() * (u.def->damageMin + u.def->damageMax + 1.0);
            if (danger > worst) {
                worst = danger;
                best = &u;
            }
        }
        if (!best) return;
        const UnitStack::DamageResult r = best->stack.applyDamage(damage);
        if (events) events->append({BattleEvent::Type::Tower, 0, best->id, tower, best->pos, r.damageDealt, r.unitsKilled, {}});
    }
}

QHash<Pos, int> Battle::reachable(const BattleUnit& who) const {
    // Breadth-first over hexes. Walkers go around stacks and rocks; flyers pass
    // over them but must land on a free hex.
    if (who.def->has(Ability::SiegeEngine)) return {{who.pos, 0}};  // a catapult stays put
    const int speed = std::max(1, who.def->battleSpeed + who.modifier(QStringLiteral("battleSpeed")));
    const bool flying = who.def->has(Ability::Flying);
    QHash<Pos, int> dist{{who.pos, 0}};
    QQueue<Pos> queue;
    queue.enqueue(who.pos);
    while (!queue.isEmpty()) {
        const Pos p = queue.dequeue();
        const int d = dist.value(p);
        if (d >= speed) continue;
        for (const Pos n : hex::neighbours(p)) {
            if (dist.contains(n) || (!flying && blockedFor(n, who))) continue;
            dist.insert(n, d + 1);
            queue.enqueue(n);
        }
    }
    QHash<Pos, int> steps;
    for (auto it = dist.cbegin(); it != dist.cend(); ++it)
        if (it.key() == who.pos || !blockedFor(it.key(), who)) steps.insert(it.key(), it.value());
    return steps;
}

bool Battle::canShoot(const BattleUnit& who) const {
    if (who.def->range <= 0 || who.shotsLeft <= 0) return false;
    for (const BattleUnit& u : m_units)
        if (u.alive() && u.side != who.side && hex::adjacent(u.pos, who.pos)) return false;
    return true;
}

int Battle::shootRange(const BattleUnit& who) const {
    if (who.def->range <= 0) return 0;
    const Hero* h = heroOf(who.side);
    return who.def->range + (h ? h->trait(m_content.known().attackRange) : 0);
}

bool Battle::canShootAt(const BattleUnit& who, const BattleUnit& target) const {
    return target.alive() && target.side != who.side && canShoot(who) && hex::distance(who.pos, target.pos) <= shootRange(who);
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
    const int diff = (from.def->attack + m_attackBonus[from.side] + from.modifier(QStringLiteral("attack")))
                   - (to.def->defense + m_defenseBonus[to.side] + to.modifier(QStringLiteral("defense")));
    double mult = 1.0;
    if (diff > 0) mult += std::min(diff * bb.attackPercentPerPoint, bb.maxAttackPercent) / 100.0;
    if (diff < 0) mult -= std::min(-diff * bb.defensePercentPerPoint, bb.maxDefensePercent) / 100.0;
    const int roll = m_rng.nextInt(from.def->damageMin, std::max(from.def->damageMin, from.def->damageMax));
    if (type == BattleEvent::Type::Shoot && from.side == 1 && m_wallLevel > 0)  // shooting from the walls
        mult *= 1.0 + m_content.balance().siege.defenderShotPercentPerLevel * m_wallLevel / 100.0;
    int damage = std::max(1, static_cast<int>(std::lround(from.stack.count() * roll * mult)));
    const bool lucky = m_luck[from.side] > 0 && chance(m_luck[from.side] * bb.luckPercentPerPoint);
    if (lucky) damage *= 2;
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
        e.lucky = lucky;
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
        if (events) events->append({BattleEvent::Type::Move, me->id, 0, me->pos, action.hex, 0, 0, {}});
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
            if (events) events->append({BattleEvent::Type::Move, me->id, 0, me->pos, from, 0, 0, {}});
            me->pos = from;
        }
        strike(*me, *target, BattleEvent::Type::Attack, events);
        if (target->alive() && !target->retaliated && !me->def->has(Ability::NoRetaliation)) {
            target->retaliated = true;
            strike(*target, *me, BattleEvent::Type::Retaliate, events);
        }
        if (me->def->has(Ability::DoubleAttack) && me->alive() && target->alive())
            strike(*me, *target, BattleEvent::Type::Attack, events);
        break;
    }
    case BattleAction::Type::Shoot: {
        if (me->def->has(Ability::SiegeEngine)) {
            // The catapult hurls a stone at a wall segment.
            if (!action.hasHex || !m_wallHp.contains(action.hex)) return QStringLiteral("aim at a standing wall segment");
            const SiegeBalance& sb = m_content.balance().siege;
            const int hit = m_rng.nextInt(sb.catapultDamageMin, sb.catapultDamageMax);
            int& hp = m_wallHp[action.hex];
            hp -= hit;
            const bool fell = hp <= 0;
            if (fell) m_wallHp.remove(action.hex);
            if (events) events->append({BattleEvent::Type::WallHit, me->id, 0, me->pos, action.hex, hit, fell ? 1 : 0, {}});
            break;
        }
        if (!target || !target->alive() || target->side == me->side) return QStringLiteral("no enemy to shoot");
        if (!canShoot(*me)) return QStringLiteral("this stack cannot shoot now");
        if (!canShootAt(*me, *target))
            return QStringLiteral("out of range (%1 hexes away, range %2)").arg(hex::distance(me->pos, target->pos)).arg(shootRange(*me));
        --me->shotsLeft;
        strike(*me, *target, BattleEvent::Type::Shoot, events);
        if (me->def->has(Ability::DoubleAttack) && me->shotsLeft > 0 && target->alive()) {
            --me->shotsLeft;
            strike(*me, *target, BattleEvent::Type::Shoot, events);
        }
        break;
    }
    case BattleAction::Type::Defend:
        if (events) events->append({BattleEvent::Type::Defend, me->id, 0, me->pos, me->pos, 0, 0, {}});
        break;
    case BattleAction::Type::Cast:
        return cast(action, events);  // does not end the stack's turn
    case BattleAction::Type::UnitCast: {  // the stack's own spell, its turn
        const QString why = whyUnitCannotCast(action.spell, action.target);
        if (!why.isEmpty()) return why;
        if (!target) return QStringLiteral("no target given");
        const SpellDef* spell = m_content.spell(action.spell);
        me->spellPoints -= spell->cost;
        for (BattleUnit* t : spellTargets(*spell, *target)) applySpell(*spell, me->def->tier, *t, me->side, me->id, events);
        break;
    }
    case BattleAction::Type::Wait: {
        if (me->waited) return QStringLiteral("this stack has already waited this round");
        me->waited = true;
        if (events) events->append({BattleEvent::Type::Wait, me->id, 0, me->pos, me->pos, 0, 0, {}});
        m_order.removeAt(m_orderPos);  // to the end of this round
        m_order.append(me->id);
        settle(events);
        return {};
    }
    case BattleAction::Type::Retreat: {
        if (const QString why = whyNoRetreat(me->side); !why.isEmpty()) return why;
        m_retreated[me->side] = true;
        if (events) events->append({BattleEvent::Type::Retreat, me->id, 0, me->pos, me->pos, 0, 0, {}});
        m_outcome = me->side == 0 ? Outcome::DefenderWon : Outcome::AttackerWon;
        return {};
    }
    case BattleAction::Type::Surrender: {
        if (const QString why = whyNoSurrender(me->side); !why.isEmpty()) return why;
        m_surrendered[me->side] = true;
        if (events) events->append({BattleEvent::Type::Retreat, me->id, 0, me->pos, me->pos, 0, 0, {}});
        m_outcome = me->side == 0 ? Outcome::DefenderWon : Outcome::AttackerWon;
        return {};
    }
    }
    checkOver();
    if (isOver()) return {};
    // Good morale: sometimes the same stack acts once more.
    if (action.type != BattleAction::Type::Defend && me->alive() && !me->moraleUsed && !ignoresMorale(*me)
        && moraleOf(*me) > 0 && chance(moraleOf(*me) * m_content.balance().battle.moralePercentPerPoint)) {
        me->moraleUsed = true;
        if (events) events->append({BattleEvent::Type::Morale, me->id, 0, me->pos, me->pos, 0, 0, {}});
        return {};
    }
    advance(events);
    return {};
}

int Battle::moraleOf(const BattleUnit& u) const {
    return m_morale[u.side] + u.modifier(QStringLiteral("morale"));
}

bool Battle::ignoresMorale(const BattleUnit& u) const {
    return u.def->has(Ability::Undead);
}

QString Battle::whyCannotCast(SpellId spellId, int target) const {
    const BattleUnit* me = activeUnit();
    if (!me) return QStringLiteral("the battle is over");
    const Hero* hero = heroOf(me->side);
    if (!hero) return QStringLiteral("there is no hero on this side");
    if (m_heroCast[me->side]) return QStringLiteral("the hero has already cast a spell this round");
    const QString why = core::whyCannotCast(*hero, m_content, spellId, SpellDef::Scope::Battle, hero->spellPoints);
    if (!why.isEmpty() || target == 0) return why;
    const SpellDef* spell = m_content.spell(spellId);
    const BattleUnit* t = unit(target);
    if (!t || !t->alive()) return QStringLiteral("no such stack");
    if (spell->target == SpellDef::Target::Enemy && t->side == me->side) return QStringLiteral("%1 is cast on an enemy").arg(spell->name);
    if (spell->target == SpellDef::Target::Ally && t->side != me->side) return QStringLiteral("%1 is cast on your own stack").arg(spell->name);
    return {};
}

QString Battle::cast(const BattleAction& action, QList<BattleEvent>* events) {
    const QString why = whyCannotCast(action.spell, action.target);
    if (!why.isEmpty()) return why;
    if (!action.target) return QStringLiteral("no target given");
    const int side = activeUnit()->side;
    const SpellDef* spell = m_content.spell(action.spell);
    Hero& hero = m_heroCopy[side];
    hero.spellPoints -= spell->cost;
    m_heroCast[side] = true;
    for (BattleUnit* t : spellTargets(*spell, *mutableUnit(action.target)))
        applySpell(*spell, hero.trait(m_content.known().spellPower), *t, side, 0, events);
    checkOver();
    return {};
}

QString Battle::whyUnitCannotCast(SpellId spellId, int target) const {
    const BattleUnit* me = activeUnit();
    if (!me) return QStringLiteral("the battle is over");
    const SpellDef* spell = m_content.spell(spellId);
    if (!spell || !me->def->spells.contains(spellId)) return QStringLiteral("this stack cannot cast that");
    if (me->spellPoints < spell->cost) return QStringLiteral("the stack has not enough spell points left");
    if (target == 0) return {};
    const BattleUnit* t = unit(target);
    if (!t || !t->alive()) return QStringLiteral("no such stack");
    if (spell->target == SpellDef::Target::Enemy && t->side == me->side) return QStringLiteral("%1 is cast on an enemy").arg(spell->name);
    if (spell->target == SpellDef::Target::Ally && t->side != me->side) return QStringLiteral("%1 is cast on your own stack").arg(spell->name);
    return {};
}

QList<BattleUnit*> Battle::spellTargets(const SpellDef& spell, BattleUnit& target) {
    QList<BattleUnit*> targets{&target};
    if (spell.area <= 0) return targets;
    for (BattleUnit& u : m_units)
        if (&u != &target && u.alive() && u.side == target.side && hex::distance(u.pos, target.pos) <= spell.area) targets.append(&u);
    return targets;
}

void Battle::applySpell(const SpellDef& spell, int power, BattleUnit& t, int side, int casterUnit, QList<BattleEvent>* events) {
    BattleEvent e;
    e.type = BattleEvent::Type::Cast;
    e.unit = casterUnit;
    e.target = t.id;
    e.to = t.pos;
    e.spell = spell.id;
    e.side = side;
    for (const SpellEffect& effect : spell.effects) {
        const int strength = effectStrength(effect, power);
        switch (effect.type) {
        case SpellEffect::Type::Damage: {
            const int percent = side >= 0 && side < 2 ? m_schoolPercent[side].value(spell.school, 100) : 100;  // ashborn: fire burns hotter
            const UnitStack::DamageResult r = t.stack.applyDamage(std::max(0, strength * percent / 100));
            e.damage += r.damageDealt;
            e.killed += r.unitsKilled;
            break;
        }
        case SpellEffect::Type::Heal:
            e.damage += t.stack.heal(std::max(0, strength));
            break;
        case SpellEffect::Type::ModifyStat:
            t.modifiers.append({effect.stat, strength, effect.rounds});
            break;
        default:
            break;  // adventure effects do nothing in battle
        }
    }
    if (events) events->append(e);
}

void Battle::startRound(QList<BattleEvent>* events) {
    ++m_round;
    if (m_round > m_content.balance().battle.maxRounds) {
        m_round = m_content.balance().battle.maxRounds;
        m_outcome = Outcome::Draw;
        return;
    }
    if (events) events->append({BattleEvent::Type::NewRound, 0, 0, {}, {}, m_round, 0, {}});
    m_order.clear();
    QList<const BattleUnit*> alive;
    m_heroCast[0] = m_heroCast[1] = false;
    for (BattleUnit& u : m_units) {
        u.retaliated = false;
        u.waited = false;
        u.moraleUsed = false;
        // Spell effects run out.
        for (StatModifier& m : u.modifiers) --m.roundsLeft;
        u.modifiers.removeIf([](const StatModifier& m) { return m.roundsLeft <= 0; });
        if (u.alive()) alive.append(&u);
    }
    std::stable_sort(alive.begin(), alive.end(), [](const BattleUnit* a, const BattleUnit* b) {
        const int ia = a->def->initiative + a->modifier(QStringLiteral("initiative"));
        const int ib = b->def->initiative + b->modifier(QStringLiteral("initiative"));
        if (ia != ib) return ia > ib;
        if (a->def->battleSpeed != b->def->battleSpeed) return a->def->battleSpeed > b->def->battleSpeed;
        if (a->side != b->side) return a->side < b->side;
        return a->id < b->id;
    });
    for (const BattleUnit* u : alive) m_order.append(u->id);
    m_orderPos = 0;
    if (m_wallLevel > 0 && !m_towers.isEmpty()) {
        towersShoot(events);
        checkOver();
    }
}

void Battle::advance(QList<BattleEvent>* events) {
    ++m_orderPos;
    settle(events);
}

void Battle::settle(QList<BattleEvent>* events) {
    for (;;) {
        if (m_orderPos >= m_order.size()) {
            startRound(events);
            if (isOver()) return;
        }
        BattleUnit* u = mutableUnit(m_order[m_orderPos]);
        if (!u->alive()) {
            ++m_orderPos;
            continue;
        }
        // Bad morale: the stack falters and loses its turn.
        if (moraleOf(*u) < 0 && !ignoresMorale(*u)
            && chance(-moraleOf(*u) * m_content.balance().battle.moralePercentPerPoint)) {
            if (events) events->append({BattleEvent::Type::Falter, u->id, 0, u->pos, u->pos, 0, 0, {}});
            ++m_orderPos;
            continue;
        }
        return;
    }
}

void Battle::checkOver() {
    const bool a = sideAlive(0), d = sideAlive(1);
    if (a && d) return;
    m_outcome = a ? Outcome::AttackerWon : Outcome::DefenderWon;
}

QJsonObject battleEventToJson(const BattleEvent& e) {
    static const char* names[] = {"move",  "attack", "shoot",  "retaliate", "defend", "round",
                                  "cast",  "wait",   "morale", "falter",    "retreat", "tower", "wall_hit", "engine"};
    QJsonObject o;
    o.insert("type", names[static_cast<int>(e.type)]);
    if (e.type == BattleEvent::Type::NewRound) {
        o.insert("round", e.damage);
        return o;
    }
    o.insert("unit", e.unit);
    if (e.target) o.insert("target", e.target);
    if (e.type == BattleEvent::Type::Move || e.type == BattleEvent::Type::WallHit) {
        o.insert("x", e.to.x);
        o.insert("y", e.to.y);
    }
    if (e.type == BattleEvent::Type::WallHit) {
        o.insert("damage", e.damage);
        if (e.killed) o.insert("fell", true);
    }
    if (e.type == BattleEvent::Type::Tower) {  // from the tower at fromX, fromY
        o.insert("fromX", e.from.x);
        o.insert("fromY", e.from.y);
    }
    if (e.target) {
        o.insert("damage", e.damage);
        o.insert("killed", e.killed);
    }
    if (e.spell.isValid()) o.insert("spell", e.spell.index());
    if (e.side >= 0) o.insert("side", e.side);
    if (e.lucky) o.insert("lucky", true);
    return o;
}

QJsonObject battleToJson(const Battle& b, int viewerSide) {
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
        j.insert("unit", u.def->id.index());
        j.insert("count", u.stack.count());
        j.insert("frontHp", u.stack.frontUnitHp());
        j.insert("maxHp", u.stack.maxHp());
        j.insert("x", u.pos.x);
        j.insert("y", u.pos.y);
        j.insert("shots", u.shotsLeft);
        if (const int r = b.shootRange(u)) j.insert("range", r);
        if (u.def->spellPoints > 0) j.insert("spellPoints", u.spellPoints);
        j.insert("retaliated", u.retaliated);
        if (u.stars > 0) j.insert("stars", u.stars);
        QJsonArray mods;
        for (const StatModifier& m : u.modifiers)
            mods.append(QJsonObject{{"stat", m.stat}, {"amount", m.amount}, {"rounds", m.roundsLeft}});
        if (!mods.isEmpty()) j.insert("modifiers", mods);
        units.append(j);
    }
    o.insert("units", units);
    QJsonArray rocks;
    for (const Pos p : b.obstacles()) rocks.append(QJsonArray{p.x, p.y});
    o.insert("obstacles", rocks);
    if (b.townDefense() > 0) o.insert("townDefense", b.townDefense());
    if (b.terrain().isValid()) o.insert("terrain", b.terrain().index());
    if (b.wallLevel() > 0) {
        QJsonObject siege;
        siege.insert("level", b.wallLevel());
        QJsonArray walls;
        for (auto it = b.walls().cbegin(); it != b.walls().cend(); ++it)
            walls.append(QJsonArray{it.key().x, it.key().y, it.value()});
        siege.insert("walls", walls);
        siege.insert("wallMaxHp", b.wallMaxHp());
        siege.insert("gate", QJsonArray{b.gate().x, b.gate().y});
        QJsonArray towers;
        for (const Pos t : b.towers()) towers.append(QJsonArray{t.x, t.y});
        siege.insert("towers", towers);
        siege.insert("engineBuilt", b.engineBuilt());
        siege.insert("canBuildEngine", viewerSide == 0 && b.whyNoSiegeEngine().isEmpty());
        o.insert("siege", siege);
    }
    if (active) {
        QJsonArray reach;
        const QHash<Pos, int> r = b.reachable(*active);
        QList<Pos> hexes = r.keys();
        std::sort(hexes.begin(), hexes.end());
        for (const Pos p : hexes)
            if (p != active->pos) reach.append(QJsonArray{p.x, p.y});
        o.insert("reachable", reach);
        o.insert("canShoot", b.canShoot(*active));
        QJsonArray shootable;
        for (const BattleUnit& u : b.units())
            if (b.canShootAt(*active, u)) shootable.append(u.id);
        o.insert("shootable", shootable);
        o.insert("canWait", !active->waited);
        QJsonArray unitSpells;
        for (const SpellId id : active->def->spells)
            if (const SpellDef* sp = b.content().spell(id))
                unitSpells.append(QJsonObject{{"id", id.index()}, {"target", sp->target == SpellDef::Target::Enemy ? "enemy" : "ally"},
                                              {"cost", sp->cost}, {"castable", b.whyUnitCannotCast(id, 0).isEmpty()}});
        if (!unitSpells.isEmpty()) o.insert("unitSpells", unitSpells);
        o.insert("canRetreat", b.whyNoRetreat(active->side).isEmpty());
        if (b.whyNoSurrender(active->side).isEmpty()) o.insert("surrenderCost", b.surrenderCost(active->side));
        QJsonArray attackable;
        for (const BattleUnit& u : b.units()) {
            Pos from;
            if (u.alive() && u.side != active->side && b.attackHex(*active, u, &from)) attackable.append(u.id);
        }
        o.insert("attackable", attackable);
    }
    if (viewerSide == 0 || viewerSide == 1) {
        if (const Hero* h = b.heroOf(viewerSide)) {
            QJsonObject hero;
            hero.insert("spellPoints", h->spellPoints);
            hero.insert("hasCast", b.heroHasCast(viewerSide));
            QJsonArray spells;
            for (const SpellId id : h->spells) {
                const SpellDef* sp = b.content().spell(id);
                if (!sp || sp->scope != SpellDef::Scope::Battle) continue;
                const bool myTurn = active && active->side == viewerSide;
                spells.append(QJsonObject{{"id", id.index()},
                                          {"target", sp->target == SpellDef::Target::Enemy ? "enemy" : "ally"},
                                          {"cost", sp->cost},
                                          {"castable", myTurn && b.whyCannotCast(id, 0).isEmpty()}});
            }
            hero.insert("spells", spells);
            o.insert("hero", hero);
        }
    }
    return o;
}

}  // namespace kot::core
