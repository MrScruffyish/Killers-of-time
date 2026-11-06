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

int maxSpellPoints(const Hero& hero, const ContentRegistry& content) {
    return hero.trait(content.known().spellPointsMax);
}

HeroId createHero(GameState& state, const ContentRegistry& content, PlayerId owner,
                  const QString& name, HeroClassId classId, Pos pos, QString* error) {
    if (!state.player(owner)) {
        setError(error, QStringLiteral("unknown owner"));
        return {};
    }
    const HeroClassDef* cls = content.heroClass(classId);
    if (!cls) {
        setError(error, QStringLiteral("unknown hero class %1").arg(classId.index()));
        return {};
    }
    Hero hero;
    hero.id = state.newId<HeroId>();
    hero.name = name;
    hero.classId = classId;
    hero.owner = owner;
    hero.traits = cls->baseTraits;
    hero.spellPoints = maxSpellPoints(hero, content);
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
            for (const auto [t, v] : cls->autoGrowth) hero.traits[t] += v;
        }
    }
    return gained;
}

bool pickTrait(Hero& hero, const ContentRegistry& content, TraitId traitId,
               QString* error) {
    if (hero.pendingTraitPicks <= 0) {
        setError(error, QStringLiteral("no trait pick available"));
        return false;
    }
    const TraitDef* trait = content.trait(traitId);
    if (!trait) {
        setError(error, QStringLiteral("unknown trait %1").arg(traitId.index()));
        return false;
    }
    if (!trait->pickable) {
        setError(error, QStringLiteral("trait '%1' cannot be picked").arg(trait->key));
        return false;
    }
    const HeroClassDef* cls = content.heroClass(hero.classId);
    const int increment = cls && cls->pickIncrement.value(traitId) > 0 ? cls->pickIncrement.value(traitId) : 1;
    hero.traits[traitId] += increment;
    --hero.pendingTraitPicks;
    return true;
}

int regenSpellPoints(Hero& hero, const ContentRegistry& content) {
    const int missing = std::max(0, maxSpellPoints(hero, content) - hero.spellPoints);
    const int gained = std::min(missing, hero.trait(content.known().spellRecovery));
    hero.spellPoints += gained;
    return gained;
}

namespace {
int wornInSlot(const Hero& hero, const ContentRegistry& content, const QString& slot) {
    int n = 0;
    for (const ArtifactId id : hero.artifacts)
        if (const ArtifactDef* a = content.artifact(id); a && a->slot == slot) ++n;
    return n;
}
void applyBonus(Hero& hero, const ContentRegistry& content, const ArtifactDef& a, int sign) {
    for (const auto [t, v] : a.traits) hero.traits[t] += sign * v;
    hero.spellPoints = std::min(hero.spellPoints, std::max(0, maxSpellPoints(hero, content)));
}
}  // namespace

bool takeArtifact(Hero& hero, const ContentRegistry& content, ArtifactId artifactId) {
    const ArtifactDef* a = content.artifact(artifactId);
    if (!a) return false;
    if (wornInSlot(hero, content, a->slot) < artifactSlotCount(a->slot)) {
        hero.artifacts.append(a->id);
        applyBonus(hero, content, *a, +1);
        return true;
    }
    hero.backpack.append(a->id);
    return false;
}

bool equipArtifact(Hero& hero, const ContentRegistry& content, ArtifactId artifactId, ArtifactId replace,
                   QString* error) {
    const ArtifactDef* a = content.artifact(artifactId);
    if (!a || !hero.backpack.contains(artifactId)) {
        setError(error, QStringLiteral("the hero does not carry that artifact"));
        return false;
    }
    if (wornInSlot(hero, content, a->slot) >= artifactSlotCount(a->slot)) {
        ArtifactId off;
        if (replace.isValid()) {
            const ArtifactDef* r = content.artifact(replace);
            if (!r || r->slot != a->slot || !hero.artifacts.contains(replace)) {
                setError(error, QStringLiteral("that artifact is not worn in the same place"));
                return false;
            }
            off = replace;
        } else {
            for (const ArtifactId id : hero.artifacts)
                if (const ArtifactDef* w = content.artifact(id); w && w->slot == a->slot) {
                    off = id;
                    break;
                }
        }
        if (!unequipArtifact(hero, content, off, error)) return false;
    }
    hero.backpack.removeOne(artifactId);
    hero.artifacts.append(artifactId);
    applyBonus(hero, content, *a, +1);
    return true;
}

bool unequipArtifact(Hero& hero, const ContentRegistry& content, ArtifactId artifactId, QString* error) {
    const ArtifactDef* a = content.artifact(artifactId);
    if (!a || !hero.artifacts.contains(artifactId)) {
        setError(error, QStringLiteral("the hero does not wear that artifact"));
        return false;
    }
    hero.artifacts.removeOne(artifactId);
    hero.backpack.append(artifactId);
    applyBonus(hero, content, *a, -1);
    return true;
}

bool giveArtifact(GameState& state, const ContentRegistry& content, HeroId fromId, HeroId toId, ArtifactId artifactId,
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

void giveHeroTemplate(Hero& hero, const ContentRegistry& content, HeroTemplateId templateId) {
    hero.templateId = templateId;
    if (const HeroSpecialty* sp = heroSpecialty(hero, content); sp && sp->trait.isValid()) hero.traits[sp->trait] += sp->traitAmount;
}

const HeroSpecialty* heroSpecialty(const Hero& hero, const ContentRegistry& content) {
    const HeroTemplateDef* t = content.heroTemplate(hero.templateId);
    return t && !t->specialty.isEmpty() ? &t->specialty : nullptr;
}

bool specialtyFits(const HeroSpecialty& specialty, UnitTypeId unit, const ContentRegistry& content) {
    if (!specialty.unit.isValid()) return false;
    // The specialty's unit and its upgrades: follow upgradesTo from it.
    UnitTypeId u = specialty.unit;
    for (int guard = 0; guard < 8 && u.isValid(); ++guard) {
        if (u == unit) return true;
        const UnitDef* def = content.unit(u);
        u = def ? def->upgradesTo : UnitTypeId();
    }
    return false;
}

QString describeSpecialty(const HeroSpecialty& sp, const ContentRegistry& content) {
    QStringList parts;
    if (const UnitDef* u = content.unit(sp.unit)) {
        QStringList bonus;
        if (sp.attack) bonus << QStringLiteral("+%1 attack").arg(sp.attack);
        if (sp.defense) bonus << QStringLiteral("+%1 defense").arg(sp.defense);
        parts << QStringLiteral("%1 (and their upgrades): %2").arg(u->name, bonus.join(QStringLiteral(", ")));
    }
    for (const auto [r, amount] : sp.income) parts << QStringLiteral("+%1 %2 a day").arg(amount).arg(content.resource(r).name.toLower());
    if (const TraitDef* t = content.trait(sp.trait)) parts << QStringLiteral("+%1 %2").arg(sp.traitAmount).arg(t->name.toLower());
    return parts.join(QStringLiteral("; "));
}

}  // namespace kot::core
