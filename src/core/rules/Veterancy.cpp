#include "core/rules/Veterancy.h"

#include "core/content/ContentRegistry.h"

namespace kot::core {

int veteranStars(int experiencePerUnit, const ContentRegistry& content) {
    int stars = 0;
    for (const int t : content.balance().veterancy.thresholds)
        if (experiencePerUnit >= t) ++stars;
    return stars;
}

int veteranBonus(int stars, const QString& stat, const ContentRegistry& content) {
    const auto& bonuses = content.balance().veterancy.starBonuses;
    int total = 0;
    for (int i = 0; i < stars && i < bonuses.size(); ++i) total += bonuses[i].value(stat);
    return total;
}

}  // namespace kot::core
