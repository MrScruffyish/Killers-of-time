// kot_balance: numbers for balancing the game content.
//
//   kot_balance units  [--race id] [--csv]       power and cost of every unit, outliers marked
//   kot_balance races  [--csv]                   weekly power and cost per race
//   kot_balance duel A B [--gold 5000] [--runs 200]   equal-cost armies fight it out
//   kot_balance duels  [--tier n] [--gold 5000] [--runs 100] [--csv]   every pair of a tier
//
// Power is a rough fighting value: HP x average damage x (1 + 5% per attack+defense
// point). Duels use the real battle rules and the battle AI (no heroes), half of the
// runs with each army attacking; a fair pair is near 50%.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QHash>
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/content/ContentChecks.h"
#include "core/content/ContentRegistry.h"
#include "core/data/DataDir.h"
#include "tools/BalanceCalc.h"

using namespace kot::core;

namespace {

using namespace kot::tools;

void row(bool csv, const QStringList& cells, const QList<int>& widths) {
    if (csv) {
        std::printf("%s\n", qPrintable(cells.join(',')));
        return;
    }
    QString line;
    for (int i = 0; i < cells.size(); ++i) line += cells[i].leftJustified(widths.value(i, 10)) + ' ';
    std::printf("%s\n", qPrintable(line.trimmed()));
}

int unitsReport(const ContentRegistry& c, const QString& race, bool csv) {
    const QList<int> w = {18, 9, 5, 8, 8, 9, 8, 10, 8};
    row(csv, {"unit", "race", "tier", "power", "cost", "pow/1000g", "growth", "week pow", "note"}, w);
    int outliers = 0;
    for (const UnitRow& r : unitRows(c, race)) {
        outliers += !r.note.isEmpty();
        row(csv, {r.id, r.race, QString::number(r.tier), QString::number(r.power, 'f', 0), QString::number(r.cost, 'f', 0),
                  QString::number(r.powerPer1000, 'f', 1), QString::number(r.growth), QString::number(r.weekPower, 'f', 0), r.note},
            w);
    }
    if (!csv) std::printf("\n%d outlier(s) (power per gold more than 1.5x away from the tier median)\n", outliers);
    return 0;
}

int racesReport(const ContentRegistry& c, bool csv) {
    const QList<int> w = {10, 14, 14, 12, 12};
    row(csv, {"race", "week power", "week cost", "pow/1000g", "town gold"}, w);
    for (const RaceRow& r : raceRows(c))
        row(csv, {r.race, QString::number(r.weekPower, 'f', 0), QString::number(r.weekCost, 'f', 0),
                  QString::number(r.powerPer1000, 'f', 1), QString::number(r.townGold) + "/day"},
            w);
    return 0;
}

int duelReport(const ContentRegistry& c, const QString& a, const QString& b, int gold, int runs) {
    const UnitDef* ua = c.unit(a);
    const UnitDef* ub = c.unit(b);
    if (!ua || !ub) {
        std::fprintf(stderr, "kot_balance: unknown unit\n");
        return 2;
    }
    const double share = duel(c, *ua, *ub, gold, runs);
    gold = duelBudget(c, *ua, *ub, gold);
    std::printf("%s (%d) vs %s (%d) for %d gold each: %s wins %.0f%% of %d battles\n", qPrintable(ua->name),
                std::max(1, int(gold / goldValue(c, ua->cost))), qPrintable(ub->name), std::max(1, int(gold / goldValue(c, ub->cost))),
                gold, qPrintable(ua->name), share * 100, runs);
    return 0;
}

int duelsReport(const ContentRegistry& c, int tier, int gold, int runs, bool csv) {
    const QList<int> w = {18, 18, 8, 10};
    row(csv, {"unit", "against", "wins", "note"}, w);
    int lopsided = 0;
    for (const DuelRow& r : duelRows(c, tier, gold, runs)) {
        lopsided += !r.note.isEmpty();
        row(csv, {r.a, r.b, QString::number(r.share * 100, 'f', 0) + "%", r.note}, w);
    }
    if (!csv) std::printf("\n%d lopsided pair(s) (one side wins more than 75%% at equal cost)\n", lopsided);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kot_balance"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Killers of Time - balance numbers.\n"
        "  kot_balance units [--race id] [--csv]\n"
        "  kot_balance races [--csv]\n"
        "  kot_balance duel <unitA> <unitB> [--gold n] [--runs n]\n"
        "  kot_balance duels [--tier n] [--gold n] [--runs n] [--csv]"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("report"), QStringLiteral("units, races, duel or duels"));
    QCommandLineOption dataOption(QStringList{"d", "data"}, QStringLiteral("Game data directory."), QStringLiteral("dir"));
    QCommandLineOption raceOption(QStringList{"r", "race"}, QStringLiteral("Only this race."), QStringLiteral("id"));
    QCommandLineOption csvOption(QStringLiteral("csv"), QStringLiteral("Comma separated output."));
    QCommandLineOption goldOption(QStringLiteral("gold"), QStringLiteral("Gold budget per army (duels)."), QStringLiteral("n"), "5000");
    QCommandLineOption runsOption(QStringLiteral("runs"), QStringLiteral("Battles per pair (duels)."), QStringLiteral("n"), "100");
    QCommandLineOption tierOption(QStringLiteral("tier"), QStringLiteral("Only this tier (duels; 0 = every tier)."), QStringLiteral("n"), "0");
    for (auto* o : {&dataOption, &raceOption, &csvOption, &goldOption, &runsOption, &tierOption}) parser.addOption(*o);
    parser.process(app);
    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) parser.showHelp(2);

    const QString dir = findDataDirectory(parser.value(dataOption));
    ContentRegistry content;
    QStringList errors;
    if (dir.isEmpty() || !content.loadFromDirectory(dir, &errors)) {
        std::fprintf(stderr, "kot_balance: cannot load the game data\n");
        for (const QString& e : errors) std::fprintf(stderr, "  %s\n", qPrintable(e));
        return 1;
    }
    const bool csv = parser.isSet(csvOption);
    const int gold = std::max(100, parser.value(goldOption).toInt());
    const int runs = std::clamp(parser.value(runsOption).toInt(), 1, 10000);
    const QString what = args.first();
    if (what == QLatin1String("units")) return unitsReport(content, parser.value(raceOption), csv);
    if (what == QLatin1String("races")) return racesReport(content, csv);
    if (what == QLatin1String("duel") && args.size() == 3) return duelReport(content, args[1], args[2], gold, runs);
    if (what == QLatin1String("duels")) return duelsReport(content, parser.value(tierOption).toInt(), gold, runs, csv);
    parser.showHelp(2);
}
