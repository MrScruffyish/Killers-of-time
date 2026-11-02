#pragma once

#include <QList>
#include <QString>

#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

struct PlayerSetup {
    QString name;
    bool isAi = false;
    QString aiProfile;
    QString race;
    int team = 1;       // players with the same number are on one team
    QString color;      // "#RRGGBB"
};

struct GameSetupParams {
    int mapWidth = 64;
    int mapHeight = 64;
    quint64 seed = 1;
    QList<PlayerSetup> players;
};

// Creates a new game: generates the map, gives every player a start town of his
// race, a hero from the tavern list with the class's starting army, the starting
// resources, scatters neutral mines and map dwellings, and starts day 1 for the
// first player. Deterministic for the same params. `playerIds` gets the id of each
// player in the order of `params.players`.
bool createGame(const ContentRegistry& content, const GameSetupParams& params, GameState* out,
                QList<PlayerId>* playerIds, QString* error = nullptr);

}  // namespace kot::core
