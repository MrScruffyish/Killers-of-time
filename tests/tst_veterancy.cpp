#include <QtTest>

#include "TestContent.h"

#include "core/battle/AutoBattle.h"
#include "core/battle/Battle.h"
#include "core/battle/BattleAi.h"
#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Veterancy.h"

using namespace kot::core;

class TstVeterancy : public QObject {
    Q_OBJECT
    ContentRegistry reg;

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
        useContent(reg);
    }

    void starsFromExperience() {
        const QList<int>& t = reg.balance().veterancy.thresholds;
        QCOMPARE(veteranStars(0, reg), 0);
        QCOMPARE(veteranStars(t[0] - 1, reg), 0);
        QCOMPARE(veteranStars(t[0], reg), 1);
        QCOMPARE(veteranStars(t.last(), reg), 5);
        QCOMPARE(veteranBonus(2, "initiative", reg), 1);
        QCOMPARE(veteranBonus(5, "morale", reg), 1);
        QCOMPARE(veteranBonus(0, "morale", reg), 0);
    }

    void mergingAveragesByCount() {
        UnitStack veterans(U("pikeman"), 10, 10);
        veterans.setExperience(100);
        UnitStack fresh(U("pikeman"), 30, 10);
        veterans.merge(fresh);
        QCOMPARE(veterans.count(), 40);
        QCOMPARE(veterans.experience(), 25);
        veterans.addUnits(10);  // recruits
        QCOMPARE(veterans.experience(), 20);
        const UnitStack part = veterans.split(5);
        QCOMPARE(part.experience(), 20);
        QCOMPARE(veterans.experience(), 20);
    }

    void battlesTeachAndStarsHelp() {
        GameState gs(3);
        gs.setMap(GameMap(6, 6, T("grass")));
        const PlayerId p = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, RC("humans"), "#ff0000");
        const HeroId h = createHero(gs, reg, p, "H", C("knight"), {0, 0});
        Army& army = *gs.army(gs.hero(h)->army);
        army.addUnits(U("pikeman"), 40, 10);
        const ArmyId m = gs.addArmy(PlayerId(), {1, 0});
        gs.army(m)->addUnits(U("militia"), 20, 6);
        QVERIFY(resolveBattle(gs, reg, army.id, m).attackerWon());
        const int xp = gs.army(gs.hero(h)->army)->stacks[0].experience();
        QVERIFY(xp > 0);

        // Five-star stacks get the bonuses in battle.
        gs.army(gs.hero(h)->army)->stacks[0].setExperience(reg.balance().veterancy.thresholds.last());
        const ArmyId m2 = gs.addArmy(PlayerId(), {1, 0});
        gs.army(m2)->addUnits(U("militia"), 5, 6);
        Battle b(gs, reg, gs.hero(h)->army, m2, 1);
        QCOMPARE(b.unit(1)->stars, 5);
        QCOMPARE(b.unit(1)->modifier("battleSpeed"), veteranBonus(5, "battleSpeed", reg));
        QCOMPARE(battleToJson(b)["units"].toArray()[0].toObject()["stars"].toInt(), 5);
    }

    void experienceIsSaved() {
        GameState gs(1);
        gs.setMap(GameMap(4, 4, T("grass")));
        const PlayerId p = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, RC("humans"), "#ff0000");
        gs.setTurnOrder({p});
        const ArmyId a = gs.addArmy(p, {1, 1});
        gs.army(a)->addUnits(U("militia"), 5, 6);
        gs.army(a)->stacks[0].setExperience(77);
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(gs.toJson(reg), reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(loaded.army(a)->stacks[0].experience(), 77);
    }
};

QTEST_GUILESS_MAIN(TstVeterancy)
#include "tst_veterancy.moc"
