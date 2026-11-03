#include "core/rules/Trade.h"

#include <algorithm>

#include "core/content/ContentRegistry.h"

namespace kot::core {

namespace {

qint64 valueOf(const ContentRegistry& content, const QString& id) {
    const ResourceDef* r = content.resource(id);
    return r ? r->value : 0;
}

qint64 efficiency(const ContentRegistry& content, int given) {
    return given > 0 ? given : content.balance().economy.tradeEfficiencyPercent;
}

}  // namespace

int tradeEfficiency(const GameState& state, const ContentRegistry& content, PlayerId player) {
    int percent = content.balance().economy.tradeEfficiencyPercent;
    for (const Town& t : state.towns()) {
        if (t.owner != player) continue;
        for (const QString& b : t.buildings)
            if (const BuildingDef* def = content.building(b))
                for (const BuildingEffect& e : def->effects)
                    if (e.type == BuildingEffect::Type::Market) percent += e.amount;
    }
    if (const Player* p = state.player(player)) percent += content.race(p->race).tradeBonus;  // humans: good traders
    return std::min(percent, 95);
}

int tradeYield(const ContentRegistry& content, const QString& give, int giveAmount,
               const QString& get, int efficiencyPercent) {
    const qint64 vg = valueOf(content, give), vt = valueOf(content, get);
    if (vg <= 0 || vt <= 0 || giveAmount <= 0) return 0;
    return static_cast<int>(giveAmount * vg * efficiency(content, efficiencyPercent) / (100 * vt));
}

int tradeCostToGet(const ContentRegistry& content, const QString& give, const QString& get,
                   int getAmount, int efficiencyPercent) {
    const qint64 vg = valueOf(content, give), vt = valueOf(content, get);
    if (vg <= 0 || vt <= 0 || getAmount <= 0) return 0;
    const qint64 num = getAmount * vt * 100;
    const qint64 den = vg * efficiency(content, efficiencyPercent);
    return static_cast<int>((num + den - 1) / den);
}

ActionResult trade(GameState& state, const ContentRegistry& content, PlayerId playerId,
                   const QString& give, int giveAmount, const QString& get) {
    ActionResult r;
    Player* p = state.player(playerId);
    if (!p) { r.error = QStringLiteral("unknown player"); return r; }
    if (!content.resource(give) || !content.resource(get)) {
        r.error = QStringLiteral("unknown resource");
        return r;
    }
    if (give == get) { r.error = QStringLiteral("cannot trade a resource for itself"); return r; }
    if (giveAmount < 1 || p->resources.value(give) < giveAmount) {
        r.error = QStringLiteral("not enough %1").arg(give);
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
    for (auto it = cost.begin(); it != cost.end(); ++it) {
        const int lack = it.value() - have.value(it.key());
        if (lack > 0) missing[it.key()] = lack;
    }
    if (missing.isEmpty()) return steps;

    // Spare stock: what we have beyond the cost, for resources we are not short of.
    ResourceAmounts spare;
    for (const ResourceDef& r : content.resources()) {
        if (missing.contains(r.id)) continue;
        const int extra = have.value(r.id) - cost.value(r.id);
        if (extra > 0) spare[r.id] = extra;
    }
    const QString currency = content.currency() ? content.currency()->id : QString();

    for (auto m = missing.begin(); m != missing.end(); ++m) {
        int remaining = m.value();
        while (remaining > 0) {
            // Source order: non-currency by spare gold value (high first), currency last.
            QString source;
            qint64 bestScore = -1;
            bool bestIsCurrency = true;
            for (auto sp = spare.begin(); sp != spare.end(); ++sp) {
                if (sp.value() <= 0 || tradeYield(content, sp.key(), sp.value(), m.key(), eff) < 1)
                    continue;
                const bool isCurrency = (sp.key() == currency);
                const qint64 score = static_cast<qint64>(sp.value()) * valueOf(content, sp.key());
                const bool better = source.isEmpty() || (bestIsCurrency && !isCurrency) ||
                                    (isCurrency == bestIsCurrency && score > bestScore);
                if (better) {
                    source = sp.key();
                    bestScore = score;
                    bestIsCurrency = isCurrency;
                }
            }
            if (source.isEmpty()) return std::nullopt;
            const int canGet = tradeYield(content, source, spare[source], m.key(), eff);
            const int get = std::min(remaining, canGet);
            const int give = tradeCostToGet(content, source, m.key(), get, eff);
            steps.append({source, give, m.key(), get});
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
