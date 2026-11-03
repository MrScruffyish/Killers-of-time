#include "core/rules/ArmyRules.h"

#include <algorithm>
#include <cstdlib>

#include "core/content/ContentRegistry.h"

namespace kot::core {

namespace {
ActionResult fail(const QString& message) {
    ActionResult r;
    r.error = message;
    return r;
}
}  // namespace

ActionResult moveUnits(GameState& state, ArmyId fromId, ArmyId toId, int stackIndex, int count) {
    Army* from = state.army(fromId);
    Army* to = state.army(toId);
    if (!from || !to) return fail(QStringLiteral("unknown army"));
    if (from == to) return fail(QStringLiteral("source and target are the same army"));
    if (!from->owner.isValid() || from->owner != to->owner)
        return fail(QStringLiteral("armies belong to different players"));
    if (from->pos != to->pos) return fail(QStringLiteral("armies are not on the same tile"));
    if (stackIndex < 0 || stackIndex >= from->stacks.size())
        return fail(QStringLiteral("no such stack"));

    UnitStack& source = from->stacks[stackIndex];
    if (count < 1 || count > source.count()) return fail(QStringLiteral("invalid unit count"));

    UnitStack* mergeInto = nullptr;
    for (UnitStack& s : to->stacks) {
        if (s.canMerge(source)) mergeInto = &s;
    }
    if (!mergeInto && to->stacks.size() >= kMaxArmyStacks)
        return fail(QStringLiteral("the target army has no free slot"));

    UnitStack moving = (count == source.count()) ? source : source.split(count);
    if (count == source.count()) from->stacks.removeAt(stackIndex);
    if (mergeInto) mergeInto->merge(moving);
    else to->stacks.append(moving);

    ActionResult ok;
    ok.ok = true;
    return ok;
}

ActionResult moveAllUnits(GameState& state, ArmyId fromId, ArmyId toId, int* moved) {
    if (moved) *moved = 0;
    const Army* from = state.army(fromId);
    if (!from) return fail(QStringLiteral("unknown army"));
    if (from->stacks.isEmpty()) return fail(QStringLiteral("there are no units to move"));
    int total = 0;
    ActionResult last;
    // From the back, so the indexes of the stacks still to move stay valid.
    for (int i = static_cast<int>(from->stacks.size()) - 1; i >= 0; --i) {
        const int count = state.army(fromId)->stacks[i].count();
        last = moveUnits(state, fromId, toId, i, count);
        if (last.ok) total += count;
        else if (last.error != QStringLiteral("the target army has no free slot")) return last;
    }
    if (moved) *moved = total;
    if (total == 0) return fail(QStringLiteral("the target army has no free slot"));
    ActionResult ok;
    ok.ok = true;
    return ok;
}

double armyStrength(const GameState& state, const ContentRegistry& content, const Army& army) {
    const Hero* hero = state.hero(army.hero);
    const int bonus = hero ? hero->trait(QStringLiteral("attack")) + hero->trait(QStringLiteral("defense")) : 0;
    double total = 0;
    for (const UnitStack& s : army.stacks) {
        const UnitDef* u = content.unit(s.unitTypeId());
        if (!u) continue;
        total += s.count() * u->maxHp * (u->damageMin + u->damageMax) / 2.0 * (1.0 + 0.05 * (u->attack + u->defense));
    }
    return total * (1.0 + 0.05 * bonus);
}

bool wouldJoin(const GameState& state, const ContentRegistry& content, ArmyId heroArmy, ArmyId monstersId) {
    const int ratio = content.balance().battle.joinStrengthRatio;
    const Army* mine = state.army(heroArmy);
    const Army* monsters = state.army(monstersId);
    if (ratio <= 0 || !mine || !monsters || monsters->owner.isValid() || monsters->hasHero() || monsters->totalUnits() == 0) return false;
    for (const Town& t : state.towns())
        if (t.garrison == monstersId) return false;
    if (armyStrength(state, content, *mine) < ratio * armyStrength(state, content, *monsters)) return false;
    // Every stack must find a place: joining a stack of the same unit or a free slot.
    int freeSlots = kMaxArmyStacks - static_cast<int>(mine->stacks.size());
    for (const UnitStack& s : monsters->stacks) {
        bool merges = false;
        for (const UnitStack& own : mine->stacks) merges = merges || own.unitTypeId() == s.unitTypeId();
        if (!merges && --freeSlots < 0) return false;
    }
    return true;
}

int joinCost(const GameState& state, const ContentRegistry& content, ArmyId monstersId) {
    const Army* monsters = state.army(monstersId);
    if (!monsters) return 0;
    long long gold = 0;
    for (const UnitStack& s : monsters->stacks)
        if (const UnitDef* u = content.unit(s.unitTypeId())) gold += static_cast<long long>(s.count()) * u->cost.value(QStringLiteral("gold"));
    return static_cast<int>(gold * content.balance().battle.joinCostPercent / 100);
}

ActionResult acceptJoin(GameState& state, const ContentRegistry& content, HeroId heroId, ArmyId monstersId) {
    const Hero* hero = state.hero(heroId);
    Army* monsters = state.army(monstersId);
    if (!hero || !monsters) return fail(QStringLiteral("unknown hero or army"));
    if (!wouldJoin(state, content, hero->army, monstersId)) return fail(QStringLiteral("they do not want to join any more"));
    const Army* mine = state.army(hero->army);
    const int price = joinCost(state, content, monstersId);
    Player* owner = state.player(hero->owner);
    if (!owner || owner->resources.value(QStringLiteral("gold")) < price)
        return fail(QStringLiteral("they ask %1 gold, you have %2").arg(price).arg(owner ? owner->resources.value(QStringLiteral("gold")) : 0));
    // Next to them, or as close as the move got (an offer from their zone of control).
    if (std::max(std::abs(mine->pos.x - monsters->pos.x), std::abs(mine->pos.y - monsters->pos.y)) > 3)
        return fail(QStringLiteral("the hero is not near them"));
    // They come over to the hero's tile and join.
    monsters->owner = hero->owner;
    monsters->pos = mine->pos;
    const ActionResult r = moveAllUnits(state, monstersId, hero->army);
    if (r.ok) owner->resources[QStringLiteral("gold")] -= price;
    if (r.ok && state.army(monstersId) && state.army(monstersId)->totalUnits() == 0) state.armies().remove(monstersId);
    return r;
}

}  // namespace kot::core
