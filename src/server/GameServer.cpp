#include "server/GameServer.h"

#include "core/util/Log.h"
#include "net/Protocol.h"

namespace kot::server {

GameServer::GameServer(QObject* parent) : QObject(parent) {
    connect(&m_server, &QTcpServer::newConnection, this, &GameServer::onNewConnection);
}

bool GameServer::start(quint16 port) {
    if (!m_server.listen(QHostAddress::Any, port)) {
        qCCritical(lcKotServer) << "Cannot listen on port" << port << ":"
                                << m_server.errorString();
        return false;
    }
    qCInfo(lcKotServer) << "Listening on port" << m_server.serverPort();
    return true;
}

void GameServer::onNewConnection() {
    while (m_server.hasPendingConnections()) {
        QTcpSocket* socket = m_server.nextPendingConnection();
        m_clients.append(socket);
        qCInfo(lcKotServer) << "Client connected from" << socket->peerAddress().toString()
                            << "- clients:" << m_clients.size();

        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_clients.removeAll(socket);
            socket->deleteLater();
            qCInfo(lcKotServer) << "Client disconnected - clients:" << m_clients.size();
            emit clientCountChanged(clientCount());
        });

        socket->write((net::serverGreeting() + QLatin1Char('\n')).toUtf8());
        emit clientCountChanged(clientCount());
    }
}

}  // namespace kot::server
