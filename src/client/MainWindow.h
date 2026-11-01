#pragma once

#include <QMainWindow>

#include "net/LobbyClient.h"

class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace kot::client {

// Temporary window until the real lobby screen: connects to a server and logs
// what the lobby looks like.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onConnectClicked();
    void onLobbyChanged();

private:
    void log(const QString& text);
    void updateButton();

    QLineEdit* m_name = nullptr;
    QLineEdit* m_host = nullptr;
    QSpinBox* m_port = nullptr;
    QPushButton* m_connectButton = nullptr;
    QPlainTextEdit* m_log = nullptr;
    net::LobbyClient m_client;
    bool m_open = false;
};

}  // namespace kot::client
