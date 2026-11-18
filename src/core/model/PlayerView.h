#pragma once

#include <QJsonObject>

#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

// What one player is allowed to know, as JSON for a client. The server never
// sends anything else, so a modified client cannot see hidden things.
//   - map: terrain only for tiles the player's team has explored (others -1);
//     "visible" marks what is in sight right now
//   - things of the player's own team are shown in full (heroes with traits,
//     armies with exact HP, towns with buildings and recruits); the player's
//     resources are private
//   - things of others (heroes, armies, towns, mines, dwellings) only while on a
//     visible tile, and without secrets (no traits, no HP, no garrison, no stock)
QJsonObject buildPlayerView(const GameState& state, const ContentRegistry& content, PlayerId viewer);

}  // namespace kot::core
