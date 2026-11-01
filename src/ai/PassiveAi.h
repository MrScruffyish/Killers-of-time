#pragma once

#include "ai/IAiPlayer.h"

namespace kot::ai {

// Does nothing. Useful as a stand-in while the real AI does not exist yet.
class PassiveAi : public IAiPlayer {
public:
    QString profileName() const override;
};

}  // namespace kot::ai
