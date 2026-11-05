// kot_saveview: shows a saved game with its content ids as the names used in the data
// files ("militia", "gold") instead of numbers, as readable JSON. Saves carry content
// ids as numbers that only mean something with the game data that made them (the
// content checksum); this needs that same data.
//
//   kot_saveview [--data dir] [--compact] <save.json>      (to standard output)

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

#include "core/content/ContentRegistry.h"
#include "core/data/DataDir.h"
#include "core/model/GameState.h"

using namespace kot::core;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Shows a saved game with names instead of content numbers."));
    parser.addHelpOption();
    QCommandLineOption dataOption(QStringLiteral("data"), QStringLiteral("The game data folder (the one the save was made with)."),
                                  QStringLiteral("dir"));
    QCommandLineOption compactOption(QStringLiteral("compact"), QStringLiteral("One line instead of indented JSON."));
    parser.addOption(dataOption);
    parser.addOption(compactOption);
    parser.addPositionalArgument(QStringLiteral("save"), QStringLiteral("The saved game (.json)."));
    parser.process(app);
    if (parser.positionalArguments().size() != 1) parser.showHelp(2);

    const QString dataDir = findDataDirectory(parser.value(dataOption));
    ContentRegistry content;
    QStringList errors;
    if (dataDir.isEmpty() || !content.loadFromDirectory(dataDir, &errors)) {
        std::fprintf(stderr, "kot_saveview: cannot load the game data: %s\n", qPrintable(errors.join("; ")));
        return 1;
    }
    GameState state;
    if (!GameState::loadFromFile(parser.positionalArguments().first(), content, &state, &errors)) {
        std::fprintf(stderr, "kot_saveview: %s\n", qPrintable(errors.join("\n")));
        return 1;
    }
    QJsonObject json = state.toJson(content, GameState::IdStyle::Text);
    const QByteArray text = QJsonDocument(json).toJson(parser.isSet(compactOption) ? QJsonDocument::Compact : QJsonDocument::Indented);
    std::fwrite(text.constData(), 1, static_cast<size_t>(text.size()), stdout);
    return 0;
}
