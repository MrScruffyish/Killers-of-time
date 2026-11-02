#include "core/magic/Spells.h"

#include <algorithm>

#include "core/content/ContentRegistry.h"

namespace kot::core {

int effectStrength(const SpellEffect& effect, int spellPower) {
    return effect.amount + effect.perPower * spellPower;
}

QString whyCannotCast(const Hero& hero, const ContentRegistry& content, const QString& spellId,
                      SpellDef::Scope scope, int spellPoints) {
    const SpellDef* spell = content.spell(spellId);
    if (!spell) return QStringLiteral("unknown spell");
    if (!hero.spells.contains(spellId)) return QStringLiteral("the hero does not know %1").arg(spell->name);
    if (spell->scope != scope)
        return scope == SpellDef::Scope::Battle ? QStringLiteral("%1 cannot be cast in battle").arg(spell->name)
                                                : QStringLiteral("%1 can only be cast in battle").arg(spell->name);
    if (hero.trait(QStringLiteral("magic")) < spell->requiredMagic)
        return QStringLiteral("%1 needs Magic %2").arg(spell->name).arg(spell->requiredMagic);
    if (spellPoints < spell->cost)
        return QStringLiteral("not enough spell points (%1 needed)").arg(spell->cost);
    return {};
}

ActionResult castAdventureSpell(GameState& state, const ContentRegistry& content, HeroId heroId,
                                const QString& spellId) {
    ActionResult result;
    Hero* hero = state.hero(heroId);
    if (!hero) {
        result.error = QStringLiteral("unknown hero");
        return result;
    }
    result.error = whyCannotCast(*hero, content, spellId, SpellDef::Scope::Adventure, hero->spellPoints);
    if (!result.error.isEmpty()) return result;
    const SpellDef* spell = content.spell(spellId);
    const Army* army = state.army(hero->army);
    const int power = hero->trait(QStringLiteral("spell_power"));
    hero->spellPoints -= spell->cost;
    for (const SpellEffect& e : spell->effects) {
        const int strength = std::max(0, effectStrength(e, power));
        if (e.type == SpellEffect::Type::Movement) {
            hero->movementPoints += strength;
        } else if (e.type == SpellEffect::Type::Reveal && army) {
            QByteArray& explored = state.exploredMap(state.teamOf(hero->owner));
            const GameMap& map = state.map();
            for (int y = army->pos.y - strength; y <= army->pos.y + strength; ++y)
                for (int x = army->pos.x - strength; x <= army->pos.x + strength; ++x) {
                    const Pos p{x, y};
                    const int dx = x - army->pos.x, dy = y - army->pos.y;
                    if (map.inBounds(p) && dx * dx + dy * dy <= strength * strength)
                        explored[static_cast<qsizetype>(y) * map.width() + x] = 1;
                }
        }
    }
    result.ok = true;
    return result;
}

int mageGuildLevel(const Town& town, const ContentRegistry& content) {
    int level = 0;
    for (const QString& b : town.buildings) {
        const BuildingDef* def = content.building(b);
        if (!def) continue;
        for (const BuildingEffect& e : def->effects)
            if (e.type == BuildingEffect::Type::MageGuild) level = std::max(level, e.amount);
    }
    return level;
}

QStringList learnSpellsInTown(GameState& state, const ContentRegistry& content, HeroId heroId) {
    QStringList learned;
    Hero* hero = state.hero(heroId);
    const Army* army = hero ? state.army(hero->army) : nullptr;
    if (!army) return learned;
    for (const Town& t : state.towns()) {
        if (t.pos != army->pos || state.isHostile(t.owner, hero->owner)) continue;
        const int level = mageGuildLevel(t, content);
        for (const SpellDef& s : content.spells()) {
            if (s.level <= level && !hero->spells.contains(s.id)) {
                hero->spells.append(s.id);
                learned.append(s.id);
            }
        }
    }
    return learned;
}

}  // namespace kot::core
