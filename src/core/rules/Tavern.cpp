#include "core/rules/Tavern.h"

#include <QSet>

#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Trade.h"

namespace kot::core {

namespace {
HireResult fail(const QString& message) {
    HireResult r;
    r.error = message;
    return r;
}
}  // namespace

bool hasTavern(const Town& town, const ContentRegistry& content) {
    for (const QString& id : town.buildings) {
        const BuildingDef* b = content.building(id);
        if (!b) continue;
        for (const BuildingEffect& e : b->effects) {
            if (e.type == BuildingEffect::Type::Tavern) return true;
        }
    }
    return false;
}

QStringList freeHeroTemplates(const GameState& state, const ContentRegistry& content,
                              const QString& race) {
    QSet<QString> inPlay;
    for (const Hero& h : state.heroes()) inPlay.insert(h.templateId);
    QStringList result;
    for (const HeroTemplateDef& t : content.heroTemplates()) {
        const HeroClassDef* cls = content.heroClass(t.classId);
        if (cls && cls->race == race && !inPlay.contains(t.id)) result.append(t.id);
    }
    return result;
}

void refreshTavernOffers(GameState& state, const ContentRegistry& content, TownId onlyTown) {
    QSet<QString> taken;  // offered in an earlier town of this draw
    for (Town& town : state.towns()) {
        if (onlyTown.isValid() && town.id != onlyTown) continue;
        town.tavernOffers.clear();
        if (!hasTavern(town, content)) continue;
        QStringList candidates;
        for (const QString& id : freeHeroTemplates(state, content, town.race)) {
            if (!taken.contains(id)) candidates.append(id);
        }
        const int wanted = content.balance().economy.tavernOffers;
        while (town.tavernOffers.size() < wanted && !candidates.isEmpty()) {
            const int pick = state.rng().nextInt(0, static_cast<int>(candidates.size()) - 1);
            taken.insert(candidates[pick]);
            town.tavernOffers.append(candidates.takeAt(pick));
        }
    }
}

HireResult hireHero(GameState& state, const ContentRegistry& content, TownId townId,
                    PlayerId playerId, const QString& templateId, bool useTrade) {
    Town* town = state.town(townId);
    if (!town) return fail(QStringLiteral("unknown town"));
    Player* player = state.player(playerId);
    if (!player || town->owner != playerId) return fail(QStringLiteral("this is not your town"));
    if (!hasTavern(*town, content)) return fail(QStringLiteral("this town has no tavern"));
    const int week = (state.day() - 1) / 7 + 1;
    if (town->lastHireWeek == week) return fail(QStringLiteral("a hero was hired in this tavern this week; the next one next week"));
    if (!town->tavernOffers.contains(templateId))
        return fail(QStringLiteral("that hero is not offered here"));
    const HeroTemplateDef* tmpl = content.heroTemplate(templateId);
    if (!tmpl) return fail(QStringLiteral("unknown hero"));

    int owned = 0;
    for (const Hero& h : state.heroes()) owned += (h.owner == playerId) ? 1 : 0;
    if (owned >= content.balance().economy.maxHeroesPerPlayer)
        return fail(QStringLiteral("you cannot have more heroes"));
    for (const Army& a : state.armies()) {
        if (a.hero.isValid() && a.pos == town->pos)
            return fail(QStringLiteral("another hero is already in the town"));
    }

    const QString currency = content.currency() ? content.currency()->id : QString();
    const ResourceAmounts cost = {{currency, content.balance().economy.heroCost}};
    QList<TradeStep> plan;
    if (player->resources.value(currency) < cost.value(currency)) {
        if (!useTrade) return fail(QStringLiteral("not enough %1").arg(currency));
        const auto trades = planTradeFor(content, player->resources, cost, tradeEfficiency(state, content, player->id));
        if (!trades) return fail(QStringLiteral("cannot be afforded, even by trading"));
        plan = *trades;
    }

    QString error;
    const HeroId id = createHero(state, content, playerId, tmpl->name, tmpl->classId, town->pos, &error);
    if (!id.isValid()) return fail(error);

    applyTradeSteps(player->resources, plan);
    player->resources[currency] -= cost.value(currency);
    state.hero(id)->templateId = templateId;
    town = state.town(townId);  // createHero may have moved the towns in memory
    if (town) town->lastHireWeek = week;
    for (Town& t : state.towns()) t.tavernOffers.removeAll(templateId);

    HireResult ok;
    ok.ok = true;
    ok.hero = id;
    return ok;
}

}  // namespace kot::core
