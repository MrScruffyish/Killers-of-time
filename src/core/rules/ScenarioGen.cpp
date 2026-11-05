#include "core/rules/ScenarioGen.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

#include "core/content/ContentRegistry.h"
#include "core/rules/GameSetup.h"
#include "core/rules/Scenario.h"

namespace kot::core {

bool validGeneratedId(const QString& id) {
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]{1,40}$"));
    return valid.match(id).hasMatch();
}

bool generateScenario(const ContentRegistry& content, const ScenarioGenParams& p, GameState* out, QString* error) {
    const auto fail = [&](const QString& why) {
        if (error) *error = why;
        return false;
    };
    if (p.races.size() < 2 || p.races.size() > 8) return fail(QStringLiteral("a scenario needs 2 to 8 players"));
    if (p.width < 24 || p.height < 24 || p.width > 192 || p.height > 192) return fail(QStringLiteral("the map size must be 24 to 192"));
    GameSetupParams setup;
    setup.mapWidth = p.width;
    setup.mapHeight = p.height;
    setup.seed = p.seed;
    setup.islands = p.islands;
    setup.seaLevel = std::clamp(p.seaLevel, 0.05, 0.7);
    setup.mountainFraction = std::clamp(p.mountains, 0.0, 0.25);
    setup.hillFraction = std::clamp(p.hills, 0.0, 0.4);
    setup.rivers = p.rivers;
    setup.roads = p.roads;
    setup.monsterPercent = std::clamp(p.monsterPercent, 10, 400);
    static const QStringList colours = {"#d62728", "#1f77b4", "#2ca02c", "#ff7f0e", "#9467bd", "#8c564b", "#e377c2", "#17becf"};
    const QStringList all = content.playableRaceKeys();
    Rng pick(p.seed * 7919 + 13);
    for (int i = 0; i < p.races.size(); ++i) {
        QString race = p.races[i].trimmed();
        if (race.isEmpty() || race == QLatin1String("random")) race = all[pick.nextInt(0, static_cast<int>(all.size()) - 1)];
        if (!all.contains(race)) return fail(QStringLiteral("unknown race '%1'").arg(race));
        setup.players.append({QStringLiteral("Player %1").arg(i + 1), i > 0, i > 0 ? QStringLiteral("simple") : QString(), race, i + 1,
                              colours[i % colours.size()]});
    }
    GameState gs;
    QList<PlayerId> ids;
    QString why;
    if (!createGame(content, setup, &gs, &ids, &why)) return fail(why);
    VictoryCondition v;
    if (p.victory == QLatin1String("capital")) {
        v.type = VictoryCondition::Type::HoldTown;
        for (const Town& t : gs.towns())
            if (t.owner == ids.value(1)) v.town = t.id;
    } else if (p.victory == QLatin1String("gold")) {
        v.type = VictoryCondition::Type::Gold;
        v.amount = p.victoryAmount > 0 ? p.victoryAmount : 50000;
    } else if (p.victory == QLatin1String("days")) {
        v.type = VictoryCondition::Type::Days;
        v.amount = p.victoryAmount > 0 ? p.victoryAmount : 90;
    } else if (p.victory != QLatin1String("conquest")) {
        return fail(QStringLiteral("unknown goal '%1' (conquest, capital, gold, days)").arg(p.victory));
    }
    gs.setVictory(v);
    *out = std::move(gs);
    return true;
}

bool generateCampaign(const ContentRegistry& content, const CampaignGenParams& p, const QString& scenarioDir,
                      const QString& campaignDir, QString* error) {
    const auto fail = [&](const QString& why) {
        if (error) *error = why;
        return false;
    };
    if (!validGeneratedId(p.id)) return fail(QStringLiteral("a name has 1-40 letters, digits, '_' or '-'"));
    if (p.chapters < 2 || p.chapters > 6) return fail(QStringLiteral("a campaign has 2 to 6 chapters"));
    if (!QDir().mkpath(scenarioDir) || !QDir().mkpath(campaignDir)) return fail(QStringLiteral("cannot write the files"));
    const QStringList all = content.playableRaceKeys();
    Rng rng(p.seed * 104729 + 7);
    const QString playerRace = p.playerRace == QLatin1String("random") || !all.contains(p.playerRace)
                                   ? all[rng.nextInt(0, static_cast<int>(all.size()) - 1)] : p.playerRace;
    // Chapter by chapter: bigger, more enemies, other goals.
    static const QStringList goals = {"capital", "conquest", "days", "conquest", "gold", "conquest"};
    QStringList ids;
    for (int c = 0; c < p.chapters; ++c) {
        ScenarioGenParams s;
        s.id = QStringLiteral("%1_%2").arg(p.id).arg(c + 1);
        s.width = s.height = std::min(128, p.startSize + 16 * c);
        s.seed = p.seed * 1000 + c * 37 + 1;
        s.islands = p.islands;
        s.races = {playerRace};
        for (int e = 0; e < 1 + c; ++e) s.races << all[rng.nextInt(0, static_cast<int>(all.size()) - 1)];
        s.monsterPercent = 100 + 20 * c;
        s.victory = c == p.chapters - 1 ? QStringLiteral("conquest") : goals[c % goals.size()];
        s.victoryAmount = s.victory == QLatin1String("days") ? 60 + 15 * c : s.victory == QLatin1String("gold") ? 40000 + 10000 * c : 0;
        GameState gs;
        QString why;
        if (!generateScenario(content, s, &gs, &why)) return fail(QStringLiteral("chapter %1: %2").arg(c + 1).arg(why));
        const QString goal = s.victory == QLatin1String("capital") ? QStringLiteral("Take the enemy capital.")
                           : s.victory == QLatin1String("days") ? QStringLiteral("Hold the most towns after %1 days.").arg(s.victoryAmount)
                           : s.victory == QLatin1String("gold") ? QStringLiteral("Be the first with %1 gold.").arg(s.victoryAmount)
                                                                : QStringLiteral("Defeat everyone.");
        if (!saveScenario(QDir(scenarioDir).filePath(s.id + QStringLiteral(".json")), s.id,
                          QStringLiteral("%1, chapter %2").arg(p.name.isEmpty() ? p.id : p.name).arg(c + 1),
                          QStringLiteral("%1 against %2 enemies on a %3x%3 map. %4").arg(playerRace).arg(c + 1).arg(s.width).arg(goal), gs, content, &why))
            return fail(why);
        ids << s.id;
    }
    QJsonObject campaign{{"id", p.id}, {"name", p.name.isEmpty() ? p.id : p.name},
                         {"description", QStringLiteral("A random campaign of %1 chapters for the %2. Your best heroes go with you.")
                                             .arg(p.chapters).arg(playerRace)},
                         {"scenarios", QJsonArray::fromStringList(ids)},
                         {"carryOver", QJsonObject{{"heroes", std::clamp(p.carryHeroes, 0, 8)}, {"army", p.carryArmy}}}};
    QSaveFile f(QDir(campaignDir).filePath(p.id + QStringLiteral(".json")));
    if (!f.open(QIODevice::WriteOnly)) return fail(f.errorString());
    f.write(QJsonDocument(campaign).toJson());
    if (!f.commit()) return fail(f.errorString());
    return true;
}

}  // namespace kot::core
