#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

// One-click actions that save the player from repeating chores in every town.
// They work for one town, or for all of the player's towns when `town` is invalid.

enum class BuyStrategy {
    BestFirst,  // highest tier first: fewer but stronger units (default)
    MostUnits   // cheapest units first: as many units as possible
};

struct BuyReport {
    int unitsBought = 0;
    int townsServed = 0;
    ResourceAmounts spent;
};

// "Buy all units": in each town buy as many units as the money allows, into the
// garrison, from the dwellings and from units sent there from map dwellings.
BuyReport buyAllUnits(GameState& state, const ContentRegistry& content, PlayerId player,
                      TownId town = {}, BuyStrategy strategy = BuyStrategy::BestFirst);

struct AutoBuildReport {
    struct Built {
        TownId town;
        QString building;
    };
    struct Choice {
        TownId town;
        QStringList buildings;  // buildings that would shut out a rival: the player decides
    };
    QList<Built> built;
    QList<Choice> needsChoice;
    QList<TownId> cannotAfford;  // had something to build, but not enough resources
};

// "Auto-build": in each town that has not built today, build the best affordable
// building that involves no choice. Buildings that would permanently exclude a rival
// building are never built automatically; they are reported in `needsChoice`.
// Priority: dwellings of higher tier units first, then cheaper buildings.
// With `useTrade` missing resources are traded for automatically.
AutoBuildReport autoBuild(GameState& state, const ContentRegistry& content, PlayerId player,
                          TownId town = {}, bool useTrade = false);

struct BuildAndBuyReport {
    AutoBuildReport build;
    BuyReport buy;
};

// "Build, then buy units": autoBuild followed by buyAllUnits.
BuildAndBuyReport buildThenBuyUnits(GameState& state, const ContentRegistry& content,
                                    PlayerId player, TownId town = {}, bool useTrade = false,
                                    BuyStrategy strategy = BuyStrategy::BestFirst);

}  // namespace kot::core
