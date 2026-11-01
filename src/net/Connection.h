#pragma once

#include <QObject>
#include <QTcpSocket>

#include "net/Message.h"
#include "net/MessageFramer.h"

namespace kot::net {

// A TCP connection that sends and receives whole messages.
// Takes ownership of the socket.
class Connection : public QObject {
    Q_OBJECT
public:
    explicit Connection(QTcpSocket* socket, QObject* parent = nullptr);

    void connectToHost(const QString& host, quint16 port);
    bool isConnected() const { return m_socket->state() == QAbstractSocket::ConnectedState; }
    // Returns false if the connection is not open or the message is too large.
    bool send(const Message& message);
    // Closes after pending data has been written.
    void close();
    QString peerDescription() const;

signals:
    void connected();
    void messageReceived(const kot::net::Message& message);
    // The other side broke the protocol; the connection is closed afterwards.
    void protocolError(const QString& reason);
    void socketError(const QString& reason);
    void disconnected();

private slots:
    void onReadyRead();

private:
    QTcpSocket* m_socket;
    MessageFramer m_framer;
};

}  // namespace kot::net
