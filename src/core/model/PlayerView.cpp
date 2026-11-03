#include "core/model/PlayerView.h"

#include "core/rules/Veterancy.h"
#include "core/rules/Victory.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>

#include "core/content/ContentRegistry.h"
#include "core/rules/TownRules.h"
#include "core/rules/Vision.h"

namespace kot::core {

namespace {

int id(quint32 value) { return static_cast<int>(value); }

QJsonObject intMap(const QMap<QString, int>& m) {
    QJsonObject o;
    for (auto it = m.begin(); it != m.end(); ++it) o.insert(it.key(), it.value());
    return o;
}

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

    QJsonObject view;
    view.insert("day", state.day());
    view.insert("difficulty", state.difficulty());
    if (const WeekEventDef* e = content.weekEvent(state.weekEvent()))
        view.insert("weekEvent", QJsonObject{{"id", e->id}, {"name", e->name}, {"description", e->description}});
    view.insert("currentPlayer", id(state.currentPlayer().value()));
    view.insert("you", id(viewer.value()));
    const Player* me = state.player(viewer);
    view.insert("resources", intMap(me ? me->resources : ResourceAmounts()));

    QJsonArray players;
    for (const Player& p : state.players()) {
        QJsonObject o;
        o.insert("id", id(p.id.value()));
        o.insert("name", p.name);
        o.insert("team", id(p.team.value()));
        o.insert("color", p.color);
        o.insert("race", p.race);
        o.insert("isAi", p.controller == Player::Controller::Ai);
        o.insert("eliminated", isEliminated(state, p.id));
        players.append(o);
    }
    view.insert("players", players);
    const GameOutcome outcome = gameOutcome(state);
    view.insert("gameOver", outcome.over);
    view.insert("victory", describeVictory(state));
    if (outcome.over) view.insert("winnerTeam", id(outcome.winner.value()));

    // Map: palette + indices, -1 where the team has not been.
    {
        QJsonArray palette, tiles;
        QHash<QString, int> index;
        for (int y = 0; y < map.height(); ++y) {
            for (int x = 0; x < w; ++x) {
                const qsizetype i = static_cast<qsizetype>(y) * w + x;
                const bool known = (i < visible.size() && visible[i]) || (i < explored.size() && explored[i]);
                if (!known) {
                    tiles.append(-1);
                    continue;
                }
                const QString& t = map.terrainAt({x, y});
                auto it = index.find(t);
                if (it == index.end()) {
                    it = index.insert(t, static_cast<int>(palette.size()));
                    palette.append(t);
                }
                tiles.append(it.value());
            }
        }
        QJsonObject m;
        m.insert("width", w);
        m.insert("height", map.height());
        m.insert("terrainPalette", palette);
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
        o.insert("class", h.classId);
        o.insert("owner", id(h.owner.value()));
        o.insert("level", h.level);
        o.insert("x", army->pos.x);
        o.insert("y", army->pos.y);
        o.insert("army", id(h.army.value()));
        if (own) {
            o.insert("experience", h.experience);
            o.insert("traits", intMap(h.traits));
            o.insert("spellPoints", h.spellPoints);
            o.insert("pendingTraitPicks", h.pendingTraitPicks);
            o.insert("movementPoints", h.movementPoints);
            o.insert("template", h.templateId);
            o.insert("spells", QJsonArray::fromStringList(h.spells));
            o.insert("artifacts", QJsonArray::fromStringList(h.artifacts));
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
            so.insert("unit", s.unitTypeId());
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
        if (!own && !sees(t.pos)) continue;
        QJsonObject o;
        o.insert("id", id(t.id.value()));
        o.insert("name", t.name);
        o.insert("race", t.race);
        o.insert("owner", id(t.owner.value()));
        o.insert("x", t.pos.x);
        o.insert("y", t.pos.y);
        if (own) {
            o.insert("garrison", id(t.garrison.value()));
            o.insert("buildings", QJsonArray::fromStringList(t.buildings));
            o.insert("lastBuildDay", t.lastBuildDay);
            o.insert("recruitPool", intMap(t.recruitPool));
            o.insert("importedUnits", intMap(t.importedUnits));
            o.insert("tavernOffers", QJsonArray::fromStringList(t.tavernOffers));
            if (t.owner == viewer) {
                // What the player can do here, worked out by the server's rules.
                QJsonArray buildOptions;
                for (const BuildingDef& b : content.buildings()) {
                    if (!b.fitsRace(t.race) || t.buildings.contains(b.id)) continue;
                    QJsonObject bo;
                    bo.insert("id", b.id);
                    bo.insert("why", whyCannotBuild(state, content, t.id, b.id, viewer));  // empty = can build now
                    buildOptions.append(bo);
                }
                o.insert("buildOptions", buildOptions);
                QJsonArray recruitOpts;
                for (const RecruitOption& r : recruitOptions(t, content)) {
                    QJsonObject ro;
                    ro.insert("unit", r.unitId);
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
        if (!ownTeam(m.owner) && !sees(m.pos)) continue;
        QJsonObject o;
        o.insert("id", id(m.id.value()));
        o.insert("resource", m.resourceId);
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
        if (!own && !sees(d.pos)) continue;
        QJsonObject o;
        o.insert("id", id(d.id.value()));
        o.insert("unit", d.unitId);
        o.insert("owner", id(d.owner.value()));
        o.insert("x", d.pos.x);
        o.insert("y", d.pos.y);
        if (own) o.insert("available", d.available);
        dwellings.append(o);
    }
    view.insert("mapDwellings", dwellings);

    QJsonArray objects;
    for (const MapObject& m : state.mapObjects()) {
        if (!sees(m.pos)) continue;
        QJsonObject o;
        o.insert("id", id(m.id.value()));
        o.insert("kind", m.kind == MapObject::Kind::Artifact ? "artifact" : "treasure");
        o.insert("x", m.pos.x);
        o.insert("y", m.pos.y);
        if (!m.artifact.isEmpty()) o.insert("artifact", m.artifact);
        if (!m.resources.isEmpty()) o.insert("resources", intMap(m.resources));
        objects.append(o);
    }
    view.insert("mapObjects", objects);
    return view;
}

}  // namespace kot::core
