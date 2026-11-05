#pragma once

#include <QString>
#include <QStringList>

#include "core/model/GameState.h"
#include "core/rules/ArmyRules.h"

namespace kot::core {

class ContentRegistry;

// True if the town has a built building with the tavern effect.
bool hasTavern(const Town& town, const ContentRegistry& content);

// Hero templates of `race` that are not in the game right now (not hired by anyone).
QList<HeroTemplateId> freeHeroTemplates(const GameState& state, const ContentRegistry& content,
                              RaceId race);

// Draws new offers (up to balance `tavernOffers`, all different, random but
// reproducible from the game's seed) for one town, or for every town when `town`
// is invalid. Towns without a tavern have no offers. A hero offered in one
// town is not offered in another one (in the same draw, or for one town: now).
void refreshTavernOffers(GameState& state, const ContentRegistry& content, TownId town = {});

// The start of a day that is not a new week: a tavern with no offers left (all
// hired, or none were free) gets new ones, e.g. after a hero fell.
void refillEmptyTaverns(GameState& state, const ContentRegistry& content);

struct HireResult : ActionResult {
    HeroId hero;
};

// Hires an offered hero in a town you own: pays `heroCost` gold and the new hero
// appears in the town with his class's starting traits and no army. Only one hero
// can stand in a town, a tavern hires out one hero a week, and a player has at
// most `maxHeroesPerPlayer`. With
// `useTrade`, missing gold is bought by trading spare resources.
HireResult hireHero(GameState& state, const ContentRegistry& content, TownId town,
                    PlayerId player, HeroTemplateId templateId, bool useTrade = false);

}  // namespace kot::core
