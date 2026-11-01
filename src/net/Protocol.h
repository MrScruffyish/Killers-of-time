#pragma once

#include <QString>
#include <QtGlobal>

namespace kot::net {

inline constexpr int kProtocolVersion = 1;
inline constexpr quint16 kDefaultPort = 45454;

// First line a server sends to a new connection, e.g. "KOT/1 Killers of Time 0.1.0".
QString serverGreeting();

}  // namespace kot::net
