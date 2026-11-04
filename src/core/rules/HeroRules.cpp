#include "core/rules/HeroRules.h"

#include <algorithm>

#include "core/content/ContentRegistry.h"
#include "core/rules/ArmyRules.h"
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
    hero.spells = cls->startingSpells;
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

namespace {
int wornInSlot(const Hero& hero, const ContentRegistry& content, const QString& slot) {
    int n = 0;
    for (const QString& id : hero.artifacts)
        if (const ArtifactDef* a = content.artifact(id); a && a->slot == slot) ++n;
    return n;
}
void applyBonus(Hero& hero, const ArtifactDef& a, int sign) {
    for (auto it = a.traits.begin(); it != a.traits.end(); ++it) {
        hero.traits[it.key()] += sign * it.value();
        if (hero.traits[it.key()] == 0) hero.traits.remove(it.key());
    }
    hero.spellPoints = std::min(hero.spellPoints, std::max(0, maxSpellPoints(hero)));
}
}  // namespace

bool takeArtifact(Hero& hero, const ContentRegistry& content, const QString& artifactId) {
    const ArtifactDef* a = content.artifact(artifactId);
    if (!a) return false;
    if (wornInSlot(hero, content, a->slot) < artifactSlotCount(a->slot)) {
        hero.artifacts.append(a->id);
        applyBonus(hero, *a, +1);
        return true;
    }
    hero.backpack.append(a->id);
    return false;
}

bool equipArtifact(Hero& hero, const ContentRegistry& content, const QString& artifactId, const QString& replace,
                   QString* error) {
    const ArtifactDef* a = content.artifact(artifactId);
    if (!a || !hero.backpack.contains(artifactId)) {
        setError(error, QStringLiteral("the hero does not carry that artifact"));
        return false;
    }
    if (wornInSlot(hero, content, a->slot) >= artifactSlotCount(a->slot)) {
        QString off;
        if (!replace.isEmpty()) {
            const ArtifactDef* r = content.artifact(replace);
            if (!r || r->slot != a->slot || !hero.artifacts.contains(replace)) {
                setError(error, QStringLiteral("that artifact is not worn in the same place"));
                return false;
            }
            off = replace;
        } else {
            for (const QString& id : hero.artifacts)
                if (const ArtifactDef* w = content.artifact(id); w && w->slot == a->slot) {
                    off = id;
                    break;
                }
        }
        if (!unequipArtifact(hero, content, off, error)) return false;
    }
    hero.backpack.removeOne(artifactId);
    hero.artifacts.append(artifactId);
    applyBonus(hero, *a, +1);
    return true;
}

bool unequipArtifact(Hero& hero, const ContentRegistry& content, const QString& artifactId, QString* error) {
    const ArtifactDef* a = content.artifact(artifactId);
    if (!a || !hero.artifacts.contains(artifactId)) {
        setError(error, QStringLiteral("the hero does not wear that artifact"));
        return false;
    }
    hero.artifacts.removeOne(artifactId);
    hero.backpack.append(artifactId);
    applyBonus(hero, *a, -1);
    return true;
}

bool giveArtifact(GameState& state, const ContentRegistry& content, HeroId fromId, HeroId toId, const QString& artifactId,
                  QString* error) {
    Hero* from = state.hero(fromId);
    Hero* to = state.hero(toId);
    if (!from || !to || from == to) {
        setError(error, QStringLiteral("two different heroes are needed"));
        return false;
    }
    if (from->owner != to->owner) {
        setError(error, QStringLiteral("the heroes belong to different players"));
        return false;
    }
    const Army* a = state.army(from->army);
    const Army* b = state.army(to->army);
    if (!a || !b || !armiesMeet(*a, *b)) {
        setError(error, QStringLiteral("the heroes must stand side by side"));
        return false;
    }
    if (from->artifacts.contains(artifactId) && !unequipArtifact(*from, content, artifactId, error)) return false;
    if (!from->backpack.removeOne(artifactId)) {
        setError(error, QStringLiteral("the hero does not have that artifact"));
        return false;
    }
    takeArtifact(*to, content, artifactId);
    return true;
}

}  // namespace kot::core
