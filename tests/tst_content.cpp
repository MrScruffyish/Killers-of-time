#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "core/content/ContentRegistry.h"

using namespace kot::core;

static void writeFile(const QString& root, const QString& rel, const QString& text) {
    const QString path = QDir(root).filePath(rel);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(text.toUtf8());
}

static const char* kResources =
    R"({"resources":[{"id":"gold","name":"Gold","currency":true},{"id":"wood","name":"Wood"}]})";
static const char* kTerrain =
    R"({"terrain":[{"id":"grass","name":"Grass","moveCost":100,"color":"#55aa55"}]})";
static const char* kTraits =
    R"({"traits":[{"id":"attack","name":"Attack","group":"battle","pickable":true}]})";
static const char* kMilitia =
    R"({"id":"militia","name":"Militia","race":"humans","tier":1,"attack":4,"defense":3,)"
    R"("damageMin":1,"damageMax":2,"maxHp":6,"battleSpeed":4,"cost":{"gold":20}})";

// A tiny valid data directory that individual tests then break on purpose.
static void writeValidData(const QString& root) {
    writeFile(root, "resources.json", kResources);
    writeFile(root, "terrain.json", kTerrain);
    writeFile(root, "traits.json", kTraits);
    writeFile(root, "units/militia.json", kMilitia);
    writeFile(root, "buildings/hall.json",
              R"({"id":"hall","name":"Hall","race":"humans","cost":{"gold":100},)"
              R"("effects":[{"type":"income","resource":"gold","amount":10}]})");
    writeFile(root, "hero_classes.json",
              R"({"heroClasses":[{"id":"knight","name":"Knight","race":"humans",)"
              R"("baseTraits":{"attack":2},"pickIncrement":{"attack":2}}]})");
    writeFile(root, "progression.json", R"({"xpThresholds":[100,300]})");
    writeFile(root, "balance.json",
              R"({"vision":{"townRadius":5,"mineRadius":2},"economy":{"townDailyGold":500},)"
              R"("movement":{"base":1000,"perTraitLevel":100,"minimum":300,)"
              R"("slowPenalties":[{"mapSpeedAtMost":2,"penalty":200}]}})");
}

class TstContent : public QObject {
    Q_OBJECT

    // Loads `dir`, expects failure, and expects some error to contain `needle`.
    static void expectError(const QString& dir, const QString& needle) {
        ContentRegistry reg;
        QStringList errors;
        QVERIFY2(!reg.loadFromDirectory(dir, &errors), "load should have failed");
        QVERIFY2(errors.join("\n").contains(needle),
                 qPrintable("expected error containing '" + needle + "', got:\n" +
                            errors.join("\n")));
        QVERIFY(reg.units().isEmpty());  // failed load leaves registry empty
    }

private slots:
    void loadsRealGameData() {
        ContentRegistry reg;
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));

        QCOMPARE(reg.resources().size(), 9);
        QVERIFY(reg.currency());
        QCOMPARE(reg.currency()->id, QString("gold"));

        QVERIFY(reg.terrainType("grass")->walkable);
        QVERIFY(!reg.terrainType("mountains")->walkable);
        QVERIFY(!reg.terrainType("mountains")->sailable);
        QVERIFY(reg.terrainType("sea")->sailable);
        QVERIFY(reg.terrainType("road")->moveCost < reg.terrainType("grass")->moveCost);

        int counts[3] = {0, 0, 0};
        for (const TraitDef& t : reg.traits()) counts[static_cast<int>(t.group)]++;
        QCOMPARE(counts[static_cast<int>(TraitGroup::Battle)], 7);
        QCOMPARE(counts[static_cast<int>(TraitGroup::Magic)], 4);
        QCOMPARE(counts[static_cast<int>(TraitGroup::Explore)], 2);
        QVERIFY(!reg.trait("spell_points_max")->pickable);
        QVERIFY(reg.trait("luck")->pickable);

        QCOMPARE(reg.unit("militia")->upgradesTo, QString("pikeman"));
        QVERIFY(reg.unit("battle_snail")->battleSpeed < reg.unit("armoured_eagle")->battleSpeed);
        QVERIFY(reg.unit("armoured_eagle")->abilities.contains("flying"));
        QCOMPARE(reg.unit("archer")->range, 10);
        QCOMPARE(reg.unit("militia")->mapSpeed, reg.unit("militia")->battleSpeed);  // derived
        QCOMPARE(reg.unit("armoured_eagle")->mapSpeed, 10);                          // explicit
        QCOMPARE(reg.unit("militia")->cost.value("gold"), 20);
        QVERIFY(reg.unit("nonexistent") == nullptr);

        QVERIFY(reg.heroClass("knight"));
        QVERIFY(reg.heroClass("wizard"));
        QVERIFY(reg.heroClass("wizard")->baseTraits.value("magic") >
                reg.heroClass("knight")->baseTraits.value("magic"));
        QVERIFY(reg.maxLevel() >= 5);

        const MovementBalance& mb = reg.balance().movement;
        QVERIFY(mb.base > 0 && mb.minimum > 0);
        QVERIFY(!mb.slowPenalties.isEmpty());
        QVERIFY(reg.balance().vision.townRadius > 0);
        QVERIFY(reg.balance().economy.townDailyGold > 0);
    }

    void loadsMinimalData() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        ContentRegistry reg;
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(tmp.path(), &errors), qPrintable(errors.join("\n")));
        QCOMPARE(reg.units().size(), 1);
    }

    void missingFile() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        QFile::remove(QDir(tmp.path()).filePath("terrain.json"));
        expectError(tmp.path(), "terrain.json: cannot open file");
    }

    void invalidJsonReportsLine() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        writeFile(tmp.path(), "traits.json", "{\n  \"traits\": [\n    { oops }\n  ]\n}");
        expectError(tmp.path(), "traits.json: invalid JSON near line 3");
    }

    void noCurrency() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        writeFile(tmp.path(), "resources.json", R"({"resources":[{"id":"gold","name":"Gold"}]})");
        expectError(tmp.path(), "exactly one resource must have");
    }

    void duplicateIds() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        writeFile(tmp.path(), "resources.json",
                  R"({"resources":[{"id":"gold","name":"Gold","currency":true},)"
                  R"({"id":"gold","name":"Gold2"}]})");
        expectError(tmp.path(), "duplicate resource id 'gold'");
    }

    void badTraitGroup() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        writeFile(tmp.path(), "traits.json",
                  R"({"traits":[{"id":"x","name":"X","group":"fire","pickable":true}]})");
        expectError(tmp.path(), "field 'group' must be one of battle, magic, explore");
    }

    void badTerrainColor() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        writeFile(tmp.path(), "terrain.json",
                  R"({"terrain":[{"id":"grass","name":"Grass","color":"green"}]})");
        expectError(tmp.path(), "must be a color like #RRGGBB");
    }

    void unitCostUnknownResource() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        QString u = kMilitia;
        u.replace(R"("gold":20)", R"("unobtainium":5)");
        writeFile(tmp.path(), "units/militia.json", u);
        expectError(tmp.path(), "cost uses unknown resource 'unobtainium'");
    }

    void unitUpgradeTargetUnknown() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        QString u = kMilitia;
        u.replace(R"("cost")", R"("upgradesTo":"ghost","cost")");
        writeFile(tmp.path(), "units/militia.json", u);
        expectError(tmp.path(), "upgradesTo unknown unit 'ghost'");
    }

    void unitDamageRange() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        QString u = kMilitia;
        u.replace(R"("damageMin":1,"damageMax":2)", R"("damageMin":5,"damageMax":2)");
        writeFile(tmp.path(), "units/militia.json", u);
        expectError(tmp.path(), "damageMin (5) is greater than damageMax (2)");
    }

    void unitRangedNeedsShots() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        QString u = kMilitia;
        u.replace(R"("cost")", R"("range":8,"cost")");
        writeFile(tmp.path(), "units/militia.json", u);
        expectError(tmp.path(), "needs shots >= 1");
    }

    void unitIdMustMatchFileName() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        writeFile(tmp.path(), "units/other.json", kMilitia);
        expectError(tmp.path(), "id 'militia' must match the file name");
    }

    void unitTypoInFieldName() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        QString u = kMilitia;
        u.replace(R"("attack")", R"("atack")");
        writeFile(tmp.path(), "units/militia.json", u);
        QTemporaryDir& t = tmp;
        ContentRegistry reg;
        QStringList errors;
        QVERIFY(!reg.loadFromDirectory(t.path(), &errors));
        const QString all = errors.join("\n");
        QVERIFY(all.contains("missing required field 'attack'"));
        QVERIFY(all.contains("unknown field 'atack'"));
    }

    void heroClassUnknownTrait() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        writeFile(tmp.path(), "hero_classes.json",
                  R"({"heroClasses":[{"id":"k","name":"K","race":"humans","baseTraits":{"nope":1}}]})");
        expectError(tmp.path(), "baseTraits uses unknown trait 'nope'");
    }

    void heroClassPickIncrementNeedsPickableTrait() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        writeFile(tmp.path(), "traits.json",
                  R"({"traits":[{"id":"sp","name":"SP","group":"magic","pickable":false}]})");
        writeFile(tmp.path(), "hero_classes.json",
                  R"({"heroClasses":[{"id":"k","name":"K","race":"humans","pickIncrement":{"sp":1}}]})");
        expectError(tmp.path(), "which is not pickable");
    }

    void progressionMustAscend() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        writeFile(tmp.path(), "progression.json", R"({"xpThresholds":[100,100]})");
        expectError(tmp.path(), "xpThresholds[1] must be a whole number greater");
    }

    void balanceSlowPenaltiesMustAscend() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        writeFile(tmp.path(), "balance.json",
                  R"({"vision":{"townRadius":5,"mineRadius":2},"economy":{"townDailyGold":1},)"
                  R"("movement":{"base":1000,"perTraitLevel":100,"minimum":300,)"
                  R"("slowPenalties":[{"mapSpeedAtMost":3,"penalty":200},)"
                  R"({"mapSpeedAtMost":3,"penalty":100}]}})");
        expectError(tmp.path(), "mapSpeedAtMost must be greater than the previous");
    }

    void balanceMissingMovement() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        writeFile(tmp.path(), "balance.json", "{}");
        expectError(tmp.path(), "missing required field 'movement'");
    }

    void buildingUnknownRequirement() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        writeFile(tmp.path(), "buildings/hall.json",
                  R"({"id":"hall","name":"Hall","race":"humans","requires":["castle"]})");
        expectError(tmp.path(), "refers to unknown building 'castle'");
    }

    void buildingUnknownEffectType() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        writeFile(tmp.path(), "buildings/hall.json",
                  R"({"id":"hall","name":"Hall","race":"humans","effects":[{"type":"magic"}]})");
        expectError(tmp.path(), "unknown effect type 'magic'");
    }

    void buildingDwellingOfUnknownUnit() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        writeFile(tmp.path(), "buildings/hall.json",
                  R"({"id":"hall","name":"Hall","race":"humans",)"
                  R"("effects":[{"type":"dwelling","unit":"ghost"}]})");
        expectError(tmp.path(), "dwelling of unknown unit 'ghost'");
    }

    void buildingRequiredAndExcluded() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        writeFile(tmp.path(), "buildings/a.json",
                  R"({"id":"a","name":"A","race":"humans","requires":["hall"],"excludes":["hall"]})");
        expectError(tmp.path(), "both required and excluded");
    }

    void buildingUpgradeMustMatchUnitUpgrade() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        writeFile(tmp.path(), "buildings/hall.json",
                  R"({"id":"hall","name":"Hall","race":"humans",)"
                  R"("effects":[{"type":"dwelling","unit":"militia"}]})");
        writeFile(tmp.path(), "buildings/hall2.json",
                  R"({"id":"hall2","name":"Hall2","race":"humans","upgradeOf":"hall",)"
                  R"("effects":[{"type":"dwelling","unit":"militia"}]})");
        expectError(tmp.path(), "upgrades to this unit");
    }

    void realBuildingsLoad() {
        ContentRegistry reg;
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
        QVERIFY(reg.building("barracks"));
        QCOMPARE(reg.building("pike_barracks")->upgradeOf, QString("barracks"));
        QVERIFY(reg.building("eagle_roost")->excludes.contains("snail_pen"));
        QVERIFY(reg.building("snail_pen")->excludes.contains("eagle_roost"));
    }

    void reportsAllErrorsAtOnce() {
        QTemporaryDir tmp;
        writeValidData(tmp.path());
        writeFile(tmp.path(), "terrain.json", R"({"terrain":[{"id":"a","name":"A","color":"x"}]})");
        writeFile(tmp.path(), "traits.json", R"({"traits":[{"id":"b","name":"B"}]})");
        ContentRegistry reg;
        QStringList errors;
        QVERIFY(!reg.loadFromDirectory(tmp.path(), &errors));
        QVERIFY(errors.size() >= 3);  // color + missing group + missing pickable
    }
};

QTEST_APPLESS_MAIN(TstContent)
#include "tst_content.moc"
