#pragma once

#include <QList>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace kot::server {

// M0: accepts connections, greets them, counts them. No game logic yet.
class GameServer : public QObject {
    Q_OBJECT
public:
    explicit GameServer(QObject* parent = nullptr);

    bool start(quint16 port);
    quint16 port() const { return m_server.serverPort(); }
    int clientCount() const { return static_cast<int>(m_clients.size()); }

signals:
    void clientCountChanged(int count);

private slots:
    void onNewConnection();

private:
    QTcpServer m_server;
    QList<QTcpSocket*> m_clients;
};

}  // namespace kot::server
