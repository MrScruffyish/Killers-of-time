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

    std::unique_ptr<GameSession> startGame(quint64 seed, const QStringList& aiRaces = {"humans", "humans"}) {
        net::LobbyState lobby;
        lobby.settings.seed = seed;
        net::SlotInfo human;
        human.index = 0; human.kind = net::SlotKind::Human; human.clientId = 1; human.name = "Ann";
        human.race = "humans"; human.team = 1; human.color = "#d62728"; human.ready = true;
        lobby.slotList.append(human);
        for (int i = 1; i <= 2; ++i) {
            net::SlotInfo ai;
            ai.index = i; ai.kind = net::SlotKind::Ai; ai.name = QString("Bot%1").arg(i); ai.race = aiRaces[i - 1];
            ai.team = 1 + i; ai.color = i == 1 ? "#2ca02c" : "#1f77b4"; ai.ready = true; ai.aiProfile = "simple";
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

    void otherRacesPlayToo() {
        auto s = startGame(21, {"undead", "sylvan"});
        QVERIFY(s->active());
        playDays(*s, 14);
        QCOMPARE(s->state().day(), 15);
        for (const core::Player& p : s->state().players()) {
            if (p.controller != core::Player::Controller::Ai) continue;
            int buildings = 0;
            for (const core::Town& t : s->state().towns())
                if (t.owner == p.id) buildings += int(t.buildings.size());
            QVERIFY2(buildings >= 2, qPrintable(p.race));
        }
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
