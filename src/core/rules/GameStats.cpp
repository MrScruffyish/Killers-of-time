#include "core/rules/GameStats.h"

#include "core/content/ContentRegistry.h"

namespace kot::core {

QJsonObject playerStats(const GameState& state, const ContentRegistry& content) {
    const QString currency = content.currency() ? content.currency()->id : QStringLiteral("gold");
    QJsonObject all;
    for (const Player& p : state.players()) {
        int armyValue = 0, towns = 0, mines = 0, heroes = 0;
        for (const Army& a : state.armies()) {
            if (a.owner != p.id) continue;
            for (const UnitStack& s : a.stacks)
                if (const UnitDef* u = content.unit(s.unitTypeId())) armyValue += s.count() * u->cost.value(currency);
        }
        for (const Town& t : state.towns()) towns += t.owner == p.id;
        for (const Mine& m : state.mines()) mines += m.owner == p.id;
        for (const Hero& h : state.heroes()) heroes += h.owner == p.id;
        all.insert(QString::number(p.id.value()), QJsonObject{{"armyValue", armyValue}, {"towns", towns}, {"mines", mines},
                                                              {"heroes", heroes}, {"gold", p.resources.value(currency)}});
    }
    return all;
}

void recordWeekStats(GameState& state, const ContentRegistry& content) {
    const int week = (state.day() - 1) / 7 + 1;
    QJsonArray& h = state.history();
    if (!h.isEmpty() && h.last().toObject().value("week").toInt() >= week) return;
    h.append(QJsonObject{{"week", week}, {"players", playerStats(state, content)}});
}

}  // namespace kot::core
