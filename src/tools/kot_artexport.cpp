// kot_artexport: draws the game's placeholder art (src/artgen) and saves it as PNG
// files, which is all the client uses (it loads pictures, it never draws them). Every
// picture the content can need is written, at its master size (ArtPaths.h); pictures
// with the owner's colour are saved as <path>.png (base) + <path>.tint.png. Replace
// any file by a better picture of the same size.
//
//   kot_artexport [--data dir] [--out dir]      (default out: <data>/art)
//   kot_artexport --timing draw|load             how long drawing every picture takes,
//                                                or loading every file of <data>/art

#include <QCommandLineParser>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <cstdio>

#include "artgen/ArtGen.h"
#include "core/content/ContentRegistry.h"
#include "core/data/DataDir.h"

using namespace kot;

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");  // no window needed
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Draws the game's art and saves it as PNG files."));
    parser.addHelpOption();
    QCommandLineOption dataOption(QStringLiteral("data"), QStringLiteral("The game data folder."), QStringLiteral("dir"));
    QCommandLineOption outOption(QStringLiteral("out"), QStringLiteral("Where to write the pictures (default <data>/art)."), QStringLiteral("dir"));
    QCommandLineOption timingOption(QStringLiteral("timing"), QStringLiteral("Only time drawing (draw) or loading (load) every picture."),
                                    QStringLiteral("draw|load"));
    parser.addOption(dataOption);
    parser.addOption(outOption);
    parser.addOption(timingOption);
    parser.process(app);

    const QString dataDir = parser.isSet(dataOption) ? parser.value(dataOption) : core::findDataDirectory();
    core::ContentRegistry content;
    QStringList errors;
    if (dataDir.isEmpty() || !content.loadFromDirectory(dataDir, &errors)) {
        std::fprintf(stderr, "kot_artexport: cannot load the data: %s\n", qPrintable(errors.join("; ")));
        return 1;
    }
    const QString out = parser.isSet(outOption) ? parser.value(outOption) : QDir(dataDir).filePath(QStringLiteral("art"));
    QElapsedTimer clock;
    clock.start();

    const QString timing = parser.value(timingOption);
    if (timing == QLatin1String("draw")) {
        artgen::drawAll(content);
        std::printf("kot_artexport: every picture drawn in %.2f s\n", clock.nsecsElapsed() / 1e9);
        return 0;
    }
    if (timing == QLatin1String("load")) {
        int files = 0;
        for (QDirIterator it(out, {QStringLiteral("*.png")}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
            const QImage img(it.next());
            files += !img.isNull();
        }
        std::printf("kot_artexport: %d pictures loaded in %.2f s\n", files, clock.nsecsElapsed() / 1e9);
        return 0;
    }
    if (!timing.isEmpty()) parser.showHelp(2);

    artgen::ExportStats stats;
    QString error;
    if (!artgen::exportAll(content, out, &stats, &error)) {
        std::fprintf(stderr, "kot_artexport: %s\n", qPrintable(error));
        return 1;
    }
    std::printf("kot_artexport: %d pictures in %s (%.1f MB) in %.1f s\n", stats.files, qPrintable(out), stats.bytes / 1048576.0,
                clock.nsecsElapsed() / 1e9);
    return 0;
}
