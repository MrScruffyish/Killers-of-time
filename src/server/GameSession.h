#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <memory>

#include "ai/IAiPlayer.h"
#include "core/battle/Battle.h"
#include "core/rules/Movement.h"

#include "core/model/GameState.h"
#include "net/LobbyTypes.h"
#include "net/Message.h"

namespace kot::core { class ContentRegistry; }

namespace kot::server {

struct CommandResult {
    bool ok = false;
    QString code;      // on failure: "not_your_turn", "bad_request", "failed", ...
    QString message;
    QJsonObject info;  // on success: details for the sender (what happened)
};

// One running game on the server. Owns the GameState, checks every command of every
// player (right turn, own things), applies it with the rules in core/rules, and
// builds each player's filtered view. AI players are played by their AI profile
// (ai/AiFactory.h) through the same commands; players who left by the passive AI.
class GameSession {
public:
    explicit GameSession(const core::ContentRegistry& content);

    // Creates the game from the finished lobby. On failure `error` says why.
    bool start(const net::LobbyState& lobby, QString* error);
    // Where campaign progress is kept (the server's saves folder).
    void setCampaignDir(const QString& dir) { m_campaignDir = dir; }
    // A scenario or campaign file ("scenarios"/"campaigns"): the game's data, else the generated ones.
    QString contentFile(const QString& kind, const QString& id) const;
    const QString& campaignId() const { return m_campaign; }  // empty: not a campaign
    int campaignChapter() const { return m_campaignChapter; }  // 0-based
    // Continues a saved game. AI players get their AI profile again; every human
    // player starts free and the game waits on his turn until someone takes him
    // over (joinPlayer). Players who leave later are skipped as before.
    void startFromState(core::GameState state);
    bool active() const { return m_active; }

    core::GameState& state() { return m_state; }
    const core::GameState& state() const { return m_state; }
    // The player a client acts for now: of his players (more than one in hot-seat),
    // the one whose stack acts in a battle, else the one whose turn it is, else his first.
    core::PlayerId playerOfClient(int clientId) const;
    QList<core::PlayerId> playersOfClient(int clientId) const { return m_clientPlayers.value(clientId); }
    QList<int> clients() const { return m_clientPlayers.keys(); }

    CommandResult handle(int clientId, const net::Message& command);
    // A human left: his player is now played by the passive AI (the game goes on).
    void clientLeft(int clientId);
    // Human players nobody plays right now (left, or not taken yet after loading).
    QList<core::PlayerId> freePlayers() const;
    // A client takes over a free human player (more than one: hot-seat). Fills `error` when that is not possible.
    bool joinPlayer(int clientId, core::PlayerId player, QString* error);

    // True while a battle waits for a human; only battle_action is accepted then.
    bool battleRunning() const { return m_battle != nullptr; }

    QJsonObject viewFor(core::PlayerId player) const;
    QJsonObject youInfo(core::PlayerId player) const;

private:
    // An attack whose battles are being fought, one defender after the other.
    struct ActiveBattle {
        core::PendingAttack attack;
        int defenderIndex = 0;
        std::unique_ptr<core::Battle> battle;  // the current one
        bool autoSide[2] = {false, false};     // a human pressed "auto"
        core::MoveResult result;               // the move, plus the reports so far
    };

    CommandResult dispatch(core::PlayerId player, const net::Message& command);
    CommandResult battleAction(core::PlayerId player, const net::Message& command);
    // Plays the AI sides and starts the next battle until a human has to act or
    // the attack is over (then m_battle is reset and m_finishedMove is set).
    void runBattles();
    bool humanControls(int side) const;
    QJsonObject moveInfo(const core::MoveResult& move) const;
    void recordBattle(const core::BattleReport& report);
    void afterCommand();
    // Plays the turns of AI players (and players who left) until a human is next.
    void playAutoTurns();
    void playAiTurn(core::PlayerId player, ai::IAiPlayer& ai);
    bool isAuto(core::PlayerId p) const { return m_autoPlayers.contains(p); }

    const core::ContentRegistry& m_content;
    core::GameState m_state;
    bool m_active = false;
    QHash<int, QList<core::PlayerId>> m_clientPlayers;  // connected humans (several: hot-seat)
    bool clientPlays(core::PlayerId player) const;
    QHash<core::PlayerId, std::shared_ptr<ai::IAiPlayer>> m_autoPlayers;  // AI and abandoned players
    std::unique_ptr<ActiveBattle> m_battle;
    core::MoveResult m_finishedMove;  // the last attack that ended
    int m_battleCounter = 0;
    QHash<core::PlayerId, QJsonObject> m_lastBattle;  // per participant: {id, report}
    QString m_campaignDir;
    QJsonArray m_aiEvents;
    // Monsters waiting for the player's answer to their offer to join.
    struct JoinOffer {
        core::PlayerId player;
        core::HeroId hero;
        core::ArmyId army;
    };
    JoinOffer m_joinOffer;  // battle events of the computer's stacks since the last human action
    QString m_campaign;          // campaign being played (empty: none)
    int m_campaignChapter = 0;
    int m_campaignChapters = 0;
    QString m_campaignName;
    bool m_campaignRecorded = false;
    QString m_campaignResult;    // "won" / "lost" when the game ended
    core::PlayerId m_campaignPlayer;  // the first human: his heroes carry over
    void recordCampaign();
};

}  // namespace kot::server
