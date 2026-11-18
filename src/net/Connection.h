#pragma once

#include <QObject>
#include <QTcpSocket>

#include "net/Message.h"
#include "net/MessageFramer.h"

namespace kot::net {

// A TCP connection that sends and receives whole messages, plain or over TLS.
// Takes ownership of the socket. A TLS connection does not check the server's
// certificate: the game's servers use a self-signed one (the game token is the secret).
class Connection : public QObject {
    Q_OBJECT
public:
    explicit Connection(QTcpSocket* socket, QObject* parent = nullptr);
    // Makes its own socket: a QSslSocket when `tls`, else a QTcpSocket.
    explicit Connection(bool tls, QObject* parent = nullptr);

    // With `tls`, the TCP socket is wrapped in TLS (the socket must be a QSslSocket);
    // `connected` then means the TLS handshake is done.
    void connectToHost(const QString& host, quint16 port, bool tls = false);
    bool isConnected() const { return m_socket->state() == QAbstractSocket::ConnectedState; }
    bool isTls() const { return m_tls; }
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
    bool m_tls = false;
    MessageFramer m_framer;
};

}  // namespace kot::net
