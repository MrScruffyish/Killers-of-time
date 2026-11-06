#include <QApplication>
#include <QCommandLineParser>
#include <QProxyStyle>

#include "client/Art.h"
#include "client/CrashLog.h"
#include "client/MainWindow.h"
#include "client/Sound.h"
#include "core/Version.h"

namespace {
// Tooltips (the yellow labels) after a quarter second instead of Qt's 0.7 s.
class QuickTips : public QProxyStyle {
public:
    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* ret) const override {
        if (hint == SH_ToolTip_WakeUpDelay) return 250;
        return QProxyStyle::styleHint(hint, option, widget, ret);
    }
};
}  // namespace

int main(int argc, char* argv[]) {
    kot::client::installCrashLog();  // backtraces of crashes go to ~/.local/share/KillersOfTime/crash.log
    QApplication app(argc, argv);
    // Settings live in ~/.config/KillersOfTime/client.conf (client/Settings.h).
    QApplication::setOrganizationName(QStringLiteral("KillersOfTime"));
    QApplication::setApplicationName(QStringLiteral("client"));
    QApplication::setApplicationVersion(QString::fromLatin1(kot::core::versionText()));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Killers of Time - the game"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption noSound(QStringLiteral("no-sound"), QStringLiteral("No sound at all (the audio system is not opened)."));
    parser.addOption(noSound);
    parser.process(app);
    // --no-sound (or KOT_NO_SOUND=1): never touch the audio system.
    if (parser.isSet(noSound)) kot::client::sound::disableForThisRun();
    app.setStyle(new QuickTips);  // the application owns it
    app.setStyleSheet(kot::client::art::styleSheet());
    kot::client::MainWindow window;
    window.show();
    return app.exec();
}
