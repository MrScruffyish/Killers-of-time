// kot_graph: draws the structure of the game content as Graphviz graphs and checks it.
//
//   kot_graph buildings [--race humans] [-o buildings.pdf]   building tree(s)
//   kot_graph units     [--race undead] [-o units.pdf]       units by tier, upgrades
//   kot_graph spells    [-o spells.pdf]                      spells by school and level
//   kot_graph check                                          loops and unbuildable buildings
//
// The output format follows the file name: .dot writes the graph text, .pdf/.png/.svg
// runs Graphviz `dot`. Without -o the dot text goes to standard output.
// Exit code 1 when `check` (or loading the data) finds problems.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>
#include <cstdio>

#include "core/content/ContentChecks.h"
#include "core/content/ContentRegistry.h"
#include "core/data/DataDir.h"

using namespace kot::core;

namespace {

QString quoted(const QString& s) {
    QString q = s;
    q.replace('\\', "\\\\").replace('"', "\\\"");
    return '"' + q + '"';
}

QString costText(const ResourceAmounts& cost) {
    QStringList parts;
    for (auto it = cost.begin(); it != cost.end(); ++it) parts << QStringLiteral("%1 %2").arg(it.value()).arg(it.key());
    return parts.join(", ");
}

QStringList racesOf(const ContentRegistry& content, const QString& only) {
    return only.isEmpty() ? content.raceIds() : QStringList{only};
}

// Requirements are arrows from what you need to what it allows (the order you build
// in); upgrades are blue; rival buildings get a red dashed line; dwellings point to
// their units (green ellipses). Buildings for every race are drawn once, in grey.
QString buildingsGraph(const ContentRegistry& content, const QString& race) {
    QString out;
    QTextStream s(&out);
    s << "digraph buildings {\n  rankdir=LR;\n  node [shape=box, style=\"rounded,filled\", fillcolor=\"#f4f0e0\", fontname=\"Helvetica\"];\n"
      << "  edge [fontname=\"Helvetica\", fontsize=9];\n";
    const QStringList races = racesOf(content, race);
    const auto shown = [&](const BuildingDef& b) { return b.race.isEmpty() || races.contains(b.race); };
    s << "  subgraph cluster_shared {\n    label=\"every race\"; style=dashed; color=grey;\n";
    for (const BuildingDef& b : content.buildings())
        if (b.race.isEmpty())
            s << "    " << quoted(b.id) << " [label=" << quoted(b.name + "\n" + costText(b.cost)) << ", fillcolor=\"#e6e6e6\"];\n";
    s << "  }\n";
    for (const QString& r : races) {
        s << "  subgraph " << quoted("cluster_" + r) << " {\n    label=" << quoted(content.race(r).name.isEmpty() ? r : content.race(r).name)
          << "; color=\"#8080c0\";\n";
        for (const BuildingDef& b : content.buildings())
            if (b.race == r) s << "    " << quoted(b.id) << " [label=" << quoted(b.name + "\n" + costText(b.cost)) << "];\n";
        for (const UnitDef& u : content.units())
            if (u.race == r)
                s << "    " << quoted("unit:" + u.id) << " [label=" << quoted(QStringLiteral("%1 (tier %2)").arg(u.name).arg(u.tier))
                  << ", shape=ellipse, fillcolor=\"#d8f0d0\"];\n";
        s << "  }\n";
    }
    // Units of other races (e.g. neutral) recruited in the shown buildings.
    for (const BuildingDef& b : content.buildings()) {
        if (!shown(b)) continue;
        for (const BuildingEffect& e : b.effects) {
            const UnitDef* u = e.type == BuildingEffect::Type::Dwelling ? content.unit(e.unit) : nullptr;
            if (u && !races.contains(u->race))
                s << "  " << quoted("unit:" + u->id) << " [label=" << quoted(QStringLiteral("%1 (tier %2, %3)").arg(u->name).arg(u->tier).arg(u->race))
                  << ", shape=ellipse, fillcolor=\"#d8f0d0\"];\n";
        }
    }
    for (const BuildingDef& b : content.buildings()) {
        if (!shown(b)) continue;
        for (const QString& req : b.requiredBuildings) s << "  " << quoted(req) << " -> " << quoted(b.id) << ";\n";
        if (!b.upgradeOf.isEmpty())
            s << "  " << quoted(b.upgradeOf) << " -> " << quoted(b.id) << " [color=blue, label=\"upgrade\"];\n";
        for (const QString& x : b.excludes)
            if (b.id < x || !content.building(x) || !content.building(x)->excludes.contains(b.id))
                s << "  " << quoted(b.id) << " -> " << quoted(x)
                  << " [color=red, style=dashed, dir=none, constraint=false, label=\"either / or\"];\n";
        for (const BuildingEffect& e : b.effects)
            if (e.type == BuildingEffect::Type::Dwelling && content.unit(e.unit))
                s << "  " << quoted(b.id) << " -> " << quoted("unit:" + e.unit) << " [color=\"#40a040\", style=dotted];\n";
    }
    s << "}\n";
    return out;
}

QString unitsGraph(const ContentRegistry& content, const QString& race) {
    QString out;
    QTextStream s(&out);
    s << "digraph units {\n  rankdir=LR;\n  node [shape=box, style=filled, fillcolor=\"#d8f0d0\", fontname=\"Helvetica\"];\n";
    QStringList races = racesOf(content, race);
    if (race.isEmpty())  // races without a town too (neutral monsters)
        for (const UnitDef& u : content.units())
            if (!races.contains(u.race)) races << u.race;
    for (const QString& r : races) {
        s << "  subgraph " << quoted("cluster_" + r) << " {\n    label=" << quoted(content.race(r).name.isEmpty() ? r : content.race(r).name) << ";\n";
        for (const UnitDef& u : content.units()) {
            if (u.race != r) continue;
            const QString label = QStringLiteral("%1 (tier %2)\natt %3  def %4  dmg %5-%6\nhp %7  speed %8%9\n%10 / week, %11%12")
                                      .arg(u.name).arg(u.tier).arg(u.attack).arg(u.defense).arg(u.damageMin).arg(u.damageMax)
                                      .arg(u.maxHp).arg(u.battleSpeed)
                                      .arg(u.range > 0 ? QStringLiteral("  range %1").arg(u.range) : QString())
                                      .arg(u.weeklyGrowth).arg(costText(u.cost))
                                      .arg(u.abilities.isEmpty() ? QString() : "\n" + u.abilities.join(", "));
            s << "    " << quoted(u.id) << " [label=" << quoted(label) << "];\n";
        }
        // Tiers in columns, left to right (invisible edges keep the order).
        QString previous;
        for (int tier = 1; tier <= 7; ++tier) {
            QStringList same;
            for (const UnitDef& u : content.units())
                if (u.race == r && u.tier == tier) same << quoted(u.id);
            if (same.isEmpty()) continue;
            if (same.size() > 1) s << "    { rank=same; " << same.join("; ") << "; }\n";
            if (!previous.isEmpty()) s << "    " << previous << " -> " << same.first() << " [style=invis];\n";
            previous = same.first();
        }
        s << "  }\n";
    }
    for (const UnitDef& u : content.units())
        if (!u.upgradesTo.isEmpty() && (race.isEmpty() || u.race == race))
            s << "  " << quoted(u.id) << " -> " << quoted(u.upgradesTo) << " [color=blue, label=\"upgrade\"];\n";
    s << "}\n";
    return out;
}

QString spellsGraph(const ContentRegistry& content) {
    QString out;
    QTextStream s(&out);
    s << "digraph spells {\n  rankdir=TB;\n  node [shape=box, style=\"rounded,filled\", fillcolor=\"#e8e0f8\", fontname=\"Helvetica\"];\n";
    QStringList schools;
    for (const SpellDef& sp : content.spells())
        if (!schools.contains(sp.school)) schools << sp.school;
    for (const QString& school : schools) {
        s << "  subgraph " << quoted("cluster_" + school) << " {\n    label=" << quoted(school) << ";\n";
        for (const SpellDef& sp : content.spells())
            if (sp.school == school)
                s << "    " << quoted(sp.id) << " [label=" << quoted(QStringLiteral("%1\nlevel %2, %3 SP, magic %4\n%5")
                                                                    .arg(sp.name).arg(sp.level).arg(sp.cost).arg(sp.requiredMagic)
                                                                    .arg(sp.scope == SpellDef::Scope::Battle ? "battle" : "adventure"))
                  << "];\n";
        s << "  }\n";
    }
    // Which race's mage guilds teach which school.
    for (const RaceDef& r : content.races()) {
        s << "  " << quoted("race:" + r.id) << " [label=" << quoted(r.name) << ", shape=house, fillcolor=\"#f4f0e0\"];\n";
        for (const SpellDef& sp : content.spells())
            if (r.spellSchools.isEmpty() || r.spellSchools.contains(sp.school))
                s << "  " << quoted("race:" + r.id) << " -> " << quoted(sp.id) << " [color=\"#a0a0a0\"];\n";
    }
    s << "}\n";
    return out;
}

bool writeOutput(const QString& dot, const QString& file) {
    if (file.isEmpty()) {
        std::fputs(dot.toUtf8().constData(), stdout);
        return true;
    }
    const QString format = QFileInfo(file).suffix().toLower();
    if (format == QLatin1String("dot") || format == QLatin1String("gv")) {
        QFile f(file);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        f.write(dot.toUtf8());
        return true;
    }
    QProcess p;
    p.start(QStringLiteral("dot"), {QStringLiteral("-T") + format, QStringLiteral("-o"), file});
    if (!p.waitForStarted(5000)) {
        std::fprintf(stderr, "kot_graph: cannot run 'dot' (install graphviz: sudo apt install graphviz)\n");
        return false;
    }
    p.write(dot.toUtf8());
    p.closeWriteChannel();
    p.waitForFinished(60000);
    if (p.exitCode() != 0) std::fputs(p.readAllStandardError().constData(), stderr);
    return p.exitCode() == 0;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kot_graph"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Killers of Time - content graphs (Graphviz) and checks.\n"
        "  kot_graph buildings|units|spells [--race id] [-o file.pdf|.png|.svg|.dot]\n"
        "  kot_graph check"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("what"), QStringLiteral("buildings, units, spells or check"));
    QCommandLineOption dataOption(QStringList{"d", "data"}, QStringLiteral("Game data directory."), QStringLiteral("dir"));
    QCommandLineOption raceOption(QStringList{"r", "race"}, QStringLiteral("Only this race."), QStringLiteral("id"));
    QCommandLineOption outOption(QStringList{"o", "out"}, QStringLiteral("Output file (format from the suffix)."), QStringLiteral("file"));
    parser.addOption(dataOption);
    parser.addOption(raceOption);
    parser.addOption(outOption);
    parser.process(app);
    const QStringList args = parser.positionalArguments();
    if (args.size() != 1) parser.showHelp(2);

    const QString dir = findDataDirectory(parser.value(dataOption));
    ContentRegistry content;
    QStringList errors;
    if (dir.isEmpty() || !content.loadFromDirectory(dir, &errors)) {
        std::fprintf(stderr, "kot_graph: cannot load the game data%s\n", dir.isEmpty() ? " (not found; use --data)" : ":");
        for (const QString& e : errors) std::fprintf(stderr, "  %s\n", qPrintable(e));
        return 1;
    }
    const QString what = args.first();
    const QString race = parser.value(raceOption);
    if (!race.isEmpty() && !content.raceIds().contains(race)) {
        std::fprintf(stderr, "kot_graph: unknown race '%s' (%s)\n", qPrintable(race), qPrintable(content.raceIds().join(", ")));
        return 2;
    }
    if (what == QLatin1String("check")) {
        const QStringList problems = checkContentGraphs(content);
        for (const QString& p : problems) std::printf("%s\n", qPrintable(p));
        if (problems.isEmpty())
            std::printf("ok: %lld buildings, %lld units, %lld spells; no loops, everything can be built\n",
                        static_cast<long long>(content.buildings().size()), static_cast<long long>(content.units().size()),
                        static_cast<long long>(content.spells().size()));
        return problems.isEmpty() ? 0 : 1;
    }
    QString dot;
    if (what == QLatin1String("buildings")) dot = buildingsGraph(content, race);
    else if (what == QLatin1String("units")) dot = unitsGraph(content, race);
    else if (what == QLatin1String("spells")) dot = spellsGraph(content);
    else parser.showHelp(2);
    return writeOutput(dot, parser.value(outOption)) ? 0 : 1;
}
