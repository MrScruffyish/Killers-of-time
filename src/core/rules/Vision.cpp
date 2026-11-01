#include "core/rules/Vision.h"

#include <algorithm>

#include "core/content/ContentRegistry.h"

namespace kot::core {

QByteArray visibleTiles(const GameState& state, TeamId team, const ContentRegistry& content) {
    const GameMap& map = state.map();
    QByteArray visible(static_cast<qsizetype>(map.width()) * map.height(), 0);

    const auto reveal = [&](Pos center, int radius) {
        radius = std::max(radius, 1);
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx * dx + dy * dy > radius * radius + radius) continue;  // roughly round
                const Pos p{center.x + dx, center.y + dy};
                if (map.inBounds(p)) visible[p.y * map.width() + p.x] = 1;
            }
        }
    };

    for (const Hero& hero : state.heroes()) {
        if (state.teamOf(hero.owner) != team) continue;
        if (const Army* army = state.army(hero.army))
            reveal(army->pos, hero.trait(QStringLiteral("scouting_radius")));
    }
    const VisionBalance& vb = content.balance().vision;
    for (const Town& t : state.towns()) {
        if (t.owner.isValid() && state.teamOf(t.owner) == team) reveal(t.pos, vb.townRadius);
    }
    for (const Mine& m : state.mines()) {
        if (m.owner.isValid() && state.teamOf(m.owner) == team) reveal(m.pos, vb.mineRadius);
    }
    return visible;
}

void updateExplored(GameState& state, const ContentRegistry& content, TeamId team) {
    const QByteArray visible = visibleTiles(state, team, content);
    QByteArray& explored = state.exploredMap(team);
    for (qsizetype i = 0; i < visible.size(); ++i) {
        if (visible[i]) explored[i] = 1;
    }
}

Visibility tileVisibility(const GameState& state, TeamId team, const QByteArray& visible, Pos p) {
    if (!state.map().inBounds(p)) return Visibility::Unexplored;
    const qsizetype i = static_cast<qsizetype>(p.y) * state.map().width() + p.x;
    if (i < visible.size() && visible[i]) return Visibility::Visible;
    const QByteArray& explored = state.exploredMap(team);
    return (i < explored.size() && explored[i]) ? Visibility::Explored : Visibility::Unexplored;
}

}  // namespace kot::core
