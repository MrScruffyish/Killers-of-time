#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/model/GameState.h"
#include "core/rules/GameSetup.h"

namespace kot::core {

class ContentRegistry;

// A scenario: a prepared map with its towns, mines, monsters, treasures and the
// starting heroes of each player slot (PROJECT.md 5.12). Stored as
//   {"format": "kot-scenario", "id", "name", "description", "game": <save format>}
// in data/scenarios/<id>.json. The lobby's players take over the scenario's players
// in turn order: names, colours, teams, human/AI and the difficulty come from the
// lobby, races and positions from the scenario.
struct ScenarioInfo {
    QString id;
    QString name;
    QString description;
    int width = 0;
    int height = 0;
    QStringList races;  // one per player slot, in turn order
};

struct Scenario {
    ScenarioInfo info;
    QJsonObject game;  // GameState JSON
};

// Reads and fully validates a scenario file.
bool loadScenario(const QString& path, const ContentRegistry& content, Scenario* out, QStringList* errors);
bool saveScenario(const QString& path, const QString& id, const QString& name, const QString& description,
                  const GameState& state, QString* error);
// The scenarios in `dir` (files that do not load are skipped).
QList<ScenarioInfo> listScenarios(const QString& dir, const ContentRegistry& content);

// Starts a game from a scenario for these players (as many as the scenario has).
// `params.players` and `params.difficulty` are used; the map settings are not.
bool startScenario(const Scenario& scenario, const ContentRegistry& content, const GameSetupParams& params,
                   GameState* out, QList<PlayerId>* playerIds, QString* error);

}  // namespace kot::core
