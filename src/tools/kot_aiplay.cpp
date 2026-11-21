// kot_aiplay: computer players against each other on random maps, to see how well
// the AI plays. Prints, week by week and averaged over the games, what each AI seat
// has: heroes, towns, army strength (all its armies, garrisons too), gold, and how
// many heroes it has lost so far; then who won and how long the AIs spent without a hero.
//
//   kot_aiplay [--games n] [--days n] [--players n] [--size n] [--map continent|islands]
//              [--profiles simple,aggressive,...] [--races humans,undead,...] [--seed n] [--views]
//   kot_aiplay --campaign intrinsic-1 [--times 20] [--days n] [--profiles simple,...] [--seed n]
//
// --campaign plays a shipped campaign with AI seats only and reports how far the runs
// get, chapter by chapter: a fixed difficulty benchmark that can be compared over time.
// Every run starts with fresh progress, so the chapters are independent.
//
// --views: also builds every player's view after each turn, as the server sends it to
// the clients, and prints what that costs (time, bytes, compressed bytes); for
// profiling the server (with perf: perf-server.sh).

#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QSet>
#include <QTemporaryDir>
#include <QVector>
#include <algorithm>
#include <clocale>
#include <cstdio>

#include "core/content/ContentRegistry.h"
#include "core/data/DataDir.h"
#include "core/rules/ArmyRules.h"
#include "core/rules/Campaign.h"
#include "core/rules/Scenario.h"
#include "core/rules/Victory.h"
#include "server/GameSession.h"

using namespace kot;

namespace {

// A fingerprint of the whole game state at the end, from what it means (names,
// numbers, positions), not how it is stored: two versions of the code that play the
// same game give the same fingerprint (a check for refactorings).
QString fingerprint(const core::GameState& st, const core::ContentRegistry& content) {
    QStringList lines;
    lines << QStringLiteral("day %1").arg(st.day());
    for (const core::Player& p : st.players()) {
        QStringList res;
        for (const auto [r, amount] : p.resources) res << QStringLiteral("%1=%2").arg(content.resource(r).key).arg(amount);  // (no zeros)
        res.sort();
        lines << QStringLiteral("player %1 %2").arg(p.id.value()).arg(res.join(','));
    }
    for (const core::Hero& h : st.heroes())
        lines << QStringLiteral("hero %1 %2 %3 %4 %5 %6 %7").arg(h.id.value()).arg(h.owner.value()).arg(h.level).arg(h.experience)
                     .arg(h.movementPoints).arg(h.spellPoints).arg(h.spells.size());
    for (const core::Army& a : st.armies()) {
        QStringList stacks;
        for (const core::UnitStack& s : a.stacks) {
            const core::UnitDef* u = content.unit(s.unitTypeId());
            stacks << QStringLiteral("%1x%2/%3").arg(u ? u->name : QStringLiteral("?")).arg(s.count()).arg(s.totalHp());
        }
        lines << QStringLiteral("army %1 %2 %3,%4 %5").arg(a.id.value()).arg(a.owner.value()).arg(a.pos.x).arg(a.pos.y).arg(stacks.join(' '));
    }
    for (const core::Town& t : st.towns()) {
        QStringList built;
        for (const auto& b : t.buildings)
            if (const core::BuildingDef* d = content.building(b)) built << d->name;
        built.sort();
        lines << QStringLiteral("town %1 %2 %3").arg(t.id.value()).arg(t.owner.value()).arg(built.join(','));
    }
    return QString::fromLatin1(QCryptographicHash::hash(lines.join('\n').toUtf8(), QCryptographicHash::Md5).toHex().left(12));
}

struct Seat {
    double heroes = 0, towns = 0, strength = 0, gold = 0, lost = 0;
};

const char* goalName(core::VictoryCondition::Type type) {
    switch (type) {
        case core::VictoryCondition::Type::Conquest: return "conquest";
        case core::VictoryCondition::Type::HoldTown: return "capital";
        case core::VictoryCondition::Type::Gold: return "gold";
        case core::VictoryCondition::Type::Days: return "days";
        case core::VictoryCondition::Type::SlayMonster: return "slay";
        case core::VictoryCondition::Type::FindArtifact: return "artifact";
        case core::VictoryCondition::Type::ExploreMap: return "explore";
    }
    return "?";
}

struct ChapterStat {
    int reached = 0, won = 0, lost = 0, timeout = 0;
    QList<int> winDays;
};

// Plays a fixed campaign with every seat run by an AI. The first seat is the one whose
// heroes carry over (it would be the human's), so a run is a difficulty measurement:
// how many chapters can be cleared before the player loses or runs out of days.
int playCampaigns(const QString& dir, const core::ContentRegistry& content, const QString& id, int times, int dayLimit,
                  quint64 firstSeed, const QStringList& profiles) {
    core::CampaignDef def;
    QStringList problems;
    if (!core::loadCampaign(QDir(dir).filePath(QStringLiteral("campaigns/%1.json").arg(id)), &def, &problems)) {
        std::fprintf(stderr, "kot_aiplay: %s\n", qPrintable(problems.join("; ")));
        return 1;
    }
    QStringList goals;
    QList<int> seats;
    for (const QString& scenarioId : def.scenarios) {
        core::Scenario scenario;
        const QString path = QDir(dir).filePath(QStringLiteral("scenarios/%1.json").arg(scenarioId));
        if (!core::loadScenario(path, content, &scenario, &problems)) {
            std::fprintf(stderr, "kot_aiplay: %s\n", qPrintable(problems.join("; ")));
            return 1;
        }
        core::GameState initial;
        if (!core::GameState::fromJson(scenario.game, content, &initial, &problems)) {
            std::fprintf(stderr, "kot_aiplay: %s\n", qPrintable(problems.join("; ")));
            return 1;
        }
        goals << QStringLiteral("%1 (%2)").arg(scenario.info.name, QString::fromLatin1(goalName(initial.victory().type)));
        seats << scenario.info.races.size();
    }
    QVector<ChapterStat> stats(def.scenarios.size());
    QList<int> furthest;
    int completed = 0, stuck = 0;
    static const char* colors[] = {"#d62728", "#1f77b4", "#2ca02c", "#ff7f0e", "#9467bd", "#8c564b", "#e377c2", "#7f7f7f"};
    for (int run = 0; run < times; ++run) {
        QTemporaryDir saves;  // each run starts a fresh campaign
        int reached = 0;
        for (int ch = 0; ch < def.scenarios.size(); ++ch) {
            net::LobbyState lobby;
            lobby.settings.campaign = id;
            lobby.settings.seed = firstSeed + run;
            lobby.settings.autosaves = 0;
            for (int i = 0; i < seats[ch]; ++i) {
                net::SlotInfo s;
                s.index = i;
                s.kind = net::SlotKind::Ai;
                s.name = QStringLiteral("AI %1").arg(i + 1);
                s.race = QStringLiteral("humans");  // the scenario decides the races
                s.aiProfile = profiles[i % profiles.size()];
                s.team = i + 1;
                s.color = QString::fromLatin1(colors[i % 8]);
                s.ready = true;
                lobby.slotList.append(s);
            }
            server::GameSession session(content);
            session.setCampaignDir(saves.path());
            QString error;
            if (!session.start(lobby, &error)) {
                std::fprintf(stderr, "kot_aiplay: run %d chapter %d: %s\n", run + 1, ch + 1, qPrintable(error));
                return 1;
            }
            if (session.campaignChapter() != ch) {
                std::fprintf(stderr, "kot_aiplay: run %d expected chapter %d, got %d\n", run + 1, ch + 1,
                             session.campaignChapter() + 1);
                return 1;
            }
            core::GameState& st = session.state();
            const core::PlayerId player = st.turnOrder().first();
            int lastDay = 0;
            bool halted = false;
            while (st.day() <= dayLimit && !core::gameOutcome(st, content).over) {
                session.playAutoTurns();
                if (st.day() == lastDay && !core::gameOutcome(st, content).over) {
                    halted = true;
                    break;
                }
                lastDay = st.day();
            }
            if (halted) ++stuck;
            session.recordCampaignResult();
            const core::GameOutcome outcome = core::gameOutcome(st, content);
            const bool won = outcome.over && outcome.winner.isValid() && outcome.winner == st.teamOf(player);
            ChapterStat& cs = stats[ch];
            ++cs.reached;
            if (won) {
                ++cs.won;
                cs.winDays << st.day();
            } else if (outcome.over) {
                ++cs.lost;
            } else {
                ++cs.timeout;
            }
            reached = ch + 1;
            std::fprintf(stderr, "run %d/%d chapter %d/%d: %s on day %d\n", run + 1, times, ch + 1,
                         static_cast<int>(def.scenarios.size()), won ? "won" : outcome.over ? "lost" : "no winner", st.day());
            if (!won) break;
            if (ch + 1 == def.scenarios.size()) ++completed;
        }
        furthest << reached;
    }
    std::printf("%s: %d runs, %d completed (%.0f%%), %d stalled games\n", qPrintable(id), times, completed,
                100.0 * completed / times, stuck);
    int goalWidth = 4;  // the header "goal"
    for (const QString& g : goals) goalWidth = std::max(goalWidth, g.size());
    std::printf("%7s | %-*s | %7s | %3s | %4s | %9s | %14s\n", "chapter", goalWidth, "goal", "reached", "won", "lost",
                "no winner", "median win day");
    for (int ch = 0; ch < stats.size(); ++ch) {
        const ChapterStat& cs = stats[ch];
        QList<int> days = cs.winDays;
        std::sort(days.begin(), days.end());
        std::printf("%7d | %-*s | %7d | %3d | %4d | %9d | %14d\n", ch + 1, goalWidth, qPrintable(goals[ch]), cs.reached,
                    cs.won, cs.lost, cs.timeout, days.isEmpty() ? 0 : days[days.size() / 2]);
    }
    std::printf("furthest chapter reached:");
    for (int ch = 0; ch < def.scenarios.size(); ++ch)
        std::printf(" %d:%d", ch + 1, static_cast<int>(std::count(furthest.begin(), furthest.end(), ch + 1)));
    std::printf("\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::setlocale(LC_NUMERIC, "C");  // 1.5, not 1,5
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Computer players against each other: how well does the AI play?"));
    parser.addHelpOption();
    QCommandLineOption dataOption(QStringLiteral("data"), QStringLiteral("The game data folder."), QStringLiteral("dir"));
    QCommandLineOption saveOption(QStringLiteral("save"), QStringLiteral("Save the last game's end state there (kot_saveview reads it)."),
                                  QStringLiteral("file"));
    parser.addOption(saveOption);
    QCommandLineOption viewsOption(QStringLiteral("views"), QStringLiteral("Also build every player's view after each turn and print the cost."));
    parser.addOption(viewsOption);
    QCommandLineOption gamesOption(QStringLiteral("games"), QStringLiteral("Games to play."), QStringLiteral("n"), "6");
    QCommandLineOption daysOption(QStringLiteral("days"), QStringLiteral("Days per game."), QStringLiteral("n"), "56");
    QCommandLineOption playersOption(QStringLiteral("players"), QStringLiteral("AI players."), QStringLiteral("n"), "3");
    QCommandLineOption sizeOption(QStringLiteral("size"), QStringLiteral("Map width and height."), QStringLiteral("n"), "64");
    QCommandLineOption mapOption(QStringLiteral("map"), QStringLiteral("continent or islands."), QStringLiteral("type"), "continent");
    QCommandLineOption profilesOption(QStringLiteral("profiles"), QStringLiteral("AI profile per seat (repeats)."), QStringLiteral("list"), "simple");
    QCommandLineOption racesOption(QStringLiteral("races"), QStringLiteral("Race per seat (repeats)."), QStringLiteral("list"),
                                   "humans,undead,sylvan,dwarves,orcs,ashborn,tidefolk,necroman,giants,halflings");
    QCommandLineOption seedOption(QStringLiteral("seed"), QStringLiteral("Seed of the first game."), QStringLiteral("n"), "1");
    QCommandLineOption campaignOption(QStringLiteral("campaign"), QStringLiteral("Play a shipped campaign with AI seats (id)."),
                                      QStringLiteral("id"));
    QCommandLineOption timesOption(QStringLiteral("times"), QStringLiteral("Campaigns to play."), QStringLiteral("n"), "20");
    for (auto* o : {&dataOption, &gamesOption, &daysOption, &playersOption, &sizeOption, &mapOption, &profilesOption, &racesOption,
                    &seedOption, &campaignOption, &timesOption})
        parser.addOption(*o);
    parser.process(app);

    const QString dir = core::findDataDirectory(parser.value(dataOption));
    core::ContentRegistry content;
    QStringList errors;
    if (dir.isEmpty() || !content.loadFromDirectory(dir, &errors)) {
        std::fprintf(stderr, "kot_aiplay: cannot load the game data\n");
        for (const QString& e : errors) std::fprintf(stderr, "  %s\n", qPrintable(e));
        return 1;
    }
    const int games = std::clamp(parser.value(gamesOption).toInt(), 1, 1000);
    const int days = std::clamp(parser.value(daysOption).toInt(), 1, 400);
    const int players = std::clamp(parser.value(playersOption).toInt(), 2, 8);
    const QStringList profiles = parser.value(profilesOption).split(',', Qt::SkipEmptyParts);
    const QStringList races = parser.value(racesOption).split(',', Qt::SkipEmptyParts);
    const quint64 firstSeed = parser.value(seedOption).toULongLong();
    if (parser.isSet(campaignOption)) {
        const int times = std::clamp(parser.value(timesOption).toInt(), 1, 1000);
        const int campaignDays = parser.isSet(daysOption) ? days : 150;  // chapters with a days goal need longer
        return playCampaigns(dir, content, parser.value(campaignOption), times, campaignDays, firstSeed, profiles);
    }
    const int weeks = (days + 6) / 7;

    QVector<QVector<Seat>> table(weeks, QVector<Seat>(players));  // sums over games
    QVector<int> wins(players, 0), out(players, 0), heroless(players, 0);
    int undecided = 0;
    struct RaceResult {
        int games = 0, wins = 0, out = 0;
        qint64 strength = 0;  // army strength at the end, summed
        int towns = 0;
    };
    QMap<QString, RaceResult> byRace;  // balance: how each race does
    QList<int> endDays;                // the day each decided game ended
    QHash<QString, int> causes;  // how heroes were lost
    const bool measureViews = parser.isSet(viewsOption);
    qint64 aiNs = 0, viewNs = 0, jsonNs = 0, viewBytes = 0, zippedBytes = 0, views = 0, biggestView = 0;
    QElapsedTimer clock;
    for (int g = 0; g < games; ++g) {
        net::LobbyState lobby;
        lobby.settings.seed = firstSeed + g;
        lobby.settings.mapWidth = lobby.settings.mapHeight = parser.value(sizeOption).toInt();
        lobby.settings.mapType = parser.value(mapOption);
        lobby.settings.autosaves = 0;
        static const char* colors[] = {"#d62728", "#1f77b4", "#2ca02c", "#ff7f0e", "#9467bd", "#8c564b", "#e377c2", "#7f7f7f"};
        for (int i = 0; i < players; ++i) {
            net::SlotInfo s;
            s.index = i;
            s.kind = net::SlotKind::Ai;
            s.name = QStringLiteral("AI %1").arg(i + 1);
            s.race = races[(i + g) % races.size()];  // races rotate over the seats from game to game
            s.aiProfile = profiles[i % profiles.size()];
            s.team = i + 1;
            s.color = colors[i];
            s.ready = true;
            lobby.slotList.append(s);
        }
        server::GameSession session(content);
        QString error;
        if (!session.start(lobby, &error)) {
            std::fprintf(stderr, "kot_aiplay: game %d: %s\n", g + 1, qPrintable(error));
            return 1;
        }
        const core::GameState& st = session.state();
        QList<core::PlayerId> ids;
        for (const core::Player& p : st.players()) ids.append(p.id);
        QHash<quint32, core::PlayerId> heroOwner;  // heroes seen alive
        session.setBattleObserver([&](const core::BattleReport& r, bool townDefended) {
            if (!r.loserHeroKilled && !r.retreated) return;
            const bool attackerLost = !r.attackerWon();
            QString what;
            if (attackerLost)
                what = townDefended ? (r.defender.owner.isValid() ? "attacking an enemy town" : "attacking a neutral town")
                       : r.defender.hero.isValid() ? "attacking a hero" : "attacking monsters";
            else
                what = townDefended ? "defending a town" : "attacked by a hero";
            ++causes[(r.retreated ? "fled " : "") + what];
        });
        QVector<int> lost(players, 0);
        int lastDay = 0;
        while (st.day() <= days && !core::gameOutcome(st, content).over) {
            if (st.day() != lastDay) {
                lastDay = st.day();
                // Heroes gone since yesterday were beaten.
                QSet<quint32> alive;
                for (const core::Hero& h : st.heroes()) alive.insert(h.id.value());
                for (auto it = heroOwner.begin(); it != heroOwner.end();) {
                    if (!alive.contains(it.key())) {
                        ++lost[ids.indexOf(it.value())];
                        it = heroOwner.erase(it);
                    } else {
                        ++it;
                    }
                }
                for (const core::Hero& h : st.heroes()) heroOwner.insert(h.id.value(), h.owner);
                for (int i = 0; i < players; ++i) {
                    int heroes = 0;
                    for (const core::Hero& h : st.heroes()) heroes += h.owner == ids[i];
                    if (heroes == 0 && !core::isEliminated(st, ids[i])) ++heroless[i];
                }
                if ((st.day() - 1) % 7 == 6 || st.day() == days) {  // the end of each week
                    Seat* row = table[(st.day() - 1) / 7].data();
                    for (int i = 0; i < players; ++i) {
                        Seat& s = row[i];
                        for (const core::Hero& h : st.heroes()) s.heroes += h.owner == ids[i];
                        for (const core::Town& t : st.towns()) s.towns += t.owner == ids[i];
                        for (const core::Army& a : st.armies())
                            if (a.owner == ids[i]) s.strength += core::armyStrength(st, content, a);
                        s.gold += st.player(ids[i])->resources.value(content.currency());
                        s.lost += lost[i];
                    }
                }
            }
            const int before = st.day();
            clock.start();
            session.playAutoTurns();
            aiNs += clock.nsecsElapsed();
            if (measureViews) {
                // What the server sends after every command: each player's whole view.
                for (const core::PlayerId p : ids) {
                    clock.start();
                    const QJsonObject view = session.viewFor(p);
                    viewNs += clock.nsecsElapsed();
                    clock.start();
                    const QByteArray json = QJsonDocument(view).toJson(QJsonDocument::Compact);
                    jsonNs += clock.nsecsElapsed();
                    viewBytes += json.size();
                    biggestView = std::max<qint64>(biggestView, json.size());
                    zippedBytes += qCompress(json, 1).size();
                    ++views;
                }
            }
            if (st.day() == before && !core::gameOutcome(st, content).over) {
                std::fprintf(stderr, "kot_aiplay: game %d stuck on day %d\n", g + 1, before);
                break;
            }
        }
        std::printf("game %d fingerprint %s\n", g + 1, qPrintable(fingerprint(st, content)));
        if (g + 1 == games && parser.isSet(saveOption)) {
            QString error;
            if (!st.saveToFile(parser.value(saveOption), content, &error)) std::fprintf(stderr, "kot_aiplay: cannot save: %s\n", qPrintable(error));
        }
        const core::GameOutcome end = core::gameOutcome(st, content);
        for (int i = 0; i < players; ++i) {
            const bool isOut = core::isEliminated(st, ids[i]);
            const bool won = end.over && end.winner.isValid() && st.teamOf(ids[i]) == end.winner;
            if (isOut) ++out[i];
            if (won) ++wins[i];
            const core::Player* pl = st.player(ids[i]);
            RaceResult& rr = byRace[pl ? content.race(pl->race).key : QStringLiteral("?")];
            ++rr.games;
            rr.wins += won;
            rr.out += isOut;
            for (const core::Army& a : st.armies())
                if (a.owner == ids[i]) rr.strength += qint64(core::armyStrength(st, content, a));
            for (const core::Town& t : st.towns()) rr.towns += t.owner == ids[i];
        }
        if (!end.over) ++undecided;
        else endDays << st.day();
        std::fprintf(stderr, "game %d/%d done (day %d)\n", g + 1, games, st.day());
    }

    std::printf("%d games, %d days, %d AIs, profiles %s\n", games, days, players, qPrintable(profiles.join(',')));
    std::printf("Averages at the end of each week (heroes / towns / army strength in thousands / gold / heroes lost so far)\n");
    std::printf("week");
    for (int i = 0; i < players; ++i) std::printf(" | %-30s", qPrintable(QStringLiteral("AI %1 (%2)").arg(i + 1).arg(profiles[i % profiles.size()])));
    std::printf("\n");
    for (int w = 0; w < weeks; ++w) {
        std::printf("%4d", w + 1);
        for (int i = 0; i < players; ++i) {
            const Seat& s = table[w][i];
            std::printf(" | %4.1f %4.1f %7.1f %6.0f %4.1f      ", s.heroes / games, s.towns / games, s.strength / games / 1000.0,
                        s.gold / games, s.lost / games);
        }
        std::printf("\n");
    }
    for (int i = 0; i < players; ++i)
        std::printf("AI %d: won %d, out %d, days without a hero (while in the game) %.1f per game\n", i + 1, wins[i], out[i],
                    double(heroless[i]) / games);
    std::printf("heroes lost (or fled) in battle:");
    for (auto it = causes.begin(); it != causes.end(); ++it) std::printf(" %s %d;", qPrintable(it.key()), it.value());
    std::printf("\n");
    std::printf("undecided after %d days: %d of %d", days, undecided, games);
    if (!endDays.isEmpty()) {
        std::sort(endDays.begin(), endDays.end());
        std::printf("; decided games ended on day %d (median, %d..%d)", endDays[endDays.size() / 2], endDays.first(), endDays.last());
    }
    std::printf("\nby race (games, won, out, towns at the end, army strength at the end in thousands, per game):\n");
    for (auto it = byRace.cbegin(); it != byRace.cend(); ++it)
        std::printf("  %-10s %3d games  won %3d  out %3d  towns %5.1f  army %7.1f\n", qPrintable(it.key()), it->games, it->wins, it->out,
                    double(it->towns) / it->games, it->strength / 1000.0 / it->games);
    std::printf("time in the AI and rules (playAutoTurns): %.2f s\n", aiNs / 1e9);
    if (measureViews && views > 0)
        std::printf("views: %lld built, %.2f ms each to build + %.2f ms to JSON; %.0f KB on average (biggest %.0f KB), %.0f KB compressed\n",
                    views, viewNs / 1e6 / views, jsonNs / 1e6 / views, viewBytes / 1024.0 / views, biggestView / 1024.0,
                    zippedBytes / 1024.0 / views);
    return 0;
}
