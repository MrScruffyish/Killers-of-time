#pragma once

#include <QWidget>

#include "net/LobbyClient.h"

class QGridLayout;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace kot::core { class ContentRegistry; }

namespace kot::client {

// The game setup screen: slots with race, team and colour, ready flags, map settings.
class LobbyPage : public QWidget {
    Q_OBJECT
public:
    // `content` (optional) gives race names and descriptions.
    LobbyPage(net::LobbyClient* client, const core::ContentRegistry* content, QWidget* parent = nullptr);
    void showError(const QString& text);

signals:
    void leaveRequested();

private slots:
    void rebuild();

private:

    net::LobbyClient* m_client;
    const core::ContentRegistry* m_content;
    QWidget* m_slotHolder;
    QGridLayout* m_grid = nullptr;
    QSpinBox* m_width;
    QSpinBox* m_height;
    QLineEdit* m_seed;
    QComboBox* m_difficulty;
    QComboBox* m_mapType;
    QSpinBox* m_autosaves;
    QCheckBox* m_fog;
    QCheckBox* m_hidden;
    QPushButton* m_makeMap;
    int m_makeSeq = 0;
    QJsonObject m_makeParams;
    bool m_defaultOpponentAdded = false;
    QLabel* m_versions;
    QComboBox* m_scenario;
    QLabel* m_scenarioInfo;
    QPushButton* m_start;
    QPushButton* m_load;
    int m_listSavesSeq = 0;
    QLabel* m_status;
};

}  // namespace kot::client
