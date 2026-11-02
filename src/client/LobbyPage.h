#pragma once

#include <QWidget>

#include "net/LobbyClient.h"

class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace kot::client {

// The game setup screen: slots with race, team and colour, ready flags, map settings.
class LobbyPage : public QWidget {
    Q_OBJECT
public:
    explicit LobbyPage(net::LobbyClient* client, QWidget* parent = nullptr);

signals:
    void leaveRequested();

private slots:
    void rebuild();

private:
    void showError(const QString& text);

    net::LobbyClient* m_client;
    QWidget* m_slotHolder;
    QGridLayout* m_grid = nullptr;
    QSpinBox* m_width;
    QSpinBox* m_height;
    QLineEdit* m_seed;
    QPushButton* m_start;
    QLabel* m_status;
};

}  // namespace kot::client
