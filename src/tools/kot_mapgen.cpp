// kot_mapgen: makes a random scenario (map, towns, mines, monsters, treasures and the
// players' start) and writes it as data/scenarios/<id>.json, ready for the lobby or
// for kot_editor.
//
//   kot_mapgen --id two_rivers --name "Two Rivers" --races humans,undead [--size 48x48] [--seed 7]

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <cstdio>

#include "core/content/ContentRegistry.h"
#include "core/data/DataDir.h"
#include "core/rules/GameSetup.h"
#include "core/rules/Scenario.h"

using namespace kot::core;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kot_mapgen"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Killers of Time - random scenario maker."));
    parser.addHelpOption();
    QCommandLineOption dataOption(QStringList{"d", "data"}, QStringLiteral("Game data directory."), QStringLiteral("dir"));
    QCommandLineOption idOption(QStringLiteral("id"), QStringLiteral("Scenario id (file name)."), QStringLiteral("id"));
    QCommandLineOption nameOption(QStringLiteral("name"), QStringLiteral("Scenario name."), QStringLiteral("text"));
    QCommandLineOption descOption(QStringLiteral("description"), QStringLiteral("Description."), QStringLiteral("text"));
    QCommandLineOption racesOption(QStringLiteral("races"), QStringLiteral("One race per player, comma separated."),
                                   QStringLiteral("list"), QStringLiteral("humans,humans"));
    QCommandLineOption sizeOption(QStringLiteral("size"), QStringLiteral("Map size WxH."), QStringLiteral("WxH"), QStringLiteral("64x64"));
    QCommandLineOption seedOption(QStringLiteral("seed"), QStringLiteral("Random seed."), QStringLiteral("n"), QStringLiteral("1"));
    QCommandLineOption outOption(QStringList{"o", "out"}, QStringLiteral("Output file (default data/scenarios/<id>.json)."), QStringLiteral("file"));
    for (auto* o : {&dataOption, &idOption, &nameOption, &descOption, &racesOption, &sizeOption, &seedOption, &outOption}) parser.addOption(*o);
    parser.process(app);

    const QString dir = findDataDirectory(parser.value(dataOption));
    ContentRegistry content;
    QStringList errors;
    if (dir.isEmpty() || !content.loadFromDirectory(dir, &errors)) {
        std::fprintf(stderr, "kot_mapgen: cannot load the game data\n");
        for (const QString& e : errors) std::fprintf(stderr, "  %s\n", qPrintable(e));
        return 1;
    }
    const QString id = parser.value(idOption);
    if (id.isEmpty() || id.contains('/') || id.contains('.')) {
        std::fprintf(stderr, "kot_mapgen: give a plain --id (letters, digits, _)\n");
        return 2;
    }
    GameSetupParams params;
    const QStringList size = parser.value(sizeOption).split('x');
    params.mapWidth = size.value(0).toInt();
    params.mapHeight = size.value(1).toInt();
    if (params.mapWidth < 16 || params.mapHeight < 16 || params.mapWidth > 256 || params.mapHeight > 256) {
        std::fprintf(stderr, "kot_mapgen: --size must be between 16x16 and 256x256\n");
        return 2;
    }
    params.seed = parser.value(seedOption).toULongLong();
    static const QStringList colours = {"#d62728", "#1f77b4", "#2ca02c", "#ff7f0e", "#9467bd", "#8c564b", "#e377c2", "#17becf"};
    const QStringList races = parser.value(racesOption).split(',', Qt::SkipEmptyParts);
    for (int i = 0; i < races.size(); ++i)
        params.players.append({QStringLiteral("Player %1").arg(i + 1), false, {}, races[i].trimmed(), i + 1, colours[i % colours.size()]});
    GameState gs;
    QString error;
    if (!createGame(content, params, &gs, nullptr, &error)) {
        std::fprintf(stderr, "kot_mapgen: %s\n", qPrintable(error));
        return 1;
    }
    const QString out = parser.isSet(outOption) ? parser.value(outOption) : QDir(dir).filePath(QStringLiteral("scenarios/%1.json").arg(id));
    if (!saveScenario(out, id, parser.value(nameOption).isEmpty() ? id : parser.value(nameOption), parser.value(descOption), gs, &error)) {
        std::fprintf(stderr, "kot_mapgen: %s\n", qPrintable(error));
        return 1;
    }
    std::printf("wrote %s (%dx%d, %lld players)\n", qPrintable(out), params.mapWidth, params.mapHeight,
                static_cast<long long>(params.players.size()));
    return 0;
}
