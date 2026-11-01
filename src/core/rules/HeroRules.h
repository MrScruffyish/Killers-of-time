#pragma once

#include <QString>

#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

// Rules for heroes: creation, experience and levels, trait picks, spell points.

// Creates a hero (and the army he leads) at `pos`, with the class's starting
// traits and a full spell point pool. Returns an invalid id and fills `error`
// when the owner or class is unknown.
HeroId createHero(GameState& state, const ContentRegistry& content, PlayerId owner,
                  const QString& name, const QString& classId, Pos pos, QString* error = nullptr);

int maxSpellPoints(const Hero& hero);

// Adds experience. For every level gained (up to the max level): the class's
// auto-growth traits increase (e.g. max spell points) and one trait pick
// becomes pending. Returns the number of levels gained.
int addExperience(Hero& hero, const ContentRegistry& content, int experience);

// Spends one pending pick to boost `traitId` by the class's increment.
bool pickTrait(Hero& hero, const ContentRegistry& content, const QString& traitId,
               QString* error = nullptr);

// Daily recovery: + "spell_recovery" spell points, capped at the maximum.
// Returns the spell points actually regained.
int regenSpellPoints(Hero& hero);

}  // namespace kot::core
