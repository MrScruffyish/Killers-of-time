#include <QSignalSpy>
#include <QtEndian>
#include <QtTest>
#include <memory>

#include "core/content/ContentRegistry.h"
#include "net/Connection.h"
#include "net/LobbyClient.h"
#include "net/Protocol.h"
#include "server/GameServer.h"

using namespace kot::net;
using kot::server::GameServer;

namespace {

// A bare protocol connection for tests that need to break the rules.
struct RawClient {
    Connection* connection = nullptr;
    QList<Message> received;
    bool connected = false;
    bool closed = false;

    explicit RawClient(quint16 port) {
        connection = new Connection(new QTcpSocket());
        QObject::connect(connection, &Connection::connected, [this]() { connected = true; });
        QObject::connect(connection, &Connection::disconnected, [this]() { closed = true; });
        QObject::connect(connection, &Connection::messageReceived,
                         [this](const Message& m) { received.append(m); });
        connection->connectToHost("127.0.0.1", port);
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

    std::unique_ptr<LobbyClient> connectClient(const QString& name) {
        auto c = std::make_unique<LobbyClient>();
        c->connectToServer("127.0.0.1", server->port(), name);
        return c;
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

    void aPlayerLeavingDoesNotStopTheGame() {
        server->setContent(&content);
        auto a = connectClient("Ann");
        auto b = connectClient("Bob");
        QTRY_COMPARE_WITH_TIMEOUT(b->lobby().members.size(), 2, 3000);
        a->setReady(true);
        b->setReady(true);
        QTRY_VERIFY_WITH_TIMEOUT(a->lobby().slotList[1].ready && a->lobby().slotList[0].ready, 3000);
        a->startGame();
        QTRY_VERIFY_WITH_TIMEOUT(!a->view().isEmpty() && !b->view().isEmpty(), 3000);
        const int idA = a->you()["playerId"].toInt();
        b->disconnectFromServer();
        QTRY_COMPARE_WITH_TIMEOUT(server->clientCount(), 1, 3000);
        a->sendCommand(msg::endTurn);  // Bob is now automatic, so it is Ann's turn again on day 2
        QTRY_COMPARE_WITH_TIMEOUT(a->view()["day"].toInt(), 2, 3000);
        QCOMPARE(a->view()["currentPlayer"].toInt(), idA);
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
