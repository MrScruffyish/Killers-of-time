#include "core/model/PlayerView.h"

#include "core/rules/Veterancy.h"
#include "core/rules/Victory.h"
#include "core/content/IdJson.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>

#include "core/content/ContentRegistry.h"
#include "core/rules/GameStats.h"
#include "core/rules/MapSites.h"
#include "core/rules/Movement.h"
#include "core/rules/TownRules.h"
#include "core/rules/Trade.h"
#include "core/rules/TurnCycle.h"
#include "core/rules/Vision.h"

namespace kot::core {

namespace {

int id(quint32 value) { return static_cast<int>(value); }

}  // namespace

QJsonObject buildPlayerView(const GameState& state, const ContentRegistry& content, PlayerId viewer) {
    const TeamId team = state.teamOf(viewer);
    const QByteArray visible = visibleTiles(state, team, content);
    const QByteArray& explored = state.exploredMap(team);
    const GameMap& map = state.map();
    const int w = map.width();

    const auto sees = [&](Pos p) {
        return map.inBounds(p) && visible.size() > p.y * w + p.x && visible[p.y * w + p.x];
    };
    const auto ownTeam = [&](PlayerId owner) { return owner.isValid() && state.teamOf(owner) == team; };
    // Places (towns, mines, dwellings, treasures) on explored land are known when the
    // map was revealed at the start (no hidden map); else only while in sight.
    const auto knows = [&](Pos p) {
        return sees(p) || (!state.hiddenMap() && map.inBounds(p) && explored.size() > p.y * w + p.x && explored[p.y * w + p.x]);
    };

    QJsonObject view;
    view.insert("day", state.day());
    view.insert("difficulty", state.difficulty());
    if (!state.intro.isEmpty()) view.insert("intro", state.intro);
    if (!state.news.isEmpty()) view.insert("news", QJsonArray::fromStringList(state.news));
    if (const WeekEventDef* e = content.weekEvent(state.weekEvent()))
        view.insert("weekEvent", QJsonObject{{"id", e->id.index()}, {"name", e->name}, {"description", e->description}});
    view.insert("currentPlayer", id(state.currentPlayer().value()));
    view.insert("you", id(viewer.value()));
    const Player* me = state.player(viewer);
    view.insert("resources", amountsToJson(me ? me->resources : ResourceAmounts()));
    view.insert("income", amountsToJson(me ? dailyIncome(state, content, viewer) : ResourceAmounts()));
    view.insert("tradePercent", me ? tradeEfficiency(state, content, viewer) : 0);

    QJsonArray players;
    // The kingdoms overview: towns, heroes and mines as they are (the map shows them
    // anyway); army strength exact for the own team, else only roughly.
    const QJsonObject stats = playerStats(state, content);
    const TeamId viewerTeam = state.teamOf(viewer);
    const auto roughly = [](int v) {
        int scale = 1;
        while (v / scale >= 100) scale *= 10;
        return (v + scale / 2) / scale * scale;
    };
    for (const Player& p : state.players()) {
        QJsonObject o;
        o.insert("id", id(p.id.value()));
        o.insert("name", p.name);
        o.insert("team", id(p.team.value()));
        o.insert("color", p.color);
        o.insert("race", p.race.index());
        o.insert("isAi", p.controller == Player::Controller::Ai);
        o.insert("eliminated", isEliminated(state, p.id));
        const QJsonObject st = stats.value(QString::number(p.id.value())).toObject();
        o.insert("towns", st.value("towns").toInt());
        o.insert("heroes", st.value("heroes").toInt());
        o.insert("mines", st.value("mines").toInt());
        const int army = st.value("armyValue").toInt();
        o.insert("armyValue", viewerTeam.isValid() && p.team == viewerTeam ? army : roughly(army));
        if (const int left = townlessDaysLeft(state, p.id); left > 0) o.insert("townlessDaysLeft", left);
        players.append(o);
    }
    view.insert("players", players);
    const GameOutcome outcome = gameOutcome(state, content);
    view.insert("gameOver", outcome.over);
    view.insert("victory", describeVictory(state));
    view.insert("defeat", describeDefeat(state));
    if (outcome.over) {
        view.insert("winnerTeam", id(outcome.winner.value()));
        // The whole story, for the end screen (hidden while the game runs).
        QJsonArray history = state.history();
        history.append(QJsonObject{{"week", (state.day() - 1) / 7 + 1}, {"day", state.day()}, {"players", playerStats(state, content)}});
        view.insert("history", history);
    }

    // Map: palette + indices, -1 where the team has not been.
    {
        QJsonArray tiles;  // the terrain id of every tile; -1 = never explored
        for (int y = 0; y < map.height(); ++y) {
            for (int x = 0; x < w; ++x) {
                const qsizetype i = static_cast<qsizetype>(y) * w + x;
                const bool known = (i < visible.size() && visible[i]) || (i < explored.size() && explored[i]);
                tiles.append(known ? map.terrainAt({x, y}).index() : -1);
            }
        }
        QJsonObject m;
        m.insert("width", w);
        m.insert("height", map.height());
        m.insert("tiles", tiles);
        m.insert("visible", QString::fromLatin1(visible.toBase64()));
        view.insert("map", m);
    }

    QSet<ArmyId> garrisons;
    for (const Town& t : state.towns()) garrisons.insert(t.garrison);

    QJsonArray heroes;
    for (const Hero& h : state.heroes()) {
        const Army* army = state.army(h.army);
        if (!army) continue;
        const bool own = ownTeam(h.owner);
        if (!own && !sees(army->pos)) continue;
        QJsonObject o;
        o.insert("id", id(h.id.value()));
        o.insert("name", h.name);
        o.insert("class", h.classId.index());
        o.insert("owner", id(h.owner.value()));
        o.insert("level", h.level);
        o.insert("x", army->pos.x);
        o.insert("y", army->pos.y);
        o.insert("army", id(h.army.value()));
        if (h.inBoat) o.insert("inBoat", true);
        if (own) {
            o.insert("experience", h.experience);
            {
                QJsonArray traits;  // value per trait id, trailing zeros left out
                int last = -1;
                for (const auto [t, v] : h.traits) last = t.index();
                for (int i = 0; i <= last; ++i) traits.append(h.traits.value(TraitId(i)));
                o.insert("traits", traits);
            }
            o.insert("spellPoints", h.spellPoints);
            o.insert("pendingTraitPicks", h.pendingTraitPicks);
            o.insert("movementPoints", h.movementPoints);
            o.insert("movementMax", armyMovementPoints(*army, &h, content));
            o.insert("template", h.templateId.index());
            {
                QJsonArray spells;
                for (const SpellId s : h.spells) spells.append(s.index());
                o.insert("spells", spells);
            }
            for (const char* field : {"artifacts", "backpack"}) {
                QJsonArray list;  // artifact ids
                for (const ArtifactId a : (QLatin1String(field) == QLatin1String("artifacts") ? h.artifacts : h.backpack)) list.append(a.index());
                o.insert(QLatin1String(field), list);
            }
            if (h.destination) o.insert("destination", QJsonArray{h.destination->x, h.destination->y});
        }
        heroes.append(o);
    }
    view.insert("heroes", heroes);

    QJsonArray armies;
    for (const Army& a : state.armies()) {
        const bool own = ownTeam(a.owner);
        if (!own && (garrisons.contains(a.id) || !sees(a.pos))) continue;  // garrisons are secret
        QJsonObject o;
        o.insert("id", id(a.id.value()));
        o.insert("owner", id(a.owner.value()));
        o.insert("hero", id(a.hero.value()));
        o.insert("x", a.pos.x);
        o.insert("y", a.pos.y);
        QJsonArray stacks;
        for (const UnitStack& s : a.stacks) {
            QJsonObject so;
            so.insert("unit", s.unitTypeId().index());
            so.insert("count", s.count());
            if (const int stars = veteranStars(s.experience(), content)) so.insert("stars", stars);
            if (own) {
                so.insert("totalHp", s.totalHp());
                so.insert("maxHp", s.maxHp());
            }
            stacks.append(so);
        }
        o.insert("stacks", stacks);
        armies.append(o);
    }
    view.insert("armies", armies);

    QJsonArray towns;
    for (const Town& t : state.towns()) {
        const bool own = ownTeam(t.owner);
        if (!own && !knows(t.pos)) continue;
        QJsonObject o;
        o.insert("id", id(t.id.value()));
        o.insert("name", t.name);
        o.insert("race", t.race.index());
        o.insert("owner", id(t.owner.value()));
        o.insert("x", t.pos.x);
        o.insert("y", t.pos.y);
        // What anyone who sees the town can tell: how many walls (buildings with a
        // defense effect) and how big it is (buildings).
        int walls = 0;
        for (const BuildingTypeId b : t.buildings)
            if (const BuildingDef* def = content.building(b))
                for (const BuildingEffect& e : def->effects) walls += e.type == BuildingEffect::Type::Defense;
        o.insert("walls", walls);
        o.insert("size", static_cast<int>(t.buildings.size()));
        if (own) {
            o.insert("garrison", id(t.garrison.value()));
            {
                QJsonArray built;  // building ids
                for (const BuildingTypeId b : t.buildings) built.append(b.index());
                o.insert("buildings", built);
            }
            o.insert("lastBuildDay", t.lastBuildDay);
            o.insert("hiredThisWeek", t.lastHireWeek == (state.day() - 1) / 7 + 1);
            {
                QJsonObject pool;  // building id (as text) -> units waiting
                for (auto it = t.recruitPool.cbegin(); it != t.recruitPool.cend(); ++it) pool.insert(QString::number(it.key().index()), it.value());
                o.insert("recruitPool", pool);
            }
            {
                QJsonObject imported;  // unit id (as text) -> count
                for (auto it = t.importedUnits.cbegin(); it != t.importedUnits.cend(); ++it) imported.insert(QString::number(it.key().index()), it.value());
                o.insert("importedUnits", imported);
            }
            {
                QJsonArray offers;  // hero template ids
                for (const HeroTemplateId h : t.tavernOffers) offers.append(h.index());
                o.insert("tavernOffers", offers);
            }
            if (t.owner == viewer) {
                // What the player can do here, worked out by the server's rules.
                QJsonArray buildOptions;
                for (const BuildingDef& b : content.buildings()) {
                    if (!b.fitsRace(t.race) || t.buildings.contains(b.id)) continue;
                    QJsonObject bo;
                    bo.insert("id", b.id.index());
                    bo.insert("why", whyCannotBuild(state, content, t.id, b.id, viewer));  // empty = can build now
                    buildOptions.append(bo);
                }
                o.insert("buildOptions", buildOptions);
                QJsonArray recruitOpts;
                for (const RecruitOption& r : recruitOptions(t, content)) {
                    QJsonObject ro;
                    ro.insert("unit", r.unitId.index());
                    ro.insert("available", r.available);
                    recruitOpts.append(ro);
                }
                o.insert("recruitOptions", recruitOpts);
            }
        }
        towns.append(o);
    }
    view.insert("towns", towns);

    QJsonArray mines;
    for (const Mine& m : state.mines()) {
        if (!ownTeam(m.owner) && !knows(m.pos)) continue;
        QJsonObject o;
        o.insert("id", id(m.id.value()));
        o.insert("resource", m.resource.index());
        o.insert("dailyAmount", m.dailyAmount);
        o.insert("owner", id(m.owner.value()));
        o.insert("x", m.pos.x);
        o.insert("y", m.pos.y);
        mines.append(o);
    }
    view.insert("mines", mines);

    QJsonArray dwellings;
    for (const MapDwelling& d : state.mapDwellings()) {
        const bool own = ownTeam(d.owner);
        if (!own && !knows(d.pos)) continue;
        QJsonObject o;
        o.insert("id", id(d.id.value()));
        o.insert("unit", d.unitId.index());
        o.insert("owner", id(d.owner.value()));
        o.insert("x", d.pos.x);
        o.insert("y", d.pos.y);
        if (own) o.insert("available", d.available);
        dwellings.append(o);
    }
    view.insert("mapDwellings", dwellings);

    QJsonArray objects;
    for (const MapObject& m : state.mapObjects()) {
        if (!knows(m.pos)) continue;
        QJsonObject o;
        o.insert("id", id(m.id.value()));
        o.insert("kind", m.kind == MapObject::Kind::Artifact ? "artifact" : m.kind == MapObject::Kind::Site ? "site" : "treasure");
        o.insert("x", m.pos.x);
        o.insert("y", m.pos.y);
        if (m.artifact.isValid()) o.insert("artifact", m.artifact.index());
        if (m.kind == MapObject::Kind::Site) {
            o.insert("site", m.site);
            // Which of your heroes would still get something here.
            QJsonArray fresh;
            for (const Hero& h : state.heroes())
                if (h.owner == viewer && siteGivesSomething(state, m, h)) fresh.append(static_cast<int>(h.id.value()));
            o.insert("giftFor", fresh);
            if (m.site == QLatin1String("quest_hut")) {
                o.insert("quest", describeQuest(m.quest, content));
                if (m.quest.done) o.insert("questDone", true);
            }
        }
        if (!m.resources.isEmpty()) o.insert("resources", amountsToJson(m.resources));
        objects.append(o);
    }
    view.insert("mapObjects", objects);
    return view;
}

}  // namespace kot::core
