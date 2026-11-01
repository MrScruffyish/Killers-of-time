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
    QString give;
    int giveAmount = 0;
    QString get;
    int getAmount = 0;
};

// How much you receive for giving `giveAmount` (rounded down). Uses the resources'
// gold values and the trade efficiency from balance.json (the fee).
int tradeYield(const ContentRegistry& content, const QString& give, int giveAmount,
               const QString& get);

// How much you must give to receive `getAmount` (rounded up).
int tradeCostToGet(const ContentRegistry& content, const QString& give, const QString& get,
                   int getAmount);

// Plain trade: give `giveAmount` of one resource for as much as possible of another.
ActionResult trade(GameState& state, const ContentRegistry& content, PlayerId player,
                   const QString& give, int giveAmount, const QString& get);

// Works out how to afford `cost` by trading spare resources (not needed for the cost
// itself): other resources are sold first, most valuable spare stock first, gold last.
// Returns an empty list if nothing is missing, nothing if it cannot be afforded.
std::optional<QList<TradeStep>> planTradeFor(const ContentRegistry& content,
                                             const ResourceAmounts& have,
                                             const ResourceAmounts& cost);

void applyTradeSteps(ResourceAmounts& resources, const QList<TradeStep>& steps);

}  // namespace kot::core
