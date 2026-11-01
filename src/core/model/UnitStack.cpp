#include "core/model/UnitStack.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace kot::core {

int effectiveMaxHp(int baseMaxHp, const HpBonus& bonus) {
    const double value = (static_cast<double>(baseMaxHp) + bonus.flat) * (1.0 + bonus.percent);
    return std::max(1, static_cast<int>(std::lround(value)));
}

UnitStack::UnitStack(QString unitTypeId, int count, int maxHpPerUnit)
    : m_unitTypeId(std::move(unitTypeId)),
      m_maxHp(std::max(1, maxHpPerUnit)),
      m_totalHp(std::max(0, count) * m_maxHp) {}

int UnitStack::count() const {
    if (m_totalHp <= 0) return 0;
    return (m_totalHp + m_maxHp - 1) / m_maxHp;
}

int UnitStack::frontUnitHp() const {
    if (m_totalHp <= 0) return 0;
    return m_totalHp - (count() - 1) * m_maxHp;
}

UnitStack::DamageResult UnitStack::applyDamage(int damage) {
    DamageResult result;
    if (isEliminated()) {
        result.eliminated = true;
        return result;
    }
    const int before = count();
    result.damageDealt = std::clamp(damage, 0, m_totalHp);
    m_totalHp -= result.damageDealt;
    result.unitsKilled = before - count();
    result.eliminated = isEliminated();
    return result;
}

int UnitStack::heal(int amount) {
    if (amount <= 0 || isEliminated()) return 0;
    const int capacity = count() * m_maxHp;
    const int healed = std::min(amount, capacity - m_totalHp);
    m_totalHp += healed;
    return healed;
}

void UnitStack::addUnits(int units) {
    if (units > 0) m_totalHp += units * m_maxHp;
}

void UnitStack::restoreFullHealth() {
    m_totalHp = count() * m_maxHp;
}

}  // namespace kot::core
