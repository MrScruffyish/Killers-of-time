#pragma once

#include <QDialog>
#include <QJsonObject>

class QComboBox;
class QSpinBox;
class QLineEdit;
class QCheckBox;
class QTabWidget;
class QVBoxLayout;

namespace kot::core { class ContentRegistry; }

namespace kot::client {

// "Make a map": the settings for a random scenario (size, land and sea, players
// and races, monsters, goal) or a random campaign (chapters, race, carried
// heroes). params() is the generate_map body.
class MapMakerDialog : public QDialog {
    Q_OBJECT
public:
    MapMakerDialog(const QStringList& races, QWidget* parent = nullptr, bool campaignTab = false);
    QJsonObject params() const;

private:
    void rebuildPlayers();

    QStringList m_races;
    QTabWidget* m_tabs;
    // Scenario
    QLineEdit* m_name;
    QComboBox* m_size;  // named sizes, data: the side length
    QComboBox* m_type;
    QSpinBox* m_players;
    QVBoxLayout* m_raceBox;
    QList<QComboBox*> m_raceCombos;
    QComboBox* m_water;
    QComboBox* m_mountains;
    QComboBox* m_rivers;
    QCheckBox* m_roads;
    QComboBox* m_monsters;
    QComboBox* m_goal;
    QComboBox* m_boss;      // used by the "slay" goal
    QComboBox* m_artifact;  // used by the "find" goal
    QSpinBox* m_goalAmount;
    QSpinBox* m_seed;
    // Campaign
    QLineEdit* m_campaignName;
    QSpinBox* m_chapters;
    QComboBox* m_campaignRace;
    QSpinBox* m_firstSize;
    QSpinBox* m_carryHeroes;
    QSpinBox* m_carryMaxLevel;
    QCheckBox* m_campaignIslands;
    QSpinBox* m_campaignSeed;
};

}  // namespace kot::client
