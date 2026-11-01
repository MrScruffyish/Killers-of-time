#pragma once

#include <QMainWindow>
#include <QTcpSocket>

class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace kot::client {

// M0: a window with a connect box and a log, to test the whole toolchain.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onConnectClicked();
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);

private:
    void log(const QString& text);
    void updateButton();

    QLineEdit* m_host = nullptr;
    QSpinBox* m_port = nullptr;
    QPushButton* m_connectButton = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QTcpSocket m_socket;
};

}  // namespace kot::client
