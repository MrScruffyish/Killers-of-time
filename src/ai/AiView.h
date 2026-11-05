#pragma once

#include <QByteArray>

#include "core/model/GameState.h"
#include "core/rules/ArmyRules.h"

namespace kot::core { class ContentRegistry; }

namespace kot::ai {

// What an AI's team may know: tiles in sight now and tiles explored before.
// AI code asks this before looking at things that are not its own.
class AiView {
public:
    AiView(const core::GameState& state, const core::ContentRegistry& content, core::PlayerId me);

    bool visible(core::Pos p) const;
    bool explored(core::Pos p) const;
    // Explored, walkable and next to an unexplored tile: a place to explore from.
    bool isFrontier(core::Pos p) const;

private:
    const core::GameState& m_state;
    const core::ContentRegistry& m_content;
    QByteArray m_visible;
    QByteArray m_explored;
    QVector<bool> m_walkable;  // per terrain id
};

// Rough fighting strength of an army (units' HP x average damage x attack bonus),
// armyStrength: core/rules/ArmyRules.h
using core::armyStrength;

}  // namespace kot::ai
