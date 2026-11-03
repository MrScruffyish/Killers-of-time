#include <QApplication>

#include "client/Art.h"
#include "client/CrashLog.h"
#include "client/MainWindow.h"
#include "client/Sound.h"
#include "core/Version.h"

int main(int argc, char* argv[]) {
    kot::client::installCrashLog();  // backtraces of crashes go to ~/.local/share/KillersOfTime/crash.log
    QApplication app(argc, argv);
    // Settings live in ~/.config/KillersOfTime/client.conf (client/Settings.h).
    QApplication::setOrganizationName(QStringLiteral("KillersOfTime"));
    QApplication::setApplicationName(QStringLiteral("client"));
    QApplication::setApplicationVersion(QString::fromLatin1(kot::core::kGameVersion));

    // --no-sound (or KOT_NO_SOUND=1): never touch the audio system.
    if (QCoreApplication::arguments().contains(QStringLiteral("--no-sound"))) kot::client::sound::disableForThisRun();
    app.setStyleSheet(kot::client::art::styleSheet());
    kot::client::MainWindow window;
    window.show();
    return app.exec();
}
