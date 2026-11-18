#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace kot::client {

// First screen: your name and one of three ways to play: alone against the computer,
// several players at this computer, or a server. The server fields belong to the last.
class ConnectPage : public QWidget {
    Q_OBJECT
public:
    explicit ConnectPage(QWidget* parent = nullptr);
    void showMessage(const QString& text, bool error);

signals:
    void soloRequested(quint16 port, const QString& name, bool tls);
    void hotseatRequested(quint16 port, const QString& name, bool tls);
    void loadRequested(quint16 port, const QString& name, bool tls);
    void joinRequested(const QString& host, quint16 port, const QString& name, const QString& gameToken, bool tls);

private:
    void remember();  // name, server, port and TLS choice go to the settings
    void requestLoad();  // Ctrl+L and the Load game button share this path
    QLineEdit* m_name;
    QLineEdit* m_host;
    QSpinBox* m_port;
    QLineEdit* m_token;
    QCheckBox* m_tls;
    QLabel* m_message;
};

}  // namespace kot::client
