#include "client/ViewModel.h"

#include "core/model/Entities.h"

#include <QJsonArray>

namespace kot::client {

namespace {

core::Pos posOf(const QJsonObject& o) { return core::Pos{o.value("x").toInt(), o.value("y").toInt()}; }

core::ResourceAmounts amounts(const QJsonObject& o) {
    core::ResourceAmounts m;
    for (auto it = o.begin(); it != o.end(); ++it) m.insert(it.key(), it.value().toInt());
    return m;
}

QStringList strings(const QJsonValue& v) {
    QStringList list;
    for (const QJsonValue& s : v.toArray()) list << s.toString();
    return list;
}

}  // namespace

const ViewBattleUnit* ViewBattle::unit(int id) const {
    for (const ViewBattleUnit& u : units)
        if (u.id == id) return &u;
    return nullptr;
}

const ViewBattleUnit* ViewBattle::unitAt(core::Pos h) const {
    for (const ViewBattleUnit& u : units)
        if (u.hex == h) return &u;
    return nullptr;
}

bool ViewBattle::myMove() const {
    const ViewBattleUnit* u = unit(active);
    return u && u->side == yourSide;
}

const ViewPlayer* GameView::player(int id) const {
    for (const ViewPlayer& p : players) if (p.id == id) return &p;
    return nullptr;
}

int GameView::myTeam() const {
    const ViewPlayer* p = player(you);
    return p ? p->team : 0;
}

bool GameView::known(core::Pos p) const {
    return inBounds(p) && tiles.value(p.y * width + p.x, -1) >= 0;
}

bool GameView::inSight(core::Pos p) const {
    const qsizetype i = static_cast<qsizetype>(p.y) * width + p.x;
    return inBounds(p) && i < visible.size() && visible[i] != 0;
}

const ViewHero* GameView::hero(int id) const {
    for (const ViewHero& h : heroes) if (h.id == id) return &h;
    return nullptr;
}
const ViewArmy* GameView::army(int id) const {
    for (const ViewArmy& a : armies) if (a.id == id) return &a;
    return nullptr;
}
const ViewTown* GameView::town(int id) const {
    for (const ViewTown& t : towns) if (t.id == id) return &t;
    return nullptr;
}

QList<const ViewHero*> GameView::myHeroes() const {
    QList<const ViewHero*> list;
    for (const ViewHero& h : heroes) if (h.owner == you) list.append(&h);
    return list;
}
QList<const ViewTown*> GameView::myTowns() const {
    QList<const ViewTown*> list;
    for (const ViewTown& t : towns) if (t.mine) list.append(&t);
    return list;
}
const ViewHero* GameView::heroAt(core::Pos p) const {
    for (const ViewHero& h : heroes) if (h.pos == p) return &h;
    return nullptr;
}
const ViewTown* GameView::townCovering(core::Pos p) const {
    for (const ViewTown& t : towns)
        if (core::townCovers(t.pos, p)) return &t;
    return nullptr;
}

const ViewTown* GameView::townAt(core::Pos p) const {
    for (const ViewTown& t : towns) if (t.pos == p) return &t;
    return nullptr;
}

bool parseGameView(const QJsonObject& json, GameView* out, QString* error) {
    const auto fail = [&](const QString& why) {
        if (error) *error = why;
        return false;
    };
    if (!json.contains("map") || !json.value("map").isObject()) return fail("the view has no map");

    GameView v;
    v.day = json.value("day").toInt(1);
    v.currentPlayer = json.value("currentPlayer").toInt();
    v.gameOver = json.value("gameOver").toBool();
    v.difficulty = json.value("difficulty").toString();
    v.victory = json.value("victory").toString();
    v.weekEventName = json.value("weekEvent").toObject().value("name").toString();
    v.weekEventText = json.value("weekEvent").toObject().value("description").toString();
    v.campaign = json.value("campaign").toObject();
    v.winnerTeam = json.value("winnerTeam").toInt();
    v.you = json.value("you").toInt();
    v.resources = amounts(json.value("resources").toObject());
    v.income = amounts(json.value("income").toObject());
    v.tradePercent = json.value("tradePercent").toInt();

    for (const QJsonValue& pv : json.value("players").toArray()) {
        const QJsonObject o = pv.toObject();
        ViewPlayer p;
        p.id = o.value("id").toInt();
        p.name = o.value("name").toString();
        p.team = o.value("team").toInt();
        p.color = o.value("color").toString();
        p.race = o.value("race").toString();
        p.isAi = o.value("isAi").toBool();
        p.eliminated = o.value("eliminated").toBool();
        v.players.append(p);
    }
    const int myTeam = v.myTeam();
    const auto ownTeam = [&](int owner) {
        const ViewPlayer* p = v.player(owner);
        return p && p->team == myTeam;
    };

    const QJsonObject map = json.value("map").toObject();
    v.width = map.value("width").toInt();
    v.height = map.value("height").toInt();
    v.terrainPalette = strings(map.value("terrainPalette"));
    for (const QJsonValue& t : map.value("tiles").toArray()) v.tiles.append(t.toInt(-1));
    v.visible = QByteArray::fromBase64(map.value("visible").toString().toLatin1());
    if (v.width <= 0 || v.height <= 0 || v.tiles.size() != v.width * v.height)
        return fail("the map size does not match its tiles");
    for (int t : v.tiles) {
        if (t >= v.terrainPalette.size()) return fail("a tile refers to a terrain that is not in the palette");
    }

    for (const QJsonValue& hv : json.value("heroes").toArray()) {
        const QJsonObject o = hv.toObject();
        ViewHero h;
        h.id = o.value("id").toInt();
        h.name = o.value("name").toString();
        h.classId = o.value("class").toString();
        h.owner = o.value("owner").toInt();
        h.level = o.value("level").toInt(1);
        h.pos = posOf(o);
        h.army = o.value("army").toInt();
        h.inBoat = o.value("inBoat").toBool();
        h.own = ownTeam(h.owner);
        if (o.contains("traits")) {
            h.experience = o.value("experience").toInt();
            h.traits = amounts(o.value("traits").toObject());
            h.spellPoints = o.value("spellPoints").toInt();
            h.pendingTraitPicks = o.value("pendingTraitPicks").toInt();
            for (const QJsonValue& s : o.value("spells").toArray()) h.spells << s.toString();
            for (const QJsonValue& a : o.value("artifacts").toArray()) h.artifacts << a.toString();
            for (const QJsonValue& a : o.value("backpack").toArray()) h.backpack << a.toString();
            h.movementPoints = o.value("movementPoints").toInt();
            h.movementMax = o.value("movementMax").toInt();
            const QJsonArray d = o.value("destination").toArray();
            if (d.size() == 2) h.destination = core::Pos{d[0].toInt(), d[1].toInt()};
        }
        v.heroes.append(h);
    }
    for (const QJsonValue& av : json.value("armies").toArray()) {
        const QJsonObject o = av.toObject();
        ViewArmy a;
        a.id = o.value("id").toInt();
        a.owner = o.value("owner").toInt();
        a.hero = o.value("hero").toInt();
        a.pos = posOf(o);
        for (const QJsonValue& sv : o.value("stacks").toArray()) {
            const QJsonObject so = sv.toObject();
            ViewStack s;
            s.unit = so.value("unit").toString();
            s.count = so.value("count").toInt();
            s.totalHp = so.value("totalHp").toInt(-1);
            s.maxHp = so.value("maxHp").toInt(-1);
            s.stars = so.value("stars").toInt();
            a.stacks.append(s);
        }
        v.armies.append(a);
    }
    for (const QJsonValue& tv : json.value("towns").toArray()) {
        const QJsonObject o = tv.toObject();
        ViewTown t;
        t.id = o.value("id").toInt();
        t.name = o.value("name").toString();
        t.race = o.value("race").toString();
        t.owner = o.value("owner").toInt();
        t.pos = posOf(o);
        t.own = ownTeam(t.owner);
        t.mine = (t.owner == v.you);
        t.garrison = o.value("garrison").toInt();
        t.lastBuildDay = o.value("lastBuildDay").toInt();
        t.walls = o.value("walls").toInt();
        t.size = o.value("size").toInt();
        t.buildings = strings(o.value("buildings"));
        t.tavernOffers = strings(o.value("tavernOffers"));
        for (const QJsonValue& bv : o.value("buildOptions").toArray())
            t.buildOptions.append({bv.toObject().value("id").toString(), bv.toObject().value("why").toString()});
        for (const QJsonValue& rv : o.value("recruitOptions").toArray())
            t.recruitOptions.append({rv.toObject().value("unit").toString(), rv.toObject().value("available").toInt()});
        v.towns.append(t);
    }
    for (const QJsonValue& mv : json.value("mines").toArray()) {
        const QJsonObject o = mv.toObject();
        v.mines.append({o.value("id").toInt(), o.value("resource").toString(), o.value("dailyAmount").toInt(),
                        o.value("owner").toInt(), posOf(o)});
    }
    for (const QJsonValue& dv : json.value("mapDwellings").toArray()) {
        const QJsonObject o = dv.toObject();
        ViewDwelling d;
        d.id = o.value("id").toInt();
        d.unit = o.value("unit").toString();
        d.owner = o.value("owner").toInt();
        d.pos = posOf(o);
        d.available = o.value("available").toInt(-1);
        v.dwellings.append(d);
    }
    for (const QJsonValue& ov : json.value("mapObjects").toArray()) {
        const QJsonObject o = ov.toObject();
        ViewObject m;
        m.id = o.value("id").toInt();
        m.artifact = o.value("kind").toString() == "artifact";
        m.artifactId = o.value("artifact").toString();
        const QJsonObject res = o.value("resources").toObject();
        for (auto it = res.begin(); it != res.end(); ++it) m.resources.insert(it.key(), it.value().toInt());
        m.pos = posOf(o);
        v.objects.append(m);
    }
    if (json.value("battle").isObject()) {
        const QJsonObject b = json.value("battle").toObject();
        ViewBattle& vb = v.battle;
        vb.present = true;
        vb.width = b.value("width").toInt();
        vb.height = b.value("height").toInt();
        vb.round = b.value("round").toInt();
        vb.active = b.value("active").toInt();
        vb.yourSide = b.value("yourSide").toInt();
        const QJsonArray sides = b.value("sides").toArray();
        for (int s = 0; s < 2 && s < sides.size(); ++s) vb.sideOwner[s] = sides[s].toObject().value("owner").toInt();
        for (const QJsonValue& uv : b.value("units").toArray()) {
            const QJsonObject u = uv.toObject();
            ViewBattleUnit bu;
            bu.id = u.value("id").toInt();
            bu.side = u.value("side").toInt();
            bu.unit = u.value("unit").toString();
            bu.count = u.value("count").toInt();
            bu.frontHp = u.value("frontHp").toInt();
            bu.maxHp = u.value("maxHp").toInt();
            bu.hex = {u.value("x").toInt(), u.value("y").toInt()};
            bu.shots = u.value("shots").toInt();
            bu.retaliated = u.value("retaliated").toBool();
            bu.stars = u.value("stars").toInt();
            bu.range = u.value("range").toInt();
            vb.units.append(bu);
        }
        for (const QJsonValue& h : b.value("reachable").toArray())
            vb.reachable.append({h.toArray().at(0).toInt(), h.toArray().at(1).toInt()});
        for (const QJsonValue& a : b.value("attackable").toArray()) vb.attackable.append(a.toInt());
        for (const QJsonValue& a : b.value("shootable").toArray()) vb.shootable.append(a.toInt());
        vb.canShoot = b.value("canShoot").toBool();
        vb.canWait = b.value("canWait").toBool();
        vb.canRetreat = b.value("canRetreat").toBool();
        vb.surrenderCost = b.contains("surrenderCost") ? b.value("surrenderCost").toInt() : -1;
        vb.townDefense = b.value("townDefense").toInt();
        vb.terrain = b.value("terrain").toString();
        const QJsonObject siege = b.value("siege").toObject();
        vb.wallLevel = siege.value("level").toInt();
        for (const QJsonValue& w : siege.value("walls").toArray()) {
            const QJsonArray a = w.toArray();
            vb.walls.insert(core::Pos{a[0].toInt(), a[1].toInt()}, a[2].toInt());
        }
        vb.wallMaxHp = siege.value("wallMaxHp").toInt();
        const QJsonArray gate = siege.value("gate").toArray();
        if (gate.size() == 2) vb.gate = core::Pos{gate[0].toInt(), gate[1].toInt()};
        for (const QJsonValue& t : siege.value("towers").toArray()) vb.towers.append(core::Pos{t.toArray()[0].toInt(), t.toArray()[1].toInt()});
        vb.engineBuilt = siege.value("engineBuilt").toBool();
        vb.canBuildEngine = siege.value("canBuildEngine").toBool();
        for (const QJsonValue& h : b.value("obstacles").toArray())
            vb.obstacles.append({h.toArray().at(0).toInt(), h.toArray().at(1).toInt()});
        for (const QJsonValue& sv : b.value("unitSpells").toArray()) {
            const QJsonObject so = sv.toObject();
            vb.unitSpells.append({so.value("id").toString(), so.value("target").toString() == "enemy",
                                  so.value("cost").toInt(), so.value("castable").toBool()});
        }
        if (b.value("hero").isObject()) {
            const QJsonObject hero = b.value("hero").toObject();
            vb.hasHero = true;
            vb.heroSpellPoints = hero.value("spellPoints").toInt();
            vb.heroHasCast = hero.value("hasCast").toBool();
            for (const QJsonValue& sv : hero.value("spells").toArray()) {
                const QJsonObject so = sv.toObject();
                vb.spells.append({so.value("id").toString(), so.value("target").toString() == "enemy",
                                  so.value("cost").toInt(), so.value("castable").toBool()});
            }
        }
        if (vb.width <= 0 || vb.height <= 0) return fail("the battle has no field");
    }
    v.battleInProgress = json.value("battleInProgress").toBool();
    if (json.value("lastBattle").isObject()) {
        v.lastBattleId = json.value("lastBattle").toObject().value("id").toInt();
        v.lastBattleReport = json.value("lastBattle").toObject().value("report").toObject();
    }
    *out = v;
    return true;
}

}  // namespace kot::client
