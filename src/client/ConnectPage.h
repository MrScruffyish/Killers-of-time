#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QSpinBox;

namespace kot::client {

// First screen: who you are and which server to join, or host a game here.
class ConnectPage : public QWidget {
    Q_OBJECT
public:
    explicit ConnectPage(QWidget* parent = nullptr);
    void showMessage(const QString& text, bool error);

signals:
    void joinRequested(const QString& host, quint16 port, const QString& name);
    void hostRequested(quint16 port, const QString& name);

private:
    QLineEdit* m_name;
    QLineEdit* m_host;
    QSpinBox* m_port;
    QLabel* m_message;
};

}  // namespace kot::client
