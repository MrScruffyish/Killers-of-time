#include "net/Protocol.h"

#include "core/Version.h"

namespace kot::net {

QString serverGreeting() {
    return QStringLiteral("KOT/%1 %2 %3")
        .arg(kProtocolVersion)
        .arg(QString::fromLatin1(core::kGameName), QString::fromLatin1(core::kGameVersion));
}

}  // namespace kot::net
