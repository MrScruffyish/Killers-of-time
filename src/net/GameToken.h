#pragma once

#include <QString>

namespace kot::net {

// Game tokens: the long random secret a client uses on one server (made once per
// server and kept in its settings, or typed in on another computer). The server keeps
// only its SHA-256 (with the seats the client plays), so a save does not reveal it.
inline constexpr int kGameTokenHexDigits = 64;  // 256 random bits

QString newGameToken();  // 64 random hex digits
// A token as typed: spaces and dashes are ignored, letters lowercased.
QString normalizeGameToken(const QString& text);
bool looksLikeGameToken(const QString& token);  // exactly 64 hex digits
// SHA-256 of the token as hex; empty for an empty token.
QString hashGameToken(const QString& token);

}  // namespace kot::net
