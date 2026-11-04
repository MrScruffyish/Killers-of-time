#pragma once

#include "ai/IAiPlayer.h"

namespace kot::ai {

// Does nothing but end its turn. Plays for humans who left the game.
class PassiveAi : public IAiPlayer {
public:
    QString profileName() const override;
    void beginTurn(const core::GameState&, const core::ContentRegistry&, core::PlayerId) override {}
    net::Message nextCommand(const core::GameState& state, const core::ContentRegistry& content,
                             core::PlayerId me) override;
};

}  // namespace kot::ai
