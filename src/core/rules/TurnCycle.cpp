#include "core/rules/TurnCycle.h"

#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Movement.h"
#include "core/rules/Vision.h"

namespace kot::core {

ResourceAmounts dailyIncome(const GameState& state, const ContentRegistry& content,
                            PlayerId player) {
    ResourceAmounts income;
    const QString currency = content.currency() ? content.currency()->id : QString();
    for (const Town& t : state.towns()) {
        if (t.owner == player && !currency.isEmpty())
            income[currency] += content.balance().economy.townDailyGold;
    }
    for (const Mine& m : state.mines()) {
        if (m.owner == player) income[m.resourceId] += m.dailyAmount;
    }
    return income;
}

void startTurn(GameState& state, const ContentRegistry& content, PlayerId playerId) {
    Player* player = state.player(playerId);
    if (!player) return;
    if (state.day() > 1) {
        const ResourceAmounts income = dailyIncome(state, content, playerId);
        for (auto it = income.begin(); it != income.end(); ++it) player->resources[it.key()] += it.value();
    }
    for (Hero& hero : state.heroes()) {
        if (hero.owner == playerId) regenSpellPoints(hero);
    }
    refreshHeroMovement(state, content, playerId);
    updateExplored(state, content, player->team);
}

void beginGame(GameState& state, const ContentRegistry& content) {
    state.setCurrentIndex(0);
    startTurn(state, content, state.currentPlayer());
}

TurnInfo endTurn(GameState& state, const ContentRegistry& content) {
    TurnInfo info;
    const int count = static_cast<int>(state.turnOrder().size());
    if (count == 0) return info;
    const int next = (state.currentIndex() + 1) % count;
    info.newDay = (next == 0);
    if (info.newDay) state.setDay(state.day() + 1);
    state.setCurrentIndex(next);
    info.player = state.currentPlayer();
    info.day = state.day();
    startTurn(state, content, info.player);
    return info;
}

}  // namespace kot::core
