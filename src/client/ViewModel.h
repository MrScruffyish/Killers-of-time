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
    QString race;
    bool isAi = false;
    bool eliminated = false;
};

struct ViewStack {
    QString unit;
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
    QString classId;
    int owner = 0;
    int level = 1;
    core::Pos pos;
    int army = 0;
    bool own = false;  // full details present (your team)
    int experience = 0;
    QMap<QString, int> traits;
    int spellPoints = 0;
    int pendingTraitPicks = 0;
    int movementPoints = 0;
    int movementMax = 0;  // a full day's movement
    QStringList spells;
    QStringList artifacts;  // worn
    QStringList backpack;   // carried, not worn
    bool inBoat = false;
    std::optional<core::Pos> destination;  // own heroes: the unfinished route's target
};

struct BuildOption {
    QString id;
    QString why;  // empty = can be built now
};

struct RecruitChoice {
    QString unit;
    int available = 0;
};

struct ViewTown {
    int id = 0;
    int lastBuildDay = 0;  // own towns: the day something was built (one building per day)
    bool hiredThisWeek = false;  // own towns: the tavern hired out its hero this week
    int walls = 0;         // buildings with a defense effect (fort, citadel), seen from outside
    int size = 0;          // number of buildings
    QString name;
    QString race;
    int owner = 0;
    core::Pos pos;
    bool own = false;
    bool mine = false;  // owned by you (not just a teammate)
    int garrison = 0;
    QStringList buildings;
    QStringList tavernOffers;
    QList<BuildOption> buildOptions;
    QList<RecruitChoice> recruitOptions;
};

struct ViewMine {
    int id = 0;
    QString resource;
    int dailyAmount = 0;
    int owner = 0;
    core::Pos pos;
};

struct ViewDwelling {
    int id = 0;
    QString unit;
    int owner = 0;
    core::Pos pos;
    int available = -1;  // -1 = unknown
};

struct ViewObject {
    int id = 0;
    bool artifact = false;      // else a treasure pile (or a site)
    QString artifactId;
    QString site;               // a place to visit (MapSites.h), else empty
    QList<int> giftFor;         // your heroes who would still get something there
    core::ResourceAmounts resources;
    core::Pos pos;
};

struct ViewBattleUnit {
    int id = 0;
    int side = 0;
    QString unit;
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
    QString terrain;  // the ground the battle is fought on
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
        QString id;
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
    QString weekEventName, weekEventText;
    QJsonObject campaign;  // {name, chapter, chapters, result?} when playing a campaign
    int winnerTeam = 0;  // 0 = nobody
    QJsonArray history;  // at game over: every player's numbers week by week (GameStats.h)
    core::ResourceAmounts income;  // what you get each day
    int tradePercent = 0;          // share of the fair value you get when trading (markets raise it)
    core::ResourceAmounts resources;
    QList<ViewPlayer> players;

    int width = 0;
    int height = 0;
    QStringList terrainPalette;
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
