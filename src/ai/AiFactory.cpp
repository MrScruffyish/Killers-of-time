#include "ai/AiFactory.h"

#include "ai/PassiveAi.h"
#include "ai/SimpleAi.h"

namespace kot::ai {

QStringList aiProfileNames() {
    return {QStringLiteral("simple"), QStringLiteral("passive")};
}

std::unique_ptr<IAiPlayer> createAi(const QString& profile, quint64 seed) {
    if (profile == QStringLiteral("passive")) return std::make_unique<PassiveAi>();
    return std::make_unique<SimpleAi>(seed);
}

}  // namespace kot::ai
