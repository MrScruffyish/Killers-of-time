#pragma once

#include <QString>
#include <QStringList>

#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

// Places on the map heroes visit for a gift; they stay on the map. Each hero
// gets a site's gift once (the magic well: once a day; the obelisk: once per team).
inline constexpr int kTeamVisit = 1000000;  // MapObject::visited keys for team visits
inline const QStringList& siteIds() {
    static const QStringList ids = {"shrine", "magic_well", "obelisk", "arena", "learning_stone", "quest_hut"};
    return ids;
}
QString siteName(const QString& site);
QString siteDescription(const QString& site);
// A quest hut's quest in words: "Bring the Dragon Blade: 5000 gold, 2000 experience."
QString describeQuest(const Quest& quest, const ContentRegistry& content);
// Whether this hero (and his owner) has what the quest asks.
bool canFulfil(const GameState& state, const Quest& quest, const Hero& hero);

// Whether the hero (or his team) would get something here now.
bool siteGivesSomething(const GameState& state, const MapObject& site, const Hero& hero);
// The hero visits: gets the gift (when he has not had it) and the site remembers him.
// Returns what happened, for the player ("learned Fireball", "nothing more here").
QString visitSite(GameState& state, const ContentRegistry& content, MapObject& site, Hero& hero);

}  // namespace kot::core
