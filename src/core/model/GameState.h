#pragma once

#include <QJsonArray>

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

// The save format. Only saves of exactly this version load (no conversion of old
// saves: an old save needs the game version that made it). Bump it when the format
// changes incompatibly. 3: content ids as numbers (with the content checksum).
inline constexpr int kSaveFormatVersion = 3;

// How a game is won besides being the last team standing (which always counts).
struct VictoryCondition {
    enum class Type { Conquest, HoldTown, Gold, Days };
    Type type = Type::Conquest;
    TownId town;      // HoldTown: whoever owns it wins
    int amount = 0;   // Gold: first to have this much gold; Days: the game ends after this day
};

// The complete state of one game. Plain data plus helpers; the rules live in
// core/rules. Owned by the server (the clients only ever see filtered views).
// What the load list shows about a save file, and whether it can be loaded.
struct SaveInfo {
    QString name;          // file name without .json
    QString gameVersion;   // the game version that saved it (empty for old saves)
    QString savedAt;       // ISO date and time (file time for old saves)
    int formatVersion = 0;
    int day = 0;
    QStringList players;
    int mapWidth = 0, mapHeight = 0;
    enum class Status { Ok, Incompatible } status = Status::Ok;
    QStringList problems;  // why it cannot be loaded
};
SaveInfo inspectSave(const QString& path, const class ContentRegistry& content);

class GameState {
public:
    explicit GameState(std::uint64_t seed = 1) : m_rng(seed) {}

    int day() const { return m_day; }
    const QString& difficulty() const { return m_difficulty; }
    const VictoryCondition& victory() const { return m_victory; }
    // This week's event (events.json id, empty = none).
    WeekEventId weekEvent() const { return m_weekEvent; }
    // Each player's numbers at the start of every week (GameStats.h), for the end screen.
    QJsonArray& history() { return m_history; }
    const QJsonArray& history() const { return m_history; }
    void setWeekEvent(WeekEventId id) { m_weekEvent = id; }
    // The scenario's story (brief): told at the start, and events on given days.
    // `news` is what happened today (the events' texts; cleared each new day).
    QString intro;
    QList<StoryEvent> storyEvents;
    QStringList news;
    void setVictory(const VictoryCondition& v) { m_victory = v; }
    void setDifficulty(const QString& d) { m_difficulty = d; }
    // Fog of war on: explored tiles out of sight are shaded and their armies hidden.
    // Off: whatever a team has explored stays in sight (Vision.h).
    bool fogOfWar() const { return m_fogOfWar; }
    void setFogOfWar(bool on) { m_fogOfWar = on; }
    // When a player is out (Victory.h): "week" (7 days without a town), "town" (at
    // once with his last town), "all" (no towns and no heroes left).
    const QString& defeatRule() const { return m_defeatRule; }

    void setDefeatRule(const QString& rule) { m_defeatRule = rule; }
    // Hidden map on: unexplored tiles are unknown (black). Off: every team knows the
    // whole land, towns, mines and dwellings from the start.
    bool hiddenMap() const { return m_hiddenMap; }
    void setHiddenMap(bool on) { m_hiddenMap = on; }
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
                       RaceId race, const QString& color);
    ArmyId addArmy(PlayerId owner, Pos pos);
    TownId addTown(const QString& name, RaceId race, Pos pos, PlayerId owner = {});
    MapDwellingId addMapDwelling(UnitTypeId unitId, Pos pos, int available, PlayerId owner = {});
    MapObjectId addMapObject(MapObject object);  // the id is assigned
    MineId addMine(ResourceId resource, int dailyAmount, Pos pos, PlayerId owner = {});

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
    // The town whose 2x2 tiles include `tile` (see townTiles), or null.
    const Town* townCovering(Pos tile) const;

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
    // Content ids are written as numbers (saves: with the content checksum, so a save
    // only loads with the same data) or as the text keys of the data files (scenarios,
    // which are data themselves; and kot_saveview, to read a save).
    enum class IdStyle { Numbers, Text };
    QJsonObject toJson(const ContentRegistry& content, IdStyle ids = IdStyle::Numbers) const;
    // Another save format version is refused with the reason.
    static bool fromJson(const QJsonObject& json, const ContentRegistry& content, GameState* out, QStringList* errors);
    // Files also carry the game version and the time of saving (for the load list).
    bool saveToFile(const QString& path, const ContentRegistry& content, QString* error) const;
    // What saveToFile writes (with gameVersion and savedAt).
    QByteArray saveBytes(const ContentRegistry& content) const;
    static bool loadFromFile(const QString& path, const ContentRegistry& content, GameState* out, QStringList* errors);

private:
    int m_day = 1;
    QString m_difficulty = QStringLiteral("normal");
    bool m_fogOfWar = true;
    QString m_defeatRule = QStringLiteral("week");

    bool m_hiddenMap = true;
    VictoryCondition m_victory;
    WeekEventId m_weekEvent;
    QJsonArray m_history;
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
