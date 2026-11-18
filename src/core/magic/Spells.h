#pragma once

#include <QString>
#include <QStringList>

#include "core/content/Defs.h"
#include "core/model/GameState.h"
#include "core/rules/ArmyRules.h"

namespace kot::core {

class ContentRegistry;

// Magic rules (PROJECT.md 5.9, first simple version). A hero casts spells from his
// spellbook (Hero::spells) with spell points; the spell's requiredMagic must not be
// above his Magic trait. Effects get stronger with his Spell Power trait.

// Strength of one effect for this caster: amount + perPower * Spell Power.
int effectStrength(const SpellEffect& effect, int spellPower);

// Why `hero` cannot cast `spellId` now in `scope` (empty = he can). Checks the
// spellbook, the scope, the Magic level and the spell points (`spellPoints`, so a
// battle can pass its own running count).
QString whyCannotCast(const Hero& hero, const ContentRegistry& content, SpellId spellId,
                      SpellDef::Scope scope, int spellPoints);

// Casts an adventure-map spell (movement, reveal). Costs the spell points.
ActionResult castAdventureSpell(GameState& state, const ContentRegistry& content, HeroId hero,
                                SpellId spellId);

// The best mage guild level of a town (0 = none).
int mageGuildLevel(const Town& town, const ContentRegistry& content);

// A hero standing in a town of his team that has a mage guild learns every spell
// up to the guild's level, of the schools the town's race teaches. Returns the spells learned.
QList<SpellId> learnSpellsInTown(GameState& state, const ContentRegistry& content, HeroId hero);

}  // namespace kot::core
