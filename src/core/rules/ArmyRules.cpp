#include "core/rules/ArmyRules.h"

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

}  // namespace kot::core
