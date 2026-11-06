#include "net/Connection.h"
#include "net/ActionTiming.h"

#include <QPointer>

#include "net/Protocol.h"

namespace kot::net {

Connection::Connection(QTcpSocket* socket, QObject* parent) : QObject(parent), m_socket(socket) {
    m_socket->setParent(this);
    connect(m_socket, &QTcpSocket::connected, this, &Connection::connected);
    connect(m_socket, &QTcpSocket::disconnected, this, &Connection::disconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &Connection::onReadyRead);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, [this]() {
        emit socketError(m_socket->errorString());
    });
}

void Connection::connectToHost(const QString& host, quint16 port) {
    m_socket->connectToHost(host, port);
}

bool Connection::send(const Message& message) {
    if (!isConnected()) return false;
    const QByteArray frame = MessageFramer::encode(message);
    if (static_cast<quint32>(frame.size() - 4) > kMaxFrameBytes) return false;
    m_socket->write(frame);
    return true;
}

void Connection::close() {
    m_socket->disconnectFromHost();
}

QString Connection::peerDescription() const {
    return QStringLiteral("%1:%2").arg(m_socket->peerAddress().toString()).arg(m_socket->peerPort());
}

void Connection::onReadyRead() {
    QPointer<Connection> self(this);  // a receiver may delete this connection
    const qint64 arrived = timing::now();
    m_framer.append(m_socket->readAll());
    for (;;) {
        Message message;
        QString error;
        const qint64 decodeStart = timing::now();
        const auto status = m_framer.next(&message, &error);
        if (status == MessageFramer::Status::Message) timing::received(message.type, arrived, timing::now() - decodeStart);
        if (status == MessageFramer::Status::NeedMore) return;
        if (status == MessageFramer::Status::Error) {
            emit protocolError(error);
            if (self) close();
            return;
        }
        emit messageReceived(message);
        if (!self) return;
    }
}

}  // namespace kot::net
