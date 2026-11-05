#include "core/model/UnitStack.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace kot::core {

int effectiveMaxHp(int baseMaxHp, const HpBonus& bonus) {
    const double value = (static_cast<double>(baseMaxHp) + bonus.flat) * (1.0 + bonus.percent);
    return std::max(1, static_cast<int>(std::lround(value)));
}

UnitStack::UnitStack(UnitTypeId unitTypeId, int count, int maxHpPerUnit)
    : m_unitTypeId(std::move(unitTypeId)),
      m_maxHp(std::max(1, maxHpPerUnit)),
      m_totalHp(std::max(0, count) * m_maxHp) {}

UnitStack UnitStack::fromTotalHp(UnitTypeId unitTypeId, int totalHp, int maxHpPerUnit) {
    UnitStack s(std::move(unitTypeId), 0, maxHpPerUnit);
    s.m_totalHp = std::max(0, totalHp);
    return s;
}

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

namespace {
int average(int a, int countA, int b, int countB) {
    const long long n = std::max(1, countA + countB);
    return static_cast<int>((static_cast<long long>(a) * countA + static_cast<long long>(b) * countB) / n);
}
}  // namespace

void UnitStack::addUnits(int units) {
    if (units <= 0) return;
    m_experience = average(m_experience, count(), 0, units);  // fresh units have none
    m_totalHp += units * m_maxHp;
}

bool UnitStack::canMerge(const UnitStack& other) const {
    return m_unitTypeId == other.m_unitTypeId && m_maxHp == other.m_maxHp;
}

UnitStack UnitStack::split(int count) {
    if (count <= 0 || count >= this->count()) return UnitStack(m_unitTypeId, 0, m_maxHp);
    m_totalHp -= count * m_maxHp;
    UnitStack part(m_unitTypeId, count, m_maxHp);
    part.m_experience = m_experience;
    return part;
}

void UnitStack::merge(const UnitStack& other) {
    if (!canMerge(other)) return;
    m_experience = average(m_experience, count(), other.m_experience, other.count());
    m_totalHp += other.m_totalHp;
}

void UnitStack::restoreFullHealth() {
    m_totalHp = count() * m_maxHp;
}

}  // namespace kot::core
