#pragma once

#include <QString>
#include <QtGlobal>

namespace kot::net {

inline constexpr int kProtocolVersion = 2;  // 2: compressed frames
inline constexpr quint16 kDefaultPort = 45454;
// A frame is a 4-byte big-endian length followed by that many bytes of UTF-8 JSON.
inline constexpr quint32 kMaxFrameBytes = 16 * 1024 * 1024;
// Frames whose JSON is at least this big are sent zlib-compressed; the top bit of the
// frame length marks them (docs/protocol.md).
inline constexpr int kCompressFromBytes = 1024;
inline constexpr quint32 kCompressedFlag = 0x80000000u;

// Message types (the "type" field of every message). See docs/protocol.md.
namespace msg {
// client -> server
inline const QString hello = QStringLiteral("hello");
inline const QString claimSlot = QStringLiteral("claim_slot");
inline const QString releaseSlot = QStringLiteral("release_slot");
inline const QString setSlot = QStringLiteral("set_slot");
inline const QString setReady = QStringLiteral("set_ready");
inline const QString setSettings = QStringLiteral("set_settings");
inline const QString startGame = QStringLiteral("start_game");
inline const QString ping = QStringLiteral("ping");
// client -> server, during the game (all except end_turn need your turn)
inline const QString endTurn = QStringLiteral("end_turn");
inline const QString moveHero = QStringLiteral("move_hero");
inline const QString build = QStringLiteral("build");
inline const QString recruit = QStringLiteral("recruit");
inline const QString recruitAtDwelling = QStringLiteral("recruit_at_dwelling");
inline const QString hireHero = QStringLiteral("hire_hero");
inline const QString trade = QStringLiteral("trade");
inline const QString moveUnits = QStringLiteral("move_units");
inline const QString answerJoin = QStringLiteral("answer_join");  // accept / fight / leave monsters offering to join
inline const QString pickTrait = QStringLiteral("pick_trait");
inline const QString equipArtifact = QStringLiteral("equip_artifact");      // wear one from the backpack
inline const QString unequipArtifact = QStringLiteral("unequip_artifact");  // take one off into the backpack
inline const QString giveArtifact = QStringLiteral("give_artifact");        // to a hero side by side
inline const QString buyAllUnits = QStringLiteral("buy_all_units");
inline const QString autoBuild = QStringLiteral("auto_build");
inline const QString buildThenBuy = QStringLiteral("build_then_buy");
inline const QString sendDwellingUnits = QStringLiteral("send_dwelling_units");
inline const QString castSpell = QStringLiteral("cast_spell");  // adventure-map spell
inline const QString buyBoat = QStringLiteral("buy_boat");
inline const QString battleAction = QStringLiteral("battle_action");  // the battle's active stack (any turn)
// client -> server: saved games and taking over players (any time after hello)
inline const QString saveGame = QStringLiteral("save_game");      // a player in the running game
inline const QString listSaves = QStringLiteral("list_saves");
inline const QString loadGame = QStringLiteral("load_game");      // the host, in the lobby
inline const QString generateMap = QStringLiteral("generate_map");  // the host makes a random scenario or campaign
inline const QString contentLists = QStringLiteral("content_lists");  // server: the scenarios and campaigns changed
inline const QString joinGame = QStringLiteral("join_game");      // take over a free human player
// server -> client
inline const QString welcome = QStringLiteral("welcome");
inline const QString lobbyState = QStringLiteral("lobby_state");
inline const QString gameStarting = QStringLiteral("game_starting");
inline const QString error = QStringLiteral("error");
inline const QString pong = QStringLiteral("pong");
inline const QString gameStarted = QStringLiteral("game_started");   // you + your view
inline const QString gameLoaded = QStringLiteral("game_loaded");     // freePlayers to choose from
inline const QString gameState = QStringLiteral("game_state");       // your view, after every change
inline const QString commandResult = QStringLiteral("command_result");  // replyTo + info
}  // namespace msg

}  // namespace kot::net
