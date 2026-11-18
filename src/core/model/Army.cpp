#include "core/model/Army.h"

namespace kot::core {

int Army::totalUnits() const {
    int total = 0;
    for (const UnitStack& s : stacks) total += s.count();
    return total;
}

bool Army::addUnits(UnitTypeId unitTypeId, int count, int maxHpPerUnit) {
    if (count <= 0) return true;
    for (UnitStack& s : stacks) {
        if (s.unitTypeId() == unitTypeId && s.maxHp() == maxHpPerUnit) {
            s.addUnits(count);
            return true;
        }
    }
    if (stacks.size() >= kMaxArmyStacks) return false;
    stacks.append(UnitStack(unitTypeId, count, maxHpPerUnit));
    return true;
}

void Army::removeEmptyStacks() {
    stacks.removeIf([](const UnitStack& s) { return s.isEliminated(); });
}

}  // namespace kot::core
