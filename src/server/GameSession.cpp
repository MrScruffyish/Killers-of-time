#include "server/GameSession.h"

#include "core/rules/ArmyRules.h"
#include "core/rules/Vision.h"

#include <QDir>
#include <QJsonArray>
#include <climits>

#include "ai/AiFactory.h"
#include "ai/PassiveAi.h"
#include "core/battle/AutoBattle.h"
#include "core/battle/BattleAi.h"
#include "core/content/ContentRegistry.h"
#include "core/data/JsonReader.h"
#include "core/magic/Spells.h"
#include "core/model/PlayerView.h"
#include "core/rules/Automation.h"
#include "core/rules/Boats.h"
#include "core/rules/Dwellings.h"
#include "core/rules/GameSetup.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Campaign.h"
#include "core/rules/Scenario.h"
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
    params.difficulty = lobby.settings.difficulty;
    params.islands = lobby.settings.mapType == QLatin1String("islands");
    QList<const net::SlotInfo*> occupiedSlots;
    for (const net::SlotInfo& s : lobby.slotList) {
        if (!s.occupied()) continue;
        PlayerSetup p;
        p.name = s.name;
        p.isAi = (s.kind == net::SlotKind::Ai);
        p.aiProfile = s.aiProfile;
        if (!s.difficulty.isEmpty()) p.aiDifficulty = s.difficulty;
        p.race = s.race;
        p.team = s.team;
        p.color = s.color;
        params.players.append(p);
        occupiedSlots.append(&s);
    }
    QList<PlayerId> ids;
    QString scenarioId = lobby.settings.scenario;
    CampaignDef campaign;
    CampaignProgress progress;
    m_campaign.clear();
    if (!lobby.settings.campaign.isEmpty()) {
        QStringList problems;
        if (!loadCampaign(QDir(m_content.dataDir()).filePath("campaigns/" + lobby.settings.campaign + ".json"), &campaign, &problems)) {
            if (error) *error = problems.join("; ");
            return false;
        }
        loadProgress(QDir(m_campaignDir).filePath("campaign_" + campaign.id + ".json"), &progress);
        if (progress.finished(campaign)) progress = CampaignProgress();  // played through: start again
        progress.campaign = campaign.id;
        scenarioId = campaign.scenarios[progress.next];
        m_campaign = campaign.id;
        m_campaignName = campaign.name;
        m_campaignChapter = progress.next;
        m_campaignChapters = static_cast<int>(campaign.scenarios.size());
        m_campaignRecorded = false;
        m_campaignResult.clear();
    }
    if (!scenarioId.isEmpty()) {
        Scenario scenario;
        QStringList problems;
        const QString path = QDir(m_content.dataDir()).filePath("scenarios/" + scenarioId + ".json");
        if (!loadScenario(path, m_content, &scenario, &problems)) {
            if (error) *error = problems.join("; ");
            return false;
        }
        if (!startScenario(scenario, m_content, params, &m_state, &ids, error)) return false;
    } else if (!createGame(m_content, params, &m_state, &ids, error)) {
        return false;
    }
    if (!lobby.settings.fogOfWar) {  // everybody sees everything from the start
        m_state.setFogOfWar(false);
        for (const Player& p : m_state.players()) updateExplored(m_state, m_content, p.team);
    }
    if (!m_campaign.isEmpty()) {
        // The first human player's heroes continue the campaign.
        for (int i = 0; i < ids.size(); ++i)
            if (!params.players[i].isAi) {
                m_campaignPlayer = ids[i];
                break;
            }
        if (!progress.heroes.isEmpty())
            applyCarriedHeroes(m_state, m_content, m_campaignPlayer, progress.heroes, campaign.carryArmy);
    }

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
        if (gameOutcome(m_state).over || m_battle) return;
        const PlayerId p = m_state.currentPlayer();
        playAiTurn(p, *m_autoPlayers.value(p));
        if (m_battle) return;  // the AI attacked a human: his turn goes on after the battle
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
        if (m_battle) return;  // waits for a human in the battle
    }
    qCWarning(lcKotServer) << "AI" << ai.profileName() << "used up its command budget";
}

void GameSession::startFromState(GameState state) {
    m_state = std::move(state);
    m_clientPlayers.clear();
    m_autoPlayers.clear();
    int i = 0;
    for (const Player& p : m_state.players()) {
        ++i;
        if (p.controller == Player::Controller::Ai)
            m_autoPlayers.insert(p.id, ai::createAi(p.aiProfile, m_state.rng().seed() ^ (0x9e3779b97f4a7c15ULL * i)));
        // Humans are free: the game waits for them (they are not skipped like players who left).
    }
    m_active = true;
    // No automatic turns yet: they start when the first human joins.
}

QList<PlayerId> GameSession::freePlayers() const {
    QList<PlayerId> free;
    for (const Player& p : m_state.players()) {
        if (p.controller == Player::Controller::Human && !m_clientPlayers.values().contains(p.id)) free.append(p.id);
    }
    return free;
}

bool GameSession::joinPlayer(int clientId, PlayerId player, QString* error) {
    if (m_clientPlayers.contains(clientId)) {
        if (error) *error = QStringLiteral("you already play in this game");
        return false;
    }
    if (!freePlayers().contains(player)) {
        if (error) *error = QStringLiteral("that player is not free");
        return false;
    }
    m_autoPlayers.remove(player);
    m_clientPlayers.insert(clientId, player);
    playAutoTurns();
    return true;
}

void GameSession::clientLeft(int clientId) {
    const PlayerId p = m_clientPlayers.take(clientId);
    if (!p.isValid()) return;
    m_autoPlayers.insert(p, std::make_shared<ai::PassiveAi>());
    if (m_battle) runBattles();  // his side is played by the battle AI now
    afterCommand();
}

QJsonObject GameSession::viewFor(PlayerId player) const {
    QJsonObject view = buildPlayerView(m_state, m_content, player);
    if (m_battle && m_battle->battle) {
        const Battle& b = *m_battle->battle;
        if (b.owner(0) == player || b.owner(1) == player) {
            const int side = b.owner(0) == player ? 0 : 1;
            QJsonObject battle = battleToJson(b, side);
            battle.insert("yourSide", side);
            view.insert("battle", battle);
        } else {
            view.insert("battleInProgress", true);
        }
    }
    if (m_lastBattle.contains(player)) view.insert("lastBattle", m_lastBattle.value(player));
    if (!m_campaign.isEmpty()) {
        QJsonObject c{{"name", m_campaignName}, {"chapter", m_campaignChapter + 1}, {"chapters", m_campaignChapters}};
        if (!m_campaignResult.isEmpty()) c.insert("result", m_campaignResult);
        view.insert("campaign", c);
    }
    return view;
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
    if (gameOutcome(m_state).over) {
        recordCampaign();
        return failure("game_over", "the game is over");
    }
    if (m_battle) {
        if (command.type != net::msg::battleAction)
            return failure("battle_in_progress", "a battle is being fought");
        CommandResult result = battleAction(player, command);
        if (result.ok) afterCommand();
        return result;
    }
    if (command.type == net::msg::battleAction) return failure("failed", "there is no battle");
    if (m_state.currentPlayer() != player)
        return failure("not_your_turn", "it is not your turn");
    CommandResult result = dispatch(player, command);
    if (result.ok) afterCommand();
    return result;
}

void GameSession::recordCampaign() {
    if (m_campaign.isEmpty() || m_campaignRecorded) return;
    const GameOutcome outcome = gameOutcome(m_state);
    if (!outcome.over) return;
    m_campaignRecorded = true;
    const bool won = outcome.winner.isValid() && outcome.winner == m_state.teamOf(m_campaignPlayer);
    m_campaignResult = won ? QStringLiteral("won") : QStringLiteral("lost");
    if (!won) return;  // the chapter can be tried again
    CampaignDef def;
    if (!loadCampaign(QDir(m_content.dataDir()).filePath("campaigns/" + m_campaign + ".json"), &def, nullptr)) return;
    CampaignProgress p;
    p.campaign = m_campaign;
    p.next = m_campaignChapter + 1;
    for (const HeroId h : bestHeroes(m_state, m_campaignPlayer, def.carryHeroes)) p.heroes << heroSnapshot(m_state, h);
    QString error;
    if (!saveProgress(QDir(m_campaignDir).filePath("campaign_" + m_campaign + ".json"), p, &error))
        qCWarning(lcKotServer) << "Cannot save the campaign progress:" << error;
}

void GameSession::afterCommand() {
    recordCampaign();
    // A player who just lost his last hero and town cannot go on: his turn ends.
    if (!m_battle && !gameOutcome(m_state).over && isEliminated(m_state, m_state.currentPlayer()))
        endTurn(m_state, m_content);
    playAutoTurns();
    recordCampaign();
}

bool GameSession::humanControls(int side) const {
    if (!m_battle || !m_battle->battle || m_battle->autoSide[side]) return false;
    const PlayerId owner = m_battle->battle->owner(side);
    return owner.isValid() && m_clientPlayers.values().contains(owner);
}

void GameSession::recordBattle(const BattleReport& report) {
    const QJsonObject entry{{"id", ++m_battleCounter}, {"report", battleReportToJson(report)}};
    for (const PlayerId p : {report.attacker.owner, report.defender.owner})
        if (p.isValid()) m_lastBattle.insert(p, entry);
}

void GameSession::runBattles() {
    while (m_battle) {
        ActiveBattle& ab = *m_battle;
        if (!ab.battle) {
            const ArmyId defender = ab.attack.defenders[ab.defenderIndex];
            ab.battle = std::make_unique<Battle>(m_state, m_content, ab.attack.army, defender, newBattleSeed(m_state));
        }
        Battle& b = *ab.battle;
        while (!b.isOver() && !humanControls(b.activeUnit()->side)) {
            // The computer's actions are kept, so the human sees them happen.
            QList<BattleEvent> ev;
            b.act(chooseBattleAction(b), &ev);
            for (const BattleEvent& e : ev)
                if (m_aiEvents.size() < 60) m_aiEvents.append(battleEventToJson(e));
        }
        if (!b.isOver()) return;  // a human's turn in the battle

        const BattleReport report = applyBattleResult(m_state, m_content, b);
        ab.result.battles.append(report);
        recordBattle(report);
        ab.battle.reset();
        const bool won = report.attackerWon();
        if (won && ab.defenderIndex + 1 < ab.attack.defenders.size() && m_state.army(ab.attack.army)) {
            ++ab.defenderIndex;
            continue;
        }
        finishAttack(m_state, m_content, ab.attack, won, &ab.result);
        m_finishedMove = ab.result;
        m_battle.reset();
    }
}

QJsonObject GameSession::moveInfo(const MoveResult& m) const {
    QJsonObject info;
    info.insert("arrived", m.arrived);
    info.insert("stepsTaken", m.stepsTaken);
    info.insert("movementSpent", m.movementSpent);
    if (m.capturedMine.isValid()) info.insert("capturedMine", toInt(m.capturedMine.value()));
    if (m.capturedTown.isValid()) info.insert("capturedTown", toInt(m.capturedTown.value()));
    if (m.capturedDwelling.isValid()) info.insert("capturedDwelling", toInt(m.capturedDwelling.value()));
    if (!m.learnedSpells.isEmpty()) info.insert("learnedSpells", QJsonArray::fromStringList(m.learnedSpells));
    if (m.landed) info.insert("landed", true);
    if (m.ambushed) info.insert("ambushed", true);
    if (m.spotted) info.insert("spotted", true);
    if (!m.foundResources.isEmpty()) info.insert("foundResources", resourcesJson(m.foundResources));
    if (!m.foundArtifacts.isEmpty()) info.insert("foundArtifacts", QJsonArray::fromStringList(m.foundArtifacts));
    if (m.attacked) {
        QJsonArray battles;
        for (const BattleReport& b : m.battles) battles.append(battleReportToJson(b));
        info.insert("battles", battles);
        info.insert("heroKilled", m.heroKilled);
    }
    return info;
}

CommandResult GameSession::battleAction(PlayerId player, const Message& cmd) {
    Battle* b = m_battle->battle.get();
    const BattleUnit* active = b ? b->activeUnit() : nullptr;
    if (!active) return failure("failed", "the battle is not waiting for anyone");
    if (b->owner(active->side) != player || !humanControls(active->side))
        return failure("not_your_turn", "it is not your stack's turn");

    QStringList errors;
    core::JsonReader r(cmd.body, cmd.type, &errors);
    const QString action = r.requireString("action");
    const bool hasHex = cmd.body.contains("x");
    const int x = r.optionalInt("x", 0, 0, 1000), y = r.optionalInt("y", 0, 0, 1000);
    const int target = r.optionalInt("target", 0, 0, 1000000);
    const QString spell = r.optionalString("spell");
    r.finish();
    if (!r.ok()) return failure("bad_request", errors.join("; "));

    BattleAction a;
    a.hex = Pos{x, y};
    a.hasHex = hasHex;
    a.target = target;
    if (action == "move") a.type = BattleAction::Type::Move;
    else if (action == "attack") a.type = BattleAction::Type::Attack;
    else if (action == "shoot") a.type = BattleAction::Type::Shoot;
    else if (action == "defend") a.type = BattleAction::Type::Defend;
    else if (action == "wait") a.type = BattleAction::Type::Wait;
    else if (action == "retreat") a.type = BattleAction::Type::Retreat;
    else if (action == "cast") {
        a.type = BattleAction::Type::Cast;
        a.spell = spell;
    } else if (action == "unit_cast") {
        a.type = BattleAction::Type::UnitCast;
        a.spell = spell;
    }
    else if (action == "auto") m_battle->autoSide[active->side] = true;
    else if (action == "ai") a = chooseBattleAction(*b);  // one move chosen by the battle AI (client auto-play)
    else return failure("bad_request", "unknown battle action '" + action + "'");

    QJsonArray events;
    m_aiEvents = QJsonArray();
    if (action != "auto") {
        QList<BattleEvent> ev;
        const QString error = b->act(a, &ev);
        if (!error.isEmpty()) return failure("failed", error);
        for (const BattleEvent& e : ev) events.append(battleEventToJson(e));
    }
    runBattles();
    for (const QJsonValue& e : m_aiEvents) events.append(e);  // then what the computer did
    m_aiEvents = QJsonArray();
    QJsonObject info{{"events", events}};
    if (!m_battle) {
        info.insert("battleOver", true);
        info.insert("move", moveInfo(m_finishedMove));
    }
    return success(info);
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
    if (cmd.type == net::msg::answerJoin) {
        const QString answer = r.requireString("answer");
        r.finish();
        if (!r.ok()) return bad();
        const JoinOffer offer = m_joinOffer;
        m_joinOffer = {};
        if (!offer.army.isValid() || offer.player != player) return failure("failed", "nobody offered to join");
        if (answer == "accept") {
            const ActionResult a = acceptJoin(m_state, m_content, offer.hero, offer.army);
            if (!a.ok) return failure("failed", a.error);
            return success({{"joined", true}});
        }
        if (answer == "leave") return success({{"left", true}});
        if (answer != "fight") return failure("bad_request", "answer: accept, fight or leave");
        // Fight them after all: the hero attacks from where he stands.
        const Hero* h = m_state.hero(offer.hero);
        const Army* monsters = m_state.army(offer.army);
        if (!h || !monsters) return failure("failed", "they are gone");
        m_battle = std::make_unique<ActiveBattle>();
        m_battle->attack = PendingAttack{offer.hero, player, h->army, monsters->pos, {offer.army}};
        m_battle->result.ok = true;
        m_battle->result.attacked = true;
        m_aiEvents = QJsonArray();
        runBattles();
        if (m_battle) {
            QJsonObject info{{"battleStarted", true}, {"events", m_aiEvents}};
            m_aiEvents = QJsonArray();
            return success(info);
        }
        return success(moveInfo(m_finishedMove));
    }
    m_joinOffer = {};  // any other command declines a standing offer
    if (cmd.type == net::msg::moveHero) {
        const HeroId hero(id32("hero"));
        const int x = r.requireInt("x", 0, 100000), y = r.requireInt("y", 0, 100000);
        r.finish();
        if (!r.ok()) return bad();
        const Hero* h = m_state.hero(hero);
        if (!h || h->owner != player) return failure("failed", "that is not your hero");
        const MoveResult m = moveHero(m_state, m_content, hero, Pos{x, y}, MoveMode::Foot, true, true);
        if (!m.ok) return failure("failed", m.error);
        if (m.joinOffer.isValid()) {
            // Much weaker monsters offer to join. The computer always takes them;
            // a human answers with answer_join.
            if (m_state.player(player)->controller == Player::Controller::Ai) {
                // The computer pays when it can, else it fights them.
                if (acceptJoin(m_state, m_content, hero, m.joinOffer).ok) {
                    QJsonObject info = moveInfo(m);
                    info.insert("joined", true);
                    return success(info);
                }
                m_joinOffer = {player, hero, m.joinOffer};
                net::Message fight;
                fight.type = net::msg::answerJoin;
                fight.body.insert("answer", "fight");
                return dispatch(player, fight);
            }
            m_joinOffer = {player, hero, m.joinOffer};
            QJsonObject info = moveInfo(m);
            QJsonArray units;
            for (const UnitStack& s : m_state.army(m.joinOffer)->stacks)
                units.append(QJsonObject{{"unit", s.unitTypeId()}, {"count", s.count()}});
            info.insert("joinOffer", QJsonObject{{"army", toInt(m.joinOffer.value())}, {"hero", toInt(hero.value())}, {"units", units},
                                                 {"cost", joinCost(m_state, m_content, m.joinOffer)}});
            return success(info);
        }
        if (!m.pending) return success(moveInfo(m));
        m_battle = std::make_unique<ActiveBattle>();
        m_battle->attack = *m.pending;
        m_battle->result = m;
        m_aiEvents = QJsonArray();
        runBattles();
        if (m_battle) {
            QJsonObject info = moveInfo(m);
            info.insert("battleStarted", true);
            info.insert("events", m_aiEvents);  // the computer may have acted first
            m_aiEvents = QJsonArray();
            return success(info);
        }
        return success(moveInfo(m_finishedMove));
    }
    if (cmd.type == net::msg::buyBoat) {
        const HeroId hero(id32("hero"));
        r.finish();
        if (!r.ok()) return bad();
        const Hero* h = m_state.hero(hero);
        if (!h || h->owner != player) return failure("failed", "that is not your hero");
        return fromAction(buyBoat(m_state, m_content, hero));
    }
    if (cmd.type == net::msg::castSpell) {
        const HeroId hero(id32("hero"));
        const QString spell = r.requireString("spell");
        r.finish();
        if (!r.ok()) return bad();
        const Hero* h = m_state.hero(hero);
        if (!h || h->owner != player) return failure("failed", "that is not your hero");
        return fromAction(castAdventureSpell(m_state, m_content, hero, spell));
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
        const bool all = r.optionalBool("all", false);
        const int stack = all ? 0 : r.requireInt("stack", 0, kMaxArmyStacks - 1);
        const int count = all ? 0 : r.requireInt("count", 1, 100000);
        r.finish();
        if (!r.ok()) return bad();
        const Army* a = m_state.army(from);
        if (!a || a->owner != player) return failure("failed", "that is not your army");
        if (all) {
            int moved = 0;
            const ActionResult res = moveAllUnits(m_state, from, to, &moved);
            if (!res.ok) return failure("failed", res.error);
            return success({{"moved", moved}, {"left", m_state.army(from) ? m_state.army(from)->totalUnits() : 0}});
        }
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
        const bool useTrade = r.optionalBool("useTrade", false);
        r.finish();
        if (!r.ok()) return bad();
        return success(buyJson(buyAllUnits(m_state, m_content, player, town, strategy, useTrade)));
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
