#include "server/GameSession.h"

#include <QJsonArray>
#include <climits>

#include "ai/AiFactory.h"
#include "ai/PassiveAi.h"
#include "core/content/ContentRegistry.h"
#include "core/data/JsonReader.h"
#include "core/model/PlayerView.h"
#include "core/rules/Automation.h"
#include "core/rules/Dwellings.h"
#include "core/rules/GameSetup.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Movement.h"
#include "core/rules/Tavern.h"
#include "core/rules/TownRules.h"
#include "core/rules/Trade.h"
#include "core/util/Log.h"
#include "core/rules/TurnCycle.h"
#include "core/rules/Victory.h"
#include "net/Protocol.h"

namespace kot::server {

using namespace kot::core;
using net::Message;

namespace {

CommandResult failure(const QString& code, const QString& message) {
    CommandResult r;
    r.code = code;
    r.message = message;
    return r;
}
CommandResult success(const QJsonObject& info = {}) {
    CommandResult r;
    r.ok = true;
    r.info = info;
    return r;
}
CommandResult fromAction(const ActionResult& a, const QJsonObject& info = {}) {
    return a.ok ? success(info) : failure("failed", a.error);
}

int toInt(quint32 v) { return static_cast<int>(v); }

QJsonObject resourcesJson(const ResourceAmounts& m) {
    QJsonObject o;
    for (auto it = m.begin(); it != m.end(); ++it) o.insert(it.key(), it.value());
    return o;
}
QJsonObject buyJson(const BuyReport& r) {
    QJsonObject o;
    o.insert("unitsBought", r.unitsBought);
    o.insert("townsServed", r.townsServed);
    o.insert("spent", resourcesJson(r.spent));
    return o;
}
QJsonObject buildReportJson(const AutoBuildReport& r) {
    QJsonObject o;
    QJsonArray built, choices, poor;
    for (const auto& b : r.built) built.append(QJsonObject{{"town", toInt(b.town.value())}, {"building", b.building}});
    for (const auto& c : r.needsChoice)
        choices.append(QJsonObject{{"town", toInt(c.town.value())}, {"buildings", QJsonArray::fromStringList(c.buildings)}});
    for (const TownId t : r.cannotAfford) poor.append(toInt(t.value()));
    o.insert("built", built);
    o.insert("needsChoice", choices);
    o.insert("cannotAfford", poor);
    return o;
}

}  // namespace

GameSession::GameSession(const ContentRegistry& content) : m_content(content) {}

bool GameSession::start(const net::LobbyState& lobby, QString* error) {
    GameSetupParams params;
    params.mapWidth = lobby.settings.mapWidth;
    params.mapHeight = lobby.settings.mapHeight;
    params.seed = lobby.settings.seed;
    QList<const net::SlotInfo*> occupiedSlots;
    for (const net::SlotInfo& s : lobby.slotList) {
        if (!s.occupied()) continue;
        PlayerSetup p;
        p.name = s.name;
        p.isAi = (s.kind == net::SlotKind::Ai);
        p.aiProfile = s.aiProfile;
        p.race = s.race;
        p.team = s.team;
        p.color = s.color;
        params.players.append(p);
        occupiedSlots.append(&s);
    }
    QList<PlayerId> ids;
    if (!createGame(m_content, params, &m_state, &ids, error)) return false;

    m_clientPlayers.clear();
    m_autoPlayers.clear();
    for (int i = 0; i < ids.size(); ++i) {
        if (occupiedSlots[i]->kind == net::SlotKind::Human) m_clientPlayers.insert(occupiedSlots[i]->clientId, ids[i]);
        else m_autoPlayers.insert(ids[i], ai::createAi(occupiedSlots[i]->aiProfile, params.seed ^ (0x9e3779b97f4a7c15ULL * (i + 1))));
    }
    m_active = true;
    playAutoTurns();
    return true;
}

void GameSession::playAutoTurns() {
    // At most one full round, so a game with no human left cannot spin forever.
    for (int i = 0; i < m_state.turnOrder().size() && isAuto(m_state.currentPlayer()); ++i) {
        if (gameOutcome(m_state).over) return;
        const PlayerId p = m_state.currentPlayer();
        playAiTurn(p, *m_autoPlayers.value(p));
        endTurn(m_state, m_content);
    }
}

void GameSession::playAiTurn(PlayerId player, ai::IAiPlayer& ai) {
    constexpr int kMaxCommands = 200;  // a confused AI cannot hang the game
    ai.beginTurn(m_state, m_content, player);
    for (int i = 0; i < kMaxCommands; ++i) {
        if (!m_state.player(player)) return;
        const Message cmd = ai.nextCommand(m_state, m_content, player);
        if (cmd.type == net::msg::endTurn) return;
        const CommandResult r = dispatch(player, cmd);
        ai.commandDone(cmd, r.ok);
    }
    qCWarning(lcKotServer) << "AI" << ai.profileName() << "used up its command budget";
}

void GameSession::clientLeft(int clientId) {
    const PlayerId p = m_clientPlayers.take(clientId);
    if (!p.isValid()) return;
    m_autoPlayers.insert(p, std::make_shared<ai::PassiveAi>());
    playAutoTurns();
}

QJsonObject GameSession::viewFor(PlayerId player) const {
    return buildPlayerView(m_state, m_content, player);
}

QJsonObject GameSession::youInfo(PlayerId player) const {
    QJsonObject o;
    o.insert("playerId", toInt(player.value()));
    o.insert("team", toInt(m_state.teamOf(player).value()));
    return o;
}

CommandResult GameSession::handle(int clientId, const Message& command) {
    if (!m_active) return failure("no_game", "no game is running");
    const PlayerId player = playerOfClient(clientId);
    if (!player.isValid()) return failure("not_a_player", "you are not a player in this game");
    if (gameOutcome(m_state).over) return failure("game_over", "the game is over");
    if (m_state.currentPlayer() != player)
        return failure("not_your_turn", "it is not your turn");
    CommandResult result = dispatch(player, command);
    // A player who just lost his last hero and town cannot go on: his turn ends.
    if (result.ok && command.type != net::msg::endTurn && isEliminated(m_state, player) && !gameOutcome(m_state).over)
        endTurn(m_state, m_content);
    if (result.ok) playAutoTurns();
    return result;
}

CommandResult GameSession::dispatch(PlayerId player, const Message& cmd) {
    QStringList errors;
    core::JsonReader r(cmd.body, cmd.type, &errors);
    const auto id32 = [&](const char* key, bool optional = false) {
        return static_cast<quint32>(optional ? r.optionalInt(key, 0, 0, INT_MAX) : r.requireInt(key, 1, INT_MAX));
    };
    const auto bad = [&]() { return failure("bad_request", errors.join("; ")); };
    const auto strategyOf = [&]() {
        return r.optionalString("strategy", "best") == "most" ? BuyStrategy::MostUnits : BuyStrategy::BestFirst;
    };

    if (cmd.type == net::msg::endTurn) {
        r.finish();
        if (!r.ok()) return bad();
        const TurnInfo t = endTurn(m_state, m_content);
        QJsonObject info;
        info.insert("day", t.day);
        info.insert("newDay", t.newDay);
        return success(info);
    }
    if (cmd.type == net::msg::moveHero) {
        const HeroId hero(id32("hero"));
        const int x = r.requireInt("x", 0, 100000), y = r.requireInt("y", 0, 100000);
        r.finish();
        if (!r.ok()) return bad();
        const Hero* h = m_state.hero(hero);
        if (!h || h->owner != player) return failure("failed", "that is not your hero");
        const MoveResult m = moveHero(m_state, m_content, hero, Pos{x, y});
        if (!m.ok) return failure("failed", m.error);
        QJsonObject info;
        info.insert("arrived", m.arrived);
        info.insert("stepsTaken", m.stepsTaken);
        info.insert("movementSpent", m.movementSpent);
        if (m.capturedMine.isValid()) info.insert("capturedMine", toInt(m.capturedMine.value()));
        if (m.capturedTown.isValid()) info.insert("capturedTown", toInt(m.capturedTown.value()));
        if (m.capturedDwelling.isValid()) info.insert("capturedDwelling", toInt(m.capturedDwelling.value()));
        if (m.attacked) {
            QJsonArray battles;
            for (const BattleReport& b : m.battles) battles.append(battleReportToJson(b));
            info.insert("battles", battles);
            info.insert("heroKilled", m.heroKilled);
        }
        return success(info);
    }
    if (cmd.type == net::msg::build) {
        const TownId town(id32("town"));
        const QString building = r.requireString("building");
        const bool useTrade = r.optionalBool("useTrade", false);
        r.finish();
        if (!r.ok()) return bad();
        if (!useTrade) return fromAction(buildBuilding(m_state, m_content, town, building, player));
        const BuildResult b = buildBuildingWithTrade(m_state, m_content, town, building, player);
        QJsonArray trades;
        for (const TradeStep& s : b.trades)
            trades.append(QJsonObject{{"give", s.give}, {"giveAmount", s.giveAmount}, {"get", s.get}, {"getAmount", s.getAmount}});
        return b.ok ? success(QJsonObject{{"trades", trades}}) : failure("failed", b.error);
    }
    if (cmd.type == net::msg::recruit) {
        const TownId town(id32("town"));
        const QString unit = r.requireString("unit");
        const int count = r.requireInt("count", 1, 100000);
        const ArmyId army(id32("army", true));
        r.finish();
        if (!r.ok()) return bad();
        return fromAction(recruit(m_state, m_content, town, player, unit, count, army));
    }
    if (cmd.type == net::msg::recruitAtDwelling) {
        const MapDwellingId d(id32("dwelling"));
        const int count = r.requireInt("count", 1, 100000);
        const ArmyId army(id32("army"));
        r.finish();
        if (!r.ok()) return bad();
        return fromAction(recruitAtDwelling(m_state, m_content, d, player, count, army));
    }
    if (cmd.type == net::msg::hireHero) {
        const TownId town(id32("town"));
        const QString hero = r.requireString("hero");
        const bool useTrade = r.optionalBool("useTrade", false);
        r.finish();
        if (!r.ok()) return bad();
        const HireResult h = hireHero(m_state, m_content, town, player, hero, useTrade);
        return h.ok ? success(QJsonObject{{"hero", toInt(h.hero.value())}}) : failure("failed", h.error);
    }
    if (cmd.type == net::msg::trade) {
        const QString give = r.requireString("give"), get = r.requireString("get");
        const int amount = r.requireInt("amount", 1, INT_MAX);
        r.finish();
        if (!r.ok()) return bad();
        return fromAction(trade(m_state, m_content, player, give, amount, get));
    }
    if (cmd.type == net::msg::moveUnits) {
        const ArmyId from(id32("from")), to(id32("to"));
        const int stack = r.requireInt("stack", 0, 6), count = r.requireInt("count", 1, 100000);
        r.finish();
        if (!r.ok()) return bad();
        const Army* a = m_state.army(from);
        if (!a || a->owner != player) return failure("failed", "that is not your army");
        return fromAction(moveUnits(m_state, from, to, stack, count));
    }
    if (cmd.type == net::msg::pickTrait) {
        const HeroId hero(id32("hero"));
        const QString trait = r.requireString("trait");
        r.finish();
        if (!r.ok()) return bad();
        Hero* h = m_state.hero(hero);
        if (!h || h->owner != player) return failure("failed", "that is not your hero");
        QString why;
        return pickTrait(*h, m_content, trait, &why) ? success() : failure("failed", why);
    }
    if (cmd.type == net::msg::buyAllUnits) {
        const TownId town(id32("town", true));
        const BuyStrategy strategy = strategyOf();
        r.finish();
        if (!r.ok()) return bad();
        return success(buyJson(buyAllUnits(m_state, m_content, player, town, strategy)));
    }
    if (cmd.type == net::msg::autoBuild) {
        const TownId town(id32("town", true));
        const bool useTrade = r.optionalBool("useTrade", false);
        r.finish();
        if (!r.ok()) return bad();
        return success(buildReportJson(autoBuild(m_state, m_content, player, town, useTrade)));
    }
    if (cmd.type == net::msg::buildThenBuy) {
        const TownId town(id32("town", true));
        const bool useTrade = r.optionalBool("useTrade", false);
        const BuyStrategy strategy = strategyOf();
        r.finish();
        if (!r.ok()) return bad();
        const BuildAndBuyReport rep = buildThenBuyUnits(m_state, m_content, player, town, useTrade, strategy);
        QJsonObject info;
        info.insert("build", buildReportJson(rep.build));
        info.insert("buy", buyJson(rep.buy));
        return success(info);
    }
    if (cmd.type == net::msg::sendDwellingUnits) {
        const TownId town(id32("town", true));
        r.finish();
        if (!r.ok()) return bad();
        const SendReport s = town.isValid() ? sendDwellingUnitsToTown(m_state, player, town)
                                            : sendDwellingUnitsToClosestTown(m_state, player);
        return success(QJsonObject{{"dwellingsEmptied", s.dwellingsEmptied}, {"unitsSent", s.unitsSent}});
    }
    return failure("unknown_message", "unknown message type '" + cmd.type + "'");
}

}  // namespace kot::server
