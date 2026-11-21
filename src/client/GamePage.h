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
class MiniMap;
class HeroDialog;

class MapView;
class TownDialog;

// The game screen: map in the middle, status on top, hero and towns on the right,
// the one-click actions as buttons, and a log of what happened.
class GamePage : public QWidget {
    Q_OBJECT
public:
    GamePage(net::LobbyClient* client, const core::ContentRegistry* content, QWidget* parent = nullptr);

    const GameView& view() const { return m_view; }
    // Messages about mines, towns and dwellings that changed owner between two
    // views (enemy turns are resolved in one batch, so the log must say it).
    static QStringList ownershipChanges(const GameView& before, const GameView& after);
    int selectedHero() const { return m_selectedHero; }
    MapView* mapView() const { return m_map; }
    BattleView* battleView() const { return m_battleView; }
    HeroDialog* heroDialog() const { return m_heroDialog; }
    class MeetDialog* meetDialog() const { return m_meetDialog; }
    TownDialog* townDialog() const { return m_townDialog; }
    QString logText() const;

protected:
    void resizeEvent(QResizeEvent* event) override;

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
    void openMarket();  // M
    void showHelp();    // F1
    void showGoal();    // G: read the scenario goal / defeat rule / story again
    void leaveGame();   // host: abandon without saving and go back to game setup
    void showKingdoms();  // K: every player's towns, heroes, mines and army strength
    void selectTown(int id);  // T cycles: the map looks there
    // Ctrl+S (and the Game menu): opens the save dialog.
    void saveGame();
    // B: the next town that can build today, opened on its build screen; a note
    // when there is none (nothing to confirm).
    void openNextTownToBuild();
    // Moves the selected hero along his plotted route (Enter, double click).
    void goAlongRoute();
    // Keeps the plotted routes in step with the server's remembered destinations.
    void syncRoutes();
    bool anyHeroCanMove() const;
    void updateEndTurnHighlight();
    void announceLevelUps(bool firstView);
    void offerToJoin(const QJsonObject& offer);  // monsters offering to join: accept / fight / leave
    void offerChest(const QJsonObject& offer);   // a chest: the gold or the experience
    void showGameToken();                        // this client's token for this server: copy it
    QString unitName(core::UnitTypeId id) const;
    void announceEnd();
    void rememberObjects(GameView* view);
    void selectNextHero();
    void castAdventureSpell();  // victory / defeat messages, once each
    QString describeBattle(const QJsonObject& battle) const;
    QString describeBattleEvents(const QJsonArray& events) const;
    void showBattle(bool firstView);
    // Hold the battle screen until the final events finish, then show the result.
    void finishBattleResult();
    void announceBattleResult(const QJsonObject& report);
    QString describeResult(const QString& type, const QJsonObject& info) const;

    net::LobbyClient* m_client;
    const core::ContentRegistry* m_content;
    GameView m_view;
    int m_selectedHero = 0;
    bool m_announcedGameOver = false;
    bool m_announcedDefeat = false;
    QSet<int> m_announcedOut;
    bool m_centered = false;
    QHash<int, Pending> m_pending;

    class TextLine* m_status;
    class TextLine* m_hoverInfo;
    class WeekBar* m_week;
    class ResourceBar* m_resources;
    class HeroPanel* m_heroPanel;
    class MarketDialog* m_market = nullptr;
    QStackedWidget* m_centre;
    MapView* m_map;
    HeroDialog* m_heroDialog;
    class MeetDialog* m_meetDialog = nullptr;
    MiniMap* m_miniMap;
    QLabel* m_players;  // who is who: a coloured mark per player
    // What each player (several in hot-seat) remembers of places out of sight.
    struct Memory {
        QList<ViewTown> towns;
        QList<ViewMine> mines;
        QList<ViewDwelling> dwellings;
        QList<ViewObject> objects;
    };
    QHash<int, Memory> m_memory;
    QWidget* m_curtain = nullptr;  // hot-seat: hides the game while it is handed over
    void showHandover(const QString& name, const QColor& colour);
    QPushButton* m_castSpell;
    class QAction* m_save;
    class QAction* m_leave = nullptr;
    BattleView* m_battleView;
    int m_seenBattleId = 0;
    int m_newsDay = 0;  // the day whose story news were shown
    int m_announcedWeek = 0;
    int m_selectedTown = 0;
    QHash<int, int> m_knownPicks;  // hero -> level-up picks seen so far
    QHash<int, core::Pos> m_routes;                     // hero -> plotted target
    QHash<int, std::optional<core::Pos>> m_serverRoutes;  // hero -> last destination the server reported
    class QTimer* m_pulse = nullptr;
    bool m_pulseOn = false;
    // The last battle's result waits for its animation so the final blow is seen.
    class QTimer* m_resultTimer = nullptr;
    int m_resultElapsed = 0;
    bool m_pendingResult = false;
    QJsonObject m_pendingReport;
    // A level-up must wait until the battle result is dismissed: otherwise its
    // window covers the result and the battle field is still on screen behind it.
    bool m_levelUpsDeferred = false;
    int m_openResultDialogs = 0;
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
