#include <QtTest>

#include "TestContent.h"

#include "core/content/ContentRegistry.h"
#include "core/battle/Battle.h"
#include "core/rules/HeroRules.h"
#include "core/rules/TurnCycle.h"

using namespace kot::core;

class TstHeroRules : public QObject {
    Q_OBJECT

    ContentRegistry reg;
    GameState gs;
    PlayerId owner;

    HeroId make(const char* cls) {
        QString err;
        const HeroId id = createHero(gs, reg, owner, "Test", C(cls), {2, 3}, &err);
        return id;
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
        useContent(reg);
    }

    void init() {
        gs = GameState(1);
        owner = gs.addPlayer("P", gs.newTeam(), Player::Controller::Human, RC("humans"), "#ffffff");
    }

    // Tavern heroes' specialties (heroes.json): trait points when he becomes that hero,
    // income every day, a bonus for one unit (and its upgrades) in battle.
    void specialties() {
        const HeroTemplateDef* traitHero = nullptr;
        const HeroTemplateDef* incomeHero = nullptr;
        const HeroTemplateDef* unitHero = nullptr;
        for (const HeroTemplateDef& t : reg.heroTemplates()) {
            if (t.specialty.trait.isValid() && !traitHero) traitHero = &t;
            if (!t.specialty.income.isEmpty() && !incomeHero) incomeHero = &t;
            if (t.specialty.unit.isValid() && reg.unit(t.specialty.unit)->upgradesTo.isValid() && !unitHero) unitHero = &t;
        }
        QVERIFY(traitHero && incomeHero && unitHero);
        QVERIFY(!describeSpecialty(unitHero->specialty, reg).isEmpty());

        const HeroId a = createHero(gs, reg, owner, "A", traitHero->classId, {2, 3});
        const int before = gs.hero(a)->trait(traitHero->specialty.trait);
        giveHeroTemplate(*gs.hero(a), reg, traitHero->id);
        QCOMPARE(gs.hero(a)->trait(traitHero->specialty.trait), before + traitHero->specialty.traitAmount);

        const ResourceAmounts plain = dailyIncome(gs, reg, owner);
        const HeroId b = createHero(gs, reg, owner, "B", incomeHero->classId, {4, 3});
        giveHeroTemplate(*gs.hero(b), reg, incomeHero->id);
        const ResourceAmounts more = dailyIncome(gs, reg, owner);
        for (const auto [r, amount] : incomeHero->specialty.income) QCOMPARE(more.value(r), plain.value(r) + amount);

        // In battle: the specialty unit and its upgrade get the bonus, others not.
        gs.setMap(GameMap(16, 8, T("grass")));
        const HeroId c = createHero(gs, reg, owner, "C", unitHero->classId, {6, 3});
        giveHeroTemplate(*gs.hero(c), reg, unitHero->id);
        Army& army = *gs.army(gs.hero(c)->army);
        army.stacks.clear();
        const UnitTypeId special = unitHero->specialty.unit, upgraded = reg.unit(special)->upgradesTo;
        army.addUnits(special, 5, reg.unit(special)->maxHp);
        army.addUnits(upgraded, 5, reg.unit(upgraded)->maxHp);
        army.addUnits(U("monk") == special ? U("militia") : U("monk"), 5, reg.unit(U("monk"))->maxHp);
        const ArmyId enemy = gs.addArmy(PlayerId(), {7, 3});
        gs.army(enemy)->addUnits(U("militia"), 5, reg.unit(U("militia"))->maxHp);
        const Battle battle(gs, reg, army.id, enemy, 1);
        int withBonus = 0;
        for (const BattleUnit& u : battle.units()) {
            if (u.side != 0) continue;
            const bool fits = u.stack.unitTypeId() == special || u.stack.unitTypeId() == upgraded;
            QCOMPARE(u.specialtyAttack, fits ? unitHero->specialty.attack : 0);
            withBonus += fits;
        }
        QCOMPARE(withBonus, 2);
    }

    void createHeroSetsUpTraitsArmyAndSpellPoints() {
        const HeroId id = make("knight");
        QVERIFY(id.isValid());
        const Hero* h = gs.hero(id);
        QCOMPARE(h->trait(TR("attack")), 3);
        QCOMPARE(h->level, 1);
        QCOMPARE(h->spellPoints, 10);
        QCOMPARE(maxSpellPoints(*h, reg), 10);
        const Army* a = gs.army(h->army);
        QVERIFY(a);
        QCOMPARE(a->hero, id);
        QCOMPARE(a->pos, (Pos{2, 3}));
        QCOMPARE(a->owner, owner);
    }

    void createHeroErrors() {
        QString err;
        QVERIFY(!createHero(gs, reg, owner, "X", C("nope"), {0, 0}, &err).isValid());
        QVERIFY(err.contains("unknown hero class"));
        QVERIFY(!createHero(gs, reg, PlayerId(999), "X", C("knight"), {0, 0}, &err).isValid());
        QVERIFY(err.contains("unknown owner"));
    }

    void levelUpGivesPickAndAutoGrowth() {
        Hero& h = *gs.hero(make("knight"));
        QCOMPARE(addExperience(h, reg, 999), 0);
        QCOMPARE(h.level, 1);
        QCOMPARE(addExperience(h, reg, 1), 1);  // total 1000 -> level 2
        QCOMPARE(h.level, 2);
        QCOMPARE(h.pendingTraitPicks, 1);
        QCOMPARE(maxSpellPoints(h, reg), 14);  // 10 + knight autoGrowth 4
        QCOMPARE(h.spellPoints, 10);      // current pool is not refilled by leveling
    }

    void severalLevelsAtOnceGiveSeveralPicks() {
        Hero& h = *gs.hero(make("knight"));
        QCOMPARE(addExperience(h, reg, 6000), 5);  // 1000,2000,3200,4500,6000 -> level 6
        QCOMPARE(h.level, 6);
        QCOMPARE(h.pendingTraitPicks, 5);
        QCOMPARE(maxSpellPoints(h, reg), 30);
    }

    void levelCapStopsLevelsButKeepsExperience() {
        Hero& h = *gs.hero(make("wizard"));
        addExperience(h, reg, 900000000);
        QCOMPARE(h.level, reg.maxLevel());
        QCOMPARE(h.experience, 900000000);
        QCOMPARE(addExperience(h, reg, 1000), 0);
    }

    void zeroOrNegativeExperienceDoesNothing() {
        Hero& h = *gs.hero(make("knight"));
        QCOMPARE(addExperience(h, reg, 0), 0);
        QCOMPARE(addExperience(h, reg, -50), 0);
        QCOMPARE(h.experience, 0);
    }

    void pickTraitUsesClassIncrement() {
        Hero& h = *gs.hero(make("knight"));
        addExperience(h, reg, 3200);  // 3 levels -> 3 picks
        QVERIFY(pickTrait(h, reg, TR("attack")));      // knight: +2
        QCOMPARE(h.trait(TR("attack")), 5);
        QVERIFY(pickTrait(h, reg, TR("luck")));        // no class entry: +1
        QCOMPARE(h.trait(TR("luck")), 1);
        QCOMPARE(h.pendingTraitPicks, 1);
    }

    void pickTraitRejections() {
        Hero& h = *gs.hero(make("knight"));
        QString err;
        QVERIFY(!pickTrait(h, reg, TR("attack"), &err));
        QVERIFY(err.contains("no trait pick"));
        addExperience(h, reg, 1000);
        QVERIFY(!pickTrait(h, reg, TR("spell_points_max"), &err));
        QVERIFY(err.contains("cannot be picked"));
        QVERIFY(!pickTrait(h, reg, TR("nonsense"), &err));
        QVERIFY(err.contains("unknown trait"));
        QCOMPARE(h.pendingTraitPicks, 1);  // failed picks are not consumed
    }

    void spellPointRegenerationIsCapped() {
        Hero& h = *gs.hero(make("knight"));  // max 10, recovery 1
        h.spellPoints = 3;
        QCOMPARE(regenSpellPoints(h, reg), 1);
        QCOMPARE(h.spellPoints, 4);
        h.spellPoints = 10;
        QCOMPARE(regenSpellPoints(h, reg), 0);

        Hero& w = *gs.hero(make("wizard"));  // max 30, recovery 2
        w.spellPoints = 29;
        QCOMPARE(regenSpellPoints(w, reg), 1);
        QCOMPARE(w.spellPoints, 30);
        w.spellPoints = 0;
        QCOMPARE(regenSpellPoints(w, reg), 2);
    }
};

QTEST_APPLESS_MAIN(TstHeroRules)
#include "tst_herorules.moc"
