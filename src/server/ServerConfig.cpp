#include "server/ServerConfig.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "core/data/JsonReader.h"

namespace kot::server {

QString ServerConfig::defaultPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/KillersOfTime/server.json");
}

bool ServerConfig::load(const QString& path, bool mustExist, QStringList* errors) {
    QFile file(path);
    if (!file.exists()) {
        if (mustExist && errors) *errors << QStringLiteral("%1: no such config file").arg(path);
        return !mustExist;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (errors) *errors << QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errors) *errors << QStringLiteral("%1: not a JSON object (%2)").arg(path, parseError.errorString());
        return false;
    }
    QStringList problems;
    core::JsonReader r(doc.object(), path, &problems);
    port = r.optionalInt(QStringLiteral("port"), port, 1, 65535);
    dataDir = r.optionalString(QStringLiteral("data"), dataDir);
    saveDir = r.optionalString(QStringLiteral("saves"), saveDir);
    savesMaxMB = r.optionalInt(QStringLiteral("savesMaxMB"), savesMaxMB, 0, 10000000);
    resume = r.optionalBool(QStringLiteral("resume"), resume);
    r.finish();
    if (errors) *errors << problems;
    return problems.isEmpty();
}

}  // namespace kot::server
