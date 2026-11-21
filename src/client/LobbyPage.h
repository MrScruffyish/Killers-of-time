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
    // Opens the saved-game list (the same as the Load game button).
    void requestLoad();
    // The next lobby this client hosts starts as a hot-seat game: a second human
    // (Player 2) sits at this computer instead of the default AI opponent.
    void setHotSeatSetup(bool on);
    // Campaigns are a solo mode: offer the campaign/map choice only for a local solo host.
    void setCampaignAllowed(bool on) { m_campaignAllowed = on; }

signals:
    void leaveRequested();

private slots:
    void rebuild();

private:
    void makeMap(bool campaignTab);

    net::LobbyClient* m_client;
    const core::ContentRegistry* m_content;
    QWidget* m_slotHolder;
    QWidget* m_modeHolder;      // campaign/map choice (solo games only)
    QWidget* m_mapHolder;       // random / pre-built map picker
    QWidget* m_campaignHolder;  // campaign picker (solo only)
    QWidget* m_setupRows;       // map rules, hidden in campaign mode
    QLabel* m_campaignNote;
    QGridLayout* m_grid = nullptr;
    QComboBox* m_mapSize;  // named sizes (data: the side length); "custom" for other sizes
    QLineEdit* m_seed;
    QComboBox* m_difficulty;
    QComboBox* m_defeat;  // when a player is out
    QComboBox* m_mapType;
    QSpinBox* m_autosaves;
    QSpinBox* m_neutralTowns;       // free towns to conquer
    QSpinBox* m_mapDwellings;       // dwellings on the map
    QSpinBox* m_mines;              // neutral mines
    QSpinBox* m_wanderingMonsters;  // roaming enemy armies
    QCheckBox* m_fog;
    QCheckBox* m_hidden;
    QPushButton* m_makeMap;
    int m_makeSeq = 0;
    QJsonObject m_makeParams;
    bool m_defaultOpponentAdded = false;
    bool m_hotSeatSetup = false;
    QLabel* m_versions;
    QPushButton* m_modeMap;       // the two solo choices: large checkable cards
    QPushButton* m_modeCampaign;
    QComboBox* m_scenario;
    QComboBox* m_campaignChoice;
    QLabel* m_scenarioInfo;
    bool m_campaignAllowed = false;
    QPushButton* m_start;
    QPushButton* m_load;
    int m_listSavesSeq = 0;
    QLabel* m_status;
};

}  // namespace kot::client
