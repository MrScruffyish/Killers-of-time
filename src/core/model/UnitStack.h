#pragma once

#include <QString>

#include "core/content/ContentIds.h"
#include <algorithm>

namespace kot::core {

// Hero Hit Points trait bonus, applied to the max HP of every unit in the army.
// `percent` is a fraction: 0.10 means +10%.
struct HpBonus {
    int flat = 0;
    double percent = 0.0;
};

// Effective max HP of one unit = (baseMaxHp + flat) * (1 + percent), rounded
// to nearest, never below 1.
int effectiveMaxHp(int baseMaxHp, const HpBonus& bonus);

// A group of identical units fighting as one (e.g. 12 armoured eagles).
//
// The stack keeps ONE pooled HP total. The unit count is derived from it:
//     count = ceil(totalHp / maxHp)
// so "units are eliminated one by one until the damage is covered" falls out
// naturally, and the front unit may be left wounded.
// Example: 10 units with 10 HP, 25 damage -> totalHp 75 -> count 8, front unit 5 HP.
class UnitStack {
public:
    struct DamageResult {
        int damageDealt = 0;   // damage actually absorbed (capped by remaining HP)
        int unitsKilled = 0;   // whole units removed from the stack
        bool eliminated = false;
    };

    UnitStack() = default;
    // `count` units at full health, each with `maxHpPerUnit` HP (clamped to >= 1).
    UnitStack(UnitTypeId unitType, int count, int maxHpPerUnit);

    // Rebuild a (possibly wounded) stack, e.g. when loading a save game.
    static UnitStack fromTotalHp(UnitTypeId unitType, int totalHp, int maxHpPerUnit);

    UnitTypeId unitTypeId() const { return m_unitTypeId; }
    int maxHp() const { return m_maxHp; }
    int totalHp() const { return m_totalHp; }
    int count() const;
    // HP of the front (possibly wounded) unit, 1..maxHp; 0 if eliminated.
    int frontUnitHp() const;
    bool isEliminated() const { return m_totalHp <= 0; }

    // Subtract damage from the pool. Negative damage counts as zero.
    DamageResult applyDamage(int damage);
    // Restore HP, never above count() * maxHp() (healing cannot revive dead
    // units). Returns the HP actually restored.
    int heal(int amount);
    // Add `units` fresh full-HP units (resurrection, recruiting, merging).
    void addUnits(int units);
    // Splitting and merging stacks of the same unit type and max HP.
    // split(): takes `count` fresh units out of the stack (0 < count < count()),
    // the remaining units keep the wounds. merge(): pools the HP of `other` in.
    bool canMerge(const UnitStack& other) const;
    UnitStack split(int count);
    void merge(const UnitStack& other);

    // Everyone alive back to full HP.
    void restoreFullHealth();

    // Battle experience, the average per unit (veterancy stars come from it,
    // core/rules/Veterancy.h). Fresh units have 0; adding or merging units averages
    // by count, splitting keeps it.
    int experience() const { return m_experience; }
    void setExperience(int perUnit) { m_experience = std::max(0, perUnit); }

private:
    UnitTypeId m_unitTypeId;
    int m_maxHp = 1;
    int m_totalHp = 0;
    int m_experience = 0;
};

}  // namespace kot::core
