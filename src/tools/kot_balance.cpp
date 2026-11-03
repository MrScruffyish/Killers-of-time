// kot_balance: numbers for balancing the game content.
//
//   kot_balance units  [--race id] [--csv]       power and cost of every unit, outliers marked
//   kot_balance races  [--csv]                   weekly power and cost per race
//   kot_balance duel A B [--gold 5000] [--runs 200]   equal-cost armies fight it out
//   kot_balance duels  [--tier n] [--gold 5000] [--runs 100] [--csv]   every pair of a tier
//   kot_balance replay <save.json> <attackerArmyId> <defenderArmyId> [--runs 50]   a battle from a saved
//                      game, fought by the battle AI with heroes, walls and all (for playtest questions)
//   kot_balance weeks  [--weeks 2] [--budget gold/week] [--runs 20] [--csv]   race against race, each with its
//                      weeks of growth (and, with a budget, only what that gold buys, best units first)
//
// Power is a rough fighting value: HP x average damage x (1 + 5% per attack+defense
// point). Duels use the real battle rules and the battle AI (no heroes), half of the
// runs with each army attacking; a fair pair is near 50%.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/battle/AutoBattle.h"
#include "core/content/ContentChecks.h"
#include "core/model/GameState.h"
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

int weeksReport(const ContentRegistry& c, int weeks, int runs, int budget, bool csv) {
    if (!csv) {
        std::printf("Armies (%d week(s)%s):\n", weeks, budget > 0 ? qPrintable(QStringLiteral(", %1 gold a week").arg(budget)) : "");
        for (const QString& race : c.raceIds()) {
            QStringList parts;
            for (const auto& [id, n] : raceWeekArmy(c, race, weeks, budget)) parts << QStringLiteral("%1 %2").arg(n).arg(id);
            std::printf("  %-10s %s\n", qPrintable(race), qPrintable(parts.join(", ")));
        }
        std::printf("\n");
    }
    const QList<int> w = {10, 10, 8, 10, 10, 18};
    row(csv, {"race A", "race B", "A wins", "A cost", "B cost", "note"}, w);
    QHash<QString, QList<double>> score;
    for (const WeekRow& r : weekRows(c, weeks, runs, budget)) {
        row(csv, {r.a, r.b, QString::number(r.share * 100, 'f', 0) + "%", QString::number(r.costA, 'f', 0), QString::number(r.costB, 'f', 0), r.note}, w);
        score[r.a] << r.share;
        score[r.b] << 1 - r.share;
    }
    if (csv) return 0;
    std::printf("\nAverage wins (a fair race is near 50%%):\n");
    QStringList races = score.keys();
    const auto avg = [&](const QString& r) { double t = 0; for (double v : score[r]) t += v; return t / std::max<qsizetype>(1, score[r].size()); };
    std::sort(races.begin(), races.end(), [&](const QString& x, const QString& y) { return avg(x) > avg(y); });
    for (const QString& r : races) std::printf("  %-10s %3.0f%%\n", qPrintable(r), avg(r) * 100);
    return 0;
}

// A battle out of a saved game, fought many times by the battle AI for both sides.
int replayReport(const ContentRegistry& content, const QString& path, int attacker, int defender, int runs) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "kot_balance: cannot read %s\n", qPrintable(path));
        return 1;
    }
    GameState saved;
    QStringList errors;
    if (!GameState::fromJson(QJsonDocument::fromJson(f.readAll()).object(), content, &saved, &errors)) {
        std::fprintf(stderr, "kot_balance: not a saved game: %s\n", qPrintable(errors.join("; ")));
        return 1;
    }
    const ArmyId a(static_cast<quint32>(attacker)), d(static_cast<quint32>(defender));
    if (!saved.army(a) || !saved.army(d)) {
        std::fprintf(stderr, "kot_balance: no army %d or %d in the save\n", attacker, defender);
        return 1;
    }
    int won = 0, rounds = 0;
    QMap<QString, int> lostA, lostD;
    for (int i = 0; i < runs; ++i) {
        GameState gs = saved;
        gs.rng().reseed(1000 + i);
        const BattleReport r = resolveBattle(gs, content, a, d);
        won += r.attackerWon();
        rounds += r.rounds;
        for (auto it = r.attacker.losses.begin(); it != r.attacker.losses.end(); ++it) lostA[it.key()] += it.value();
        for (auto it = r.defender.losses.begin(); it != r.defender.losses.end(); ++it) lostD[it.key()] += it.value();
    }
    std::printf("attacker won %d of %d (%.0f%%), %.1f rounds on average\n", won, runs, 100.0 * won / runs, double(rounds) / runs);
    const auto show = [&](const char* who, const QMap<QString, int>& lost) {
        std::printf("%s loses on average:", who);
        for (auto it = lost.begin(); it != lost.end(); ++it) std::printf(" %s %.1f", qPrintable(it.key()), double(it.value()) / runs);
        std::printf("\n");
    };
    show("attacker", lostA);
    show("defender", lostD);
    return 0;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kot_balance"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Killers of Time - balance numbers.\n"
        "  kot_balance units [--race id] [--csv]\n"
        "  kot_balance races [--csv]\n"
        "  kot_balance duel <unitA> <unitB> [--gold n] [--runs n]\n"
        "  kot_balance duels [--tier n] [--gold n] [--runs n] [--csv]\n"
        "  kot_balance weeks [--weeks n] [--runs n] [--csv]\n"
        "  kot_balance replay <save.json> <attackerArmy> <defenderArmy> [--runs n]"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("report"), QStringLiteral("units, races, duel, duels or weeks"));
    QCommandLineOption dataOption(QStringList{"d", "data"}, QStringLiteral("Game data directory."), QStringLiteral("dir"));
    QCommandLineOption raceOption(QStringList{"r", "race"}, QStringLiteral("Only this race."), QStringLiteral("id"));
    QCommandLineOption csvOption(QStringLiteral("csv"), QStringLiteral("Comma separated output."));
    QCommandLineOption goldOption(QStringLiteral("gold"), QStringLiteral("Gold budget per army (duels)."), QStringLiteral("n"), "5000");
    QCommandLineOption runsOption(QStringLiteral("runs"), QStringLiteral("Battles per pair (duels)."), QStringLiteral("n"), "100");
    QCommandLineOption tierOption(QStringLiteral("tier"), QStringLiteral("Only this tier (duels; 0 = every tier)."), QStringLiteral("n"), "0");
    QCommandLineOption budgetOption(QStringLiteral("budget"), QStringLiteral("Gold a week to buy with (weeks; 0 = buy everything)."), QStringLiteral("n"), "0");
    QCommandLineOption weeksOption(QStringLiteral("weeks"), QStringLiteral("Weeks of growth per army (weeks)."), QStringLiteral("n"), "2");
    for (auto* o : {&dataOption, &raceOption, &csvOption, &goldOption, &runsOption, &tierOption, &weeksOption, &budgetOption}) parser.addOption(*o);
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
    if (what == QLatin1String("replay") && args.size() == 4)
        return replayReport(content, args[1], args[2].toInt(), args[3].toInt(), parser.isSet(runsOption) ? runs : 50);
    if (what == QLatin1String("weeks"))
        return weeksReport(content, std::clamp(parser.value(weeksOption).toInt(), 1, 52), parser.isSet(runsOption) ? runs : 20,
                           std::max(0, parser.value(budgetOption).toInt()), csv);
    if (what == QLatin1String("duels")) return duelsReport(content, parser.value(tierOption).toInt(), gold, runs, csv);
    parser.showHelp(2);
}
