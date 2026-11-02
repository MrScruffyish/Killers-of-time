#include <QCommandLineParser>
#include <QCoreApplication>
#include <QStandardPaths>

#include "ai/AiFactory.h"
#include "core/Version.h"
#include "core/content/ContentRegistry.h"
#include "core/data/DataDir.h"
#include "core/util/Log.h"
#include "net/Protocol.h"
#include "server/GameServer.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kot_server"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(kot::core::kGameVersion));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Killers of Time - game server"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption portOption(QStringList{QStringLiteral("p"), QStringLiteral("port")},
                                  QStringLiteral("TCP port to listen on."),
                                  QStringLiteral("port"),
                                  QString::number(kot::net::kDefaultPort));
    QCommandLineOption dataOption(QStringList{QStringLiteral("d"), QStringLiteral("data")},
                                  QStringLiteral("Game data directory (default: found automatically)."),
                                  QStringLiteral("dir"));
    QCommandLineOption savesOption(QStringList{QStringLiteral("s"), QStringLiteral("saves")},
                                   QStringLiteral("Folder for saved games (default: the user's app data folder)."),
                                   QStringLiteral("dir"));
    parser.addOption(portOption);
    parser.addOption(dataOption);
    parser.addOption(savesOption);
    parser.process(app);

    bool ok = false;
    const uint port = parser.value(portOption).toUInt(&ok);
    if (!ok || port == 0 || port > 65535) {
        qCritical("Invalid port: %s", qPrintable(parser.value(portOption)));
        return 2;
    }

    const QString dataDir = kot::core::findDataDirectory(parser.value(dataOption));
    if (dataDir.isEmpty()) {
        qCritical("Cannot find the data directory. Use --data <dir> or set KOT_DATA_DIR.");
        return 2;
    }
    kot::core::ContentRegistry content;
    QStringList errors;
    if (!content.loadFromDirectory(dataDir, &errors)) {
        qCritical("The game data in %s is invalid:\n%s", qPrintable(dataDir),
                  qPrintable(errors.join(QLatin1Char('\n'))));
        return 2;
    }
    qCInfo(lcKotServer) << "Loaded game data from" << dataDir;

    kot::server::GameServer server(content.raceIds(), kot::ai::aiProfileNames());
    server.setContent(&content);
    const QString saves = parser.isSet(savesOption)
        ? parser.value(savesOption)
        : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/saves");
    server.setSaveDir(saves);
    qCInfo(lcKotServer) << "Saved games go to" << saves;
    if (!server.start(static_cast<quint16>(port))) return 1;
    return app.exec();
}
