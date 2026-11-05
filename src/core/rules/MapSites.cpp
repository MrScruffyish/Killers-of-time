#include "core/rules/MapSites.h"

#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"

namespace kot::core {

QString siteName(const QString& site) {
    if (site == "shrine") return QStringLiteral("Shrine of Magic");
    if (site == "magic_well") return QStringLiteral("Magic Well");
    if (site == "obelisk") return QStringLiteral("Seer's Obelisk");
    if (site == "arena") return QStringLiteral("Arena");
    if (site == "learning_stone") return QStringLiteral("Learning Stone");
    return site;
}

QString siteDescription(const QString& site) {
    if (site == "shrine") return QStringLiteral("Teaches each hero a spell (level 1 or 2).");
    if (site == "magic_well") return QStringLiteral("Fills a hero's spell points, once a day.");
    if (site == "obelisk") return QStringLiteral("Shows your team the land far around it.");
    if (site == "arena") return QStringLiteral("Each hero trains here once: +1 Attack or +1 Defense.");
    if (site == "learning_stone") return QStringLiteral("Each hero learns here once: +1000 experience.");
    return {};
}

namespace {
int visitKey(const GameState& state, const MapObject& site, const Hero& hero) {
    return site.site == QLatin1String("obelisk") ? kTeamVisit + static_cast<int>(state.teamOf(hero.owner).value()) : static_cast<int>(hero.id.value());
}
}  // namespace

bool siteGivesSomething(const GameState& state, const MapObject& site, const Hero& hero) {
    const auto it = site.visited.constFind(visitKey(state, site, hero));
    if (it == site.visited.cend()) return true;
    return site.site == QLatin1String("magic_well") && it.value() != state.day();
}

QString visitSite(GameState& state, const ContentRegistry& content, MapObject& site, Hero& hero) {
    if (!siteGivesSomething(state, site, hero)) return QStringLiteral("nothing more for this hero here");
    QString what;
    if (site.site == QLatin1String("shrine")) {
        QList<SpellId> can;
        for (const SpellDef& s : content.spells())
            if (s.level <= 2 && !hero.spells.contains(s.id)) can << s.id;
        if (can.isEmpty()) {
            what = QStringLiteral("knows every spell the shrine teaches");
        } else {
            const SpellId id = can[state.rng().nextInt(0, static_cast<int>(can.size()) - 1)];
            hero.spells << id;
            what = QStringLiteral("learned %1").arg(content.spell(id)->name);
        }
    } else if (site.site == QLatin1String("magic_well")) {
        hero.spellPoints = std::max(hero.spellPoints, maxSpellPoints(hero, content));
        what = QStringLiteral("spell points filled");
    } else if (site.site == QLatin1String("obelisk")) {
        constexpr int kRadius = 14;
        QByteArray& explored = state.exploredMap(state.teamOf(hero.owner));
        const GameMap& map = state.map();
        for (int y = site.pos.y - kRadius; y <= site.pos.y + kRadius; ++y)
            for (int x = site.pos.x - kRadius; x <= site.pos.x + kRadius; ++x) {
                const int dx = x - site.pos.x, dy = y - site.pos.y;
                if (map.inBounds({x, y}) && dx * dx + dy * dy <= kRadius * kRadius) explored[static_cast<qsizetype>(y) * map.width() + x] = 1;
            }
        what = QStringLiteral("the land around is shown on your map");
    } else if (site.site == QLatin1String("arena")) {
        const bool attack = state.rng().nextInt(0, 1);
        const TraitId trait = attack ? content.known().attack : content.known().defense;
        if (trait.isValid()) hero.traits[trait] += 1;
        what = attack ? QStringLiteral("+1 Attack") : QStringLiteral("+1 Defense");
    } else if (site.site == QLatin1String("learning_stone")) {
        const int levels = addExperience(hero, content, 1000);
        what = levels > 0 ? QStringLiteral("+1000 experience, a new level") : QStringLiteral("+1000 experience");
    }
    site.visited.insert(visitKey(state, site, hero), state.day());
    return what;
}

}  // namespace kot::core
