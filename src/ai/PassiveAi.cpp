#include "ai/PassiveAi.h"

#include "net/Protocol.h"

namespace kot::ai {

QString PassiveAi::profileName() const {
    return QStringLiteral("passive");
}

net::Message PassiveAi::nextCommand(const core::GameState&, const core::ContentRegistry&, core::PlayerId) {
    return net::Message{net::msg::endTurn, {}, 0};
}

}  // namespace kot::ai
