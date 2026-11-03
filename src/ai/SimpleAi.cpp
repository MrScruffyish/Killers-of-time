#include "ai/SimpleAi.h"

#include "core/rules/Movement.h"

#include <QJsonObject>
#include <algorithm>
#include <cmath>

#include "ai/AiView.h"
#include "core/content/ContentRegistry.h"
#include "core/magic/Spells.h"
#include "core/rules/Boats.h"
#include "net/Protocol.h"

namespace kot::ai {

using namespace kot::core;

namespace {

constexpr int kMaxMovesPerHero = 4;
constexpr int kThreatRange = 8;        // enemies this close to a town are a threat

net::Message command(const QString& type, QJsonObject body = {}) {
    return net::Message{type, std::move(body), 0};
}

int toInt(quint32 v) { return static_cast<int>(v); }

int distance(Pos a, Pos b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }

std::uint64_t key(quint32 hero, Pos p) {
    return (std::uint64_t(hero) << 32) | (std::uint64_t(std::uint16_t(p.x)) << 16) | std::uint16_t(p.y);
}

bool isMagicClass(const ContentRegistry& content, const Hero& h) {
    const HeroClassDef* c = content.heroClass(h.classId);
    return c && c->baseTraits.value(QStringLiteral("magic")) >= 3;
}

}  // namespace

AiPersonality personalityFor(const QString& profile) {
    AiPersonality p;
    p.name = profile;
    if (profile == QLatin1String("aggressive")) {
        p.attackMargin = 1.1;
        p.townValue = 1.6;
        p.armyValue = 1.6;
        p.mineValue = 0.8;
        p.extraHeroes = 1;
    } else if (profile == QLatin1String("economic")) {
        p.attackMargin = 1.7;
        p.townValue = 0.8;
        p.armyValue = 0.6;
        p.mineValue = 1.6;
        p.treasureValue = 1.3;
    } else if (profile == QLatin1String("explorer")) {
        p.attackMargin = 1.5;
        p.exploreValue = 2.0;
        p.treasureValue = 1.6;
        p.extraHeroes = 1;
    } else {
        p.name = QStringLiteral("simple");
    }
    return p;
}

QString SimpleAi::profileName() const {
    return m_p.name;
}

void SimpleAi::beginTurn(const GameState&, const ContentRegistry&, PlayerId) {
    m_built = m_hired = m_sentUnits = false;
    m_garrisonTaken.clear();
    m_moves.clear();
    m_heroDone.clear();
    m_spellCast.clear();
    m_failedTargets.clear();
    m_boatTried.clear();
    m_resting.clear();
}

void SimpleAi::commandDone(const net::Message& cmd, bool ok) {
    if (cmd.type != net::msg::moveHero || ok) return;
    // Remember the refused target and try something else next.
    m_failedTargets.insert(m_lastTarget);
    const quint32 hero = static_cast<quint32>(cmd.body.value("hero").toInt());
    if (m_moves.value(hero) >= kMaxMovesPerHero) m_heroDone.insert(hero);
}

net::Message SimpleAi::nextCommand(const GameState& state, const ContentRegistry& content, PlayerId me) {
    const Player* player = state.player(me);
    if (!player) return command(net::msg::endTurn);

    // 1. Level-up picks that fit the class.
    static const QStringList mightTraits = {"attack", "defense", "hit_points", "morale", "attack", "defense"};
    static const QStringList magicTraits = {"spell_power", "magic", "spell_recovery", "spell_power", "defense"};
    for (const Hero& h : state.heroes()) {
        if (h.owner == me && h.pendingTraitPicks > 0) {
            const QStringList& pool = isMagicClass(content, h) ? magicTraits : mightTraits;
            const QString trait = pool[m_rng.nextInt(0, static_cast<int>(pool.size()) - 1)];
            return command(net::msg::pickTrait, {{"hero", toInt(h.id.value())}, {"trait", trait}});
        }
    }

    // 2. Units from map dwellings to the closest town.
    if (!m_sentUnits) {
        m_sentUnits = true;
        for (const MapDwelling& d : state.mapDwellings())
            if (d.owner == me && d.available > 0) return command(net::msg::sendDwellingUnits);
    }

    // 3. Build, then buy units everywhere.
    if (!m_built) {
        m_built = true;
        return command(net::msg::buildThenBuy, {{"useTrade", true}});
    }

    // 4. Hire heroes while rich enough.
    if (!m_hired) {
        m_hired = true;
        int heroes = 0;
        for (const Hero& h : state.heroes()) heroes += (h.owner == me);
        const int wanted = (state.day() < 14 ? 2 : state.day() < 28 ? 3 : 4) + m_p.extraHeroes;
        const int cost = content.balance().economy.heroCost;
        if (heroes < wanted && player->resources.value("gold") >= cost + 1000) {
            for (const Town& t : state.towns()) {
                if (t.owner != me || t.tavernOffers.isEmpty()) continue;
                bool heroThere = false;
                for (const Army& a : state.armies()) heroThere = heroThere || (a.pos == t.pos && a.hasHero());
                if (heroThere) continue;
                const QString offer = t.tavernOffers[m_rng.nextInt(0, static_cast<int>(t.tavernOffers.size()) - 1)];
                return command(net::msg::hireHero, {{"town", toInt(t.id.value())}, {"hero", offer}});
            }
        }
    }

    // 5. Heroes in an own, unthreatened town take the garrison along (one stack per command).
    const AiView view(state, content, me);
    const auto threatNear = [&](Pos town) {
        for (const Army& a : state.armies())
            if (a.hasHero() && state.isHostile(a.owner, me) && view.visible(a.pos) && distance(a.pos, town) <= kThreatRange)
                return true;
        return false;
    };
    for (const Hero& h : state.heroes()) {
        if (h.owner != me || m_garrisonTaken.contains(h.id.value())) continue;
        const Army* army = state.army(h.army);
        if (!army) continue;
        for (const Town& t : state.towns()) {
            if (t.owner != me || t.pos != army->pos || threatNear(t.pos)) continue;
            const Army* garrison = state.army(t.garrison);
            if (!garrison || garrison->stacks.isEmpty()) continue;
            const UnitStack& s = garrison->stacks.first();
            bool fits = army->stacks.size() < kMaxArmyStacks;
            for (const UnitStack& own : army->stacks) fits = fits || own.canMerge(s);
            if (!fits) break;
            return command(net::msg::moveUnits, {{"from", toInt(garrison->id.value())}, {"to", toInt(army->id.value())},
                                                 {"stack", 0}, {"count", s.count()}});
        }
        m_garrisonTaken.insert(h.id.value());
    }

    // 6. Movement spells, then moves.
    for (const Hero& h : state.heroes()) {
        if (h.owner != me || m_spellCast.contains(h.id.value())) continue;
        m_spellCast.insert(h.id.value());
        for (const QString& id : h.spells) {
            const SpellDef* s = content.spell(id);
            bool movement = false;
            for (const SpellEffect& e : s ? s->effects : QList<SpellEffect>()) movement = movement || e.type == SpellEffect::Type::Movement;
            if (movement && whyCannotCast(h, content, id, SpellDef::Scope::Adventure, h.spellPoints).isEmpty())
                return command(net::msg::castSpell, {{"hero", toInt(h.id.value())}, {"spell", id}});
        }
    }
    for (const Hero& h : state.heroes()) {
        if (h.owner != me || h.movementPoints <= 0 || m_heroDone.contains(h.id.value())) continue;
        if (m_moves.value(h.id.value()) >= kMaxMovesPerHero) continue;
        net::Message move = h.inBoat ? sailMove(state, content, me, h) : heroMove(state, content, me, h);
        if (move.type.isEmpty() && !h.inBoat && !m_resting.contains(h.id.value()))
            move = boatPlan(state, content, me, h);  // nothing left here
        if (move.type.isEmpty()) {
            m_heroDone.insert(h.id.value());
            continue;
        }
        ++m_moves[h.id.value()];
        return move;
    }

    // 7. Done.
    return command(net::msg::endTurn);
}

// The land a hero can walk to from `from` (8 directions, walkable tiles).
static QSet<Pos> walkableRegion(const GameState& state, const ContentRegistry& content, Pos from) {
    QSet<Pos> seen{from};
    QList<Pos> todo{from};
    while (!todo.isEmpty()) {
        const Pos p = todo.takeLast();
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const Pos n{p.x + dx, p.y + dy};
                if (seen.contains(n) || !state.map().inBounds(n)) continue;
                const TerrainDef* t = content.terrainType(state.map().terrainAt(n));
                if (!t || !t->walkable) continue;
                seen.insert(n);
                todo.append(n);
            }
    }
    return seen;
}

// Nothing more to do on this island: buy a boat in a town with a shipyard (or go there).
net::Message SimpleAi::boatPlan(const GameState& state, const ContentRegistry& content, PlayerId me, const Hero& hero) {
    const Army* army = state.army(hero.army);
    if (!army || army->totalUnits() == 0 || m_boatTried.contains(hero.id.value())) return {};
    const Player* player = state.player(me);
    const ResourceAmounts& cost = content.balance().economy.boatCost;
    for (auto it = cost.begin(); it != cost.end(); ++it)
        if (player->resources.value(it.key()) < it.value()) return {};
    for (const Town& t : state.towns()) {
        if (t.owner != me || !hasShipyard(t, content) || !freeWaterNext(state, content, t.pos)) continue;
        if (t.pos == army->pos) {
            m_boatTried.insert(hero.id.value());
            m_homeLand[hero.id.value()] = walkableRegion(state, content, t.pos);
            return command(net::msg::buyBoat, {{"hero", toInt(hero.id.value())}});
        }
        if (!m_failedTargets.contains(key(hero.id.value(), t.pos))) {
            m_lastTarget = key(hero.id.value(), t.pos);
            return command(net::msg::moveHero, {{"hero", toInt(hero.id.value())}, {"x", t.pos.x}, {"y", t.pos.y}});
        }
    }
    return {};
}

// In a boat: land on known shore away from home, else sail to the edge of the known sea.
net::Message SimpleAi::sailMove(const GameState& state, const ContentRegistry& content, PlayerId me, const Hero& hero) {
    const Army* army = state.army(hero.army);
    if (!army) return {};
    const AiView view(state, content, me);
    const QSet<Pos> home = m_homeLand.value(hero.id.value());
    const auto sailable = [&](Pos p) {
        const TerrainDef* t = state.map().inBounds(p) ? content.terrainType(state.map().terrainAt(p)) : nullptr;
        return t && t->sailable;
    };
    const auto walkable = [&](Pos p) {
        const TerrainDef* t = state.map().inBounds(p) ? content.terrainType(state.map().terrainAt(p)) : nullptr;
        return t && t->walkable && !t->sailable;
    };
    const Pos at = army->pos;
    // Land right here if foreign shore is next to us.
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const Pos n{at.x + dx, at.y + dy};
            if ((dx || dy) && walkable(n) && !home.contains(n) && view.explored(n) && !m_failedTargets.contains(key(hero.id.value(), n))) {
                bool free = true;
                for (const Army& a : state.armies()) free = free && a.pos != n;
                if (!free) continue;
                m_lastTarget = key(hero.id.value(), n);
                return command(net::msg::moveHero, {{"hero", toInt(hero.id.value())}, {"x", n.x}, {"y", n.y}});
            }
        }
    // Else the nearest known water next to foreign shore, or next to the unknown.
    Pos best{-1, -1};
    int bestDistance = 1 << 30;
    for (int y = 0; y < state.map().height(); ++y)
        for (int x = 0; x < state.map().width(); ++x) {
            const Pos p{x, y};
            if (!sailable(p) || !view.explored(p) || p == at || m_failedTargets.contains(key(hero.id.value(), p))) continue;
            bool good = false;
            for (int dy = -1; dy <= 1 && !good; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const Pos n{x + dx, y + dy};
                    if (!state.map().inBounds(n)) continue;
                    if ((walkable(n) && !home.contains(n) && view.explored(n)) || !view.explored(n)) good = true;
                }
            const int d = std::max(std::abs(x - at.x), std::abs(y - at.y));
            if (good && d < bestDistance) {
                bestDistance = d;
                best = p;
            }
        }
    if (best.x < 0) return {};
    m_lastTarget = key(hero.id.value(), best);
    return command(net::msg::moveHero, {{"hero", toInt(hero.id.value())}, {"x", best.x}, {"y", best.y}});
}

net::Message SimpleAi::heroMove(const GameState& state, const ContentRegistry& content, PlayerId me,
                                const Hero& hero) {
    const Army* army = state.army(hero.army);
    if (!army) return {};
    const AiView view(state, content, me);
    const double myStrength = armyStrength(state, content, *army);
    const quint32 hid = hero.id.value();

    struct Target {
        Pos pos;
        double value;
    };
    QList<Target> targets;
    // Only what can be walked to: things on other islands need a boat (boatPlan).
    const QSet<Pos> island = walkableRegion(state, content, army->pos);
    const auto add = [&](Pos p, double value) {
        if (p != army->pos && island.contains(p) && !m_failedTargets.contains(key(hid, p))) targets.append({p, value});
    };

    QSet<ArmyId> garrisons;
    for (const Town& t : state.towns()) garrisons.insert(t.garrison);
    // Visible hostile armies: dangerous ones are avoided, weaker ones are prey.
    QList<const Army*> dangers;
    for (const Army& a : state.armies()) {
        if (garrisons.contains(a.id) || !view.visible(a.pos) || !state.isHostile(a.owner, me)) continue;
        const double theirs = armyStrength(state, content, a);
        if (myStrength > m_p.attackMargin * theirs && army->totalUnits() > 0) add(a.pos, (a.hasHero() ? 80 : 30) * m_p.armyValue);
        else if (theirs > myStrength) dangers.append(&a);
    }
    // Inside a strong army's zone of control it would attack us.
    const int zone = zoneOfControlRadius(hero, content);
    const auto risky = [&](Pos p) {
        for (const Army* d : dangers)
            if (distance(d->pos, p) <= zone) return true;
        return false;
    };

    // Own towns: defend when threatened, collect reinforcements when worth it.
    for (const Town& t : state.towns()) {
        if (t.owner != me) continue;
        bool threat = false;
        for (const Army* d : dangers) threat = threat || (d->hasHero() && distance(d->pos, t.pos) <= kThreatRange);
        if (threat) {
            if (army->pos == t.pos) return {};  // stay and defend
            add(t.pos, 150);
            continue;
        }
        const Army* g = state.army(t.garrison);
        if (g && army->totalUnits() > 0 && armyStrength(state, content, *g) >= 0.6 * myStrength) add(t.pos, 60);
        else if (g && army->totalUnits() == 0 && g->totalUnits() > 0) add(t.pos, 200);  // an empty hero gets an army
    }
    if (army->totalUnits() > 0) {
        for (const Town& t : state.towns()) {
            if (!view.visible(t.pos) || !state.isHostile(t.owner, me) || risky(t.pos)) continue;
            // The garrison is secret: only strong heroes try.
            if (myStrength > (2500 + 100 * state.day()) / m_p.townValue) add(t.pos, 120 * m_p.townValue);
            else if (!t.owner.isValid() && myStrength > 1500) add(t.pos, 90 * m_p.townValue);
        }
        for (const Mine& m : state.mines())
            if (view.visible(m.pos) && state.isHostile(m.owner, me) && !risky(m.pos))
                add(m.pos, (m.resourceId == QStringLiteral("gold") ? 70 : 40) * m_p.mineValue);
        for (const MapDwelling& d : state.mapDwellings())
            if (view.visible(d.pos) && state.isHostile(d.owner, me) && !risky(d.pos)) add(d.pos, 35 * m_p.mineValue);
        for (const MapObject& o : state.mapObjects())
            if (view.visible(o.pos) && !risky(o.pos)) add(o.pos, (o.kind == MapObject::Kind::Artifact ? 65 : 30) * m_p.treasureValue);
    }
    // Explore: the nearest edges of the known world.
    const int w = state.map().width(), hgt = state.map().height();
    QList<Pos> frontier;
    for (int y = 0; y < hgt; ++y)
        for (int x = 0; x < w; ++x)
            if (view.isFrontier({x, y})) frontier.append({x, y});
    std::stable_sort(frontier.begin(), frontier.end(),
                     [&](Pos a, Pos b) { return distance(a, army->pos) < distance(b, army->pos); });
    int explore = 0;
    for (int i = 0; i < frontier.size() && explore < 6; ++i)
        if (!risky(frontier[i])) {
            add(frontier[i], 15 * m_p.exploreValue);
            ++explore;
        }
    if (targets.isEmpty()) return {};

    // Keep going where the hero went yesterday while that is still a target
    // (no flip-flopping between far goals).
    if (hero.destination) {
        // Could not get further toward it today: rest instead of walking off elsewhere.
        if (m_failedTargets.contains(key(hid, *hero.destination))) {
            m_resting.insert(hid);
            return {};
        }
        for (const Target& t : targets)
            if (t.pos == *hero.destination) {
                m_lastTarget = key(hid, t.pos);
                return command(net::msg::moveHero, {{"hero", toInt(hid)}, {"x", t.pos.x}, {"y", t.pos.y}});
            }
    }

    // Best value per distance; a little randomness among near-equal choices.
    QList<double> scores;
    double best = 0;
    for (const Target& t : targets) {
        const double s = t.value / (distance(t.pos, army->pos) + 2.0);
        scores.append(s);
        best = std::max(best, s);
    }
    QList<int> good;
    for (int i = 0; i < targets.size(); ++i)
        if (scores[i] >= best * 0.9) good.append(i);
    const Target& pick = targets[good[m_rng.nextInt(0, static_cast<int>(good.size()) - 1)]];
    m_lastTarget = key(hid, pick.pos);
    return command(net::msg::moveHero, {{"hero", toInt(hid)}, {"x", pick.pos.x}, {"y", pick.pos.y}});
}

}  // namespace kot::ai
