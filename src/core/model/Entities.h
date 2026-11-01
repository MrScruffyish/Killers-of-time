#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

#include "core/content/Defs.h"
#include "core/model/Position.h"
#include "core/util/Ids.h"

namespace kot::core {

struct Player {
    enum class Controller { Human, Ai };

    PlayerId id;
    QString name;
    TeamId team;
    Controller controller = Controller::Human;
    QString aiProfile;  // only for Ai players
    QString race;
    QString color;      // "#RRGGBB"
    ResourceAmounts resources;
};

struct Hero {
    HeroId id;
    QString name;
    QString classId;
    PlayerId owner;
    int level = 1;
    int experience = 0;
    QMap<QString, int> traits;   // trait id -> value (see traits.json)
    int spellPoints = 0;         // current; the maximum is trait "spell_points_max"
    int pendingTraitPicks = 0;   // level-ups whose trait pick is not chosen yet
    int movementPoints = 0;      // left this turn, in hundredths (100 = one grass tile)
    ArmyId army;                 // the army this hero leads (holds his position)

    int trait(const QString& traitId) const { return traits.value(traitId, 0); }
};

struct Town {
    TownId id;
    QString name;
    QString race;
    PlayerId owner;  // invalid = neutral
    Pos pos;
    QStringList buildings;  // built building ids (building data comes in M3)
    ArmyId garrison;        // invalid = none
    int lastBuildDay = 0;   // day something was last built here (one building per day)
    // Units waiting to be recruited: dwelling chain root building id -> count.
    QMap<QString, int> recruitPool;
    // Units sent here from map dwellings: unit id -> count, recruitable like the pool.
    QMap<QString, int> importedUnits;
};

// A creature dwelling outside towns. Grows units every week; a hero captures it
// by visiting and can recruit there, or its units can be sent to a town.
struct MapDwelling {
    MapDwellingId id;
    QString unitId;
    Pos pos;
    PlayerId owner;  // invalid = neutral
    int available = 0;
};

struct Mine {
    MineId id;
    QString resourceId;
    int dailyAmount = 0;
    PlayerId owner;  // invalid = neutral
    Pos pos;
};

}  // namespace kot::core
