#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <QtTest>

#include "net/Message.h"
#include "net/MessageFramer.h"
#include "net/Protocol.h"

using namespace kot::net;

static Message make(const QString& type, const QJsonObject& body = {}, int seq = 0) {
    Message m;
    m.type = type;
    m.body = body;
    m.seq = seq;
    return m;
}

class TstFramer : public QObject {
    Q_OBJECT
private slots:
    void messageJsonRoundTrip() {
        const Message m = make("hello", {{"name", "Ann"}, {"n", 3}}, 7);
        Message back;
        QString error;
        QVERIFY(Message::fromJson(m.toJson(), &back, &error));
        QCOMPARE(back.type, QString("hello"));
        QCOMPARE(back.seq, 7);
        QCOMPARE(back.body.value("name").toString(), QString("Ann"));
        QVERIFY(!back.body.contains("type"));
        QVERIFY(!back.body.contains("seq"));
    }

    void seqIsOptional() {
        Message back;
        QVERIFY(Message::fromJson({{"type", "ping"}}, &back, nullptr));
        QCOMPARE(back.seq, 0);
        QVERIFY(!make("ping").toJson().contains("seq"));
    }

    void rejectsBadMessages() {
        Message back;
        QString error;
        QVERIFY(!Message::fromJson({}, &back, &error));
        QVERIFY(error.contains("type"));
        QVERIFY(!Message::fromJson({{"type", 5}}, &back, &error));
        QVERIFY(!Message::fromJson({{"type", ""}}, &back, &error));
        QVERIFY(!Message::fromJson({{"type", "x"}, {"seq", "no"}}, &back, &error));
        QVERIFY(!Message::fromJson({{"type", "x"}, {"seq", -1}}, &back, &error));
    }

    void singleFrame() {
        MessageFramer f;
        f.append(MessageFramer::encode(make("hello", {{"a", 1}}, 2)));
        Message m;
        QString error;
        QCOMPARE(f.next(&m, &error), MessageFramer::Status::Message);
        QCOMPARE(m.type, QString("hello"));
        QCOMPARE(m.seq, 2);
        QCOMPARE(f.next(&m, &error), MessageFramer::Status::NeedMore);
    }

    void frameHasBigEndianLengthPrefix() {
        const QByteArray frame = MessageFramer::encode(make("x"));
        const quint32 length = qFromBigEndian<quint32>(frame.constData());
        QCOMPARE(static_cast<qsizetype>(length), frame.size() - 4);
    }

    void splitAcrossManyChunks() {
        const QByteArray frame = MessageFramer::encode(make("lobby_state", {{"text", "héllo wörld"}}));
        MessageFramer f;
        Message m;
        QString error;
        for (qsizetype i = 0; i < frame.size() - 1; ++i) {
            f.append(frame.mid(i, 1));
            QCOMPARE(f.next(&m, &error), MessageFramer::Status::NeedMore);
        }
        f.append(frame.right(1));
        QCOMPARE(f.next(&m, &error), MessageFramer::Status::Message);
        QCOMPARE(m.body.value("text").toString(), QString("héllo wörld"));
    }

    void severalFramesInOneChunk() {
        MessageFramer f;
        f.append(MessageFramer::encode(make("a")) + MessageFramer::encode(make("b")) +
                 MessageFramer::encode(make("c")));
        Message m;
        QString error;
        QStringList types;
        while (f.next(&m, &error) == MessageFramer::Status::Message) types << m.type;
        QCOMPARE(types, QStringList({"a", "b", "c"}));
    }

    void zeroLengthIsAnError() {
        MessageFramer f;
        f.append(QByteArray(4, '\0'));
        Message m;
        QString error;
        QCOMPARE(f.next(&m, &error), MessageFramer::Status::Error);
        QVERIFY(error.contains("frame length"));
    }

    void hugeLengthIsAnError() {
        MessageFramer f;
        QByteArray header(4, '\0');
        qToBigEndian<quint32>(kMaxFrameBytes + 1, header.data());
        f.append(header);
        Message m;
        QString error;
        QCOMPARE(f.next(&m, &error), MessageFramer::Status::Error);
    }

    void bigMessagesAreCompressed() {
        // A view-sized message: compressed on the wire, the same after reading.
        QJsonArray tiles;
        for (int i = 0; i < 20000; ++i) tiles.append(i % 7);
        const Message big = make("game_state", {{"tiles", tiles}}, 5);
        const QByteArray frame = MessageFramer::encode(big);
        const quint32 header = qFromBigEndian<quint32>(frame.constData());
        QVERIFY(header & kCompressedFlag);
        const QByteArray json = QJsonDocument(big.toJson()).toJson(QJsonDocument::Compact);
        QVERIFY(frame.size() < json.size() / 4);
        MessageFramer f;
        f.append(frame);
        Message m;
        QCOMPARE(f.next(&m, nullptr), MessageFramer::Status::Message);
        QCOMPARE(m.body.value("tiles").toArray().size(), 20000);
        QCOMPARE(m.seq, 5);
        // Small ones stay plain JSON.
        QVERIFY(!(qFromBigEndian<quint32>(MessageFramer::encode(make("ping")).constData()) & kCompressedFlag));
    }

    void badCompressedFramesAreErrors() {
        // Claims to unpack to more than the limit: refused before unpacking.
        QByteArray packed(4, '\0');
        qToBigEndian<quint32>(kMaxFrameBytes + 1, packed.data());
        packed += QByteArray(16, 'x');
        QByteArray frame(4, '\0');
        qToBigEndian<quint32>(static_cast<quint32>(packed.size()) | kCompressedFlag, frame.data());
        MessageFramer f;
        f.append(frame + packed);
        Message m;
        QString error;
        QCOMPARE(f.next(&m, &error), MessageFramer::Status::Error);
        QVERIFY(error.contains("unpacked size"));
        // Garbage instead of a zlib stream.
        QByteArray junk(4, '\0');
        qToBigEndian<quint32>(100, junk.data());
        junk += QByteArray(20, 'y');
        qToBigEndian<quint32>(static_cast<quint32>(junk.size()) | kCompressedFlag, frame.data());
        MessageFramer g;
        g.append(frame + junk);
        QCOMPARE(g.next(&m, &error), MessageFramer::Status::Error);
    }

    void invalidJsonIsAnError() {
        const QByteArray junk = "not json at all";
        QByteArray frame(4, '\0');
        qToBigEndian<quint32>(static_cast<quint32>(junk.size()), frame.data());
        MessageFramer f;
        f.append(frame + junk);
        Message m;
        QString error;
        QCOMPARE(f.next(&m, &error), MessageFramer::Status::Error);
        QVERIFY(error.contains("JSON"));
    }

    void nonObjectJsonIsAnError() {
        const QByteArray json = "[1,2,3]";
        QByteArray frame(4, '\0');
        qToBigEndian<quint32>(static_cast<quint32>(json.size()), frame.data());
        MessageFramer f;
        f.append(frame + json);
        Message m;
        QString error;
        QCOMPARE(f.next(&m, &error), MessageFramer::Status::Error);
    }

    void errorStaysSticky() {
        MessageFramer f;
        f.append(QByteArray(4, '\0'));
        Message m;
        QString error;
        QCOMPARE(f.next(&m, &error), MessageFramer::Status::Error);
        f.append(MessageFramer::encode(make("ok")));
        QCOMPARE(f.next(&m, &error), MessageFramer::Status::Error);
    }
};

QTEST_APPLESS_MAIN(TstFramer)
#include "tst_framer.moc"
