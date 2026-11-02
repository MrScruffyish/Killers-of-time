// Save/load of GameState as JSON. See docs/save-format.md.
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <climits>

#include "core/content/ContentRegistry.h"
#include "core/data/JsonReader.h"
#include "core/model/GameState.h"

namespace kot::core {

namespace {

constexpr int kMaxId = INT_MAX;

QJsonObject intMapToJson(const QMap<QString, int>& map) {
    QJsonObject o;
    for (auto it = map.begin(); it != map.end(); ++it) o.insert(it.key(), it.value());
    return o;
}

void putOptionalId(QJsonObject& o, const QString& key, quint32 id) {
    if (id != 0) o.insert(key, static_cast<int>(id));
}

}  // namespace

QJsonObject GameState::toJson() const {
    QJsonObject root;
    root.insert("formatVersion", kSaveFormatVersion);
    root.insert("day", m_day);
    root.insert("difficulty", m_difficulty);
    root.insert("nextId", static_cast<int>(m_nextId));
    root.insert("currentIndex", m_currentIndex);

    // Map: terrain palette + flat list of palette indices.
    {
        QJsonArray palette;
        QHash<QString, int> index;
        QJsonArray tiles;
        for (const QString& t : m_map.tiles()) {
            auto it = index.find(t);
            if (it == index.end()) {
                it = index.insert(t, static_cast<int>(palette.size()));
                palette.append(t);
            }
            tiles.append(it.value());
        }
        QJsonObject map;
        map.insert("width", m_map.width());
        map.insert("height", m_map.height());
        map.insert("terrainPalette", palette);
        map.insert("tiles", tiles);
        root.insert("map", map);
    }

    QJsonObject explored;
    for (auto it = m_explored.begin(); it != m_explored.end(); ++it)
        explored.insert(QString::number(it.key().value()),
                        QString::fromLatin1(it.value().toBase64()));
    root.insert("explored", explored);

    QJsonObject rng;
    rng.insert("seed", QString::number(m_rng.seed()));  // string: 64-bit does not fit a JSON number
    rng.insert("state", m_rng.engineState());
    root.insert("rng", rng);

    QJsonArray turnOrder;
    for (PlayerId id : m_turnOrder) turnOrder.append(static_cast<int>(id.value()));
    root.insert("turnOrder", turnOrder);

    QJsonArray players;
    for (const Player& p : m_players) {
        QJsonObject o;
        o.insert("id", static_cast<int>(p.id.value()));
        o.insert("name", p.name);
        o.insert("team", static_cast<int>(p.team.value()));
        o.insert("controller", p.controller == Player::Controller::Ai ? "ai" : "human");
        if (!p.aiProfile.isEmpty()) o.insert("aiProfile", p.aiProfile);
        if (p.incomePercent) o.insert("incomePercent", p.incomePercent);
        if (p.growthPercent) o.insert("growthPercent", p.growthPercent);
        o.insert("race", p.race);
        o.insert("color", p.color);
        o.insert("resources", intMapToJson(p.resources));
        players.append(o);
    }
    root.insert("players", players);

    QJsonArray heroes;
    for (const Hero& h : m_heroes) {
        QJsonObject o;
        o.insert("id", static_cast<int>(h.id.value()));
        o.insert("name", h.name);
        o.insert("class", h.classId);
        o.insert("owner", static_cast<int>(h.owner.value()));
        o.insert("level", h.level);
        o.insert("experience", h.experience);
        o.insert("traits", intMapToJson(h.traits));
        o.insert("spellPoints", h.spellPoints);
        o.insert("pendingTraitPicks", h.pendingTraitPicks);
        o.insert("movementPoints", h.movementPoints);
        o.insert("army", static_cast<int>(h.army.value()));
        if (!h.templateId.isEmpty()) o.insert("template", h.templateId);
        if (!h.spells.isEmpty()) o.insert("spells", QJsonArray::fromStringList(h.spells));
        if (!h.artifacts.isEmpty()) o.insert("artifacts", QJsonArray::fromStringList(h.artifacts));
        heroes.append(o);
    }
    root.insert("heroes", heroes);

    QJsonArray armies;
    for (const Army& a : m_armies) {
        QJsonObject o;
        o.insert("id", static_cast<int>(a.id.value()));
        putOptionalId(o, "owner", a.owner.value());
        putOptionalId(o, "hero", a.hero.value());
        o.insert("x", a.pos.x);
        o.insert("y", a.pos.y);
        QJsonArray stacks;
        for (const UnitStack& s : a.stacks) {
            QJsonObject so;
            so.insert("unit", s.unitTypeId());
            so.insert("maxHp", s.maxHp());
            so.insert("totalHp", s.totalHp());
            stacks.append(so);
        }
        o.insert("stacks", stacks);
        armies.append(o);
    }
    root.insert("armies", armies);

    QJsonArray towns;
    for (const Town& t : m_towns) {
        QJsonObject o;
        o.insert("id", static_cast<int>(t.id.value()));
        o.insert("name", t.name);
        o.insert("race", t.race);
        putOptionalId(o, "owner", t.owner.value());
        o.insert("x", t.pos.x);
        o.insert("y", t.pos.y);
        o.insert("buildings", QJsonArray::fromStringList(t.buildings));
        putOptionalId(o, "garrison", t.garrison.value());
        o.insert("lastBuildDay", t.lastBuildDay);
        o.insert("recruitPool", intMapToJson(t.recruitPool));
        o.insert("importedUnits", intMapToJson(t.importedUnits));
        o.insert("tavernOffers", QJsonArray::fromStringList(t.tavernOffers));
        towns.append(o);
    }
    root.insert("towns", towns);

    QJsonArray dwellings;
    for (const MapDwelling& d : m_mapDwellings) {
        QJsonObject o;
        o.insert("id", static_cast<int>(d.id.value()));
        o.insert("unit", d.unitId);
        putOptionalId(o, "owner", d.owner.value());
        o.insert("x", d.pos.x);
        o.insert("y", d.pos.y);
        o.insert("available", d.available);
        dwellings.append(o);
    }
    root.insert("mapDwellings", dwellings);

    QJsonArray objects;
    for (const MapObject& m : m_mapObjects) {
        QJsonObject o;
        o.insert("id", static_cast<int>(m.id.value()));
        o.insert("kind", m.kind == MapObject::Kind::Artifact ? "artifact" : "treasure");
        o.insert("x", m.pos.x);
        o.insert("y", m.pos.y);
        if (!m.resources.isEmpty()) o.insert("resources", intMapToJson(m.resources));
        if (!m.artifact.isEmpty()) o.insert("artifact", m.artifact);
        objects.append(o);
    }
    root.insert("mapObjects", objects);

    QJsonArray mines;
    for (const Mine& m : m_mines) {
        QJsonObject o;
        o.insert("id", static_cast<int>(m.id.value()));
        o.insert("resource", m.resourceId);
        o.insert("dailyAmount", m.dailyAmount);
        putOptionalId(o, "owner", m.owner.value());
        o.insert("x", m.pos.x);
        o.insert("y", m.pos.y);
        mines.append(o);
    }
    root.insert("mines", mines);
    return root;
}

bool GameState::fromJson(const QJsonObject& json, const ContentRegistry& content, GameState* out,
                         QStringList* errors) {
    QStringList errs;
    GameState gs;
    QSet<quint32> usedIds;
    quint32 maxId = 0;

    auto note = [&](quint32 id) { maxId = std::max(maxId, id); };
    auto claim = [&](const QString& ctx, quint32 id) {
        note(id);
        if (usedIds.contains(id)) {
            errs << ctx + QStringLiteral(": id %1 is used twice").arg(id);
            return false;
        }
        usedIds.insert(id);
        return true;
    };
    // Calls fn(reader, context) for every object in `arr`.
    auto each = [&](const QJsonArray& arr, const QString& name, auto fn) {
        for (qsizetype i = 0; i < arr.size(); ++i) {
            const QString ctx = QStringLiteral("%1[%2]").arg(name).arg(i);
            if (!arr[i].isObject()) {
                errs << ctx + QStringLiteral(": entry must be an object");
                continue;
            }
            JsonReader r(arr[i].toObject(), ctx, &errs);
            fn(r, ctx);
        }
    };

    JsonReader top(json, QStringLiteral("save"), &errs);
    const int version = top.requireInt("formatVersion", 1, 1000000);
    if (top.ok() && version != kSaveFormatVersion)
        top.addError(QStringLiteral("unsupported formatVersion %1 (expected %2)")
                         .arg(version).arg(kSaveFormatVersion));
    gs.m_day = top.requireInt("day", 1, 10000000);
    gs.m_difficulty = top.optionalString("difficulty", QStringLiteral("normal"));
    const int nextId = top.requireInt("nextId", 1, kMaxId);
    const int currentIndex = top.optionalInt("currentIndex", 0, 0, kMaxId);
    const QJsonObject mapObj = top.requireObject("map");
    const QJsonObject rngObj = top.requireObject("rng");
    const QJsonObject exploredObj = top.requireObject("explored");
    const QJsonArray turnOrderA = top.requireArray("turnOrder");
    const QJsonArray playersA = top.requireArray("players");
    const QJsonArray heroesA = top.requireArray("heroes");
    const QJsonArray armiesA = top.requireArray("armies");
    const QJsonArray townsA = top.requireArray("towns");
    const QJsonArray minesA = top.requireArray("mines");
    const QJsonArray dwellingsA = top.requireArray("mapDwellings");
    const QJsonArray objectsA = top.has("mapObjects") ? top.requireArray("mapObjects") : QJsonArray();
    top.finish();
    if (!top.ok()) {
        if (errors) *errors = errs;
        return false;
    }

    // ---- map
    {
        JsonReader r(mapObj, QStringLiteral("map"), &errs);
        const int w = r.requireInt("width", 1, 4096);
        const int h = r.requireInt("height", 1, 4096);
        const QJsonArray paletteA = r.requireArray("terrainPalette");
        const QJsonArray tilesA = r.requireArray("tiles");
        r.finish();
        if (r.ok()) {
            QStringList palette;
            bool good = true;
            for (const QJsonValue& v : paletteA) {
                if (!v.isString() || !content.terrainType(v.toString())) {
                    errs << QStringLiteral("map: unknown terrain '%1' in terrainPalette")
                                .arg(v.toString());
                    good = false;
                }
                palette << v.toString();
            }
            if (tilesA.size() != static_cast<qsizetype>(w) * h) {
                errs << QStringLiteral("map: tiles has %1 entries, expected width*height = %2")
                            .arg(tilesA.size()).arg(static_cast<qsizetype>(w) * h);
                good = false;
            }
            QList<QString> tiles;
            if (good) {
                tiles.reserve(tilesA.size());
                for (const QJsonValue& v : tilesA) {
                    const int i = v.toInt(-1);
                    if (!v.isDouble() || i < 0 || i >= palette.size()) {
                        errs << QStringLiteral("map: a tile refers to terrain index %1 that is "
                                               "not in terrainPalette").arg(v.toDouble());
                        good = false;
                        break;
                    }
                    tiles.append(palette[i]);
                }
            }
            if (good) gs.m_map = GameMap::fromTiles(w, h, std::move(tiles));
        }
    }

    // ---- rng
    {
        JsonReader r(rngObj, QStringLiteral("rng"), &errs);
        const QString seedText = r.requireString("seed");
        const QString state = r.requireString("state");
        r.finish();
        if (r.ok()) {
            bool ok = false;
            const quint64 seed = seedText.toULongLong(&ok);
            if (!ok) {
                errs << QStringLiteral("rng: seed must be a number written as text");
            } else {
                gs.m_rng.reseed(seed);
                if (!gs.m_rng.restoreEngineState(state))
                    errs << QStringLiteral("rng: state is not valid");
            }
        }
    }

    const auto checkPos = [&](const QString& ctx, Pos p) {
        if (gs.m_map.width() > 0 && !gs.m_map.inBounds(p))
            errs << ctx + QStringLiteral(": position (%1, %2) is outside the %3x%4 map")
                              .arg(p.x).arg(p.y).arg(gs.m_map.width()).arg(gs.m_map.height());
    };

    // ---- players
    each(playersA, QStringLiteral("players"), [&](JsonReader& r, const QString& ctx) {
        Player p;
        p.id = PlayerId(static_cast<quint32>(r.requireInt("id", 1, kMaxId)));
        p.name = r.requireString("name");
        p.team = TeamId(static_cast<quint32>(r.requireInt("team", 1, kMaxId)));
        const QString controller = r.requireString("controller");
        if (controller == QLatin1String("human")) p.controller = Player::Controller::Human;
        else if (controller == QLatin1String("ai")) p.controller = Player::Controller::Ai;
        else if (!controller.isEmpty())
            r.addError(QStringLiteral("field 'controller' must be 'human' or 'ai'"));
        p.aiProfile = r.optionalString("aiProfile");
        p.incomePercent = r.optionalInt("incomePercent", 0, -100, 1000);
        p.growthPercent = r.optionalInt("growthPercent", 0, -100, 1000);
        p.race = r.requireId("race");
        p.color = r.requireColor("color");
        p.resources = r.optionalIntMap("resources", 0, 1000000000);
        r.finish();
        for (auto it = p.resources.begin(); it != p.resources.end(); ++it) {
            if (!content.resource(it.key()))
                r.addError(QStringLiteral("unknown resource '%1'").arg(it.key()));
        }
        note(p.team.value());
        if (r.ok() && claim(ctx, p.id.value())) gs.m_players.insert(p.id, p);
    });

    // ---- heroes
    each(heroesA, QStringLiteral("heroes"), [&](JsonReader& r, const QString& ctx) {
        Hero h;
        h.id = HeroId(static_cast<quint32>(r.requireInt("id", 1, kMaxId)));
        h.name = r.requireString("name");
        h.classId = r.requireId("class");
        h.owner = PlayerId(static_cast<quint32>(r.requireInt("owner", 1, kMaxId)));
        h.level = r.requireInt("level", 1, std::max(1, content.maxLevel()));
        h.experience = r.requireInt("experience", 0, 1000000000);
        h.traits = r.optionalIntMap("traits", 0, 1000000);
        h.spellPoints = r.requireInt("spellPoints", 0, 1000000);
        h.pendingTraitPicks = r.requireInt("pendingTraitPicks", 0, 10000);
        h.movementPoints = r.requireInt("movementPoints", 0, 10000000);
        h.army = ArmyId(static_cast<quint32>(r.requireInt("army", 1, kMaxId)));
        h.templateId = r.optionalString("template");
        h.spells = r.optionalStringList("spells");
        h.artifacts = r.optionalStringList("artifacts");
        r.finish();
        for (const QString& s : h.spells)
            if (!content.spell(s)) r.addError(QStringLiteral("unknown spell '%1'").arg(s));
        for (const QString& a : h.artifacts)
            if (!content.artifact(a)) r.addError(QStringLiteral("unknown artifact '%1'").arg(a));
        if (r.ok() && !h.templateId.isEmpty() && !content.heroTemplate(h.templateId))
            r.addError(QStringLiteral("unknown hero template '%1'").arg(h.templateId));
        if (r.ok() && !content.heroClass(h.classId))
            r.addError(QStringLiteral("unknown hero class '%1'").arg(h.classId));
        for (auto it = h.traits.begin(); it != h.traits.end(); ++it) {
            if (!content.trait(it.key()))
                r.addError(QStringLiteral("unknown trait '%1'").arg(it.key()));
        }
        if (r.ok() && claim(ctx, h.id.value())) gs.m_heroes.insert(h.id, h);
    });

    // ---- armies
    each(armiesA, QStringLiteral("armies"), [&](JsonReader& r, const QString& ctx) {
        Army a;
        a.id = ArmyId(static_cast<quint32>(r.requireInt("id", 1, kMaxId)));
        a.owner = PlayerId(static_cast<quint32>(r.optionalInt("owner", 0, 0, kMaxId)));
        a.hero = HeroId(static_cast<quint32>(r.optionalInt("hero", 0, 0, kMaxId)));
        a.pos.x = r.requireInt("x", 0, 100000);
        a.pos.y = r.requireInt("y", 0, 100000);
        const QJsonArray stacksA = r.requireArray("stacks");
        r.finish();
        for (qsizetype i = 0; i < stacksA.size(); ++i) {
            const QString sctx = QStringLiteral("%1.stacks[%2]").arg(ctx).arg(i);
            if (!stacksA[i].isObject()) {
                errs << sctx + QStringLiteral(": entry must be an object");
                continue;
            }
            JsonReader sr(stacksA[i].toObject(), sctx, &errs);
            const QString unit = sr.requireId("unit");
            const int maxHp = sr.requireInt("maxHp", 1, 100000000);
            const int totalHp = sr.requireInt("totalHp", 1, 2000000000);
            sr.finish();
            if (sr.ok() && !content.unit(unit))
                sr.addError(QStringLiteral("unknown unit '%1'").arg(unit));
            if (sr.ok()) a.stacks.append(UnitStack::fromTotalHp(unit, totalHp, maxHp));
        }
        if (a.stacks.size() > kMaxArmyStacks)
            r.addError(QStringLiteral("more than %1 stacks").arg(kMaxArmyStacks));
        checkPos(ctx, a.pos);
        if (r.ok() && claim(ctx, a.id.value())) gs.m_armies.insert(a.id, a);
    });

    // ---- towns
    each(townsA, QStringLiteral("towns"), [&](JsonReader& r, const QString& ctx) {
        Town t;
        t.id = TownId(static_cast<quint32>(r.requireInt("id", 1, kMaxId)));
        t.name = r.requireString("name");
        t.race = r.requireId("race");
        t.owner = PlayerId(static_cast<quint32>(r.optionalInt("owner", 0, 0, kMaxId)));
        t.pos.x = r.requireInt("x", 0, 100000);
        t.pos.y = r.requireInt("y", 0, 100000);
        t.buildings = r.optionalStringList("buildings");
        t.garrison = ArmyId(static_cast<quint32>(r.optionalInt("garrison", 0, 0, kMaxId)));
        t.lastBuildDay = r.optionalInt("lastBuildDay", 0, 0, 10000000);
        t.recruitPool = r.optionalIntMap("recruitPool", 0, 1000000);
        t.importedUnits = r.optionalIntMap("importedUnits", 0, 1000000);
        t.tavernOffers = r.optionalStringList("tavernOffers");
        for (const QString& offer : t.tavernOffers) {
            if (!content.heroTemplate(offer))
                r.addError(QStringLiteral("unknown hero template '%1' in tavernOffers").arg(offer));
        }
        r.finish();
        for (const QString& b : t.buildings) {
            if (!content.building(b)) r.addError(QStringLiteral("unknown building '%1'").arg(b));
        }
        for (auto it = t.recruitPool.begin(); it != t.recruitPool.end(); ++it) {
            if (!content.building(it.key()))
                r.addError(QStringLiteral("recruitPool uses unknown building '%1'").arg(it.key()));
        }
        checkPos(ctx, t.pos);
        if (r.ok() && claim(ctx, t.id.value())) gs.m_towns.insert(t.id, t);
    });

    // ---- map dwellings
    each(dwellingsA, QStringLiteral("mapDwellings"), [&](JsonReader& r, const QString& ctx) {
        MapDwelling d;
        d.id = MapDwellingId(static_cast<quint32>(r.requireInt("id", 1, kMaxId)));
        d.unitId = r.requireId("unit");
        d.owner = PlayerId(static_cast<quint32>(r.optionalInt("owner", 0, 0, kMaxId)));
        d.pos.x = r.requireInt("x", 0, 100000);
        d.pos.y = r.requireInt("y", 0, 100000);
        d.available = r.requireInt("available", 0, 1000000);
        r.finish();
        if (r.ok() && !content.unit(d.unitId))
            r.addError(QStringLiteral("unknown unit '%1'").arg(d.unitId));
        checkPos(ctx, d.pos);
        if (r.ok() && claim(ctx, d.id.value())) gs.m_mapDwellings.insert(d.id, d);
    });

    // ---- map objects (treasures, artifacts)
    each(objectsA, QStringLiteral("mapObjects"), [&](JsonReader& r, const QString& ctx) {
        MapObject m;
        m.id = MapObjectId(static_cast<quint32>(r.requireInt("id", 1, kMaxId)));
        const QString kind = r.requireString("kind");
        m.pos.x = r.requireInt("x", 0, 100000);
        m.pos.y = r.requireInt("y", 0, 100000);
        m.resources = r.optionalIntMap("resources", 1, 100000000);
        m.artifact = r.optionalString("artifact");
        r.finish();
        if (kind == "artifact") m.kind = MapObject::Kind::Artifact;
        else if (kind != "treasure") r.addError(QStringLiteral("kind must be treasure or artifact"));
        if (r.ok() && m.kind == MapObject::Kind::Artifact && !content.artifact(m.artifact))
            r.addError(QStringLiteral("unknown artifact '%1'").arg(m.artifact));
        for (auto it = m.resources.begin(); it != m.resources.end(); ++it)
            if (!content.resource(it.key())) r.addError(QStringLiteral("unknown resource '%1'").arg(it.key()));
        checkPos(ctx, m.pos);
        if (r.ok() && claim(ctx, m.id.value())) gs.m_mapObjects.insert(m.id, m);
    });

    // ---- mines
    each(minesA, QStringLiteral("mines"), [&](JsonReader& r, const QString& ctx) {
        Mine m;
        m.id = MineId(static_cast<quint32>(r.requireInt("id", 1, kMaxId)));
        m.resourceId = r.requireId("resource");
        m.dailyAmount = r.requireInt("dailyAmount", 1, 1000000);
        m.owner = PlayerId(static_cast<quint32>(r.optionalInt("owner", 0, 0, kMaxId)));
        m.pos.x = r.requireInt("x", 0, 100000);
        m.pos.y = r.requireInt("y", 0, 100000);
        r.finish();
        if (r.ok() && !content.resource(m.resourceId))
            r.addError(QStringLiteral("unknown resource '%1'").arg(m.resourceId));
        checkPos(ctx, m.pos);
        if (r.ok() && claim(ctx, m.id.value())) gs.m_mines.insert(m.id, m);
    });

    // ---- fog of war memory
    if (gs.m_map.width() > 0) {
        QSet<quint32> teams;
        for (const Player& p : gs.m_players) teams.insert(p.team.value());
        const qsizetype expected = static_cast<qsizetype>(gs.m_map.width()) * gs.m_map.height();
        for (auto it = exploredObj.begin(); it != exploredObj.end(); ++it) {
            bool ok = false;
            const quint32 teamId = it.key().toUInt(&ok);
            if (!ok || !teams.contains(teamId)) {
                errs << QStringLiteral("explored: '%1' is not a team in this game").arg(it.key());
                continue;
            }
            const QByteArray data = QByteArray::fromBase64(it.value().toString().toLatin1());
            if (!it.value().isString() || data.size() != expected) {
                errs << QStringLiteral("explored: team %1 needs %2 tiles of data")
                            .arg(teamId).arg(expected);
                continue;
            }
            gs.m_explored.insert(TeamId(teamId), data);
        }
    }

    // ---- turn order
    for (qsizetype i = 0; i < turnOrderA.size(); ++i) {
        const PlayerId id(static_cast<quint32>(turnOrderA[i].toInt(0)));
        if (!gs.m_players.contains(id))
            errs << QStringLiteral("turnOrder[%1]: unknown player %2").arg(i).arg(id.value());
        else
            gs.m_turnOrder.append(id);
    }
    if (currentIndex != 0 && currentIndex >= gs.m_turnOrder.size())
        errs << QStringLiteral("save: currentIndex %1 is outside turnOrder").arg(currentIndex);
    gs.m_currentIndex = currentIndex;

    // ---- cross references between objects
    const auto needPlayer = [&](const QString& what, quint32 id, bool optional) {
        if (id == 0 && optional) return;
        if (!gs.m_players.contains(PlayerId(id)))
            errs << QStringLiteral("%1: owner %2 is not a player").arg(what).arg(id);
    };
    for (const Hero& h : gs.m_heroes) {
        const QString what = QStringLiteral("hero %1 (%2)").arg(h.id.value()).arg(h.name);
        needPlayer(what, h.owner.value(), false);
        const Army* a = gs.army(h.army);
        if (!a) errs << what + QStringLiteral(": army %1 does not exist").arg(h.army.value());
        else if (a->hero != h.id)
            errs << what + QStringLiteral(": army %1 does not list this hero").arg(h.army.value());
    }
    for (const Army& a : gs.m_armies) {
        const QString what = QStringLiteral("army %1").arg(a.id.value());
        needPlayer(what, a.owner.value(), true);
        if (a.hero.isValid()) {
            const Hero* h = gs.hero(a.hero);
            if (!h) errs << what + QStringLiteral(": hero %1 does not exist").arg(a.hero.value());
            else if (h->army != a.id)
                errs << what + QStringLiteral(": hero %1 leads a different army").arg(a.hero.value());
        }
    }
    for (const Town& t : gs.m_towns) {
        const QString what = QStringLiteral("town %1 (%2)").arg(t.id.value()).arg(t.name);
        needPlayer(what, t.owner.value(), true);
        if (t.garrison.isValid() && !gs.army(t.garrison))
            errs << what + QStringLiteral(": garrison army %1 does not exist").arg(t.garrison.value());
    }
    for (const MapDwelling& d : gs.m_mapDwellings)
        needPlayer(QStringLiteral("dwelling %1").arg(d.id.value()), d.owner.value(), true);
    for (const Mine& m : gs.m_mines)
        needPlayer(QStringLiteral("mine %1").arg(m.id.value()), m.owner.value(), true);

    if (!errs.isEmpty()) {
        if (errors) *errors = errs;
        return false;
    }
    gs.m_nextId = std::max<quint32>(static_cast<quint32>(nextId), maxId + 1);
    *out = std::move(gs);
    if (errors) errors->clear();
    return true;
}

bool GameState::saveToFile(const QString& path, QString* error) const {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool GameState::loadFromFile(const QString& path, const ContentRegistry& content, GameState* out,
                             QStringList* errors) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errors) *errors = {QStringLiteral("%1: cannot open (%2)").arg(path, file.errorString())};
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errors)
            *errors = {QStringLiteral("%1: not a valid save file (%2)").arg(path, pe.errorString())};
        return false;
    }
    return fromJson(doc.object(), content, out, errors);
}

}  // namespace kot::core
