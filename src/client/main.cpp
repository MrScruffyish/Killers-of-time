#include <QApplication>

#include "client/Art.h"
#include "client/MainWindow.h"
#include "core/Version.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("kot_client"));
    QApplication::setApplicationVersion(QString::fromLatin1(kot::core::kGameVersion));

    app.setStyleSheet(kot::client::art::styleSheet());
    kot::client::MainWindow window;
    window.show();
    return app.exec();
}
