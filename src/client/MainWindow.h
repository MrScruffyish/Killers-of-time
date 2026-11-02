#pragma once

#include <QMainWindow>
#include <QProcess>

#include "core/content/ContentRegistry.h"
#include "net/LobbyClient.h"

class QStackedWidget;

namespace kot::client {

class ConnectPage;
class GamePage;
class LobbyPage;

// Connect screen -> lobby -> game. Owns the game content and the connection.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void connectToServer(const QString& host, quint16 port, const QString& name);
    // Starts kot_server (next to this program) and joins it.
    void hostLocalGame(quint16 port, const QString& name);

    QString currentPage() const;  // "connect", "lobby" or "game"
    net::LobbyClient& client() { return m_client; }
    const core::ContentRegistry& content() const { return m_content; }
    LobbyPage* lobbyPage() const { return m_lobby; }
    GamePage* gamePage() const { return m_game; }

private:
    void showConnect(const QString& message, bool error);

    core::ContentRegistry m_content;
    bool m_contentLoaded = false;
    net::LobbyClient m_client;
    QProcess m_serverProcess;
    QStackedWidget* m_pages;
    ConnectPage* m_connect;
    LobbyPage* m_lobby;
    GamePage* m_game = nullptr;
};

}  // namespace kot::client
