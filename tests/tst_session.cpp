#include <QJsonArray>
#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "net/Protocol.h"
#include "server/GameSession.h"

using namespace kot;
using namespace kot::server;
using net::Message;

class TstSession : public QObject {
    Q_OBJECT
    core::ContentRegistry reg;
    std::unique_ptr<GameSession> session;
    core::PlayerId pA, pB, pBot;

    static Message cmd(const QString& type, const QJsonObject& body = {}, int seq = 1) {
        Message m;
        m.type = type;
        m.body = body;
        m.seq = seq;
        return m;
    }
    static net::SlotInfo human(int index, int client, const QString& name, int team, const QString& color) {
        net::SlotInfo s;
        s.index = index; s.kind = net::SlotKind::Human; s.clientId = client; s.name = name;
        s.race = "humans"; s.team = team; s.color = color; s.ready = true;
        return s;
    }

    CommandResult doIt(int client, const QString& type, const QJsonObject& body = {}) {
        return session->handle(client, cmd(type, body));
    }
    const core::Town* townOf(core::PlayerId p) {
        for (const core::Town& t : session->state().towns()) if (t.owner == p) return &t;
        return nullptr;
    }
    const core::Hero* heroOf(core::PlayerId p) {
        for (const core::Hero& h : session->state().heroes()) if (h.owner == p) return &h;
        return nullptr;
    }
    int id(auto v) { return int(v.value()); }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void init() {
        net::LobbyState lobby;
        lobby.settings.seed = 5;
        lobby.slotList.append(human(0, 1, "Ann", 1, "#d62728"));
        lobby.slotList.append(human(1, 2, "Bob", 2, "#1f77b4"));
        net::SlotInfo ai;
        ai.index = 2; ai.kind = net::SlotKind::Ai; ai.name = "Bot"; ai.race = "humans";
        ai.team = 2; ai.color = "#2ca02c"; ai.ready = true; ai.aiProfile = "passive"; ai.difficulty = "normal";
        lobby.slotList.append(ai);
        net::SlotInfo open;
        open.index = 3;
        lobby.slotList.append(open);

        session = std::make_unique<GameSession>(reg);
        QString error;
        QVERIFY2(session->start(lobby, &error), qPrintable(error));
        pA = session->playerOfClient(1);
        pB = session->playerOfClient(2);
        for (const core::Player& p : session->state().players())
            if (p.controller == core::Player::Controller::Ai) pBot = p.id;
    }

    void hotSeatPlayersShareOneClient() {
        // Ann and Cid at one computer (client 1), against a passive bot.
        net::LobbyState lobby;
        lobby.settings.seed = 9;
        lobby.slotList.append(human(0, 1, "Ann", 1, "#d62728"));
        net::SlotInfo cid = human(1, 1, "Cid", 2, "#1f77b4");
        cid.kind = net::SlotKind::HotSeat;
        lobby.slotList.append(cid);
        GameSession hs(reg);
        QString error;
        QVERIFY2(hs.start(lobby, &error), qPrintable(error));
        QCOMPARE(hs.playersOfClient(1).size(), 2);
        const core::PlayerId ann = hs.playersOfClient(1)[0], c = hs.playersOfClient(1)[1];
        QCOMPARE(hs.state().player(c)->controller, core::Player::Controller::Human);
        // Ann's turn: the client acts for Ann; after her End turn it acts for Cid.
        QCOMPARE(hs.playerOfClient(1), ann);
        Message end;
        end.type = net::msg::endTurn;
        QVERIFY(hs.handle(1, end).ok);
        QCOMPARE(hs.state().currentPlayer(), c);
        QCOMPARE(hs.playerOfClient(1), c);
        QCOMPARE(hs.viewFor(hs.playerOfClient(1))["you"].toInt(), id(c));
        QVERIFY(hs.handle(1, end).ok);  // Cid ends his turn too: day 2, Ann again
        QCOMPARE(hs.state().currentPlayer(), ann);
        QCOMPARE(hs.state().day(), 2);
    }

    void aRevealedMapIsKnownFromTheStart() {
        net::LobbyState lobby;
        lobby.settings.seed = 7;
        lobby.settings.hiddenMap = false;
        lobby.slotList.append(human(0, 1, "Ann", 1, "#d62728"));
        lobby.slotList.append(human(1, 2, "Bob", 2, "#1f77b4"));
        GameSession s(reg);
        QString error;
        QVERIFY2(s.start(lobby, &error), qPrintable(error));
        const QJsonObject view = s.viewFor(s.playerOfClient(1));
        for (const QJsonValue& t : view["map"].toObject()["tiles"].toArray()) QVERIFY(t.toInt() >= 0);  // nothing black
        int towns = 0;
        for (const QJsonValue& t : view["towns"].toArray()) towns += t.toObject()["owner"].toInt() == int(s.playerOfClient(2).value());
        QCOMPARE(towns, 1);  // Bob's capital is known
        QVERIFY(s.state().fogOfWar());  // fog stays on: his armies far away are not shown
    }

    void startCreatesTheGame() {
        QVERIFY(session->active());
        QCOMPARE(session->clients().size(), 2);
        QCOMPARE(session->state().players().size(), 3);
        QVERIFY(pA.isValid() && pB.isValid() && pBot.isValid());
        QCOMPARE(session->state().currentPlayer(), pA);
        QCOMPARE(session->youInfo(pA)["playerId"].toInt(), id(pA));
    }

    void gameEndsWhenOneTeamIsLeft() {
        core::GameState& gs = session->state();
        for (const core::Hero& h : QList<core::Hero>(gs.heroes().values()))
            if (h.owner != pA) { gs.armies().remove(h.army); gs.heroes().remove(h.id); }
        for (core::Town& t : gs.towns())
            if (t.owner != pA) t.owner = core::PlayerId();
        const QJsonObject view = session->viewFor(pA);
        QVERIFY(view["gameOver"].toBool());
        QCOMPARE(view["winnerTeam"].toInt(), id(gs.teamOf(pA)));
        for (const QJsonValue& v : view["players"].toArray())
            QCOMPARE(v.toObject()["eliminated"].toBool(), v.toObject()["id"].toInt() != id(pA));
        QCOMPARE(doIt(1, net::msg::endTurn).code, QString("game_over"));
    }

    void humanAttackFightsAnInteractiveBattle() {
        core::GameState& gs = session->state();
        const core::Hero* h = heroOf(pA);
        core::Army* army = gs.army(h->army);
        const core::Pos at = army->pos;
        // Monsters right next to Ann's hero (on a free land tile).
        core::Pos spot{-1, -1};
        for (int dx = -1; dx <= 1 && spot.x < 0; ++dx)
            for (int dy = -1; dy <= 1 && spot.x < 0; ++dy) {
                const core::Pos p{at.x + dx, at.y + dy};
                bool free = (dx || dy) && gs.map().inBounds(p) && reg.terrain().value(gs.map().terrainAt(p)).walkable;
                for (const core::Army& a : gs.armies()) free = free && a.pos != p;
                free = free && !gs.townCovering(p);
                for (const core::Mine& m : gs.mines()) free = free && m.pos != p;
                for (const core::MapDwelling& d : gs.mapDwellings()) free = free && d.pos != p;
                if (free) spot = p;
            }
        QVERIFY(spot.x >= 0);
        const core::ArmyId monsters = gs.addArmy(core::PlayerId(), spot);
        gs.army(monsters)->addUnits("militia", 3, 6);

        CommandResult r = doIt(1, net::msg::moveHero, {{"hero", id(h->id)}, {"x", spot.x}, {"y", spot.y}});
        QVERIFY2(r.ok, qPrintable(r.message));
        // 3 militia are far weaker: they offer to join first. Ann wants a fight.
        QVERIFY(r.info.contains("joinOffer"));
        QCOMPARE(r.info["joinOffer"].toObject()["units"].toArray().first().toObject()["count"].toInt(), 3);
        r = doIt(1, net::msg::answerJoin, {{"answer", "fight"}});
        QVERIFY2(r.ok, qPrintable(r.message));
        QVERIFY(r.info["battleStarted"].toBool());
        QVERIFY(session->battleRunning());
        const QJsonObject view = session->viewFor(pA);
        QVERIFY(view.contains("battle"));
        QCOMPARE(view["battle"].toObject()["yourSide"].toInt(), 0);
        QVERIFY(session->viewFor(pB)["battleInProgress"].toBool());

        QCOMPARE(doIt(1, net::msg::endTurn).code, QString("battle_in_progress"));
        QCOMPARE(doIt(2, net::msg::battleAction, {{"action", "defend"}}).code, QString("not_your_turn"));
        QCOMPARE(doIt(1, net::msg::battleAction, {{"action", "fly"}}).code, QString("bad_request"));
        // Ann's hero casts the first spell he can (every class starts with one).
        const QJsonObject b0 = session->viewFor(pA)["battle"].toObject();
        const QJsonObject heroInfo = b0["hero"].toObject();
        const int spBefore = heroInfo["spellPoints"].toInt();
        QJsonObject spell;
        for (const QJsonValue& v : heroInfo["spells"].toArray()) if (v.toObject()["castable"].toBool()) { spell = v.toObject(); break; }
        QVERIFY(!spell.isEmpty());
        int target = 0;
        for (const QJsonValue& u : b0["units"].toArray())
            if ((u.toObject()["side"].toInt() == 1) == (spell["target"].toString() == "enemy")) target = u.toObject()["id"].toInt();
        r = doIt(1, net::msg::battleAction, {{"action", "cast"}, {"spell", spell["id"]}, {"target", target}});
        QVERIFY2(r.ok, qPrintable(r.message));
        // Your stack's events come first, then what the computer's stacks did.
        QCOMPARE(r.info["events"].toArray().first().toObject()["type"].toString(), QString("cast"));
        if (session->battleRunning()) {
            const QJsonObject heroAfter = session->viewFor(pA)["battle"].toObject()["hero"].toObject();
            QCOMPARE(heroAfter["spellPoints"].toInt(), spBefore - spell["cost"].toInt());
            QVERIFY(heroAfter["hasCast"].toBool());
        }
        if (session->battleRunning()) {  // the spell may already have won it
            r = doIt(1, net::msg::battleAction, {{"action", "defend"}});
            QVERIFY2(r.ok, qPrintable(r.message));
        }
        if (session->battleRunning()) {  // one move by the battle AI (the client's auto-play)
            r = doIt(1, net::msg::battleAction, {{"action", "ai"}});
            QVERIFY2(r.ok, qPrintable(r.message));
            QVERIFY(!r.info["events"].toArray().isEmpty() || r.info["battleOver"].toBool());
        }
        if (session->battleRunning()) {
            r = doIt(1, net::msg::battleAction, {{"action", "auto"}});
            QVERIFY2(r.ok, qPrintable(r.message));
        }
        QVERIFY(!session->battleRunning());
        QVERIFY(r.info["battleOver"].toBool());
        QCOMPARE(r.info["move"].toObject()["battles"].toArray().size(), 1);
        QVERIFY(!gs.army(monsters));  // 3 militia stand no chance
        QVERIFY(session->viewFor(pA).contains("lastBattle"));
        QVERIFY(doIt(1, net::msg::endTurn).ok);  // the game goes on
    }

    void weakMonstersOfferToJoin() {
        core::GameState& gs = session->state();
        const core::Hero* h = heroOf(pA);
        const core::Pos at = gs.army(h->army)->pos;
        core::Pos spot{-1, -1};
        for (const core::Pos p : {core::Pos{at.x - 1, at.y + 1}, core::Pos{at.x - 1, at.y}, core::Pos{at.x, at.y + 1}}) {
            bool free = spot.x < 0 && gs.map().inBounds(p) && reg.terrainType(gs.map().terrainAt(p))->walkable && !gs.townCovering(p);
            for (const core::Army& a : gs.armies()) free = free && a.pos != p;
            for (const core::Mine& m : gs.mines()) free = free && m.pos != p;
            for (const core::MapDwelling& d : gs.mapDwellings()) free = free && d.pos != p;
            if (free) spot = p;
        }
        QVERIFY(spot.x >= 0);
        const core::ArmyId monsters = gs.addArmy(core::PlayerId(), spot);
        gs.army(monsters)->addUnits("militia", 2, 6);
        const int before = gs.army(h->army)->totalUnits();
        CommandResult r = doIt(1, net::msg::moveHero, {{"hero", id(h->id)}, {"x", spot.x}, {"y", spot.y}});
        QVERIFY2(r.ok, qPrintable(r.message));
        QVERIFY(r.info.contains("joinOffer"));
        QVERIFY(!session->battleRunning());
        // Leave them: nothing happens. Another command also drops the offer.
        QVERIFY(doIt(1, net::msg::answerJoin, {{"answer", "leave"}}).ok);
        QVERIFY(gs.army(monsters));
        QCOMPARE(doIt(1, net::msg::answerJoin, {{"answer", "accept"}}).code, QString("failed"));  // no offer now
        // Ask again (from where he stands) and welcome them.
        r = doIt(1, net::msg::moveHero, {{"hero", id(h->id)}, {"x", spot.x}, {"y", spot.y}});
        QVERIFY(r.info.contains("joinOffer"));
        const int cost = r.info["joinOffer"].toObject()["cost"].toInt();
        QCOMPARE(cost, 2 * reg.unit("militia")->cost.value("gold") * reg.balance().battle.joinCostPercent / 100);
        const int gold = gs.player(pA)->resources.value("gold");
        r = doIt(1, net::msg::answerJoin, {{"answer", "accept"}});
        QVERIFY2(r.ok, qPrintable(r.message));
        QVERIFY(!gs.army(monsters));
        QCOMPARE(gs.army(h->army)->totalUnits(), before + 2);
        QCOMPARE(gs.player(pA)->resources.value("gold"), gold - cost);  // they were paid
    }

    void bothHumansPlayTheirOwnStacks() {
        core::GameState& gs = session->state();
        const core::Hero* ha = heroOf(pA);
        const core::Hero* hb = heroOf(pB);
        const core::Pos at = gs.army(ha->army)->pos;
        core::Pos spot{-1, -1};
        for (const core::Pos p : {core::Pos{at.x + 1, at.y}, core::Pos{at.x - 1, at.y}, core::Pos{at.x, at.y + 1},
                                  core::Pos{at.x, at.y - 1}}) {
            bool free = spot.x < 0 && gs.map().inBounds(p) && reg.terrainType(gs.map().terrainAt(p))->walkable;
            for (const core::Army& a : gs.armies()) free = free && a.pos != p;
            free = free && !gs.townCovering(p);
            for (const core::Mine& m : gs.mines()) free = free && m.pos != p;
            for (const core::MapDwelling& d : gs.mapDwellings()) free = free && d.pos != p;
            if (free) spot = p;
        }
        QVERIFY(spot.x >= 0);
        gs.army(hb->army)->pos = spot;  // Bob's hero walked up to Ann

        QVERIFY(doIt(1, net::msg::moveHero, {{"hero", id(ha->id)}, {"x", spot.x}, {"y", spot.y}}).info["battleStarted"].toBool());
        QCOMPARE(session->viewFor(pB)["battle"].toObject()["yourSide"].toInt(), 1);
        int annActs = 0, bobActs = 0;
        for (int i = 0; i < 400 && session->battleRunning(); ++i) {
            const QJsonObject battle = session->viewFor(pA)["battle"].toObject();
            int activeSide = -1;
            for (const QJsonValue& u : battle["units"].toArray())
                if (u.toObject()["id"].toInt() == battle["active"].toInt()) activeSide = u.toObject()["side"].toInt();
            const int actor = activeSide == 0 ? 1 : 2, other = activeSide == 0 ? 2 : 1;
            QCOMPARE(doIt(other, net::msg::battleAction, {{"action", "defend"}}).code, QString("not_your_turn"));
            // Attack the first reachable enemy, else defend.
            const QJsonArray targets = battle["attackable"].toArray();
            const QJsonObject body = targets.isEmpty() ? QJsonObject{{"action", "defend"}}
                                                       : QJsonObject{{"action", "attack"}, {"target", targets[0].toInt()}};
            QVERIFY(doIt(actor, net::msg::battleAction, body).ok);
            ++(activeSide == 0 ? annActs : bobActs);
        }
        QVERIFY(!session->battleRunning());
        QVERIFY(annActs > 0 && bobActs > 0);
        QVERIFY(session->viewFor(pB).contains("lastBattle"));
        QCOMPARE(session->viewFor(pB)["lastBattle"], session->viewFor(pA)["lastBattle"]);
    }

    void startFailsForAnInvalidLobby() {
        net::LobbyState bad;
        bad.slotList.append(human(0, 1, "Solo", 1, "#d62728"));
        GameSession s(reg);
        QString error;
        QVERIFY(!s.start(bad, &error));
        QVERIFY(error.contains("two players"));
        QVERIFY(!s.active());
        QCOMPARE(s.handle(1, cmd(net::msg::endTurn)).code, QString("no_game"));
    }

    void onlyTheCurrentPlayerMayAct() {
        QCOMPARE(doIt(2, net::msg::endTurn).code, QString("not_your_turn"));
        QCOMPARE(doIt(2, net::msg::trade, {{"give", "gold"}, {"amount", 1000}, {"get", "wood"}}).code, QString("not_your_turn"));
        QCOMPARE(doIt(99, net::msg::endTurn).code, QString("not_a_player"));
    }

    void turnsPassAndTheBotIsSkipped() {
        QVERIFY(doIt(1, net::msg::endTurn).ok);
        QCOMPARE(session->state().currentPlayer(), pB);
        QCOMPARE(session->state().day(), 1);
        const CommandResult r = doIt(2, net::msg::endTurn);  // the bot's turn passes by itself
        QVERIFY(r.ok);
        QCOMPARE(session->state().currentPlayer(), pA);
        QCOMPARE(session->state().day(), 2);
    }

    void movingOwnHero() {
        const core::Hero* h = heroOf(pA);
        const core::Pos start = session->state().army(h->army)->pos;
        // Not right or up: those are the town's walls.
        core::Pos target{start.x - 1, start.y};
        if (!reg.terrainType(session->state().map().terrainAt(target))->walkable) target = {start.x, start.y + 1};
        const CommandResult r = doIt(1, net::msg::moveHero, {{"hero", id(h->id)}, {"x", target.x}, {"y", target.y}});
        QVERIFY2(r.ok, qPrintable(r.message));
        QVERIFY(r.info["arrived"].toBool());
        QCOMPARE(session->state().army(h->army)->pos, target);
    }

    void cannotMoveSomeoneElsesHero() {
        const core::Hero* other = heroOf(pB);
        const CommandResult r = doIt(1, net::msg::moveHero, {{"hero", id(other->id)}, {"x", 1}, {"y", 1}});
        QCOMPARE(r.code, QString("failed"));
        QVERIFY(r.message.contains("not your hero"));
    }

    void badRequestsAreRejectedWithReasons() {
        const CommandResult r = doIt(1, net::msg::moveHero, {{"hero", 1}});
        QCOMPARE(r.code, QString("bad_request"));
        QVERIFY(r.message.contains("x"));
        QCOMPARE(doIt(1, net::msg::build, {{"town", 1}}).code, QString("bad_request"));
        QCOMPARE(doIt(1, "dance").code, QString("unknown_message"));
    }

    void buildingAndRecruiting() {
        const core::Town* town = townOf(pA);
        QVERIFY(doIt(1, net::msg::build, {{"town", id(town->id)}, {"building", "barracks"}}).ok);
        QVERIFY(town->buildings.contains("barracks"));
        QVERIFY(doIt(1, net::msg::recruit, {{"town", id(town->id)}, {"unit", "militia"}, {"count", 5}}).ok);
        QCOMPARE(session->state().army(town->garrison)->totalUnits(), 5);
        const CommandResult again = doIt(1, net::msg::build, {{"town", id(town->id)}, {"building", "village_hall"}});
        QVERIFY(again.message.contains("already built here today"));
    }

    void cannotBuildInSomeoneElsesTown() {
        const core::Town* theirs = townOf(pB);
        const CommandResult r = doIt(1, net::msg::build, {{"town", id(theirs->id)}, {"building", "barracks"}});
        QVERIFY(r.message.contains("not your town"));
    }

    void tradeAndBuildWithTrade() {
        QVERIFY(doIt(1, net::msg::trade, {{"give", "gold"}, {"amount", 1000}, {"get", "wood"}}).ok);
        QCOMPARE(session->state().player(pA)->resources.value("wood"), 28);
        session->state().player(pA)->resources["wood"] = 0;
        const core::Town* town = townOf(pA);
        const CommandResult r = doIt(1, net::msg::build, {{"town", id(town->id)}, {"building", "barracks"}, {"useTrade", true}});
        QVERIFY2(r.ok, qPrintable(r.message));
        QCOMPARE(r.info["trades"].toArray().size(), 1);
    }

    void oneClickActions() {
        const core::Town* town = townOf(pA);
        const CommandResult built = doIt(1, net::msg::autoBuild, {});
        QVERIFY(built.ok);
        QCOMPARE(built.info["built"].toArray().size(), 1);
        QVERIFY(town->buildings.contains("barracks"));

        const CommandResult bought = doIt(1, net::msg::buyAllUnits, {{"strategy", "most"}});
        QVERIFY(bought.ok);
        QCOMPARE(bought.info["unitsBought"].toInt(), 12);

        session->state().player(pA)->resources["gold"] = 10000;
        session->state().towns()[town->id].lastBuildDay = 0;
        const CommandResult both = doIt(1, net::msg::buildThenBuy, {{"town", id(town->id)}});
        QVERIFY(both.ok);
        QVERIFY(both.info.contains("build"));
        QVERIFY(both.info.contains("buy"));

        QVERIFY(doIt(1, net::msg::sendDwellingUnits, {}).ok);
        QVERIFY(doIt(1, net::msg::sendDwellingUnits, {{"town", id(town->id)}}).ok);
    }

    void pickTraitNeedsAPendingPick() {
        const CommandResult r = doIt(1, net::msg::pickTrait, {{"hero", id(heroOf(pA)->id)}, {"trait", "attack"}});
        QCOMPARE(r.code, QString("failed"));
        QVERIFY(r.message.contains("no trait pick"));
        session->state().hero(heroOf(pA)->id)->pendingTraitPicks = 1;
        QVERIFY(doIt(1, net::msg::pickTrait, {{"hero", id(heroOf(pA)->id)}, {"trait", "attack"}}).ok);
    }

    void moveUnitsBetweenOwnArmies() {
        const core::Hero* h = heroOf(pA);
        const core::Town* town = townOf(pA);  // the hero starts in his town
        const CommandResult r = doIt(1, net::msg::moveUnits,
            {{"from", id(h->army)}, {"to", id(town->garrison)}, {"stack", 0}, {"count", 4}});
        QVERIFY2(r.ok, qPrintable(r.message));
        QCOMPARE(session->state().army(town->garrison)->totalUnits(), 4);
        const core::Army* enemy = session->state().army(townOf(pB)->garrison);
        QVERIFY(doIt(1, net::msg::moveUnits, {{"from", id(enemy->id)}, {"to", id(h->army)}, {"stack", 0}, {"count", 1}})
                    .message.contains("not your army"));
    }

    void playerViewsHideTheEnemy() {
        const QJsonObject view = session->viewFor(pA);
        QCOMPARE(view["heroes"].toArray().size(), 1);
        QCOMPARE(view["towns"].toArray().size(), 1);
        QCOMPARE(view["you"].toInt(), id(pA));
    }

    void leavingHandsThePlayerToThePassiveAi() {
        session->clientLeft(1);  // it is his turn: the turn passes at once
        QCOMPARE(session->state().currentPlayer(), pB);
        QCOMPARE(session->clients().size(), 1);
        QCOMPARE(doIt(1, net::msg::endTurn).code, QString("not_a_player"));
        // Bob ends his turn: everyone else is automatic, so the day advances back to Bob.
        QVERIFY(doIt(2, net::msg::endTurn).ok);
        QCOMPARE(session->state().currentPlayer(), pB);
        QCOMPARE(session->state().day(), 2);
    }

    void everyoneLeavingDoesNotHang() {
        session->clientLeft(1);
        session->clientLeft(2);
        QVERIFY(session->clients().isEmpty());  // and the call returned
    }
};

QTEST_APPLESS_MAIN(TstSession)
#include "tst_session.moc"
