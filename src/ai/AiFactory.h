#pragma once

#include <QStringList>
#include <memory>

#include "ai/IAiPlayer.h"

namespace kot::ai {

// AI profiles the lobby offers; the first one is the default.
QStringList aiProfileNames();

// Creates the AI for a profile name (unknown names give the simple AI).
// `seed` makes the AI's own random choices repeatable.
std::unique_ptr<IAiPlayer> createAi(const QString& profile, quint64 seed);

}  // namespace kot::ai
