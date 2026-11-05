#include "core/content/ContentChecks.h"

#include <QHash>
#include <QSet>
#include <functional>

#include "core/content/ContentRegistry.h"

namespace kot::core {

namespace {

// Depth-first search for cycles; each cycle is reported once as "a -> b -> a".
QStringList findLoops(const QStringList& nodes, const std::function<QStringList(const QString&)>& next) {
    enum { White, Grey, Black };
    QHash<QString, int> colour;
    QStringList path;
    QStringList loops;
    QSet<QString> reported;
    std::function<void(const QString&)> visit = [&](const QString& n) {
        colour[n] = Grey;
        path.append(n);
        for (const QString& m : next(n)) {
            if (colour.value(m, White) == Grey) {
                const QStringList cycle = path.mid(path.indexOf(m)) << m;
                QStringList key = cycle.mid(0, cycle.size() - 1);
                key.sort();
                if (!reported.contains(key.join(','))) {
                    reported.insert(key.join(','));
                    loops << cycle.join(QStringLiteral(" -> "));
                }
            } else if (colour.value(m, White) == White) {
                visit(m);
            }
        }
        path.removeLast();
        colour[n] = Black;
    };
    for (const QString& n : nodes)
        if (colour.value(n, White) == White) visit(n);
    return loops;
}

// (These checks work on the keys of the data files, which they report.)
QStringList needs(const ContentRegistry& content, const QString& key) {
    const BuildingDef* b = content.buildingByKey(key);
    if (!b) return {};
    QStringList out;
    for (const BuildingTypeId r : b->requiredBuildings) out << content.building(r)->key;
    if (b->upgradeOf.isValid()) out << content.building(b->upgradeOf)->key;
    return out;
}

}  // namespace

QStringList findBuildingLoops(const ContentRegistry& content) {
    QStringList result;
    QStringList keys;
    for (const BuildingDef& b : content.buildings()) keys << b.key;
    for (const QString& loop : findLoops(keys, [&](const QString& id) { return needs(content, id); }))
        result << QStringLiteral("buildings: requirement loop %1").arg(loop);
    return result;
}

QStringList findUnitUpgradeLoops(const ContentRegistry& content) {
    QStringList result;
    const auto next = [&](const QString& key) {
        const UnitDef* u = content.unitByKey(key);
        return u && u->upgradesTo.isValid() ? QStringList{content.unit(u->upgradesTo)->key} : QStringList{};
    };
    QStringList keys;
    for (const UnitDef& u : content.units()) keys << u.key;
    for (const QString& loop : findLoops(keys, next))
        result << QStringLiteral("units: upgrade loop %1").arg(loop);
    return result;
}

QStringList findUnbuildableBuildings(const ContentRegistry& content) {
    QStringList result;
    for (const BuildingDef& b : content.buildings()) {
        // Everything it needs, transitively (loops are reported elsewhere).
        QSet<QString> all;
        QStringList todo = needs(content, b.key);
        while (!todo.isEmpty()) {
            const QString n = todo.takeLast();
            if (all.contains(n) || n == b.key) continue;
            all.insert(n);
            todo << needs(content, n);
        }
        for (const QString& n : all) {
            const BuildingDef* d = content.buildingByKey(n);
            if (!d) continue;
            if (d->race.isValid() && b.race.isValid() && d->race != b.race)
                result << QStringLiteral("building '%1' (%2) needs '%3' of another race (%4)")
                              .arg(b.key, content.race(b.race).key, n, content.race(d->race).key);
        }
        QSet<QString> withSelf = all;
        withSelf.insert(b.key);
        for (const QString& n : withSelf) {
            const BuildingDef* d = content.buildingByKey(n);
            if (!d) continue;
            for (const BuildingTypeId xi : d->excludes) {
                const QString& x = content.building(xi)->key;
                if (withSelf.contains(x) && n < x)
                    result << QStringLiteral("building '%1' can never be built: it needs both '%2' and '%3', which exclude each other").arg(b.key, n, x);
            }
        }
    }
    return result;
}

QStringList checkContentGraphs(const ContentRegistry& content) {
    return findBuildingLoops(content) + findUnitUpgradeLoops(content) + findUnbuildableBuildings(content);
}

}  // namespace kot::core
