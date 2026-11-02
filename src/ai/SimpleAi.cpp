#include "ai/SimpleAi.h"

#include <QJsonObject>
#include <algorithm>
#include <cmath>

#include "ai/AiView.h"
#include "core/content/ContentRegistry.h"
#include "core/magic/Spells.h"
#include "net/Protocol.h"

namespace kot::ai {

using namespace kot::core;

namespace {

constexpr int kMaxMovesPerHero = 4;
constexpr double kAttackMargin = 1.3;  // attack only clearly weaker armies
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

QString SimpleAi::profileName() const {
    return QStringLiteral("simple");
}

void SimpleAi::beginTurn(const GameState&, const ContentRegistry&, PlayerId) {
    m_built = m_hired = m_sentUnits = false;
    m_garrisonTaken.clear();
    m_moves.clear();
    m_heroDone.clear();
    m_spellCast.clear();
    m_failedTargets.clear();
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
        const int wanted = state.day() < 14 ? 2 : state.day() < 28 ? 3 : 4;
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
        const net::Message move = heroMove(state, content, me, h);
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
    const auto add = [&](Pos p, double value) {
        if (p != army->pos && !m_failedTargets.contains(key(hid, p))) targets.append({p, value});
    };

    QSet<ArmyId> garrisons;
    for (const Town& t : state.towns()) garrisons.insert(t.garrison);
    // Visible hostile armies: dangerous ones are avoided, weaker ones are prey.
    QList<const Army*> dangers;
    for (const Army& a : state.armies()) {
        if (garrisons.contains(a.id) || !view.visible(a.pos) || !state.isHostile(a.owner, me)) continue;
        const double theirs = armyStrength(state, content, a);
        if (myStrength > kAttackMargin * theirs && army->totalUnits() > 0) add(a.pos, a.hasHero() ? 80 : 30);
        else if (theirs > myStrength) dangers.append(&a);
    }
    const auto risky = [&](Pos p) {
        for (const Army* d : dangers)
            if (distance(d->pos, p) <= 1) return true;
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
            if (myStrength > 2500 + 100 * state.day()) add(t.pos, 120);
            else if (!t.owner.isValid() && myStrength > 1500) add(t.pos, 90);
        }
        for (const Mine& m : state.mines())
            if (view.visible(m.pos) && state.isHostile(m.owner, me) && !risky(m.pos))
                add(m.pos, m.resourceId == QStringLiteral("gold") ? 70 : 40);
        for (const MapDwelling& d : state.mapDwellings())
            if (view.visible(d.pos) && state.isHostile(d.owner, me) && !risky(d.pos)) add(d.pos, 35);
        for (const MapObject& o : state.mapObjects())
            if (view.visible(o.pos) && !risky(o.pos)) add(o.pos, o.kind == MapObject::Kind::Artifact ? 65 : 30);
    }
    // Explore: the nearest edges of the known world.
    const int w = state.map().width(), hgt = state.map().height();
    QList<Pos> frontier;
    for (int y = 0; y < hgt; ++y)
        for (int x = 0; x < w; ++x)
            if (view.isFrontier({x, y})) frontier.append({x, y});
    std::stable_sort(frontier.begin(), frontier.end(),
                     [&](Pos a, Pos b) { return distance(a, army->pos) < distance(b, army->pos); });
    for (int i = 0; i < std::min<qsizetype>(6, frontier.size()); ++i) add(frontier[i], 15);
    if (targets.isEmpty()) return {};

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
