#pragma once

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QWidget>

#include "client/ViewModel.h"
#include "net/LobbyClient.h"

class QComboBox;
class QJsonArray;
class QStackedWidget;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace kot::core { class ContentRegistry; }

namespace kot::client {

class BattleView;

class MapView;
class TownDialog;

// The game screen: map in the middle, status on top, hero and towns on the right,
// the one-click actions as buttons, and a log of what happened.
class GamePage : public QWidget {
    Q_OBJECT
public:
    GamePage(net::LobbyClient* client, const core::ContentRegistry* content, QWidget* parent = nullptr);

    const GameView& view() const { return m_view; }
    int selectedHero() const { return m_selectedHero; }
    MapView* mapView() const { return m_map; }
    BattleView* battleView() const { return m_battleView; }
    TownDialog* townDialog() const { return m_townDialog; }
    QString logText() const;

private slots:
    void onViewChanged();
    void onTileClicked(core::Pos tile);
    void onTileRightClicked(core::Pos tile);

private:
    struct Pending {
        QString type;
        QString what;
    };

    void send(const QString& type, const QJsonObject& body, const QString& what);
    void log(const QString& text);
    void updateStatus();
    void updateHeroPanel();
    void selectHero(int id);
    QString unitName(const QString& id) const;
    void announceEnd();  // victory / defeat messages, once each
    QString describeBattle(const QJsonObject& battle) const;
    QString describeBattleEvents(const QJsonArray& events) const;
    void showBattle(bool firstView);
    QString describeResult(const QString& type, const QJsonObject& info) const;
    void chooseTrait();

    net::LobbyClient* m_client;
    const core::ContentRegistry* m_content;
    GameView m_view;
    int m_selectedHero = 0;
    bool m_announcedGameOver = false;
    bool m_announcedDefeat = false;
    QSet<int> m_announcedOut;
    bool m_centered = false;
    QHash<int, Pending> m_pending;

    QLabel* m_status;
    QStackedWidget* m_centre;
    MapView* m_map;
    BattleView* m_battleView;
    int m_seenBattleId = 0;
    QComboBox* m_heroCombo;
    QLabel* m_heroInfo;
    QPushButton* m_levelUp;
    QListWidget* m_townList;
    QPushButton* m_endTurn;
    QPushButton* m_buildAndBuy;
    QPushButton* m_buyAll;
    QPushButton* m_sendUnits;
    QPlainTextEdit* m_log;
    TownDialog* m_townDialog;
};

}  // namespace kot::client
