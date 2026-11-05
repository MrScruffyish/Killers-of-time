#include "core/rules/Scenario.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

#include "core/content/ContentRegistry.h"
#include "core/rules/Movement.h"
#include "core/rules/TurnCycle.h"
#include "core/rules/Vision.h"

namespace kot::core {

namespace {

bool infoFrom(const QJsonObject& root, const GameState& gs, const ContentRegistry& content, ScenarioInfo* info) {
    info->id = root.value("id").toString();
    info->name = root.value("name").toString(info->id);
    info->description = root.value("description").toString();
    info->width = gs.map().width();
    info->height = gs.map().height();
    info->races.clear();
    for (const PlayerId p : gs.turnOrder())
        if (const Player* pl = gs.player(p)) info->races << content.race(pl->race).key;
    return !info->id.isEmpty();
}

}  // namespace

bool loadScenario(const QString& path, const ContentRegistry& content, Scenario* out, QStringList* errors) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errors) *errors << QStringLiteral("%1: cannot open").arg(path);
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject()) {
        if (errors) *errors << QStringLiteral("%1: %2").arg(path, pe.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value("format").toString() != QLatin1String("kot-scenario")) {
        if (errors) *errors << QStringLiteral("%1: not a scenario (format must be kot-scenario)").arg(path);
        return false;
    }
    GameState gs;
    QStringList problems;
    if (!GameState::fromJson(root.value("game").toObject(), content, &gs, &problems)) {
        if (errors)
            for (const QString& p : problems) *errors << QStringLiteral("%1: %2").arg(path, p);
        return false;
    }
    Scenario s;
    if (!infoFrom(root, gs, content, &s.info)) {
        if (errors) *errors << QStringLiteral("%1: the scenario needs an id").arg(path);
        return false;
    }
    if (gs.turnOrder().size() < 2) {
        if (errors) *errors << QStringLiteral("%1: a scenario needs at least two players").arg(path);
        return false;
    }
    s.game = root.value("game").toObject();
    *out = s;
    return true;
}

bool saveScenario(const QString& path, const QString& id, const QString& name, const QString& description,
                  const GameState& state, const ContentRegistry& content, QString* error) {
    QJsonObject root;
    root.insert("format", "kot-scenario");
    root.insert("id", id);
    root.insert("name", name);
    root.insert("description", description);
    root.insert("game", state.toJson(content, GameState::IdStyle::Text));
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

QList<ScenarioInfo> listScenarios(const QString& dir, const ContentRegistry& content) {
    QList<ScenarioInfo> list;
    for (const QFileInfo& fi : QDir(dir).entryInfoList({"*.json"}, QDir::Files, QDir::Name)) {
        Scenario s;
        if (loadScenario(fi.filePath(), content, &s, nullptr)) list << s.info;
    }
    return list;
}

bool startScenario(const Scenario& scenario, const ContentRegistry& content, const GameSetupParams& params,
                   GameState* out, QList<PlayerId>* playerIds, QString* error) {
    GameState gs;
    QStringList problems;
    if (!GameState::fromJson(scenario.game, content, &gs, &problems)) {
        if (error) *error = problems.join("; ");
        return false;
    }
    const QList<PlayerId> order = gs.turnOrder();
    if (params.players.size() != order.size()) {
        if (error)
            *error = QStringLiteral("the scenario \"%1\" is for %2 players, the lobby has %3")
                         .arg(scenario.info.name).arg(order.size()).arg(params.players.size());
        return false;
    }
    DifficultyLevel level = content.balance().level(params.difficulty);
    if (!content.balance().difficulty.contains(params.difficulty)) level = DifficultyLevel();
    gs.setDifficulty(params.difficulty);
    QMap<int, TeamId> teams;
    for (int i = 0; i < order.size(); ++i) {
        const PlayerSetup& ps = params.players[i];
        Player* p = gs.player(order[i]);
        if (!teams.contains(ps.team)) teams.insert(ps.team, gs.newTeam());
        p->name = ps.name;
        p->team = teams[ps.team];
        p->color = ps.color;
        p->controller = ps.isAi ? Player::Controller::Ai : Player::Controller::Human;
        p->aiProfile = ps.aiProfile;
        if (!ps.isAi) {
            for (const auto [r, amount] : ResourceAmounts(p->resources)) p->resources[r] = amount * level.humanStartPercent / 100;
        } else {
            const DifficultyLevel slot = content.balance().aiSlot.value(ps.aiDifficulty);
            p->incomePercent = level.aiIncomePercent + slot.aiIncomePercent;
            p->growthPercent = level.aiGrowthPercent + slot.aiGrowthPercent;
        }
        for (Town& t : gs.towns())
            if (t.owner == order[i] && t.name.endsWith(QStringLiteral("'s capital"))) t.name = ps.name + QStringLiteral("'s capital");
    }
    // The explored maps belonged to the scenario's old teams: start fresh.
    for (const PlayerId id : order) {
        gs.exploredMap(gs.teamOf(id)).fill(0);
        refreshHeroMovement(gs, content, id);
        updateExplored(gs, content, gs.teamOf(id));
    }
    gs.setDay(1);
    beginGame(gs, content);
    *out = std::move(gs);
    if (playerIds) *playerIds = order;
    return true;
}

}  // namespace kot::core
