#include "core/rules/Victory.h"

#include <QHash>
#include <QSet>

namespace kot::core {

bool isEliminated(const GameState& state, PlayerId player) {
    for (const Town& t : state.towns())
        if (t.owner == player) return false;
    for (const Hero& h : state.heroes())
        if (h.owner == player) return false;
    return true;
}

GameOutcome gameOutcome(const GameState& state) {
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
            if (v.amount > 0 && p.resources.value(QStringLiteral("gold")) >= v.amount && !isEliminated(state, p.id)) {
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

}  // namespace kot::core
