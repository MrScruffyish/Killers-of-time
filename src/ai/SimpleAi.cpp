#include "ai/SimpleAi.h"

#include <QJsonObject>
#include <algorithm>

#include "ai/AiView.h"
#include "core/content/ContentRegistry.h"
#include "core/magic/Spells.h"
#include "net/Protocol.h"

namespace kot::ai {

using namespace kot::core;

namespace {

constexpr int kMaxMovesPerHero = 4;
constexpr double kAttackMargin = 1.3;  // attack only clearly weaker armies
constexpr int kHeroWanted = 3;

net::Message command(const QString& type, QJsonObject body = {}) {
    return net::Message{type, std::move(body), 0};
}

int toInt(quint32 v) { return static_cast<int>(v); }

int distance(Pos a, Pos b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }

}  // namespace

QString SimpleAi::profileName() const {
    return QStringLiteral("simple");
}

void SimpleAi::beginTurn(const GameState&, const ContentRegistry&, PlayerId) {
    m_built = false;
    m_hired = false;
    m_garrisonTaken.clear();
    m_moves.clear();
    m_heroDone.clear();
    m_spellCast.clear();
}

void SimpleAi::commandDone(const net::Message& cmd, bool ok) {
    if (cmd.type == net::msg::moveHero && !ok) m_heroDone.insert(static_cast<quint32>(cmd.body.value("hero").toInt()));
}

net::Message SimpleAi::nextCommand(const GameState& state, const ContentRegistry& content, PlayerId me) {
    const Player* player = state.player(me);
    if (!player) return command(net::msg::endTurn);

    // 1. Level-up picks.
    static const QStringList battleTraits = {"attack", "defense", "hit_points", "movement_points"};
    for (const Hero& h : state.heroes()) {
        if (h.owner == me && h.pendingTraitPicks > 0) {
            const QString trait = battleTraits[m_rng.nextInt(0, static_cast<int>(battleTraits.size()) - 1)];
            return command(net::msg::pickTrait, {{"hero", toInt(h.id.value())}, {"trait", trait}});
        }
    }

    // 2. Build, then buy units everywhere.
    if (!m_built) {
        m_built = true;
        return command(net::msg::buildThenBuy, {{"useTrade", true}});
    }

    // 3. Hire a hero when rich enough.
    if (!m_hired) {
        m_hired = true;
        int heroes = 0;
        for (const Hero& h : state.heroes()) heroes += (h.owner == me);
        const int cost = content.balance().economy.heroCost;
        if (heroes < kHeroWanted && player->resources.value("gold") >= cost + 1500) {
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

    // 4. Heroes in an own town take the garrison along (one stack per command).
    for (const Hero& h : state.heroes()) {
        if (h.owner != me || m_garrisonTaken.contains(h.id.value())) continue;
        const Army* army = state.army(h.army);
        if (!army) continue;
        for (const Town& t : state.towns()) {
            if (t.owner != me || t.pos != army->pos) continue;
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

    // 5. Move heroes (a movement spell first, once a turn).
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
        const net::Message move = heroMove(state, content, me, h);
        if (move.type.isEmpty()) {
            m_heroDone.insert(h.id.value());
            continue;
        }
        ++m_moves[h.id.value()];
        return move;
    }

    // 6. Done.
    return command(net::msg::endTurn);
}

net::Message SimpleAi::heroMove(const GameState& state, const ContentRegistry& content, PlayerId me,
                                const Hero& hero) {
    const Army* army = state.army(hero.army);
    if (!army || army->totalUnits() == 0) return {};
    const AiView view(state, content, me);
    const double myStrength = armyStrength(state, content, *army);

    QList<Pos> targets;
    for (const Mine& m : state.mines())
        if (view.visible(m.pos) && state.isHostile(m.owner, me)) targets.append(m.pos);
    for (const MapDwelling& d : state.mapDwellings())
        if (view.visible(d.pos) && state.isHostile(d.owner, me)) targets.append(d.pos);
    for (const MapObject& o : state.mapObjects())
        if (view.visible(o.pos)) targets.append(o.pos);
    for (const Town& t : state.towns()) {
        if (!view.visible(t.pos) || !state.isHostile(t.owner, me)) continue;
        // The garrison is secret: guess it is about as strong as we are, so only strong heroes go.
        if (myStrength > 2000) targets.append(t.pos);
    }
    QSet<ArmyId> garrisons;
    for (const Town& t : state.towns()) garrisons.insert(t.garrison);
    for (const Army& a : state.armies()) {
        if (garrisons.contains(a.id) || !view.visible(a.pos) || !state.isHostile(a.owner, me)) continue;
        if (myStrength > kAttackMargin * armyStrength(state, content, a)) targets.append(a.pos);
    }

    if (targets.isEmpty()) {
        // Explore: frontier tiles in sight distance, nearest first.
        const int w = state.map().width(), hgt = state.map().height();
        for (int y = 0; y < hgt; ++y)
            for (int x = 0; x < w; ++x)
                if (view.isFrontier({x, y}) && Pos{x, y} != army->pos) targets.append({x, y});
    }
    if (targets.isEmpty()) return {};

    std::stable_sort(targets.begin(), targets.end(),
                     [&](Pos a, Pos b) { return distance(a, army->pos) < distance(b, army->pos); });
    const int pick = m_rng.nextInt(0, std::min(2, static_cast<int>(targets.size()) - 1));
    const Pos target = targets[pick];
    return command(net::msg::moveHero, {{"hero", toInt(hero.id.value())}, {"x", target.x}, {"y", target.y}});
}

}  // namespace kot::ai
