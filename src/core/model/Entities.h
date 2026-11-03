#pragma once

#include <optional>

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
    int incomePercent = 0;   // difficulty bonus: +/- percent of all income
    int growthPercent = 0;   // difficulty bonus: +/- percent dwelling growth in his towns
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
    QString templateId;          // the tavern hero this one was hired as (empty = none)
    QStringList spells;          // the spellbook: spell ids this hero knows
    QStringList artifacts;       // artifacts carried (their trait bonuses are in `traits`)
    bool inBoat = false;         // sails (moves on water only) until he lands
    std::optional<Pos> destination;  // where the last move was going when movement ran out

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
    QStringList tavernOffers;  // hero template ids that can be hired here now
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

// Something lying on the map that a hero picks up by walking onto it (then it is gone):
// a pile of resources, or an artifact.
struct MapObject {
    enum class Kind { Treasure, Artifact };
    MapObjectId id;
    Kind kind = Kind::Treasure;
    Pos pos;
    ResourceAmounts resources;  // Treasure
    QString artifact;           // Artifact: artifact id
};

struct Mine {
    MineId id;
    QString resourceId;
    int dailyAmount = 0;
    PlayerId owner;  // invalid = neutral
    Pos pos;
};

}  // namespace kot::core
