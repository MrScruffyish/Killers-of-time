#include "core/rules/ScenarioGen.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QVector>
#include <algorithm>

#include "core/content/ContentRegistry.h"
#include "core/rules/GameSetup.h"
#include "core/rules/Scenario.h"

namespace kot::core {

bool validGeneratedId(const QString& id) {
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]{1,40}$"));
    return valid.match(id).hasMatch();
}

QStringList slayBossKeys() {
    return {"flying_purple_people_eater", "ancient_behemoth", "emerald_archdragon", "elder_kraken", "doom_sovereign",
            "bone_overlord", "storm_king", "infernal_sovereign", "elder_leviathan", "ancient_colossus"};
}

QStringList findArtifactKeys() {
    return {"dragon_blade", "crown_of_kings", "winged_boots", "crown_of_command", "dragon_scale", "ring_of_the_sages"};
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
    setup.excludedArtifacts = p.excludedArtifacts;
    if (p.victory == QLatin1String("slay")) {
        setup.slayMonster = content.unitId(p.monster);
        if (!content.unit(setup.slayMonster)) return fail(QStringLiteral("unknown monster '%1'").arg(p.monster));
    } else if (p.victory == QLatin1String("artifact")) {
        setup.goalArtifact = content.artifactId(p.artifact);
        if (!content.artifact(setup.goalArtifact)) return fail(QStringLiteral("unknown artifact '%1'").arg(p.artifact));
    }
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
    if (p.victory == QLatin1String("slay")) {
        // createGame placed the monster and recorded its army in the victory condition.
        if (gs.victory().type != VictoryCondition::Type::SlayMonster) return fail(QStringLiteral("no monster was placed"));
    } else {
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
        } else if (p.victory == QLatin1String("artifact")) {
            // createGame put the prize on the map; the victory condition names it.
            v.type = VictoryCondition::Type::FindArtifact;
            v.targetArtifact = setup.goalArtifact;
        } else if (p.victory == QLatin1String("explore")) {
            v.type = VictoryCondition::Type::ExploreMap;
            v.amount = std::clamp(p.victoryAmount > 0 ? p.victoryAmount : 60, 1, 100);
        } else if (p.victory != QLatin1String("conquest")) {
            return fail(QStringLiteral("unknown goal '%1' (conquest, capital, gold, days, slay, artifact, explore)").arg(p.victory));
        }
        gs.setVictory(v);
    }
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
    if (p.chapters < 2 || p.chapters > 20) return fail(QStringLiteral("a campaign has 2 to 20 chapters"));
    if (!p.goals.isEmpty() && p.goals.size() != p.chapters)
        return fail(QStringLiteral("give one goal per chapter (%1)").arg(p.chapters));
    if (!QDir().mkpath(scenarioDir) || !QDir().mkpath(campaignDir)) return fail(QStringLiteral("cannot write the files"));
    const QStringList all = content.playableRaceKeys();
    Rng rng(p.seed * 104729 + 7);
    const QString playerRace = p.playerRace == QLatin1String("random") || !all.contains(p.playerRace)
                                   ? all[rng.nextInt(0, static_cast<int>(all.size()) - 1)] : p.playerRace;
    // Chapter by chapter: bigger, more enemies (up to the 8-player limit), other
    // goals. Slay goals get a different boss each time, find goals a different prize.
    static const QStringList rotatingGoals = {"capital", "slay", "artifact", "days", "explore", "gold", "slay", "capital", "explore", "artifact", "gold"};
    const QStringList bosses = slayBossKeys(), prizes = findArtifactKeys();
    // Decide the whole campaign's goals and prizes up front: every chapter then
    // reserves all goal artifacts, so no earlier map (or quest) can give one away.
    QVector<QString> chapterGoals(p.chapters), chapterPrizes(p.chapters);
    QList<ArtifactId> reserved;
    int bossIndex = 0, prizeIndex = 0;
    for (int c = 0; c < p.chapters; ++c) {
        chapterGoals[c] = !p.goals.isEmpty() ? p.goals[c]
                        : c == p.chapters - 1 ? QStringLiteral("conquest")
                                              : rotatingGoals[c % rotatingGoals.size()];
        if (chapterGoals[c] == QLatin1String("artifact")) {
            chapterPrizes[c] = prizes[prizeIndex++ % prizes.size()];
            reserved << content.artifactId(chapterPrizes[c]);
        }
    }
    QStringList ids;
    for (int c = 0; c < p.chapters; ++c) {
        ScenarioGenParams s;
        s.id = QStringLiteral("%1_%2").arg(p.id).arg(c + 1);
        s.width = s.height = std::min(128, p.startSize + 16 * c);
        s.seed = p.seed * 1000 + c * 37 + 1;
        s.islands = p.islands;
        s.races = {playerRace};
        const int enemies = std::min(1 + c, 7);
        for (int e = 0; e < enemies; ++e) s.races << all[rng.nextInt(0, static_cast<int>(all.size()) - 1)];
        s.monsterPercent = 100 + 20 * c;
        s.victory = chapterGoals[c];
        s.excludedArtifacts = reserved;
        if (s.victory == QLatin1String("slay")) s.monster = bosses[bossIndex++ % bosses.size()];
        if (s.victory == QLatin1String("artifact")) s.artifact = chapterPrizes[c];
        s.victoryAmount = s.victory == QLatin1String("days")   ? 60 + 15 * c
                        : s.victory == QLatin1String("gold")   ? 40000 + 10000 * c
                        : s.victory == QLatin1String("explore") ? 60 + std::min(c, 8) * 4
                                                                : 0;
        GameState gs;
        QString why;
        if (!generateScenario(content, s, &gs, &why)) return fail(QStringLiteral("chapter %1: %2").arg(c + 1).arg(why));
        const QString goal = s.victory == QLatin1String("capital") ? QStringLiteral("Take the enemy capital.")
                           : s.victory == QLatin1String("slay") ? QStringLiteral("Slay the %1.").arg(content.unit(content.unitId(s.monster))->name)
                           : s.victory == QLatin1String("days") ? QStringLiteral("Hold the most towns after %1 days.").arg(s.victoryAmount)
                           : s.victory == QLatin1String("gold") ? QStringLiteral("Be the first with %1 gold.").arg(s.victoryAmount)
                           : s.victory == QLatin1String("artifact") ? QStringLiteral("Bring back the %1.").arg(content.artifact(content.artifactId(s.artifact))->name)
                           : s.victory == QLatin1String("explore") ? QStringLiteral("Explore %1% of the land.").arg(s.victoryAmount)
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
