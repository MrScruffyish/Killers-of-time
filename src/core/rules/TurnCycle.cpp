#include "core/rules/TurnCycle.h"
#include "core/rules/GameStats.h"

#include <algorithm>

#include "core/magic/Spells.h"
#include "core/rules/Victory.h"

#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Movement.h"
#include "core/rules/Tavern.h"
#include "core/rules/TownRules.h"
#include "core/rules/Vision.h"

namespace kot::core {

ResourceAmounts dailyIncome(const GameState& state, const ContentRegistry& content,
                            PlayerId player) {
    ResourceAmounts income;
    const ResourceId currency = content.currency();
    for (const Town& t : state.towns()) {
        if (t.owner != player) continue;
        income[currency] += content.balance().economy.townDailyGold * (100 + content.race(t.race).incomePercent) / 100;
        for (const auto [r, amount] : buildingIncome(t, content)) income[r] += amount;
    }
    for (const Mine& m : state.mines()) {
        if (m.owner == player) income[m.resource] += m.dailyAmount;
    }
    // Difficulty bonus or malus (AI players).
    const Player* p = state.player(player);
    if (p && p->incomePercent != 0)
        for (const auto [r, amount] : ResourceAmounts(income)) income[r] = std::max(0, amount * (100 + p->incomePercent) / 100);
    return income;
}

void startTurn(GameState& state, const ContentRegistry& content, PlayerId playerId) {
    Player* player = state.player(playerId);
    if (!player) return;
    if (state.day() > 1) {
        for (const auto [r, amount] : dailyIncome(state, content, playerId)) player->resources[r] += amount;
    }
    QList<HeroId> heroes;
    for (Hero& hero : state.heroes()) {
        if (hero.owner != playerId) continue;
        regenSpellPoints(hero, content);
        heroes.append(hero.id);
    }
    for (const HeroId h : heroes) learnSpellsInTown(state, content, h);  // heroes in a town with a mage guild
    refreshHeroMovement(state, content, playerId);
    updateExplored(state, content, player->team);
}

void beginGame(GameState& state, const ContentRegistry& content) {
    state.setCurrentIndex(0);
    startTurn(state, content, state.currentPlayer());
    recordWeekStats(state, content);  // week 1 as it starts
}

void startWeekEvent(GameState& state, const ContentRegistry& content) {
    state.setWeekEvent(WeekEventId());
    int total = 0;
    for (const WeekEventDef& e : content.weekEvents()) total += e.weight;
    if (total <= 0) return;
    int roll = state.rng().nextInt(1, total);
    for (const WeekEventDef& e : content.weekEvents()) {
        roll -= e.weight;
        if (roll > 0) continue;
        state.setWeekEvent(e.id);
        for (Player& p : state.players())
            for (const auto [r, amount] : e.resources) p.resources[r] = std::max(0, p.resources.value(r) + amount);
        return;
    }
}

TurnInfo endTurn(GameState& state, const ContentRegistry& content) {
    TurnInfo info;
    const int count = static_cast<int>(state.turnOrder().size());
    if (count == 0) return info;
    updateTownless(state);  // who lost his last town this turn
    // Players who are out of the game are skipped (but the day still advances).
    int next = state.currentIndex();
    for (int i = 0; i < count; ++i) {
        next = (next + 1) % count;
        if (next == 0) {
            info.newDay = true;
            state.setDay(state.day() + 1);
            if (isNewWeek(state.day())) {
                startWeekEvent(state, content);
                applyWeeklyGrowth(state, content);
                refreshTavernOffers(state, content);
                recordWeekStats(state, content);
            } else {
                refillEmptyTaverns(state, content);
            }
        }
        if (info.newDay) updateTownless(state);  // a new day: whose time ran out
        if (!isEliminated(state, state.turnOrder()[next])) break;
    }
    state.setCurrentIndex(next);
    info.player = state.currentPlayer();
    info.day = state.day();
    startTurn(state, content, info.player);
    return info;
}

}  // namespace kot::core
