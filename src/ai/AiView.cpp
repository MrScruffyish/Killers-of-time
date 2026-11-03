#include "ai/AiView.h"

#include "core/rules/ArmyRules.h"

#include "core/content/ContentRegistry.h"
#include "core/rules/Vision.h"

namespace kot::ai {

using namespace kot::core;

AiView::AiView(const GameState& state, const ContentRegistry& content, PlayerId me)
    : m_state(state), m_content(content) {
    const TeamId team = state.teamOf(me);
    m_visible = visibleTiles(state, team, content);
    m_explored = state.exploredMap(team);
}

bool AiView::visible(Pos p) const {
    if (!m_state.map().inBounds(p)) return false;
    const qsizetype i = static_cast<qsizetype>(p.y) * m_state.map().width() + p.x;
    return i < m_visible.size() && m_visible[i] != 0;
}

bool AiView::explored(Pos p) const {
    if (!m_state.map().inBounds(p)) return false;
    const qsizetype i = static_cast<qsizetype>(p.y) * m_state.map().width() + p.x;
    return i < m_explored.size() && m_explored[i] != 0;
}

bool AiView::isFrontier(Pos p) const {
    if (!explored(p)) return false;
    const auto t = m_content.terrain().constFind(m_state.map().terrainAt(p));
    if (t == m_content.terrain().cend() || !t->walkable) return false;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const Pos n{p.x + dx, p.y + dy};
            if (m_state.map().inBounds(n) && !explored(n)) return true;
        }
    return false;
}


}  // namespace kot::ai
