#include "core/rules/Victory.h"

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
    return o;
}

}  // namespace kot::core
