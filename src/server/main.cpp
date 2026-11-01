#include <QCommandLineParser>
#include <QCoreApplication>

#include "core/Version.h"
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
    parser.addOption(portOption);
    parser.process(app);

    bool ok = false;
    const uint port = parser.value(portOption).toUInt(&ok);
    if (!ok || port == 0 || port > 65535) {
        qCritical("Invalid port: %s", qPrintable(parser.value(portOption)));
        return 2;
    }

    kot::server::GameServer server;
    if (!server.start(static_cast<quint16>(port))) {
        return 1;
    }
    return app.exec();
}
