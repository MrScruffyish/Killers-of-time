#pragma once

#include <QString>
#include <QStringList>

#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

// Settings for a random scenario (the lobby's "Make a map" and kot_mapgen).
struct ScenarioGenParams {
    QString id;                 // file name (letters, digits, _ and -)
    QString name, description;  // empty: made up from the settings
    int width = 64, height = 64;
    quint64 seed = 1;
    QStringList races;          // one per player (2-8); "random" picks one
    bool islands = false;
    double seaLevel = 0.32;     // 0.1 (little water) .. 0.6 (much)
    double mountains = 0.06;    // share of mountains
    double hills = 0.16;
    int rivers = -1;            // -1: by size
    bool roads = true;
    int monsterPercent = 100;   // size of the monster groups
    // Goal: "conquest", "capital" (take the second player's capital), "gold"
    // (victoryAmount gold first) or "days" (most towns after victoryAmount days).
    QString victory = QStringLiteral("conquest");
    int victoryAmount = 0;
};

// Makes the scenario: a new game in `out` (players named Player 1..N, the
// first one meant for the human) with its goal set. Fills `error` on failure.
bool generateScenario(const ContentRegistry& content, const ScenarioGenParams& params, GameState* out, QString* error);

// A random campaign: `chapters` scenarios that grow (bigger maps, more and
// stronger enemies, the goals changing), written as <id>_1.json.. into
// `scenarioDir` and the campaign <id>.json into `campaignDir`.
struct CampaignGenParams {
    QString id, name;
    int chapters = 3;           // 2..6
    QString playerRace = QStringLiteral("random");
    int startSize = 48;         // first map; each chapter is 16 tiles bigger (at most 128)
    quint64 seed = 1;
    int carryHeroes = 1;
    bool carryArmy = true;
    bool islands = false;       // every map with islands
};
bool generateCampaign(const ContentRegistry& content, const CampaignGenParams& params, const QString& scenarioDir,
                      const QString& campaignDir, QString* error);

// A valid file name for a generated scenario or campaign.
bool validGeneratedId(const QString& id);

}  // namespace kot::core
