// kot_mapgen: makes a random scenario or a whole campaign and writes it into the
// game data directory, ready for the lobby or for kot_editor. It uses the same
// generators as the server's "make a map" command, so the goal options match.
//
//   kot_mapgen --id two_rivers --name "Two Rivers" --races humans,undead [--size 48x48] [--seed 7]
//               [--victory explore --amount 70] [--monster ancient_behemoth] [--artifact dragon_blade]
//
//   kot_mapgen --campaign --id test_war --name "Test War" --chapters 5
//               --goals slay,artifact,explore,days,conquest --race humans --seed 12

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <cstdio>

#include "core/content/ContentRegistry.h"
#include "core/data/DataDir.h"
#include "core/rules/Scenario.h"
#include "core/rules/ScenarioGen.h"

using namespace kot::core;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kot_mapgen"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Killers of Time - random scenario and campaign maker."));
    parser.addHelpOption();
    QCommandLineOption dataOption(QStringList{"d", "data"}, QStringLiteral("Game data directory."), QStringLiteral("dir"));
    QCommandLineOption idOption(QStringLiteral("id"), QStringLiteral("Scenario/campaign id (file name)."), QStringLiteral("id"));
    QCommandLineOption nameOption(QStringLiteral("name"), QStringLiteral("Display name."), QStringLiteral("text"));
    QCommandLineOption descOption(QStringLiteral("description"), QStringLiteral("Description."), QStringLiteral("text"));
    QCommandLineOption racesOption(QStringLiteral("races"), QStringLiteral("One race per player, comma separated."),
                                   QStringLiteral("list"), QStringLiteral("humans,humans"));
    QCommandLineOption sizeOption(QStringLiteral("size"), QStringLiteral("Map size WxH (first map for a campaign)."),
                                  QStringLiteral("WxH"), QStringLiteral("48x48"));
    QCommandLineOption seedOption(QStringLiteral("seed"), QStringLiteral("Random seed."), QStringLiteral("n"), QStringLiteral("1"));
    QCommandLineOption outOption(QStringList{"o", "out"},
                                 QStringLiteral("Scenario: output file (default data/scenarios/<id>.json). Campaign: output directory."),
                                 QStringLiteral("path"));
    QCommandLineOption victoryOption(QStringLiteral("victory"),
                                     QStringLiteral("Goal: conquest, capital, gold, days, slay, artifact or explore."),
                                     QStringLiteral("goal"), QStringLiteral("conquest"));
    QCommandLineOption amountOption(QStringLiteral("amount"), QStringLiteral("Goal amount: gold, days or explore percent."),
                                    QStringLiteral("n"), QStringLiteral("0"));
    QCommandLineOption monsterOption(QStringLiteral("monster"), QStringLiteral("Boss unit key for the slay goal."),
                                     QStringLiteral("key"), QStringLiteral("flying_purple_people_eater"));
    QCommandLineOption artifactOption(QStringLiteral("artifact"), QStringLiteral("Artifact key for the find goal."),
                                      QStringLiteral("key"), QStringLiteral("dragon_blade"));
    QCommandLineOption islandsOption(QStringLiteral("islands"), QStringLiteral("Put each player on his own island (boats needed)."));
    QCommandLineOption campaignOption(QStringLiteral("campaign"), QStringLiteral("Generate a whole campaign instead of one scenario."));
    QCommandLineOption chaptersOption(QStringLiteral("chapters"), QStringLiteral("Campaign length (2-20)."), QStringLiteral("n"), QStringLiteral("3"));
    QCommandLineOption goalsOption(QStringLiteral("goals"), QStringLiteral("One goal per campaign chapter, comma separated."),
                                   QStringLiteral("list"));
    QCommandLineOption raceOption(QStringLiteral("race"), QStringLiteral("Your race in a campaign (default random)."),
                                  QStringLiteral("key"), QStringLiteral("random"));
    QCommandLineOption heroesOption(QStringLiteral("carry-heroes"), QStringLiteral("Heroes to carry between chapters."),
                                    QStringLiteral("n"), QStringLiteral("1"));
    QCommandLineOption noArmyOption(QStringLiteral("no-carry-army"), QStringLiteral("Do not carry armies between chapters."));
    for (auto* o : {&dataOption, &idOption, &nameOption, &descOption, &racesOption, &sizeOption, &seedOption, &outOption,
                    &victoryOption, &amountOption, &monsterOption, &artifactOption, &islandsOption, &campaignOption,
                    &chaptersOption, &goalsOption, &raceOption, &heroesOption, &noArmyOption})
        parser.addOption(*o);
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
    if (!validGeneratedId(id)) {
        std::fprintf(stderr, "kot_mapgen: give a plain --id (1-40 letters, digits, '_' or '-')\n");
        return 2;
    }
    const QStringList size = parser.value(sizeOption).split('x');
    const int width = size.value(0).toInt(), height = size.value(1).toInt();
    const quint64 seed = parser.value(seedOption).toULongLong();
    const QString name = parser.value(nameOption).isEmpty() ? id : parser.value(nameOption);
    QString why;

    if (parser.isSet(campaignOption)) {
        CampaignGenParams p;
        p.id = id;
        p.name = name;
        p.description = parser.value(descOption);
        p.chapters = parser.value(chaptersOption).toInt();
        p.playerRace = parser.value(raceOption);
        p.startSize = width;
        p.seed = seed;
        p.carryHeroes = parser.value(heroesOption).toInt();
        p.carryArmy = !parser.isSet(noArmyOption);
        p.islands = parser.isSet(islandsOption);
        p.goals = parser.value(goalsOption).split(',', Qt::SkipEmptyParts);
        const QString base = parser.isSet(outOption) ? parser.value(outOption) : dir;
        if (!generateCampaign(content, p, QDir(base).filePath(QStringLiteral("scenarios")),
                              QDir(base).filePath(QStringLiteral("campaigns")), &why)) {
            std::fprintf(stderr, "kot_mapgen: %s\n", qPrintable(why));
            return 1;
        }
        std::printf("wrote %s (%d chapters, %s, %dx%d first map)\n",
                    qPrintable(QDir(base).filePath(QStringLiteral("campaigns/%1.json").arg(id))), p.chapters,
                    qPrintable(p.playerRace), width, width);
        return 0;
    }

    ScenarioGenParams p;
    p.id = id;
    p.width = width;
    p.height = height > 0 ? height : width;
    p.seed = seed;
    p.races = parser.value(racesOption).split(',', Qt::SkipEmptyParts);
    for (QString& r : p.races) r = r.trimmed();
    p.islands = parser.isSet(islandsOption);
    p.victory = parser.value(victoryOption);
    p.victoryAmount = parser.value(amountOption).toInt();
    p.monster = parser.value(monsterOption);
    p.artifact = parser.value(artifactOption);
    GameState gs;
    if (!generateScenario(content, p, &gs, &why)) {
        std::fprintf(stderr, "kot_mapgen: %s\n", qPrintable(why));
        return 1;
    }
    const QString out = parser.isSet(outOption) ? parser.value(outOption) : QDir(dir).filePath(QStringLiteral("scenarios/%1.json").arg(id));
    if (!saveScenario(out, id, name, parser.value(descOption), gs, content, &why)) {
        std::fprintf(stderr, "kot_mapgen: %s\n", qPrintable(why));
        return 1;
    }
    std::printf("wrote %s (%dx%d, %lld players, %s)\n", qPrintable(out), p.width, p.height,
                static_cast<long long>(p.races.size()), qPrintable(p.victory));
    return 0;
}
