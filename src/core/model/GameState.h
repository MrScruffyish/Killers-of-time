#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QStringList>

#include "core/model/Army.h"
#include "core/model/Entities.h"
#include "core/model/GameMap.h"
#include "core/util/Rng.h"

namespace kot::core {

class ContentRegistry;

inline constexpr int kSaveFormatVersion = 1;

// The complete state of one game. Plain data plus helpers; the rules live in
// core/rules. Owned by the server (the clients only ever see filtered views).
class GameState {
public:
    explicit GameState(std::uint64_t seed = 1) : m_rng(seed) {}

    int day() const { return m_day; }
    void setDay(int day) { m_day = day; }

    GameMap& map() { return m_map; }
    const GameMap& map() const { return m_map; }
    void setMap(GameMap map) { m_map = std::move(map); }

    Rng& rng() { return m_rng; }
    const Rng& rng() const { return m_rng; }

    // Ids are unique across all kinds of objects.
    template <typename Id>
    Id newId() {
        return Id(m_nextId++);
    }
    TeamId newTeam() { return newId<TeamId>(); }

    PlayerId addPlayer(const QString& name, TeamId team, Player::Controller controller,
                       const QString& race, const QString& color);
    ArmyId addArmy(PlayerId owner, Pos pos);
    TownId addTown(const QString& name, const QString& race, Pos pos, PlayerId owner = {});
    MapDwellingId addMapDwelling(const QString& unitId, Pos pos, int available, PlayerId owner = {});
    MapObjectId addMapObject(MapObject object);  // the id is assigned
    MineId addMine(const QString& resourceId, int dailyAmount, Pos pos, PlayerId owner = {});

    const QMap<PlayerId, Player>& players() const { return m_players; }
    QMap<PlayerId, Player>& players() { return m_players; }
    const QMap<HeroId, Hero>& heroes() const { return m_heroes; }
    QMap<HeroId, Hero>& heroes() { return m_heroes; }
    const QMap<ArmyId, Army>& armies() const { return m_armies; }
    QMap<ArmyId, Army>& armies() { return m_armies; }
    const QMap<TownId, Town>& towns() const { return m_towns; }
    QMap<TownId, Town>& towns() { return m_towns; }
    const QMap<MapDwellingId, MapDwelling>& mapDwellings() const { return m_mapDwellings; }
    QMap<MapDwellingId, MapDwelling>& mapDwellings() { return m_mapDwellings; }
    const QMap<MapObjectId, MapObject>& mapObjects() const { return m_mapObjects; }
    QMap<MapObjectId, MapObject>& mapObjects() { return m_mapObjects; }
    const QMap<MineId, Mine>& mines() const { return m_mines; }
    QMap<MineId, Mine>& mines() { return m_mines; }

    Player* player(PlayerId id);
    Hero* hero(HeroId id);
    Army* army(ArmyId id);
    Town* town(TownId id);
    Mine* mine(MineId id);
    MapDwelling* mapDwelling(MapDwellingId id);
    const MapDwelling* mapDwelling(MapDwellingId id) const;
    const Player* player(PlayerId id) const;
    const Hero* hero(HeroId id) const;
    const Army* army(ArmyId id) const;
    const Town* town(TownId id) const;
    const Mine* mine(MineId id) const;

    // The team a player belongs to (invalid id for unknown/neutral).
    TeamId teamOf(PlayerId id) const;
    // True if `owner` (possibly neutral = invalid id) is not on `me`'s team.
    bool isHostile(PlayerId owner, PlayerId me) const;

    // Fog of war memory: one byte per tile (row-major), 1 = explored by that team.
    // The non-const version creates a zeroed map of the right size when needed.
    QByteArray& exploredMap(TeamId team);
    const QByteArray& exploredMap(TeamId team) const;

    // Turn order (sequential turns, decision D1).
    const QList<PlayerId>& turnOrder() const { return m_turnOrder; }
    void setTurnOrder(QList<PlayerId> order) { m_turnOrder = std::move(order); m_currentIndex = 0; }
    PlayerId currentPlayer() const;
    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index) { m_currentIndex = index; }

    // Save game as JSON. fromJson validates everything against the content
    // and reports all problems; on failure `out` is left untouched.
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& json, const ContentRegistry& content, GameState* out,
                         QStringList* errors);
    bool saveToFile(const QString& path, QString* error) const;
    static bool loadFromFile(const QString& path, const ContentRegistry& content, GameState* out,
                             QStringList* errors);

private:
    int m_day = 1;
    GameMap m_map;
    Rng m_rng;
    std::uint32_t m_nextId = 1;
    QMap<PlayerId, Player> m_players;
    QMap<HeroId, Hero> m_heroes;
    QMap<ArmyId, Army> m_armies;
    QMap<TownId, Town> m_towns;
    QMap<MineId, Mine> m_mines;
    QMap<MapDwellingId, MapDwelling> m_mapDwellings;
    QMap<MapObjectId, MapObject> m_mapObjects;
    QMap<TeamId, QByteArray> m_explored;
    QList<PlayerId> m_turnOrder;
    int m_currentIndex = 0;
};

}  // namespace kot::core
