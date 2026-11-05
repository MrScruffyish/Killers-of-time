#include "core/rules/Trade.h"

#include <algorithm>

#include "core/content/ContentRegistry.h"

namespace kot::core {

namespace {

qint64 valueOf(const ContentRegistry& content, ResourceId id) {
    return id.isValid() && id.index() < content.resources().size() ? content.resource(id).value : 0;
}

qint64 efficiency(const ContentRegistry& content, int given) {
    return given > 0 ? given : content.balance().economy.tradeEfficiencyPercent;
}

}  // namespace

int tradeEfficiency(const GameState& state, const ContentRegistry& content, PlayerId player) {
    int percent = content.balance().economy.tradeEfficiencyPercent;
    for (const Town& t : state.towns()) {
        if (t.owner != player) continue;
        for (const BuildingTypeId b : t.buildings)
            if (const BuildingDef* def = content.building(b))
                for (const BuildingEffect& e : def->effects)
                    if (e.type == BuildingEffect::Type::Market) percent += e.amount;
    }
    if (const Player* p = state.player(player)) percent += content.race(p->race).tradeBonus;  // humans: good traders
    return std::min(percent, 95);
}

int tradeYield(const ContentRegistry& content, ResourceId give, int giveAmount, ResourceId get, int efficiencyPercent) {
    const qint64 vg = valueOf(content, give), vt = valueOf(content, get);
    if (vg <= 0 || vt <= 0 || giveAmount <= 0) return 0;
    return static_cast<int>(giveAmount * vg * efficiency(content, efficiencyPercent) / (100 * vt));
}

int tradeCostToGet(const ContentRegistry& content, ResourceId give, ResourceId get, int getAmount, int efficiencyPercent) {
    const qint64 vg = valueOf(content, give), vt = valueOf(content, get);
    if (vg <= 0 || vt <= 0 || getAmount <= 0) return 0;
    const qint64 num = getAmount * vt * 100;
    const qint64 den = vg * efficiency(content, efficiencyPercent);
    return static_cast<int>((num + den - 1) / den);
}

ActionResult trade(GameState& state, const ContentRegistry& content, PlayerId playerId, ResourceId give, int giveAmount, ResourceId get) {
    ActionResult r;
    Player* p = state.player(playerId);
    if (!p) { r.error = QStringLiteral("unknown player"); return r; }
    if (valueOf(content, give) <= 0 || valueOf(content, get) <= 0) {
        r.error = QStringLiteral("unknown resource");
        return r;
    }
    if (give == get) { r.error = QStringLiteral("cannot trade a resource for itself"); return r; }
    if (giveAmount < 1 || p->resources.value(give) < giveAmount) {
        r.error = QStringLiteral("not enough %1").arg(content.resource(give).key);
        return r;
    }
    const int got = tradeYield(content, give, giveAmount, get, tradeEfficiency(state, content, playerId));
    if (got < 1) { r.error = QStringLiteral("that is too little to trade"); return r; }
    p->resources[give] -= giveAmount;
    p->resources[get] += got;
    r.ok = true;
    return r;
}

std::optional<QList<TradeStep>> planTradeFor(const ContentRegistry& content,
                                             const ResourceAmounts& have,
                                             const ResourceAmounts& cost, int eff) {
    QList<TradeStep> steps;
    ResourceAmounts missing;
    for (const auto [r, amount] : cost) {
        const int lack = amount - have.value(r);
        if (lack > 0) missing[r] = lack;
    }
    if (missing.isEmpty()) return steps;

    // Spare stock: what we have beyond the cost, for resources we are not short of.
    ResourceAmounts spare;
    for (const ResourceDef& r : content.resources()) {
        if (missing.value(r.id) > 0) continue;
        const int extra = have.value(r.id) - cost.value(r.id);
        if (extra > 0) spare[r.id] = extra;
    }
    const ResourceId currency = content.currency();

    for (const auto [wanted, lacking] : missing) {
        int remaining = lacking;
        while (remaining > 0) {
            // Source order: non-currency by spare gold value (high first), currency last.
            ResourceId source;
            qint64 bestScore = -1;
            bool bestIsCurrency = true;
            for (const auto [r, extra] : spare) {
                if (extra <= 0 || tradeYield(content, r, extra, wanted, eff) < 1) continue;
                const bool isCurrency = (r == currency);
                const qint64 score = static_cast<qint64>(extra) * valueOf(content, r);
                const bool better = !source.isValid() || (bestIsCurrency && !isCurrency) ||
                                    (isCurrency == bestIsCurrency && score > bestScore);
                if (better) {
                    source = r;
                    bestScore = score;
                    bestIsCurrency = isCurrency;
                }
            }
            if (!source.isValid()) return std::nullopt;
            const int canGet = tradeYield(content, source, spare.value(source), wanted, eff);
            const int get = std::min(remaining, canGet);
            const int give = tradeCostToGet(content, source, wanted, get, eff);
            steps.append({source, give, wanted, get});
            spare[source] -= give;
            remaining -= get;
        }
    }
    return steps;
}

void applyTradeSteps(ResourceAmounts& resources, const QList<TradeStep>& steps) {
    for (const TradeStep& s : steps) {
        resources[s.give] -= s.giveAmount;
        resources[s.get] += s.getAmount;
    }
}

}  // namespace kot::core
