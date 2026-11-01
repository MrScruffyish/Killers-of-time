#pragma once

#include <QString>
#include <QtGlobal>

namespace kot::net {

inline constexpr int kProtocolVersion = 1;
inline constexpr quint16 kDefaultPort = 45454;
// A frame is a 4-byte big-endian length followed by that many bytes of UTF-8 JSON.
inline constexpr quint32 kMaxFrameBytes = 16 * 1024 * 1024;

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
// server -> client
inline const QString welcome = QStringLiteral("welcome");
inline const QString lobbyState = QStringLiteral("lobby_state");
inline const QString gameStarting = QStringLiteral("game_starting");
inline const QString error = QStringLiteral("error");
inline const QString pong = QStringLiteral("pong");
}  // namespace msg

}  // namespace kot::net
