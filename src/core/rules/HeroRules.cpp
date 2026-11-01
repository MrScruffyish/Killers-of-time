#include "core/rules/HeroRules.h"

#include <algorithm>

#include "core/content/ContentRegistry.h"
#include "core/rules/Movement.h"

namespace kot::core {

namespace {
void setError(QString* error, const QString& message) {
    if (error) *error = message;
}
}  // namespace

int maxSpellPoints(const Hero& hero) {
    return hero.trait(QStringLiteral("spell_points_max"));
}

HeroId createHero(GameState& state, const ContentRegistry& content, PlayerId owner,
                  const QString& name, const QString& classId, Pos pos, QString* error) {
    if (!state.player(owner)) {
        setError(error, QStringLiteral("unknown owner"));
        return {};
    }
    const HeroClassDef* cls = content.heroClass(classId);
    if (!cls) {
        setError(error, QStringLiteral("unknown hero class '%1'").arg(classId));
        return {};
    }
    Hero hero;
    hero.id = state.newId<HeroId>();
    hero.name = name;
    hero.classId = classId;
    hero.owner = owner;
    hero.traits = cls->baseTraits;
    hero.spellPoints = maxSpellPoints(hero);
    hero.army = state.addArmy(owner, pos);
    state.army(hero.army)->hero = hero.id;
    hero.movementPoints = armyMovementPoints(*state.army(hero.army), &hero, content);
    const HeroId id = hero.id;
    state.heroes().insert(id, hero);
    return id;
}

int addExperience(Hero& hero, const ContentRegistry& content, int experience) {
    if (experience <= 0) return 0;
    hero.experience += experience;
    const HeroClassDef* cls = content.heroClass(hero.classId);
    const QList<int>& thresholds = content.xpThresholds();
    int gained = 0;
    while (hero.level < content.maxLevel() && hero.experience >= thresholds[hero.level - 1]) {
        ++hero.level;
        ++gained;
        ++hero.pendingTraitPicks;
        if (cls) {
            for (auto it = cls->autoGrowth.begin(); it != cls->autoGrowth.end(); ++it)
                hero.traits[it.key()] += it.value();
        }
    }
    return gained;
}

bool pickTrait(Hero& hero, const ContentRegistry& content, const QString& traitId,
               QString* error) {
    if (hero.pendingTraitPicks <= 0) {
        setError(error, QStringLiteral("no trait pick available"));
        return false;
    }
    const TraitDef* trait = content.trait(traitId);
    if (!trait) {
        setError(error, QStringLiteral("unknown trait '%1'").arg(traitId));
        return false;
    }
    if (!trait->pickable) {
        setError(error, QStringLiteral("trait '%1' cannot be picked").arg(traitId));
        return false;
    }
    const HeroClassDef* cls = content.heroClass(hero.classId);
    const int increment = cls ? cls->pickIncrement.value(traitId, 1) : 1;
    hero.traits[traitId] += increment;
    --hero.pendingTraitPicks;
    return true;
}

int regenSpellPoints(Hero& hero) {
    const int missing = std::max(0, maxSpellPoints(hero) - hero.spellPoints);
    const int gained = std::min(missing, hero.trait(QStringLiteral("spell_recovery")));
    hero.spellPoints += gained;
    return gained;
}

}  // namespace kot::core
