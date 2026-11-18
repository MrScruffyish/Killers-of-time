#pragma once

#include <QString>

namespace kot::core {

class ContentRegistry;

// Unit veterancy (balance.json "veterancy"): a stack's experience per unit gives it
// 0..5 stars; every star adds its bonus, cumulatively.
int veteranStars(int experiencePerUnit, const ContentRegistry& content);
// The total bonus of `stars` stars for `stat` (attack, defense, initiative,
// battleSpeed, morale).
int veteranBonus(int stars, const QString& stat, const ContentRegistry& content);

}  // namespace kot::core
