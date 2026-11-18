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

    // `gameToken` empty: use the one this computer keeps for that server (make one the
    // first time). A token typed in is kept for the server, so it can be used again.
    // `hotSeat`: the lobby starts with a second human at this computer (first connect only).
    // `loadSavedGame`: open the saved-game list once the lobby is up.
    void connectToServer(const QString& host, quint16 port, const QString& name, const QString& gameToken = QString(),
                         bool tls = false, bool hotSeat = false, bool loadSavedGame = false);
    // Starts kot_server (next to this program) and joins it. `hotSeat`: several humans
    // take turns at this computer. `loadSavedGame`: open the saved-game list in the lobby.
    void hostLocalGame(quint16 port, const QString& name, bool tls = false, bool hotSeat = false,
                       bool loadSavedGame = false);

    QString currentPage() const;  // "connect", "lobby" or "game"
    net::LobbyClient& client() { return m_client; }
    const core::ContentRegistry& content() const { return m_content; }
    LobbyPage* lobbyPage() const { return m_lobby; }
    GamePage* gamePage() const { return m_game; }

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void showConnect(const QString& message, bool error);
    // The setup pages (connect, lobby) and the game keep their own window size:
    // a maximized game does not make the setup huge next time.
    void useWindowSize(bool game);
    // Keep the setup pages a normal, centred window instead of a restored full screen.
    void fitSetupWindow();
    bool m_gameSize = false;
    bool m_sizeRestored = false;
    bool m_loadAfterConnect = false;  // this connection was started from "Load game"
    int m_startLoadSeq = 0;  // reply to our list_saves, showing the list on the start screen

    core::ContentRegistry m_content;
    const core::ContentRegistry* m_previousArtContent = nullptr;  // restored when this window closes
    bool m_contentLoaded = false;
    net::LobbyClient m_client;
    QProcess m_serverProcess;
    QStackedWidget* m_pages;
    ConnectPage* m_connect;
    LobbyPage* m_lobby;
    GamePage* m_game = nullptr;
};

}  // namespace kot::client
