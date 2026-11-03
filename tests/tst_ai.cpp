#include <QtTest>

#include "ai/AiFactory.h"
#include "ai/AiView.h"
#include "core/content/ContentRegistry.h"
#include "core/rules/Victory.h"
#include "net/Protocol.h"
#include "server/GameSession.h"

using namespace kot;
using namespace kot::server;

// A human who only ends turns plays against two simple AIs for a few weeks.
class TstAi : public QObject {
    Q_OBJECT
    core::ContentRegistry reg;

    std::unique_ptr<GameSession> startGame(quint64 seed, const QStringList& aiRaces = {"humans", "humans"},
                                           const QStringList& profiles = {"simple", "simple"}) {
        net::LobbyState lobby;
        lobby.settings.seed = seed;
        net::SlotInfo human;
        human.index = 0; human.kind = net::SlotKind::Human; human.clientId = 1; human.name = "Ann";
        human.race = "humans"; human.team = 1; human.color = "#d62728"; human.ready = true;
        lobby.slotList.append(human);
        for (int i = 1; i <= 2; ++i) {
            net::SlotInfo ai;
            ai.index = i; ai.kind = net::SlotKind::Ai; ai.name = QString("Bot%1").arg(i); ai.race = aiRaces[i - 1];
            ai.team = 1 + i; ai.color = i == 1 ? "#2ca02c" : "#1f77b4"; ai.ready = true; ai.aiProfile = profiles[i - 1];
            lobby.slotList.append(ai);
        }
        auto session = std::make_unique<GameSession>(reg);
        QString error;
        if (!session->start(lobby, &error)) qWarning() << error;
        return session;
    }
    // The human only ends turns; when an AI attacks him he lets the computer fight.
    static void playDays(GameSession& s, int days) {
        const net::Message end{net::msg::endTurn, {}, 1};
        const net::Message autoFight{net::msg::battleAction, {{"action", "auto"}}, 1};
        for (int i = 0; i < days; ++i) {
            for (int guard = 0; guard < 20 && s.battleRunning(); ++guard) s.handle(1, autoFight);
            s.handle(1, end);
            for (int guard = 0; guard < 20 && s.battleRunning(); ++guard) s.handle(1, autoFight);
        }
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void profiles() {
        QCOMPARE(ai::aiProfileNames().first(), QString("simple"));
        QCOMPARE(ai::createAi("simple", 1)->profileName(), QString("simple"));
        QCOMPARE(ai::createAi("passive", 1)->profileName(), QString("passive"));
        QCOMPARE(ai::createAi("nonsense", 1)->profileName(), QString("simple"));
        for (const QString& p : ai::aiProfileNames()) QCOMPARE(ai::createAi(p, 1)->profileName(), p);
    }

    void simpleAiDevelopsAndExplores() {
        auto s = startGame(11);
        QVERIFY(s->active());
        QList<core::PlayerId> bots;
        for (const core::Player& p : s->state().players())
            if (p.controller == core::Player::Controller::Ai) bots.append(p.id);
        QCOMPARE(bots.size(), 2);

        const auto explored = [&](core::PlayerId p) {
            const QByteArray& e = s->state().exploredMap(s->state().teamOf(p));
            return static_cast<int>(e.count('\1'));
        };
        QMap<core::PlayerId, int> exploredAtStart;
        for (core::PlayerId b : bots) exploredAtStart[b] = explored(b);

        playDays(*s, 21);
        // The idle human may well have been conquered by now; if not, it is his turn again.
        const core::PlayerId ann = s->playerOfClient(1);
        if (!core::isEliminated(s->state(), ann)) {
            QCOMPARE(s->state().day(), 22);
            QCOMPARE(s->state().currentPlayer(), ann);
        } else {
            QVERIFY(s->state().day() >= 8);
        }

        int captured = 0, buildings = 0;
        for (core::PlayerId b : bots) {
            QVERIFY2(explored(b) > exploredAtStart[b], "the AI should explore");
            for (const core::Mine& m : s->state().mines()) captured += (m.owner == b);
            for (const core::MapDwelling& d : s->state().mapDwellings()) captured += (d.owner == b);
            for (const core::Town& t : s->state().towns()) buildings += (t.owner == b) ? int(t.buildings.size()) : 0;
        }
        QVERIFY2(captured > 0, "the AIs should capture something in three weeks");
        QVERIFY2(buildings > 2, "the AIs should build");
    }

    void otherRacesPlayToo_data() {
        QTest::addColumn<QStringList>("races");
        QTest::newRow("undead, sylvan") << QStringList{"undead", "sylvan"};
        QTest::newRow("ashborn, tidefolk") << QStringList{"ashborn", "tidefolk"};
    }
    void otherRacesPlayToo() {
        QFETCH(QStringList, races);
        auto s = startGame(21, races);
        QVERIFY(s->active());
        playDays(*s, 14);
        QVERIFY(s->state().day() >= 15);  // an attack on the idle human can take an extra round
        for (const core::Player& p : s->state().players()) {
            if (p.controller != core::Player::Controller::Ai) continue;
            int buildings = 0;
            for (const core::Town& t : s->state().towns())
                if (t.owner == p.id) buildings += int(t.buildings.size());
            QVERIFY2(buildings >= 2, qPrintable(p.race));
        }
    }

    void personalitiesPlay() {
        auto s = startGame(8, {"undead", "humans"}, {"aggressive", "economic"});
        playDays(*s, 10);
        QVERIFY(s->state().day() >= 8);
        for (const core::Player& p : s->state().players()) {
            if (p.controller != core::Player::Controller::Ai) continue;
            QVERIFY(p.aiProfile == "aggressive" || p.aiProfile == "economic");
            int buildings = 0;
            for (const core::Town& t : s->state().towns()) buildings += t.owner == p.id ? int(t.buildings.size()) : 0;
            QVERIFY2(buildings >= 2 || core::isEliminated(s->state(), p.id), qPrintable(p.aiProfile));
        }
    }

    // On most island maps the AIs take to the sea once their island is done (a map
    // can keep them busy at home for the 60 days, so six maps are played and half must sail).
    int islandAwayDays(quint64 seed) {
        net::LobbyState lobby;
        lobby.settings.seed = seed;
        lobby.settings.mapType = "islands";
        lobby.settings.mapWidth = lobby.settings.mapHeight = 48;
        net::SlotInfo human;
        human.index = 0; human.kind = net::SlotKind::Human; human.clientId = 1; human.name = "Ann";
        human.race = "humans"; human.team = 1; human.color = "#d62728"; human.ready = true;
        lobby.slotList.append(human);
        for (int i = 1; i <= 2; ++i) {
            net::SlotInfo ai;
            ai.index = i; ai.kind = net::SlotKind::Ai; ai.name = QString("Bot%1").arg(i); ai.race = i == 1 ? "undead" : "sylvan";
            ai.team = 1 + i; ai.color = i == 1 ? "#2ca02c" : "#1f77b4"; ai.ready = true; ai.aiProfile = "explorer";
            lobby.slotList.append(ai);
        }
        GameSession s(reg);
        QString error;
        if (!s.start(lobby, &error)) return -1;
        // Each AI's home island: the land around its first town.
        const auto island = [&](core::Pos from) {
            QSet<core::Pos> seen{from};
            QList<core::Pos> todo{from};
            while (!todo.isEmpty()) {
                const core::Pos p = todo.takeLast();
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const core::Pos n{p.x + dx, p.y + dy};
                        if (seen.contains(n) || !s.state().map().inBounds(n)) continue;
                        const core::TerrainDef* t = reg.terrainType(s.state().map().terrainAt(n));
                        if (t && t->walkable && !t->sailable) {
                            seen.insert(n);
                            todo.append(n);
                        }
                    }
            }
            return seen;
        };
        QHash<quint32, QSet<core::Pos>> home;
        for (const core::Town& t : s.state().towns())
            if (t.owner.isValid() && s.state().player(t.owner)->controller == core::Player::Controller::Ai) home.insert(t.owner.value(), island(t.pos));
        int sailingDays = 0, awayDays = 0;
        for (int day = 0; day < 60; ++day) {
            playDays(s, 1);
            bool sailing = false, away = false;
            for (const core::Hero& h : s.state().heroes()) {
                if (s.state().player(h.owner)->controller != core::Player::Controller::Ai) continue;
                sailing = sailing || h.inBoat;
                const core::Army* a = s.state().army(h.army);
                away = away || (!h.inBoat && a && !home.value(h.owner.value()).contains(a->pos));
            }
            sailingDays += sailing;
            awayDays += away;
        }
        qInfo() << "seed" << seed << "days with an AI hero at sea:" << sailingDays << "on another island:" << awayDays;
        return sailingDays + awayDays;
    }

    void aisSailOnIslandMaps() {
        int maps = 0;
        for (const quint64 seed : {31, 32, 33, 34, 35, 36}) maps += islandAwayDays(seed) > 0;
        QVERIFY2(maps >= 3, "AIs should take to the sea when their island is done");
    }

    void deterministic() {
        auto a = startGame(3);
        auto b = startGame(3);
        playDays(*a, 10);
        playDays(*b, 10);
        QCOMPARE(a->state().toJson(), b->state().toJson());
    }
};

QTEST_GUILESS_MAIN(TstAi)
#include "tst_ai.moc"
