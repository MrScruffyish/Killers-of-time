#pragma once

#include <QJsonArray>

#include <optional>

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include "core/content/Defs.h"
#include "core/model/Position.h"

namespace kot::client {

// The player's view of the game as the server sends it (see docs/protocol.md),
// parsed into plain structs. No GUI code in here.

struct ViewPlayer {
    int id = 0;
    QString name;
    int team = 0;
    QString color;
    core::RaceId race;
    bool isAi = false;
    bool eliminated = false;
    int towns = 0;
    int heroes = 0;
    int mines = 0;
    int armyValue = 0;          // what the units cost; rounded for other teams
    int townlessDaysLeft = 0;   // > 0: no town left, out after this many days
    bool away = false;          // a human who is not connected now
};

struct ViewStack {
    core::UnitTypeId unit;
    int count = 0;
    int totalHp = -1;  // -1 = not shown (not your team's army)
    int maxHp = -1;
    int stars = 0;  // veterancy
};

struct ViewArmy {
    int id = 0;
    int owner = 0;
    int hero = 0;
    core::Pos pos;
    QList<ViewStack> stacks;
};

struct ViewHero {
    int id = 0;
    QString name;
    core::HeroClassId classId;
    core::HeroTemplateId templateId;  // the tavern hero he is (his specialty), if any
    int owner = 0;
    int level = 1;
    core::Pos pos;
    int army = 0;
    bool own = false;  // full details present (your team)
    int experience = 0;
    core::TraitValues traits;  // value per trait
    int spellPoints = 0;
    int pendingTraitPicks = 0;
    int movementPoints = 0;
    int movementMax = 0;  // a full day's movement
    QList<core::SpellId> spells;
    QList<core::ArtifactId> artifacts;  // worn
    QList<core::ArtifactId> backpack;   // carried, not worn
    bool inBoat = false;
    std::optional<core::Pos> destination;  // own heroes: the unfinished route's target
};

struct BuildOption {
    core::BuildingTypeId id;
    QString why;  // empty = can be built now
};

struct RecruitChoice {
    core::UnitTypeId unit;
    int available = 0;
};

struct ViewTown {
    int id = 0;
    int lastBuildDay = 0;  // own towns: the day something was built (one building per day)
    bool hiredThisWeek = false;  // own towns: the tavern hired out its hero this week
    int walls = 0;         // buildings with a defense effect (fort, citadel), seen from outside
    int size = 0;          // number of buildings
    QString name;
    core::RaceId race;
    int owner = 0;
    core::Pos pos;
    bool own = false;
    bool mine = false;  // owned by you (not just a teammate)
    int garrison = 0;
    QList<core::BuildingTypeId> buildings;
    QList<core::HeroTemplateId> tavernOffers;
    QList<BuildOption> buildOptions;
    QList<RecruitChoice> recruitOptions;
};

struct ViewMine {
    int id = 0;
    core::ResourceId resource;
    int dailyAmount = 0;
    int owner = 0;
    core::Pos pos;
};

struct ViewDwelling {
    int id = 0;
    core::UnitTypeId unit;
    int owner = 0;
    core::Pos pos;
    int available = -1;  // -1 = unknown
};

struct ViewObject {
    int id = 0;
    bool artifact = false;      // else a treasure pile (or a site)
    core::ArtifactId artifactId;
    QString site;               // a place to visit (MapSites.h), else empty
    QList<int> giftFor;         // your heroes who would still get something there
    QString quest;              // a quest hut: its wish and reward in words
    bool questDone = false;
    core::ResourceAmounts resources;
    core::Pos pos;
};

struct ViewBattleUnit {
    int id = 0;
    int side = 0;
    core::UnitTypeId unit;
    int count = 0;
    int frontHp = 0;
    int maxHp = 0;
    core::Pos hex;
    int shots = 0;
    bool retaliated = false;
    int stars = 0;
    int range = 0;  // how far it shoots (hexes, with the hero's bonus), 0 = melee
};

// A battle you take part in (server: battleToJson).
struct ViewBattle {
    bool present = false;
    int width = 0;
    int height = 0;
    int round = 0;
    int active = 0;       // unit id whose turn it is
    int yourSide = 0;
    int sideOwner[2] = {0, 0};
    QList<ViewBattleUnit> units;
    QList<core::Pos> reachable;  // for the active unit
    QList<int> attackable;
    QList<int> shootable;  // enemies the active stack can shoot now (in range)
    bool canShoot = false;
    bool canWait = false;
    bool canRetreat = false;
    int surrenderCost = -1;  // -1: cannot surrender now
    int townDefense = 0;
    int terrain = -1;  // the terrain id of the ground the battle is fought on
    // A siege: wall segments (hex -> HP), the gate, the towers, the catapult.
    int wallLevel = 0;
    QMap<core::Pos, int> walls;
    int wallMaxHp = 0;
    core::Pos gate{-1, -1};
    QList<core::Pos> towers;
    bool engineBuilt = false;
    bool canBuildEngine = false;
    QList<core::Pos> obstacles;
    // Your hero in this battle (absent: no hero).
    struct Spell {
        core::SpellId id;
        bool targetsEnemy = true;
        int cost = 0;
        bool castable = false;
    };
    QList<Spell> unitSpells;  // the active stack's own spells
    bool hasHero = false;
    int heroSpellPoints = 0;
    bool heroHasCast = false;
    QList<Spell> spells;

    const ViewBattleUnit* unit(int id) const;
    const ViewBattleUnit* unitAt(core::Pos hex) const;
    bool myMove() const;  // the active unit is on your side
};

struct GameView {
    int day = 1;
    int currentPlayer = 0;
    int you = 0;
    bool gameOver = false;
    QString difficulty;
    QString victory;  // what wins this game
    QString defeat;   // when a player is out
    QString weekEventName, weekEventText;
    QString intro;      // the scenario's story at the start
    QStringList news;   // what happened today (the story's events)
    QJsonObject campaign;  // {name, chapter, chapters, result?} when playing a campaign
    int winnerTeam = 0;  // 0 = nobody
    QJsonArray history;  // at game over: every player's numbers week by week (GameStats.h)
    core::ResourceAmounts income;  // what you get each day
    int tradePercent = 0;          // share of the fair value you get when trading (markets raise it)
    core::ResourceAmounts resources;
    QList<ViewPlayer> players;

    int width = 0;
    int height = 0;

    QList<int> tiles;     // palette index per tile, -1 = never explored
    QByteArray visible;   // 1 = in sight right now

    QList<ViewHero> heroes;
    QList<ViewArmy> armies;
    QList<ViewTown> towns;
    QList<ViewMine> mines;
    QList<ViewDwelling> dwellings;
    QList<ViewObject> objects;
    ViewBattle battle;
    bool battleInProgress = false;  // someone else is fighting
    int lastBattleId = 0;           // newest battle you took part in
    QJsonObject lastBattleReport;

    bool isMyTurn() const { return currentPlayer == you; }
    const ViewPlayer* player(int id) const;
    int myTeam() const;
    bool inBounds(core::Pos p) const { return p.x >= 0 && p.y >= 0 && p.x < width && p.y < height; }
    bool known(core::Pos p) const;
    bool inSight(core::Pos p) const;
    const ViewHero* hero(int id) const;
    const ViewArmy* army(int id) const;
    const ViewTown* town(int id) const;
    QList<const ViewHero*> myHeroes() const;   // heroes you own
    QList<const ViewTown*> myTowns() const;
    const ViewHero* heroAt(core::Pos p) const;
    const ViewTown* townAt(core::Pos p) const;
    // The town whose 2x2 tiles include `p` (core::townTiles), or null.
    const ViewTown* townCovering(core::Pos p) const;
};

// Parses a `game_started.view` / `game_state` body. Returns false (and says why) if
// the data is inconsistent.
bool parseGameView(const QJsonObject& json, GameView* out, QString* error = nullptr);

}  // namespace kot::client
