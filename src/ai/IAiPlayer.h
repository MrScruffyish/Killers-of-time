#pragma once

#include <QString>

#include "core/model/GameState.h"
#include "net/Message.h"

namespace kot::core { class ContentRegistry; }

namespace kot::ai {

// An AI player. It plays through the same commands a human client sends (net::Message,
// see docs/protocol.md); the server checks and applies them exactly like human commands.
// The AI reads the game state but should only use what its team can see (see AiView.h).
//
// One turn: beginTurn(), then nextCommand() until it returns end_turn. After every
// command the server reports back with commandDone(). The server caps the number of
// commands per turn, so a confused AI cannot hang the game.
class IAiPlayer {
public:
    virtual ~IAiPlayer() = default;
    virtual QString profileName() const = 0;

    virtual void beginTurn(const core::GameState& state, const core::ContentRegistry& content,
                           core::PlayerId me) = 0;
    virtual net::Message nextCommand(const core::GameState& state, const core::ContentRegistry& content,
                                     core::PlayerId me) = 0;
    virtual void commandDone(const net::Message& command, bool ok) { Q_UNUSED(command); Q_UNUSED(ok); }
};

}  // namespace kot::ai
