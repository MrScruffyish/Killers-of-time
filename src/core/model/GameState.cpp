#include "core/model/GameState.h"

namespace kot::core {

PlayerId GameState::addPlayer(const QString& name, TeamId team, Player::Controller controller,
                              const QString& race, const QString& color) {
    Player p;
    p.id = newId<PlayerId>();
    p.name = name;
    p.team = team;
    p.controller = controller;
    p.race = race;
    p.color = color;
    m_players.insert(p.id, p);
    m_turnOrder.append(p.id);
    return p.id;
}

ArmyId GameState::addArmy(PlayerId owner, Pos pos) {
    Army a;
    a.id = newId<ArmyId>();
    a.owner = owner;
    a.pos = pos;
    m_armies.insert(a.id, a);
    return a.id;
}

TownId GameState::addTown(const QString& name, const QString& race, Pos pos, PlayerId owner) {
    Town t;
    t.id = newId<TownId>();
    t.name = name;
    t.race = race;
    t.pos = pos;
    t.owner = owner;
    t.garrison = addArmy(owner, pos);
    m_towns.insert(t.id, t);
    return t.id;
}

MineId GameState::addMine(const QString& resourceId, int dailyAmount, Pos pos, PlayerId owner) {
    Mine m;
    m.id = newId<MineId>();
    m.resourceId = resourceId;
    m.dailyAmount = dailyAmount;
    m.pos = pos;
    m.owner = owner;
    m_mines.insert(m.id, m);
    return m.id;
}

namespace {
template <typename Map, typename Id>
auto* findIn(Map& map, Id id) {
    auto it = map.find(id);
    return it == map.end() ? nullptr : &it.value();
}
}  // namespace

Player* GameState::player(PlayerId id) { return findIn(m_players, id); }
Hero* GameState::hero(HeroId id) { return findIn(m_heroes, id); }
Army* GameState::army(ArmyId id) { return findIn(m_armies, id); }
Town* GameState::town(TownId id) { return findIn(m_towns, id); }
Mine* GameState::mine(MineId id) { return findIn(m_mines, id); }
const Player* GameState::player(PlayerId id) const { return findIn(m_players, id); }
const Hero* GameState::hero(HeroId id) const { return findIn(m_heroes, id); }
const Army* GameState::army(ArmyId id) const { return findIn(m_armies, id); }
const Town* GameState::town(TownId id) const { return findIn(m_towns, id); }
const Mine* GameState::mine(MineId id) const { return findIn(m_mines, id); }

TeamId GameState::teamOf(PlayerId id) const {
    const Player* p = player(id);
    return p ? p->team : TeamId();
}

bool GameState::isHostile(PlayerId owner, PlayerId me) const {
    return !owner.isValid() || teamOf(owner) != teamOf(me);
}

QByteArray& GameState::exploredMap(TeamId team) {
    QByteArray& data = m_explored[team];
    const qsizetype size = static_cast<qsizetype>(m_map.width()) * m_map.height();
    if (data.size() != size) data = QByteArray(size, 0);
    return data;
}

const QByteArray& GameState::exploredMap(TeamId team) const {
    static const QByteArray none;
    const auto it = m_explored.constFind(team);
    return it == m_explored.constEnd() ? none : it.value();
}

PlayerId GameState::currentPlayer() const {
    if (m_currentIndex < 0 || m_currentIndex >= m_turnOrder.size()) return PlayerId();
    return m_turnOrder[m_currentIndex];
}

}  // namespace kot::core
