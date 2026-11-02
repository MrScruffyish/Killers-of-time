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
        core::Pos target{start.x + 1, start.y};
        if (!reg.terrainType(session->state().map().terrainAt(target))->walkable) target = {start.x - 1, start.y};
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
