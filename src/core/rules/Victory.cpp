#include "core/rules/Victory.h"

#include <QHash>
#include <QSet>

#include "core/content/ContentRegistry.h"

namespace kot::core {

namespace {
bool hasTown(const GameState& state, PlayerId player) {
    for (const Town& t : state.towns())
        if (t.owner == player) return true;
    return false;
}
bool timeRanOut(const GameState& state, const Player& p) {
    if (state.defeatRule() == QLatin1String("town")) return true;
    if (state.defeatRule() == QLatin1String("all")) return false;
    return p.townlessSince > 0 && state.day() - p.townlessSince >= kTownlessGraceDays;
}
// How much of the walkable land a team has revealed, in percent (water and
// impassable rock do not count towards exploration).
int exploredLandPercent(const GameState& state, const ContentRegistry& content, TeamId team) {
    const QByteArray& explored = state.exploredMap(team);
    const QVector<TerrainId>& cells = state.map().cells();
    int land = 0, seen = 0;
    for (qsizetype i = 0; i < cells.size() && i < explored.size(); ++i) {
        if (!content.terrain(cells[i]).walkable) continue;
        ++land;
        if (explored[i]) ++seen;
    }
    return land ? seen * 100 / land : 0;
}
}  // namespace

bool isEliminated(const GameState& state, PlayerId player) {
    if (hasTown(state, player)) return false;
    if (const Player* p = state.player(player); p && timeRanOut(state, *p)) return true;
    for (const Hero& h : state.heroes())
        if (h.owner == player) return false;
    return true;
}

int townlessDaysLeft(const GameState& state, PlayerId player) {
    const Player* p = state.player(player);
    if (state.defeatRule() != QLatin1String("week")) return -1;
    if (!p || p->townlessSince <= 0 || hasTown(state, player) || isEliminated(state, player)) return -1;
    return kTownlessGraceDays - (state.day() - p->townlessSince);
}

void updateTownless(GameState& state) {
    for (Player& p : state.players()) {
        if (hasTown(state, p.id)) {
            p.townlessSince = 0;
            continue;
        }
        if (p.townlessSince == 0) p.townlessSince = state.day();
        if (!timeRanOut(state, p)) continue;
        // Out of the game: his heroes and their armies leave the map.
        QList<HeroId> gone;
        for (const Hero& h : state.heroes())
            if (h.owner == p.id) gone << h.id;
        for (const HeroId id : gone) {
            for (auto it = state.armies().begin(); it != state.armies().end();) {
                if (it->hero == id) it = state.armies().erase(it);
                else ++it;
            }
            state.heroes().remove(id);
        }
    }
}

GameOutcome gameOutcome(const GameState& state, const ContentRegistry& content) {
    QSet<TeamId> alive;
    for (const Player& p : state.players())
        if (!isEliminated(state, p.id)) alive.insert(p.team);
    GameOutcome o;
    o.over = alive.size() <= 1;
    if (o.over && alive.size() == 1) o.winner = *alive.begin();
    if (o.over) return o;

    const VictoryCondition& v = state.victory();
    switch (v.type) {
    case VictoryCondition::Type::HoldTown:
        if (const Town* t = state.town(v.town); t && t->owner.isValid()) {
            o.over = true;
            o.winner = state.teamOf(t->owner);
        }
        break;
    case VictoryCondition::Type::Gold:
        for (const Player& p : state.players())
            if (v.amount > 0 && p.resources.value(content.currency()) >= v.amount && !isEliminated(state, p.id)) {
                o.over = true;
                o.winner = p.team;
                break;
            }
        break;
    case VictoryCondition::Type::Days:
        if (v.amount > 0 && state.day() > v.amount) {
            // Time is up: the team with the most towns wins (none if tied).
            QHash<TeamId, int> towns;
            for (const Town& t : state.towns())
                if (t.owner.isValid()) ++towns[state.teamOf(t.owner)];
            int best = -1;
            bool tie = false;
            for (auto it = towns.cbegin(); it != towns.cend(); ++it) {
                if (it.value() > best) {
                    best = it.value();
                    o.winner = it.key();
                    tie = false;
                } else if (it.value() == best) {
                    tie = true;
                }
            }
            if (tie) o.winner = TeamId();
            o.over = true;
        }
        break;
    case VictoryCondition::Type::SlayMonster:
        // A missing target alone proves nothing (joins and editor edits remove armies
        // too): the kill is recorded by noteArmyDestroyed, with its winner.
        if (v.slayerTeam.isValid()) {
            o.over = true;
            o.winner = v.slayerTeam;
        }
        break;
    case VictoryCondition::Type::FindArtifact:
        for (const Hero& h : state.heroes()) {
            if (!h.owner.isValid() || isEliminated(state, h.owner)) continue;
            if (h.artifacts.contains(v.targetArtifact) || h.backpack.contains(v.targetArtifact)) {
                o.over = true;
                o.winner = state.teamOf(h.owner);
                break;
            }
        }
        break;
    case VictoryCondition::Type::ExploreMap:
        // With no hidden map every team already knows every tile at the start, so
        // there would be nothing to explore: the goal is inactive in that case.
        if (!state.hiddenMap()) break;
        for (const Player& p : state.players()) {
            if (isEliminated(state, p.id)) continue;
            if (v.amount > 0 && exploredLandPercent(state, content, p.team) >= v.amount) {
                o.over = true;
                o.winner = p.team;
                break;
            }
        }
        break;
    case VictoryCondition::Type::Conquest:
        break;
    }
    return o;
}

void noteArmyDestroyed(GameState& state, ArmyId army, PlayerId winner) {
    VictoryCondition v = state.victory();
    if (v.type != VictoryCondition::Type::SlayMonster || v.targetArmy != army || v.slayerTeam.isValid()) return;
    const Player* p = state.player(winner);
    if (!p) return;
    v.slayerTeam = p->team;
    state.setVictory(v);
}

QString describeVictory(const GameState& state, const ContentRegistry& content) {
    const VictoryCondition& v = state.victory();
    switch (v.type) {
    case VictoryCondition::Type::HoldTown: {
        const Town* t = state.town(v.town);
        return QStringLiteral("Take %1 (or defeat everyone)").arg(t ? t->name : QStringLiteral("the town"));
    }
    case VictoryCondition::Type::Gold:
        return QStringLiteral("Be the first with %1 gold (or defeat everyone)").arg(v.amount);
    case VictoryCondition::Type::Days:
        return QStringLiteral("Hold the most towns after day %1 (or defeat everyone)").arg(v.amount);
    case VictoryCondition::Type::SlayMonster: {
        if (v.slayerTeam.isValid()) return QStringLiteral("The monster has been slain");
        QString name = QStringLiteral("the monster");
        if (const Army* a = state.army(v.targetArmy); a && !a->stacks.isEmpty()) {
            if (const UnitDef* u = content.unit(a->stacks.first().unitTypeId())) name = u->name;
        }
        return QStringLiteral("Slay %1 (or defeat everyone)").arg(name);
    }
    case VictoryCondition::Type::FindArtifact: {
        const ArtifactDef* a = content.artifact(v.targetArtifact);
        return QStringLiteral("Find the %1 (or defeat everyone)").arg(a ? a->name : QStringLiteral("artifact"));
    }
    case VictoryCondition::Type::ExploreMap:
        return QStringLiteral("Explore %1% of the land (or defeat everyone)").arg(v.amount);
    case VictoryCondition::Type::Conquest:
        break;
    }
    return QStringLiteral("Defeat everyone");
}

QString describeDefeat(const GameState& state) {
    if (state.defeatRule() == QLatin1String("town")) return QStringLiteral("A player who loses his last town is out at once");
    if (state.defeatRule() == QLatin1String("all")) return QStringLiteral("A player is out only when he has no towns and no heroes left");
    return QStringLiteral("A player who loses his last town has %1 days to take one back, else he is out").arg(kTownlessGraceDays);
}

}  // namespace kot::core
