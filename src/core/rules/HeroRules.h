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
                  const QString& name, HeroClassId classId, Pos pos, QString* error = nullptr);

int maxSpellPoints(const Hero& hero, const ContentRegistry& content);

// Makes the hero the tavern hero `templateId` (name kept): remembers it and gives
// the specialty's trait points (its income and unit bonus count where they apply:
// heroSpecialty).
void giveHeroTemplate(Hero& hero, const ContentRegistry& content, HeroTemplateId templateId);
// The hero's specialty (empty when he is no tavern hero or it has none).
const HeroSpecialty* heroSpecialty(const Hero& hero, const ContentRegistry& content);
// Whether a specialty's unit bonus counts for `unit`: the unit, or one it upgrades from.
bool specialtyFits(const HeroSpecialty& specialty, UnitTypeId unit, const ContentRegistry& content);
// The specialty in a few words: "Archers: +2 attack, +1 defense", "+350 gold a day".
QString describeSpecialty(const HeroSpecialty& specialty, const ContentRegistry& content);

// Adds experience. For every level gained (up to the max level): the class's
// auto-growth traits increase (e.g. max spell points) and one trait pick
// becomes pending. Returns the number of levels gained.
int addExperience(Hero& hero, const ContentRegistry& content, int experience);

// Spends one pending pick to boost `traitId` by the class's increment.
bool pickTrait(Hero& hero, const ContentRegistry& content, TraitId traitId,
               QString* error = nullptr);

// Artifacts. A hero wears a limited number per slot (artifactSlots()); the
// bonuses of worn ones are added to his traits, carried ones (the backpack) give
// nothing. Picking one up wears it when its slot has room, else it goes to the
// backpack; returns true when worn.
bool takeArtifact(Hero& hero, const ContentRegistry& content, ArtifactId artifactId);
// Wears an artifact from the backpack. When its slot is full, `replace` (worn,
// same slot) or else the first worn one of that slot goes to the backpack.
bool equipArtifact(Hero& hero, const ContentRegistry& content, ArtifactId artifactId,
                   ArtifactId replace = {}, QString* error = nullptr);
// Takes a worn artifact off into the backpack.
bool unequipArtifact(Hero& hero, const ContentRegistry& content, ArtifactId artifactId, QString* error = nullptr);

// Two of a player's heroes who meet (ArmyRules armiesMeet: side by side): one gives
// an artifact (worn or carried) to the other, who wears it when its slot has room,
// else carries it.
bool giveArtifact(GameState& state, const ContentRegistry& content, HeroId from, HeroId to, ArtifactId artifactId,
                  QString* error = nullptr);

// Daily recovery: + "spell_recovery" spell points, capped at the maximum.
// Returns the spell points actually regained.
int regenSpellPoints(Hero& hero, const ContentRegistry& content);

}  // namespace kot::core
