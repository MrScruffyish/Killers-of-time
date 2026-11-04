#pragma once

#include <QList>

#include "core/model/Position.h"
#include "core/model/UnitStack.h"
#include "core/util/Ids.h"

namespace kot::core {

inline constexpr int kMaxArmyStacks = 10;

// A group of unit stacks standing somewhere on the map. An army led by a hero
// can move and capture; a hero-less army is a garrison (town defenders etc.).
struct Army {
    ArmyId id;
    PlayerId owner;  // invalid = neutral (monsters)
    HeroId hero;     // invalid = no hero
    Pos pos;
    QList<UnitStack> stacks;

    bool hasHero() const { return hero.isValid(); }
    int totalUnits() const;
    // Adds units to an existing stack of the same type (same max HP), or
    // starts a new stack. Returns false if all 7 slots are taken.
    bool addUnits(const QString& unitTypeId, int count, int maxHpPerUnit);
    // Removes eliminated stacks.
    void removeEmptyStacks();
};

}  // namespace kot::core
