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
#include "server/ServerConfig.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kot_server"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(kot::core::versionText()));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Killers of Time - game server"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption configOption(QStringList{QStringLiteral("c"), QStringLiteral("config")},
                                    QStringLiteral("Config file (default: %1 when it exists). The options below override it.")
                                        .arg(kot::server::ServerConfig::defaultPath()),
                                    QStringLiteral("file"));
    QCommandLineOption portOption(QStringList{QStringLiteral("p"), QStringLiteral("port")},
                                  QStringLiteral("TCP port to listen on (default %1).").arg(kot::net::kDefaultPort),
                                  QStringLiteral("port"));
    QCommandLineOption dataOption(QStringList{QStringLiteral("d"), QStringLiteral("data")},
                                  QStringLiteral("Game data directory (default: found automatically)."),
                                  QStringLiteral("dir"));
    QCommandLineOption savesOption(QStringList{QStringLiteral("s"), QStringLiteral("saves")},
                                   QStringLiteral("Folder for saved games (default: the user's app data folder)."),
                                   QStringLiteral("dir"));
    QCommandLineOption savesMaxOption(QStringLiteral("saves-max-mb"),
                                      QStringLiteral("Most space the save folder may use, in MB; the oldest saves are removed to make room (0 = no limit)."),
                                      QStringLiteral("mb"));
    QCommandLineOption resumeOption(QStringLiteral("resume"),
                                    QStringLiteral("Go on with the newest saved game (after a restart): it waits for the players, "
                                                   "who get their seats back when they connect."));
    parser.addOption(resumeOption);
    parser.addOption(configOption);
    parser.addOption(savesMaxOption);
    parser.addOption(portOption);
    parser.addOption(dataOption);
    parser.addOption(savesOption);
    parser.process(app);

    // The config file, then the command line on top.
    kot::server::ServerConfig config;
    {
        QStringList problems;
        const bool asked = parser.isSet(configOption);
        const QString path = asked ? parser.value(configOption) : kot::server::ServerConfig::defaultPath();
        if (!config.load(path, asked, &problems)) {
            qCritical("Bad config file:\n%s", qPrintable(problems.join(QLatin1Char('\n'))));
            return 2;
        }
    }
    bool ok = true;
    if (parser.isSet(portOption)) config.port = parser.value(portOption).toInt(&ok);
    if (!ok || config.port < 0 || config.port > 65535) {
        qCritical("Invalid port: %s", qPrintable(parser.value(portOption)));
        return 2;
    }
    const quint16 port = config.port > 0 ? static_cast<quint16>(config.port) : kot::net::kDefaultPort;
    if (parser.isSet(dataOption)) config.dataDir = parser.value(dataOption);
    if (parser.isSet(savesOption)) config.saveDir = parser.value(savesOption);
    if (parser.isSet(resumeOption)) config.resume = true;
    if (parser.isSet(savesMaxOption)) config.savesMaxMB = parser.value(savesMaxOption).toInt(&ok);
    if (!ok || config.savesMaxMB < 0) {
        qCritical("Invalid --saves-max-mb: %s", qPrintable(parser.value(savesMaxOption)));
        return 2;
    }

    const QString dataDir = kot::core::findDataDirectory(config.dataDir);
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

    kot::server::GameServer server(content.playableRaceKeys(), kot::ai::aiProfileNames());
    server.setContent(&content);
    const QString saves = !config.saveDir.isEmpty()
        ? config.saveDir
        : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/saves");
    server.setSaveDir(saves);
    server.setSaveLimitBytes(qint64(config.savesMaxMB) * 1024 * 1024);
    qCInfo(lcKotServer) << "Saved games go to" << saves
                        << (config.savesMaxMB > 0 ? QStringLiteral("(at most %1 MB)").arg(config.savesMaxMB) : QStringLiteral("(no size limit)"));
    if (!server.start(port)) return 1;
    if (config.resume) {
        QString error;
        if (server.resumeNewestSave(&error)) qCInfo(lcKotServer) << "Resumed the newest saved game; waiting for the players";
        else qCWarning(lcKotServer) << "Nothing to resume:" << error;
    }
    return app.exec();
}
