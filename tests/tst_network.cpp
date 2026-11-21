#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include <memory>

#include "core/Version.h"
#include "core/content/ContentRegistry.h"
#include "core/model/GameState.h"
#include "net/Connection.h"
#include "net/GameToken.h"
#include "net/LobbyClient.h"
#include "net/Protocol.h"
#include "server/GameServer.h"
#include "server/ServerConfig.h"
#include "server/Tls.h"

using namespace kot::net;
using kot::server::GameServer;

namespace {

// A bare protocol connection for tests that need to break the rules.
struct RawClient {
    Connection* connection = nullptr;
    QList<Message> received;
    bool connected = false;
    bool closed = false;

    explicit RawClient(quint16 port, bool tls = false) {
        connection = new Connection(tls);
        QObject::connect(connection, &Connection::connected, [this]() { connected = true; });
        QObject::connect(connection, &Connection::disconnected, [this]() { closed = true; });
        QObject::connect(connection, &Connection::messageReceived,
                         [this](const Message& m) { received.append(m); });
        connection->connectToHost("127.0.0.1", port, tls);
    }
    ~RawClient() { delete connection; }

    void send(const QString& type, const QJsonObject& body = {}, int seq = 0) {
        Message m;
        m.type = type;
        m.body = body;
        m.seq = seq;
        connection->send(m);
    }
    bool gotType(const QString& type) const {
        for (const Message& m : received) if (m.type == type) return true;
        return false;
    }
    QString lastErrorCode() const {
        for (auto it = received.crbegin(); it != received.crend(); ++it)
            if (it->type == msg::error) return it->body.value("code").toString();
        return {};
    }
};

}  // namespace

class TstNetwork : public QObject {
    Q_OBJECT
    QScopedPointer<GameServer> server;
    kot::core::ContentRegistry content;

    // A client with its own new token (a different person), or with `token` (the same one again).
    std::unique_ptr<LobbyClient> connectClient(const QString& name, const QString& token = QString(), bool tls = false) {
        auto c = std::make_unique<LobbyClient>();
        c->setContentChecksum(content.checksum());
        c->setGameToken(token.isEmpty() ? newGameToken() : token);
        c->connectToServer("127.0.0.1", server->port(), name, tls);
        return c;
    }

    // Ann and Bob start a game; returns their player ids.
    QPair<int, int> startTwoPlayerGame(LobbyClient& a, LobbyClient& b) {
        [&]() {
            QTRY_COMPARE_WITH_TIMEOUT(b.lobby().members.size(), 2, 3000);
            a.setReady(true);
            b.setReady(true);
            QTRY_VERIFY_WITH_TIMEOUT(a.lobby().slotList[1].ready && a.lobby().slotList[0].ready, 3000);
            a.startGame();
            QTRY_VERIFY_WITH_TIMEOUT(!a.view().isEmpty() && !b.view().isEmpty(), 3000);
        }();
        return {a.you()["playerId"].toInt(), b.you()["playerId"].toInt()};
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(content.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void init() {
        server.reset(new GameServer({"humans", "undead"}, {"passive"}));
        QVERIFY(server->start(0));
    }

    void handshakeGivesIdRacesAndALobby() {
        auto a = connectClient("Ann");
        QTRY_VERIFY_WITH_TIMEOUT(a->isConnected() && !a->lobby().members.isEmpty(), 3000);
        QVERIFY(a->clientId() > 0);
        QCOMPARE(a->races(), QStringList({"humans", "undead"}));
        QCOMPARE(a->aiProfiles(), QStringList({"passive"}));
        QCOMPARE(a->lobby().members.size(), 1);
        QVERIFY(a->isHost());
        QCOMPARE(a->lobby().slotList[0].kind, SlotKind::Human);
        QCOMPARE(a->lobby().slotList[0].name, QString("Ann"));
        QCOMPARE(server->clientCount(), 1);
    }

    void twoClientsSeeTheSameLobby() {
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        QTRY_COMPARE_WITH_TIMEOUT(b->lobby().members.size(), 2, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(a->lobby().members.size(), 2, 3000);
        QVERIFY(a->isHost());
        QVERIFY(!b->isHost());
        QCOMPARE(a->lobby().toJson(), b->lobby().toJson());
        QCOMPARE(server->clientCount(), 2);
    }

    void changesReachEveryone() {
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        QTRY_COMPARE_WITH_TIMEOUT(b->lobby().members.size(), 2, 3000);
        b->setSlot(1, {{"race", "undead"}, {"team", 2}});
        b->setReady(true);
        QTRY_VERIFY_WITH_TIMEOUT(a->lobby().slotList[1].ready, 3000);
        QCOMPARE(a->lobby().slotList[1].race, QString("undead"));
        QCOMPARE(a->lobby().slotList[1].team, 2);

        a->setSlot(2, {{"kind", "ai"}});
        QTRY_COMPARE_WITH_TIMEOUT(b->lobby().slotList[2].kind, SlotKind::Ai, 3000);
        a->setSettings({{"mapWidth", 50}, {"seed", "99"}});
        QTRY_COMPARE_WITH_TIMEOUT(b->lobby().settings.mapWidth, 50, 3000);
        QCOMPARE(b->lobby().settings.seed, quint64(99));
    }

    void errorsNameTheRequest() {
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        QTRY_COMPARE_WITH_TIMEOUT(b->lobby().members.size(), 2, 3000);
        QString code;
        int replyTo = 0;
        connect(b.get(), &LobbyClient::errorReceived, this,
                [&](const QString& c, const QString&, int r) { code = c; replyTo = r; });
        const int seq = b->setSlot(1, {{"kind", "closed"}});
        QVERIFY(seq > 0);
        QTRY_COMPARE_WITH_TIMEOUT(code, QString("not_host"), 3000);
        QCOMPARE(replyTo, seq);
        QCOMPARE(a->lobby().slotList[1].kind, SlotKind::Human);  // nothing changed
    }

    void startingTheGameTellsEveryone() {
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        QTRY_COMPARE_WITH_TIMEOUT(b->lobby().members.size(), 2, 3000);
        QSignalSpy serverSpy(server.data(), &GameServer::gameStartRequested);
        QSignalSpy spyA(a.get(), &LobbyClient::gameStarting);
        QSignalSpy spyB(b.get(), &LobbyClient::gameStarting);
        a->setReady(true);
        b->setReady(true);
        QTRY_VERIFY_WITH_TIMEOUT(a->lobby().slotList[1].ready && a->lobby().slotList[0].ready, 3000);
        a->startGame();
        QTRY_COMPARE_WITH_TIMEOUT(spyA.count(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(spyB.count(), 1, 3000);
        QCOMPARE(serverSpy.count(), 1);
        QVERIFY(a->lobby().started);
    }

    void startIsRefusedUntilReady() {
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        QTRY_COMPARE_WITH_TIMEOUT(b->lobby().members.size(), 2, 3000);
        QString code;
        connect(a.get(), &LobbyClient::errorReceived, this,
                [&](const QString& c, const QString&, int) { code = c; });
        a->startGame();
        QTRY_COMPARE_WITH_TIMEOUT(code, QString("not_ready"), 3000);
        QVERIFY(!a->lobby().started);
    }

    void leavingFreesTheSlotForOthers() {
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        QTRY_COMPARE_WITH_TIMEOUT(a->lobby().members.size(), 2, 3000);
        b->disconnectFromServer();
        QTRY_COMPARE_WITH_TIMEOUT(a->lobby().members.size(), 1, 3000);
        QCOMPARE(a->lobby().slotList[1].kind, SlotKind::Open);
        QTRY_COMPARE_WITH_TIMEOUT(server->clientCount(), 1, 3000);
    }

    void hostLeavingPassesTheHost() {
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        QTRY_COMPARE_WITH_TIMEOUT(b->lobby().members.size(), 2, 3000);
        a->disconnectFromServer();
        QTRY_VERIFY_WITH_TIMEOUT(b->isHost(), 3000);
    }

    void namesAreCleanedUp() {
        auto a = connectClient("   ");
        QTRY_VERIFY_WITH_TIMEOUT(!a->lobby().members.isEmpty(), 3000);
        QVERIFY(a->lobby().members[0].name.startsWith("Player "));
        auto b = connectClient(QString(60, 'x'));
        QTRY_COMPARE_WITH_TIMEOUT(b->lobby().members.size(), 2, 3000);
        QCOMPARE(b->lobby().members[1].name.size(), 24);
    }

    // ---- breaking the rules
    void wrongProtocolVersionIsRefused() {
        RawClient raw(server->port());
        QTRY_VERIFY_WITH_TIMEOUT(raw.connected, 3000);
        raw.send(msg::hello, {{"protocol", 99}, {"name", "Old"}}, 1);
        QTRY_VERIFY_WITH_TIMEOUT(raw.gotType(msg::error), 3000);
        QCOMPARE(raw.lastErrorCode(), QString("protocol_mismatch"));
        QTRY_VERIFY_WITH_TIMEOUT(raw.closed, 3000);
        QCOMPARE(server->clientCount(), 0);
    }

    void otherGameDataIsRefused() {
        server->setContent(&content);
        LobbyClient c;
        c.setContentChecksum("not-the-same");
        QSignalSpy failed(&c, &LobbyClient::connectionFailed);
        QString code;
        connect(&c, &LobbyClient::errorReceived, this, [&](const QString& k, const QString&, int) { code = k; });
        c.connectToServer("127.0.0.1", server->port(), "Eve");
        QTRY_COMPARE_WITH_TIMEOUT(code, QString("content_mismatch"), 3000);
    }

    void firstMessageMustBeHello() {
        RawClient raw(server->port());
        QTRY_VERIFY_WITH_TIMEOUT(raw.connected, 3000);
        raw.send(msg::claimSlot, {{"slot", 0}});
        QTRY_VERIFY_WITH_TIMEOUT(raw.closed, 3000);
        QCOMPARE(raw.lastErrorCode(), QString("expected_hello"));
        QVERIFY(server->lobby().state().members.isEmpty());
    }

    void garbageBytesCloseTheConnection() {
        QTcpSocket socket;
        socket.connectToHost("127.0.0.1", server->port());
        QVERIFY(socket.waitForConnected(3000));
        socket.write(QByteArray(8, '\xff'));  // length 0xFFFFFFFF
        socket.flush();
        QTRY_VERIFY_WITH_TIMEOUT(socket.state() == QAbstractSocket::UnconnectedState, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(server->clientCount(), 0, 3000);
    }

    void unknownMessageKeepsTheConnection() {
        RawClient raw(server->port());
        QTRY_VERIFY_WITH_TIMEOUT(raw.connected, 3000);
        raw.send(msg::hello, {{"protocol", kProtocolVersion}, {"name", "Raw"}}, 1);
        QTRY_VERIFY_WITH_TIMEOUT(raw.gotType(msg::welcome), 3000);
        raw.send("dance", {}, 2);
        QTRY_COMPARE_WITH_TIMEOUT(raw.lastErrorCode(), QString("unknown_message"), 3000);
        QVERIFY(!raw.closed);
    }

    void pingGetsAPongWithTheSameSeq() {
        RawClient raw(server->port());
        QTRY_VERIFY_WITH_TIMEOUT(raw.connected, 3000);
        raw.send(msg::hello, {{"protocol", kProtocolVersion}, {"name", "Raw"}}, 1);
        QTRY_VERIFY_WITH_TIMEOUT(raw.gotType(msg::welcome), 3000);
        raw.send(msg::ping, {}, 77);
        QTRY_VERIFY_WITH_TIMEOUT(raw.gotType(msg::pong), 3000);
        QCOMPARE(raw.received.last().seq, 77);
    }

    // ---- a real game over the network
    void aGameFromStartToEndTurn() {
        server->setContent(&content);
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        QTRY_COMPARE_WITH_TIMEOUT(b->lobby().members.size(), 2, 3000);
        QSignalSpy startedA(a.get(), &LobbyClient::gameStarted);
        QSignalSpy startedB(b.get(), &LobbyClient::gameStarted);
        a->setReady(true);
        b->setReady(true);
        QTRY_VERIFY_WITH_TIMEOUT(a->lobby().slotList[1].ready && a->lobby().slotList[0].ready, 3000);
        a->startGame();
        QTRY_COMPARE_WITH_TIMEOUT(startedA.count(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(startedB.count(), 1, 3000);

        // Each player gets his own identity and his own filtered view.
        const int idA = a->you()["playerId"].toInt(), idB = b->you()["playerId"].toInt();
        QVERIFY(idA > 0 && idB > 0 && idA != idB);
        QCOMPARE(a->view()["you"].toInt(), idA);
        QCOMPARE(a->view()["heroes"].toArray().size(), 1);
        QCOMPARE(b->view()["heroes"].toArray().size(), 1);
        QVERIFY(a->view()["heroes"].toArray()[0].toObject() != b->view()["heroes"].toArray()[0].toObject());
        QCOMPARE(a->view()["currentPlayer"].toInt(), idA);
        QVERIFY(server->session());

        // Bob may not act yet.
        QString code;
        connect(b.get(), &LobbyClient::errorReceived, this, [&](const QString& c, const QString&, int) { code = c; });
        b->sendCommand(msg::endTurn);
        QTRY_COMPARE_WITH_TIMEOUT(code, QString("not_your_turn"), 3000);

        // Ann ends her turn: she gets a result, both get new views.
        int day = 0;
        connect(a.get(), &LobbyClient::commandResult, this, [&](int, const QJsonObject& info) { day = info["day"].toInt(); });
        a->sendCommand(msg::endTurn);
        QTRY_COMPARE_WITH_TIMEOUT(b->view()["currentPlayer"].toInt(), idB, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(a->view()["currentPlayer"].toInt(), idB, 3000);
        QCOMPARE(day, 1);

        // Bob ends his: a new day starts for Ann.
        b->sendCommand(msg::endTurn);
        QTRY_COMPARE_WITH_TIMEOUT(a->view()["currentPlayer"].toInt(), idA, 3000);
        QCOMPARE(a->view()["day"].toInt(), 2);
    }

    void theGameWaitsForAPlayerWhoLeft() {
        server->setContent(&content);
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        QTRY_COMPARE_WITH_TIMEOUT(b->lobby().members.size(), 2, 3000);
        a->setReady(true);
        b->setReady(true);
        QTRY_VERIFY_WITH_TIMEOUT(a->lobby().slotList[1].ready && a->lobby().slotList[0].ready, 3000);
        a->startGame();
        QTRY_VERIFY_WITH_TIMEOUT(!a->view().isEmpty() && !b->view().isEmpty(), 3000);
        const int idB = b->you()["playerId"].toInt();
        b->disconnectFromServer();
        QTRY_COMPARE_WITH_TIMEOUT(server->clientCount(), 1, 3000);
        a->sendCommand(msg::endTurn);  // now Bob's turn, and he is away: no computer plays it
        QTRY_COMPARE_WITH_TIMEOUT(a->view()["currentPlayer"].toInt(), idB, 3000);
        QTest::qWait(300);
        QCOMPARE(a->view()["currentPlayer"].toInt(), idB);
        QCOMPARE(a->view()["day"].toInt(), 1);
        bool away = false;
        for (const QJsonValue& p : a->view()["players"].toArray())
            if (p.toObject()["id"].toInt() == idB) away = p.toObject()["away"].toBool();
        QVERIFY(away);  // shown as not connected
    }

    void aPlayerWhoLeftCanComeBack() {
        server->setContent(&content);
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        const auto [idA, idB] = startTwoPlayerGame(*a, *b);
        QVERIFY(idA > 0 && idB > 0);
        b->disconnectFromServer();
        QTRY_COMPARE_WITH_TIMEOUT(server->clientCount(), 1, 3000);

        // Someone else (another key) cannot take Bob's seat while he is away ...
        auto stranger = connectClient("Not Bob");
        QString strangerCode;
        connect(stranger.get(), &LobbyClient::errorReceived, this, [&](const QString& c, const QString&, int) { strangerCode = c; });
        QTRY_VERIFY_WITH_TIMEOUT(stranger->isConnected(), 3000);
        QVERIFY(stranger->freePlayers().isEmpty());
        stranger->joinGame(idB);
        QTRY_COMPARE_WITH_TIMEOUT(strangerCode, QString("failed"), 3000);
        stranger.reset();
        QTRY_COMPARE_WITH_TIMEOUT(server->clientCount(), 1, 3000);
        // ... Bob's client comes with his key and gets his seat back at once.
        auto b2 = connectClient("Bob again", b->gameToken());
        QTRY_COMPARE_WITH_TIMEOUT(b2->you()["playerId"].toInt(), idB, 3000);
        QCOMPARE(b2->view()["you"].toInt(), idB);

        // Bob plays again: after Ann's turn it is his.
        a->sendCommand(msg::endTurn);
        QTRY_COMPARE_WITH_TIMEOUT(b2->view()["currentPlayer"].toInt(), idB, 3000);

        // Nobody can take a player who is already played.
        auto c = connectClient("Cid");
        QString code;
        connect(c.get(), &LobbyClient::errorReceived, this, [&](const QString& cd, const QString&, int) { code = cd; });
        QTRY_VERIFY_WITH_TIMEOUT(c->isConnected(), 3000);
        QVERIFY(c->freePlayers().isEmpty());
        c->joinGame(idA);
        QTRY_COMPARE_WITH_TIMEOUT(code, QString("failed"), 3000);
    }

    void saveAndLoadARunningGame() {
        QTemporaryDir saves;
        server->setContent(&content);
        server->setSaveDir(saves.path());
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        const auto [idA, idB] = startTwoPlayerGame(*a, *b);
        a->sendCommand(msg::endTurn);
        QTRY_COMPARE_WITH_TIMEOUT(b->view()["currentPlayer"].toInt(), idB, 3000);

        QJsonObject info;
        connect(b.get(), &LobbyClient::commandResult, this, [&](int, const QJsonObject& i) { info = i; });
        QString code;
        connect(b.get(), &LobbyClient::errorReceived, this, [&](const QString& c, const QString&, int) { code = c; });
        b->saveGame("../evil");
        QTRY_COMPARE_WITH_TIMEOUT(code, QString("failed"), 3000);
        b->saveGame("my-game_1");
        QTRY_COMPARE_WITH_TIMEOUT(info["name"].toString(), QString("my-game_1"), 3000);
        const QJsonObject savedView = b->view();
        b->listSaves();
        QTRY_COMPARE_WITH_TIMEOUT(info["saves"].toArray().size(), 1, 3000);
        {
            const QJsonObject g = info["games"].toArray().first().toObject();
            QCOMPARE(g["status"].toString(), QString("ok"));
            QCOMPARE(g["gameVersion"].toString(), QString(kot::core::kGameVersion));
            QVERIFY(g["day"].toInt() >= 1);
            QVERIFY(!g["players"].toArray().isEmpty());
        }

        // A fresh server loads it; both players take their places again.
        const QString keyA = a->gameToken(), keyB = b->gameToken();
        a.reset();
        b.reset();
        server.reset(new GameServer({"humans"}, {"simple", "passive"}));
        QVERIFY(server->start(0));
        server->setContent(&content);
        server->setSaveDir(saves.path());
        auto a2 = connectClient("Ann", keyA);
        auto b2 = connectClient("Bob", keyB);
        QTRY_COMPARE_WITH_TIMEOUT(b2->lobby().members.size(), 2, 3000);
        a2->loadGame("my-game_1");  // each gets his own seat back (by his key)
        QTRY_COMPARE_WITH_TIMEOUT(b2->you()["playerId"].toInt(), idB, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(a2->you()["playerId"].toInt(), idA, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(b2->view()["currentPlayer"].toInt(), idB, 3000);
        QCOMPARE(b2->view()["day"].toInt(), savedView["day"].toInt());
        QCOMPARE(b2->view()["heroes"], savedView["heroes"]);
        b2->sendCommand(msg::endTurn);
        QTRY_COMPARE_WITH_TIMEOUT(a2->view()["day"].toInt(), 2, 3000);
    }

    void autosavesEveryDayKeepingTheNewest() {
        QTemporaryDir saves;
        {
            QFile old(saves.filePath("autosave_day0070.json"));  // from an older game and version
            QVERIFY(old.open(QIODevice::WriteOnly));
            old.write("{}");
            old.close();
            QVERIFY(old.open(QIODevice::ReadWrite));
            QVERIFY(old.setFileTime(QDateTime::currentDateTime().addDays(-1), QFileDevice::FileModificationTime));
            old.close();
        }
        server->setContent(&content);
        server->setSaveDir(saves.path());
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        QTRY_COMPARE_WITH_TIMEOUT(b->lobby().members.size(), 2, 3000);
        a->setSettings({{"autosaves", 2}});
        QTRY_COMPARE_WITH_TIMEOUT(b->lobby().settings.autosaves, 2, 3000);
        startTwoPlayerGame(*a, *b);
        for (int day = 2; day <= 5; ++day) {
            a->sendCommand(msg::endTurn);
            QTRY_COMPARE_WITH_TIMEOUT(b->view()["currentPlayer"].toInt(), b->you()["playerId"].toInt(), 3000);
            b->sendCommand(msg::endTurn);
            QTRY_COMPARE_WITH_TIMEOUT(a->view()["day"].toInt(), day, 3000);
        }
        // The old game's autosave is the oldest, so it went; this game's newest two stay.
        QTRY_VERIFY_WITH_TIMEOUT(QDir(saves.path()).entryList({"autosave_*.json"}, QDir::Files, QDir::Name).size() == 2, 3000);
        const QStringList names = QDir(saves.path()).entryList({"autosave_*.json"}, QDir::Files, QDir::Name);
        QVERIFY2(names[0].endsWith("_day0004.json") && names[1].endsWith("_day0005.json"), qPrintable(names.join(" ")));
        kot::core::GameState loaded;
        QVERIFY(kot::core::GameState::loadFromFile(saves.filePath(names[1]), content, &loaded, nullptr));
        QCOMPARE(loaded.day(), 5);
    }

    void onlyTheHostReplacesASave() {
        QTemporaryDir saves;
        server->setContent(&content);
        server->setSaveDir(saves.path());
        auto a = connectClient("Ann");  // the host
        auto b = connectClient("Bob");
        startTwoPlayerGame(*a, *b);
        QJsonObject info;
        connect(a.get(), &LobbyClient::commandResult, this, [&](int, const QJsonObject& i) { info = i; });
        QString codeB;
        connect(b.get(), &LobbyClient::errorReceived, this, [&](const QString& c, const QString&, int) { codeB = c; });
        b->saveGame("ours");  // a guest may save ...
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(saves.filePath("ours.json")), 3000);
        b->saveGame("ours");  // ... but not replace a save
        QTRY_COMPARE_WITH_TIMEOUT(codeB, QString("save_exists"), 3000);
        a->saveGame("ours");  // the host may
        QTRY_COMPARE_WITH_TIMEOUT(info["name"].toString(), QString("ours"), 3000);
    }

    void aRestartedServerResumesTheGame() {
        QTemporaryDir saves;
        server->setContent(&content);
        server->setSaveDir(saves.path());
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        const auto [idA, idB] = startTwoPlayerGame(*a, *b);
        a->sendCommand(msg::endTurn);
        QTRY_COMPARE_WITH_TIMEOUT(b->view()["currentPlayer"].toInt(), idB, 3000);
        b->saveGame("before-restart");
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(saves.filePath("before-restart.json")), 3000);
        const QString keyA = a->gameToken(), keyB = b->gameToken();
        a.reset();
        b.reset();
        // The server goes down and comes back with --resume: the game waits for them.
        server.reset(new GameServer({"humans"}, {"simple", "passive"}));
        QVERIFY(server->start(0));
        server->setContent(&content);
        server->setSaveDir(saves.path());
        QString error;
        QVERIFY2(server->resumeNewestSave(&error), qPrintable(error));
        // Bob comes first (his turn); each gets his own seat back by his code.
        auto b2 = connectClient("Bob", keyB);
        QTRY_COMPARE_WITH_TIMEOUT(b2->you()["playerId"].toInt(), idB, 3000);
        QCOMPARE(b2->view()["currentPlayer"].toInt(), idB);
        b2->sendCommand(msg::endTurn);  // then nothing happens until Ann comes
        QTRY_COMPARE_WITH_TIMEOUT(b2->view()["currentPlayer"].toInt(), idA, 3000);
        auto a2 = connectClient("Ann", keyA);
        QTRY_COMPARE_WITH_TIMEOUT(a2->you()["playerId"].toInt(), idA, 3000);
        a2->sendCommand(msg::endTurn);
        QTRY_COMPARE_WITH_TIMEOUT(b2->view()["day"].toInt(), 2, 3000);
    }

    void fullSaveSpaceRemovesTheOldestSaves() {
        QTemporaryDir saves;
        const auto oldFile = [&](const QString& name, int kb, int daysAgo) {
            QFile f(saves.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(kb * 1024, ' '));
            f.close();
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(QDateTime::currentDateTime().addDays(-daysAgo), QFileDevice::FileModificationTime));
        };
        oldFile("oldest.json", 400, 3);
        oldFile("older.json", 400, 2);
        oldFile("campaign_x.json", 150, 5);  // campaign progress: never removed
        server->setContent(&content);
        server->setSaveDir(saves.path());
        server->setSaveLimitBytes(960 * 1024);  // 960 KB: 950 used, the save needs more
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        startTwoPlayerGame(*a, *b);
        QJsonObject info;
        connect(a.get(), &LobbyClient::commandResult, this, [&](int, const QJsonObject& i) { info = i; });
        a->saveGame("mine");
        QTRY_COMPARE_WITH_TIMEOUT(info["name"].toString(), QString("mine"), 3000);
        QVERIFY(QFile::exists(saves.filePath("mine.json")));
        QVERIFY(!QFile::exists(saves.filePath("oldest.json")));  // made room
        QVERIFY(QFile::exists(saves.filePath("campaign_x.json")));
        qint64 used = 0;
        for (const QFileInfo& f : QDir(saves.path()).entryInfoList(QDir::Files)) used += f.size();
        QVERIFY(used <= 960 * 1024);
        QVERIFY(QFile::exists(saves.filePath("older.json")));  // only as much as needed went
        // A save bigger than the whole space is refused.
        server->setSaveLimitBytes(1024);
        QString code;
        connect(a.get(), &LobbyClient::errorReceived, this, [&](const QString& c, const QString&, int) { code = c; });
        a->saveGame("too-big");
        QTRY_COMPARE_WITH_TIMEOUT(code, QString("failed"), 3000);
        QVERIFY(!QFile::exists(saves.filePath("too-big.json")));
    }

    void serverConfigFile() {
        QTemporaryDir dir;
        const auto write = [&](const QByteArray& json) {
            QFile f(dir.filePath("server.json"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(json);
        };
        const QString tokenA(64, QLatin1Char('a')), tokenB(64, QLatin1Char('b'));
        write(QJsonDocument(QJsonObject{{"port", 4000}, {"saves", "/tmp/x"}, {"savesMaxMB", 250},
                                        {"allowedGameTokens", QJsonArray{tokenA, tokenB}}})
                  .toJson());
        kot::server::ServerConfig c;
        QStringList errors;
        QVERIFY2(c.load(dir.filePath("server.json"), true, &errors), qPrintable(errors.join("; ")));
        QCOMPARE(c.port, 4000);
        QCOMPARE(c.saveDir, QString("/tmp/x"));
        QCOMPARE(c.savesMaxMB, 250);
        QCOMPARE(c.allowedGameTokens, QStringList({tokenA, tokenB}));
        QVERIFY(c.dataDir.isEmpty());
        write(R"({"port": 4000, "savesMax": 250})");  // unknown field
        QVERIFY(!kot::server::ServerConfig().load(dir.filePath("server.json"), true, &errors));
        QVERIFY(!kot::server::ServerConfig().load(dir.filePath("missing.json"), true, &errors));
        QVERIFY(kot::server::ServerConfig().load(dir.filePath("missing.json"), false, &errors));  // the default path need not exist
    }

    void gameTokensAreLongAndStrict() {
        const QString token = newGameToken();
        QCOMPARE(token.size(), kGameTokenHexDigits);
        QVERIFY(looksLikeGameToken(token));
        QVERIFY(!looksLikeGameToken(token.left(32)));  // the old short keys are not game tokens
        QVERIFY(!looksLikeGameToken(token + QStringLiteral("0")));
        QCOMPARE(normalizeGameToken(QStringLiteral(" %1 ").arg(token.toUpper())), token);
    }

    void gameTokensAreNormalizedAndHashed() {
        // A known SHA-256 vector: this is all the server keeps for a seat.
        QCOMPARE(hashGameToken(QStringLiteral("abc")),
                 QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        QCOMPARE(hashGameToken(QString()), QString());
        const QString token = newGameToken();
        QCOMPARE(hashGameToken(token).size(), 64);
        QVERIFY(hashGameToken(token) != hashGameToken(newGameToken()));

        // Typing aids do not change the token: case, spaces, tabs, newlines and dashes.
        const QString typed = QStringLiteral("  %1-\n\t%2  ").arg(token.left(32).toUpper(), token.mid(32));
        QCOMPARE(normalizeGameToken(typed), token);
        QCOMPARE(hashGameToken(normalizeGameToken(typed)), hashGameToken(token));

        // Only exactly 64 lowercase hex digits are a token.
        QVERIFY(!looksLikeGameToken(QString()));
        QVERIFY(!looksLikeGameToken(QString(kGameTokenHexDigits, QLatin1Char('A'))));
        QVERIFY(!looksLikeGameToken(token.left(63)));
        QVERIFY(!looksLikeGameToken(token + QLatin1Char('g')));
        QVERIFY(!looksLikeGameToken(QStringLiteral(" ") + token));

        // Every new token is a different 64 hex digits.
        const QString other = newGameToken();
        QCOMPARE(other.size(), kGameTokenHexDigits);
        QVERIFY(looksLikeGameToken(other));
        QVERIFY(other != token);
    }

    void onlyKnownGameTokensMayJoin() {
        const QString allowed = newGameToken();
        QString typed = allowed;  // as a person may type it
        typed.insert(8, QLatin1Char(' '));
        typed.insert(20, QLatin1Char('-'));
        QTemporaryDir dir;
        auto guarded = std::make_unique<GameServer>(QStringList{"humans"}, QStringList{"passive"});
        guarded->setContent(&content);
        guarded->setSaveDir(dir.path());
        QString error;
        QVERIFY(!guarded->setAllowedGameTokens({QStringLiteral("not a token")}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY2(guarded->setAllowedGameTokens({typed}, &error), qPrintable(error));
        QVERIFY(guarded->hasAllowedGameTokens());
        QVERIFY(guarded->start(0));

        auto welcome = std::make_unique<LobbyClient>();
        welcome->setContentChecksum(content.checksum());
        welcome->setGameToken(allowed);
        welcome->connectToServer("127.0.0.1", guarded->port(), "Ann");
        QTRY_VERIFY_WITH_TIMEOUT(welcome->isConnected(), 3000);
        QCOMPARE(guarded->clientCount(), 1);

        auto stranger = std::make_unique<LobbyClient>();
        stranger->setContentChecksum(content.checksum());
        QString refusal;
        QObject::connect(stranger.get(), &LobbyClient::errorReceived,
                         [&](const QString& code, const QString& text, int) { refusal = code + ": " + text; });
        QSignalSpy failed(stranger.get(), &LobbyClient::connectionFailed);
        stranger->setGameToken(newGameToken());
        stranger->connectToServer("127.0.0.1", guarded->port(), "Bob");
        QTRY_VERIFY_WITH_TIMEOUT(failed.count() >= 1, 3000);
        QVERIFY2(refusal.startsWith(QStringLiteral("not_allowed:")), qPrintable(refusal));
        QVERIFY(!stranger->isConnected());
        QTRY_COMPARE_WITH_TIMEOUT(guarded->clientCount(), 1, 3000);
    }

    void theSaveKeepsOnlyTheTokenHash() {
        QTemporaryDir saves;
        server->setContent(&content);
        server->setSaveDir(saves.path());
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        const QString tokenA = a->gameToken(), tokenB = b->gameToken();
        startTwoPlayerGame(*a, *b);

        QJsonObject info;
        connect(a.get(), &LobbyClient::commandResult, this, [&](int, const QJsonObject& i) { info = i; });
        a->saveGame("token-test");
        QTRY_COMPARE_WITH_TIMEOUT(info["name"].toString(), QString("token-test"), 3000);

        QFile file(saves.filePath("token-test.json"));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
        const QByteArray bytes = file.readAll();
        // Neither secret is in the save: a seat carries the SHA-256 it is claimed with.
        QVERIFY(!bytes.contains(tokenA.toUtf8()));
        QVERIFY(!bytes.contains(tokenB.toUtf8()));
        QVERIFY(bytes.contains(hashGameToken(tokenA).toUtf8()));
        QVERIFY(bytes.contains(hashGameToken(tokenB).toUtf8()));
        QStringList hashes;
        for (const QJsonValue& v : QJsonDocument::fromJson(bytes).object()["players"].toArray())
            hashes << v.toObject()["gameTokenHash"].toString();
        QStringList expected{hashGameToken(tokenA), hashGameToken(tokenB)};
        hashes.sort();
        expected.sort();
        QCOMPARE(hashes, expected);
    }

    void campaignChaptersAreSavedWhenTheyStart() {
        QTemporaryDir saves;
        server->setContent(&content);
        server->setSaveDir(saves.path());
        auto a = connectClient("Ann");
        QObject::connect(a.get(), &LobbyClient::errorReceived,
                         [](const QString& code, const QString& text, int) { qWarning() << "ERROR" << code << text; });
        QTRY_VERIFY_WITH_TIMEOUT(a->isConnected() && !a->lobby().members.isEmpty(), 3000);
        // Campaigns are solo: the server fills the other seat with AI.
        a->setSettings({{"campaign", "first_war"}});
        QTRY_COMPARE_WITH_TIMEOUT(a->lobby().settings.campaign, QString("first_war"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(a->lobby().slotList.size(), 2, 3000);
        a->startGame();
        QTRY_VERIFY_WITH_TIMEOUT(!a->view().isEmpty(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(saves.filePath("campaign_first_war_chapter1_start.json")), 3000);
        // It shows in the load list; the campaign's progress file does not.
        QJsonObject info;
        connect(a.get(), &LobbyClient::commandResult, this, [&](int, const QJsonObject& i) { info = i; });
        a->listSaves();
        QTRY_VERIFY_WITH_TIMEOUT(info.contains("saves"), 3000);
        for (const QJsonValue& v : info["saves"].toArray()) QVERIFY(v.toString().contains("_chapter"));
    }

    void choosingACampaignFillsTheOtherSeatsWithAi() {
        QTemporaryDir saves;
        server->setContent(&content);
        server->setSaveDir(saves.path());
        auto a = connectClient("Ann");
        QTRY_VERIFY_WITH_TIMEOUT(a->isConnected() && !a->campaigns().isEmpty(), 3000);
        QCOMPARE(a->lobby().slotList.size(), 4);
        QCOMPARE(a->lobby().slotList[0].kind, SlotKind::Human);
        QCOMPARE(a->lobby().slotList[1].kind, SlotKind::Open);

        QJsonObject campaign;
        for (const QJsonValue& v : a->campaigns())
            if (v.toObject()["id"].toString() == QStringLiteral("intrinsic-1")) campaign = v.toObject();
        QVERIFY(!campaign.isEmpty());
        const QJsonArray seats = campaign["seats"].toArray();
        QCOMPARE(seats.size(), 5);  // one entry per chapter
        QCOMPARE(seats.at(0).toInt(), 2);
        QCOMPARE(seats.at(3).toInt(), 5);
        QCOMPARE(seats.at(4).toInt(), 6);

        a->setSettings({{"campaign", "intrinsic-1"}});
        QTRY_COMPARE_WITH_TIMEOUT(a->lobby().slotList.size(), 2, 3000);
        QCOMPARE(a->lobby().slotList[0].kind, SlotKind::Human);
        QCOMPARE(a->lobby().slotList[0].name, QString("Ann"));
        QCOMPARE(a->lobby().slotList[1].kind, SlotKind::Ai);
        QVERIFY(a->lobby().slotList[1].ready);

        // Back to a map: the open slots return, and the host keeps his seat.
        a->setSettings({{"campaign", QString()}});
        QTRY_COMPARE_WITH_TIMEOUT(a->lobby().slotList.size(), 4, 3000);
        QCOMPARE(a->lobby().slotList[0].kind, SlotKind::Human);
        QCOMPARE(a->lobby().slotList[1].kind, SlotKind::Open);
    }

    void aCampaignStartsWithTheCurrentChaptersSeatCount() {
        QTemporaryDir saves;
        server->setContent(&content);
        server->setSaveDir(saves.path());
        // Chapter 4 of intrinsic-1 needs five players.
        QFile progress(saves.filePath("campaign_intrinsic-1.json"));
        QVERIFY(progress.open(QIODevice::WriteOnly));
        progress.write(QJsonDocument(QJsonObject{{"campaign", "intrinsic-1"}, {"next", 3}}).toJson());
        progress.close();
        auto a = connectClient("Ann");
        QTRY_VERIFY_WITH_TIMEOUT(a->isConnected() && !a->campaigns().isEmpty(), 3000);
        a->setSettings({{"campaign", "intrinsic-1"}});
        QTRY_COMPARE_WITH_TIMEOUT(a->lobby().slotList.size(), 5, 5000);
        for (int i = 0; i < 5; ++i) QCOMPARE(a->lobby().slotList[i].kind, i ? SlotKind::Ai : SlotKind::Human);
        a->startGame();
        QTRY_VERIFY_WITH_TIMEOUT(!a->view().isEmpty(), 5000);
    }

    void hostCanAbandonAndResetTheLobby() {
        server->setContent(&content);
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        startTwoPlayerGame(*a, *b);
        QVERIFY(server->session() && server->session()->active());

        // A guest cannot end the shared game.
        QSignalSpy guestError(b.get(), &LobbyClient::errorReceived);
        b->abandonGame();
        QTRY_COMPARE_WITH_TIMEOUT(guestError.count(), 1, 3000);
        QCOMPARE(guestError.at(0).at(0).toString(), QStringLiteral("not_host"));
        QVERIFY(server->session() && server->session()->active());

        QSignalSpy goneA(a.get(), &LobbyClient::gameAbandoned);
        QSignalSpy goneB(b.get(), &LobbyClient::gameAbandoned);
        QSignalSpy reply(a.get(), &LobbyClient::commandResult);
        a->abandonGame();
        QTRY_VERIFY_WITH_TIMEOUT(goneA.count() == 1 && goneB.count() == 1 && reply.count() >= 1, 3000);
        QVERIFY(!server->session() || !server->session()->active());
        QVERIFY(a->view().isEmpty());
        QVERIFY(b->view().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(a->lobby().slotList.size(), 4, 3000);
        QVERIFY(!a->lobby().started);
        QCOMPARE(a->lobby().settings.campaign, QString());
        QCOMPARE(a->lobby().slotList[0].clientId, a->clientId());
        QCOMPARE(a->lobby().slotList[1].clientId, b->clientId());
        QCOMPARE(b->lobby().slotList[1].clientId, b->clientId());
    }

    void theHostMakesARandomMap() {
        QTemporaryDir saves;
        server->setContent(&content);
        server->setSaveDir(saves.path());
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        QTRY_COMPARE_WITH_TIMEOUT(b->lobby().members.size(), 2, 3000);
        QJsonObject info;
        connect(a.get(), &LobbyClient::commandResult, this, [&](int, const QJsonObject& i) { info = i; });
        // Only the host may.
        b->generateMap({{"kind", "scenario"}, {"id", "bobs"}, {"width", 32}, {"races", QJsonArray{"humans", "undead"}}});
        a->generateMap({{"kind", "scenario"}, {"id", "anns"}, {"name", "Ann's map"}, {"width", 32}, {"seed", 4},
                        {"races", QJsonArray{"humans", "random"}}});
        QTRY_COMPARE_WITH_TIMEOUT(info["id"].toString(), QString("anns"), 5000);
        QVERIFY(QFile::exists(saves.filePath("scenarios/anns.json")));
        QVERIFY(!QFile::exists(saves.filePath("scenarios/bobs.json")));
        // Everyone gets the new list, and it can be played.
        const auto listed = [](const QJsonArray& list, const QString& id) {
            for (const QJsonValue& v : list)
                if (v.toObject()["id"].toString() == id) return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(listed(b->scenarios(), "anns"), 3000);
        // A campaign too.
        a->generateMap({{"kind", "campaign"}, {"id", "annsc"}, {"name", "Ann's war"}, {"chapters", 2}, {"size", 32}, {"race", "humans"}});
        QTRY_COMPARE_WITH_TIMEOUT(info["id"].toString(), QString("annsc"), 8000);
        QVERIFY(QFile::exists(saves.filePath("campaigns/annsc.json")));
        QVERIFY(QFile::exists(saves.filePath("scenarios/annsc_2.json")));
        QTRY_VERIFY_WITH_TIMEOUT(listed(b->campaigns(), "annsc"), 3000);

        a->setSettings({{"scenario", "anns"}});
        QTRY_COMPARE_WITH_TIMEOUT(b->lobby().settings.scenario, QString("anns"), 3000);
        startTwoPlayerGame(*a, *b);
    }

    void tlsKeepsTheGameAndTheToken() {
        QTemporaryDir dir;
        QString cert, key, error;
        QVERIFY2(kot::server::ensureTlsCertificate(dir.path(), &cert, &key, &error), qPrintable(error));
        auto tlsServer = std::make_unique<GameServer>(QStringList{"humans", "undead"}, QStringList{"passive"});
        tlsServer->setContent(&content);
        tlsServer->setSaveDir(dir.path());
        QVERIFY2(tlsServer->setTls(cert, key, &error), qPrintable(error));
        QVERIFY(tlsServer->tlsEnabled());
        QVERIFY(tlsServer->start(0));
        const auto connect = [&](const QString& name, const QString& token) {
            auto c = std::make_unique<LobbyClient>();
            c->setContentChecksum(content.checksum());
            c->setGameToken(token);
            c->connectToServer("127.0.0.1", tlsServer->port(), name, true);
            return c;
        };
        const QString tokenA = newGameToken();
        auto a = connect("Ann", tokenA);
        auto b = connect("Bob", newGameToken());
        QTRY_COMPARE_WITH_TIMEOUT(b->lobby().members.size(), 2, 5000);
        a->setReady(true);
        b->setReady(true);
        QTRY_VERIFY_WITH_TIMEOUT(a->lobby().slotList[1].ready && a->lobby().slotList[0].ready, 5000);
        QSignalSpy startedA(a.get(), &LobbyClient::gameStarted);
        a->startGame();
        QTRY_COMPARE_WITH_TIMEOUT(startedA.count(), 1, 5000);
        const int idA = a->you()["playerId"].toInt();
        QVERIFY(idA > 0);
        QCOMPARE(a->view()["you"].toInt(), idA);
        QVERIFY(!b->view().isEmpty());

        // The token still brings Ann back to her seat, over TLS too.
        a.reset();
        QTRY_COMPARE_WITH_TIMEOUT(tlsServer->clientCount(), 1, 3000);
        auto a2 = connect("Ann again", tokenA);
        QTRY_COMPARE_WITH_TIMEOUT(a2->you()["playerId"].toInt(), idA, 5000);
        QCOMPARE(a2->view()["you"].toInt(), idA);
        // A token nobody used gets no seat: it is only offered the free ones, and there are none.
        auto stranger = connect("Not Ann", newGameToken());
        QTRY_VERIFY_WITH_TIMEOUT(stranger->isConnected(), 5000);
        QVERIFY(stranger->freePlayers().isEmpty());
    }

    void tlsOverheadIsSmall() {
        if (!qEnvironmentVariableIsSet("KOT_TLS_BENCH")) QSKIP("set KOT_TLS_BENCH=1 to measure the TLS overhead");
        QTemporaryDir dir;
        QString cert, key, error;
        QVERIFY2(kot::server::ensureTlsCertificate(dir.path(), &cert, &key, &error), qPrintable(error));
        auto tlsServer = std::make_unique<GameServer>(QStringList{"humans"}, QStringList{"passive"});
        tlsServer->setContent(&content);
        QVERIFY2(tlsServer->setTls(cert, key, &error), qPrintable(error));
        QVERIFY(tlsServer->start(0));
        qint64 plainHandshake = 0, plainPing = 0, tlsHandshake = 0, tlsPing = 0;
        const auto measure = [&](quint16 port, bool tls, qint64* handshake, qint64* ping) {
            QElapsedTimer timer;
            timer.start();
            RawClient raw(port, tls);
            qint64 welcomeAt = -1;
            QObject::connect(raw.connection, &Connection::connected, [&]() {
                raw.send(msg::hello, {{"protocol", kProtocolVersion}, {"name", "Bench"}, {"content", content.checksum()}}, 1);
            });
            QObject::connect(raw.connection, &Connection::messageReceived, [&](const Message& m) {
                if (m.type == msg::welcome && welcomeAt < 0) welcomeAt = timer.nsecsElapsed();
            });
            QTRY_VERIFY_WITH_TIMEOUT(raw.gotType(msg::welcome), 5000);
            *handshake = welcomeAt;  // TCP + TLS handshake + hello + welcome
            timer.restart();
            for (int i = 0; i < 200; ++i) {
                raw.received.clear();
                raw.send(msg::ping, {}, 10 + i);
                QTRY_VERIFY_WITH_TIMEOUT(raw.gotType(msg::pong), 5000);
            }
            *ping = timer.nsecsElapsed() / 200;
        };
        measure(server->port(), false, &plainHandshake, &plainPing);
        measure(tlsServer->port(), true, &tlsHandshake, &tlsPing);
        qInfo("KOT_TLS_BENCH: connect+hello plain %.3f ms, TLS %.3f ms; ping round trip plain %.3f ms, TLS %.3f ms",
              plainHandshake / 1e6, tlsHandshake / 1e6, plainPing / 1e6, tlsPing / 1e6);
        QVERIFY2(tlsPing < 5'000'000, "a TLS ping round trip should stay under 5 ms on localhost");
    }

    void connectingToNothingReportsAFailure() {
        const quint16 deadPort = server->port();
        server.reset();  // nobody listens there any more
        LobbyClient c;
        QSignalSpy spy(&c, &LobbyClient::connectionFailed);
        c.connectToServer("127.0.0.1", deadPort, "Ann");
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 3000);
        QVERIFY(!c.isConnected());
    }

    void serverLossIsReported() {
        auto a = connectClient("Ann");
        QTRY_VERIFY_WITH_TIMEOUT(a->isConnected(), 3000);
        QSignalSpy lost(a.get(), &LobbyClient::connectionFailed);
        server.reset();
        QTRY_VERIFY_WITH_TIMEOUT(lost.count() >= 1, 3000);
        QVERIFY(!a->isConnected());
    }
};

QTEST_GUILESS_MAIN(TstNetwork)
#include "tst_network.moc"
