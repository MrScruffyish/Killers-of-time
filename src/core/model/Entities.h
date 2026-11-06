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
    RaceId race;
    QString color;      // "#RRGGBB"
    ResourceAmounts resources;
    int incomePercent = 0;   // difficulty bonus: +/- percent of all income
    int growthPercent = 0;   // difficulty bonus: +/- percent dwelling growth in his towns
    int townlessSince = 0;   // day he was left without a town (0 = he has one); see Victory.h
    // Human seats: the SHA-256 (hex) of the key of the client that plays it (each client
    // makes a random key per server and sends it in the hello). Only that client gets
    // the seat back; the key itself is never stored. Empty: nobody has played it yet.
    QString seatKeyHash;
};

struct Hero {
    HeroId id;
    QString name;
    HeroClassId classId;
    PlayerId owner;
    int level = 1;
    int experience = 0;
    TraitValues traits;          // value per trait (see traits.json)
    int spellPoints = 0;         // current; the maximum is trait "spell_points_max"
    int pendingTraitPicks = 0;   // level-ups whose trait pick is not chosen yet
    int movementPoints = 0;      // left this turn, in hundredths (100 = one grass tile)
    ArmyId army;                 // the army this hero leads (holds his position)
    HeroTemplateId templateId;   // the tavern hero this one was hired as (invalid = none)
    QList<SpellId> spells;       // the spellbook: the spells this hero knows
    QList<ArtifactId> artifacts;  // artifacts worn (their trait bonuses are in `traits`)
    QList<ArtifactId> backpack;   // artifacts carried but not worn (no bonus)
    bool inBoat = false;         // sails (moves on water only) until he lands
    std::optional<Pos> destination;  // where the last move was going when movement ran out

    int trait(TraitId traitId) const { return traits.value(traitId); }
};

// A town covers 2x2 tiles: its position is the gate (bottom left, where heroes
// enter), the tiles right of it and above it are its walls (they block walking).
// Tiles outside the map are left out.
inline QList<Pos> townTiles(Pos gate, int mapWidth, int mapHeight) {
    QList<Pos> tiles;
    for (const Pos p : {gate, Pos{gate.x + 1, gate.y}, Pos{gate.x, gate.y - 1}, Pos{gate.x + 1, gate.y - 1}})
        if (p.x >= 0 && p.y >= 0 && p.x < mapWidth && p.y < mapHeight) tiles.append(p);
    return tiles;
}
inline bool townCovers(Pos gate, Pos tile) {
    return (tile.x == gate.x || tile.x == gate.x + 1) && (tile.y == gate.y || tile.y == gate.y - 1);
}

struct Town {
    TownId id;
    QString name;
    RaceId race;
    PlayerId owner;  // invalid = neutral
    Pos pos;
    QList<BuildingTypeId> buildings;  // built buildings
    ArmyId garrison;        // invalid = none
    int lastBuildDay = 0;   // day something was last built here (one building per day)
    int lastHireWeek = 0;   // week a hero was last hired in its tavern (one per week; 0 = never)
    // Units waiting to be recruited: dwelling chain root building -> count.
    QMap<BuildingTypeId, int> recruitPool;
    // Units sent here from map dwellings: unit id -> count, recruitable like the pool.
    QMap<UnitTypeId, int> importedUnits;
    QList<HeroTemplateId> tavernOffers;  // the heroes that can be hired here now
};

// A creature dwelling outside towns. Grows units every week; a hero captures it
// by visiting and can recruit there, or its units can be sent to a town.
struct MapDwelling {
    MapDwellingId id;
    UnitTypeId unitId;
    Pos pos;
    PlayerId owner;  // invalid = neutral
    int available = 0;
};

// Something lying on the map that a hero picks up by walking onto it (then it is gone):
// a pile of resources, or an artifact.
// What a quest hut's hermit asks (one of artifact, resources, level) and gives.
// Done once: the first hero who brings it gets the reward.
struct Quest {
    ArtifactId needArtifact;         // given up
    ResourceAmounts needResources;   // paid by the owner
    int needLevel = 0;               // the hero's level at least
    ResourceAmounts rewardResources;
    ArtifactId rewardArtifact;
    int rewardExperience = 0;
    bool done = false;
};

// A scenario's story: on `day` its text is told to everyone; optionally every player
// gets `gift`, and monsters appear at `where` (if the tile is free).
struct StoryEvent {
    int day = 1;
    QString text;
    ResourceAmounts gift;
    UnitTypeId monster;
    int monsterCount = 0;
    Pos where;
};

struct MapObject {
    enum class Kind { Treasure, Artifact, Site };
    MapObjectId id;
    Kind kind = Kind::Treasure;
    Pos pos;
    ResourceAmounts resources;  // Treasure
    ArtifactId artifact;        // Artifact
    // Site: a place heroes visit (it stays): shrine (teaches a spell), magic_well
    // (spell points back, once a day), obelisk (shows the land around it), arena
    // (+1 attack or defense), learning_stone (experience). See MapSites.h.
    QString site;
    QMap<int, int> visited;     // hero id (or kTeamVisit + team id) -> the day of the visit
    Quest quest;                // site "quest_hut"
};

struct Mine {
    MineId id;
    ResourceId resource;
    int dailyAmount = 0;
    PlayerId owner;  // invalid = neutral
    Pos pos;
};

}  // namespace kot::core
