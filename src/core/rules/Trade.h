#pragma once

#include <QList>
#include <QString>
#include <optional>

#include "core/model/GameState.h"
#include "core/rules/ArmyRules.h"

namespace kot::core {

class ContentRegistry;

// One exchange: give `giveAmount` of `give`, receive `getAmount` of `get`.
struct TradeStep {
    ResourceId give;
    int giveAmount = 0;
    ResourceId get;
    int getAmount = 0;
};

// The share of the fair value a player gets when trading: tradeEfficiencyPercent from
// balance.json, plus the `market` building effects of his towns (at most 95).
int tradeEfficiency(const GameState& state, const ContentRegistry& content, PlayerId player);

// How much you receive for giving `giveAmount` (rounded down). Uses the resources'
// gold values and the trade efficiency (-1 = the base one from balance.json).
int tradeYield(const ContentRegistry& content, ResourceId give, int giveAmount, ResourceId get, int efficiencyPercent = -1);

// How much you must give to receive `getAmount` (rounded up).
int tradeCostToGet(const ContentRegistry& content, ResourceId give, ResourceId get, int getAmount, int efficiencyPercent = -1);

// Plain trade: give `giveAmount` of one resource for as much as possible of another.
ActionResult trade(GameState& state, const ContentRegistry& content, PlayerId player, ResourceId give, int giveAmount, ResourceId get);

// Works out how to afford `cost` by trading spare resources (not needed for the cost
// itself): other resources are sold first, most valuable spare stock first, gold last.
// Returns an empty list if nothing is missing, nothing if it cannot be afforded.
std::optional<QList<TradeStep>> planTradeFor(const ContentRegistry& content,
                                             const ResourceAmounts& have,
                                             const ResourceAmounts& cost, int efficiencyPercent = -1);

void applyTradeSteps(ResourceAmounts& resources, const QList<TradeStep>& steps);

}  // namespace kot::core
