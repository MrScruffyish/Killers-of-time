#include "core/rules/Victory.h"

#include <QHash>
#include <QSet>

#include "core/content/ContentRegistry.h"

namespace kot::core {

namespace {
bool hasTown(const GameState& state, PlayerId player) {
    for (const Town& t : state.towns())
        if (t.owner == player) return true;
    return false;
}
bool timeRanOut(const GameState& state, const Player& p) {
    if (state.defeatRule() == QLatin1String("town")) return true;
    if (state.defeatRule() == QLatin1String("all")) return false;
    return p.townlessSince > 0 && state.day() - p.townlessSince >= kTownlessGraceDays;
}
}  // namespace

bool isEliminated(const GameState& state, PlayerId player) {
    if (hasTown(state, player)) return false;
    if (const Player* p = state.player(player); p && timeRanOut(state, *p)) return true;
    for (const Hero& h : state.heroes())
        if (h.owner == player) return false;
    return true;
}

int townlessDaysLeft(const GameState& state, PlayerId player) {
    const Player* p = state.player(player);
    if (state.defeatRule() != QLatin1String("week")) return -1;
    if (!p || p->townlessSince <= 0 || hasTown(state, player) || isEliminated(state, player)) return -1;
    return kTownlessGraceDays - (state.day() - p->townlessSince);
}

void updateTownless(GameState& state) {
    for (Player& p : state.players()) {
        if (hasTown(state, p.id)) {
            p.townlessSince = 0;
            continue;
        }
        if (p.townlessSince == 0) p.townlessSince = state.day();
        if (!timeRanOut(state, p)) continue;
        // Out of the game: his heroes and their armies leave the map.
        QList<HeroId> gone;
        for (const Hero& h : state.heroes())
            if (h.owner == p.id) gone << h.id;
        for (const HeroId id : gone) {
            for (auto it = state.armies().begin(); it != state.armies().end();) {
                if (it->hero == id) it = state.armies().erase(it);
                else ++it;
            }
            state.heroes().remove(id);
        }
    }
}

GameOutcome gameOutcome(const GameState& state, const ContentRegistry& content) {
    QSet<TeamId> alive;
    for (const Player& p : state.players())
        if (!isEliminated(state, p.id)) alive.insert(p.team);
    GameOutcome o;
    o.over = alive.size() <= 1;
    if (o.over && alive.size() == 1) o.winner = *alive.begin();
    if (o.over) return o;

    const VictoryCondition& v = state.victory();
    switch (v.type) {
    case VictoryCondition::Type::HoldTown:
        if (const Town* t = state.town(v.town); t && t->owner.isValid()) {
            o.over = true;
            o.winner = state.teamOf(t->owner);
        }
        break;
    case VictoryCondition::Type::Gold:
        for (const Player& p : state.players())
            if (v.amount > 0 && p.resources.value(content.currency()) >= v.amount && !isEliminated(state, p.id)) {
                o.over = true;
                o.winner = p.team;
                break;
            }
        break;
    case VictoryCondition::Type::Days:
        if (v.amount > 0 && state.day() > v.amount) {
            // Time is up: the team with the most towns wins (none if tied).
            QHash<TeamId, int> towns;
            for (const Town& t : state.towns())
                if (t.owner.isValid()) ++towns[state.teamOf(t.owner)];
            int best = -1;
            bool tie = false;
            for (auto it = towns.cbegin(); it != towns.cend(); ++it) {
                if (it.value() > best) {
                    best = it.value();
                    o.winner = it.key();
                    tie = false;
                } else if (it.value() == best) {
                    tie = true;
                }
            }
            if (tie) o.winner = TeamId();
            o.over = true;
        }
        break;
    case VictoryCondition::Type::Conquest:
        break;
    }
    return o;
}

QString describeVictory(const GameState& state) {
    const VictoryCondition& v = state.victory();
    switch (v.type) {
    case VictoryCondition::Type::HoldTown: {
        const Town* t = state.town(v.town);
        return QStringLiteral("Take %1 (or defeat everyone)").arg(t ? t->name : QStringLiteral("the town"));
    }
    case VictoryCondition::Type::Gold:
        return QStringLiteral("Be the first with %1 gold (or defeat everyone)").arg(v.amount);
    case VictoryCondition::Type::Days:
        return QStringLiteral("Hold the most towns after day %1 (or defeat everyone)").arg(v.amount);
    case VictoryCondition::Type::Conquest:
        break;
    }
    return QStringLiteral("Defeat everyone");
}

QString describeDefeat(const GameState& state) {
    if (state.defeatRule() == QLatin1String("town")) return QStringLiteral("A player who loses his last town is out at once");
    if (state.defeatRule() == QLatin1String("all")) return QStringLiteral("A player is out only when he has no towns and no heroes left");
    return QStringLiteral("A player who loses his last town has %1 days to take one back, else he is out").arg(kTownlessGraceDays);
}

}  // namespace kot::core
