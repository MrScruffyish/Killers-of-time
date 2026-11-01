#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"

using namespace kot::core;

class TstHeroRules : public QObject {
    Q_OBJECT

    ContentRegistry reg;
    GameState gs;
    PlayerId owner;

    HeroId make(const char* cls) {
        QString err;
        const HeroId id = createHero(gs, reg, owner, "Test", cls, {2, 3}, &err);
        return id;
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void init() {
        gs = GameState(1);
        owner = gs.addPlayer("P", gs.newTeam(), Player::Controller::Human, "humans", "#ffffff");
    }

    void createHeroSetsUpTraitsArmyAndSpellPoints() {
        const HeroId id = make("knight");
        QVERIFY(id.isValid());
        const Hero* h = gs.hero(id);
        QCOMPARE(h->trait("attack"), 3);
        QCOMPARE(h->level, 1);
        QCOMPARE(h->spellPoints, 10);
        QCOMPARE(maxSpellPoints(*h), 10);
        const Army* a = gs.army(h->army);
        QVERIFY(a);
        QCOMPARE(a->hero, id);
        QCOMPARE(a->pos, (Pos{2, 3}));
        QCOMPARE(a->owner, owner);
    }

    void createHeroErrors() {
        QString err;
        QVERIFY(!createHero(gs, reg, owner, "X", "nope", {0, 0}, &err).isValid());
        QVERIFY(err.contains("unknown hero class"));
        QVERIFY(!createHero(gs, reg, PlayerId(999), "X", "knight", {0, 0}, &err).isValid());
        QVERIFY(err.contains("unknown owner"));
    }

    void levelUpGivesPickAndAutoGrowth() {
        Hero& h = *gs.hero(make("knight"));
        QCOMPARE(addExperience(h, reg, 999), 0);
        QCOMPARE(h.level, 1);
        QCOMPARE(addExperience(h, reg, 1), 1);  // total 1000 -> level 2
        QCOMPARE(h.level, 2);
        QCOMPARE(h.pendingTraitPicks, 1);
        QCOMPARE(maxSpellPoints(h), 14);  // 10 + knight autoGrowth 4
        QCOMPARE(h.spellPoints, 10);      // current pool is not refilled by leveling
    }

    void severalLevelsAtOnceGiveSeveralPicks() {
        Hero& h = *gs.hero(make("knight"));
        QCOMPARE(addExperience(h, reg, 6000), 5);  // 1000,2000,3200,4500,6000 -> level 6
        QCOMPARE(h.level, 6);
        QCOMPARE(h.pendingTraitPicks, 5);
        QCOMPARE(maxSpellPoints(h), 30);
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
        QVERIFY(pickTrait(h, reg, "attack"));      // knight: +2
        QCOMPARE(h.trait("attack"), 5);
        QVERIFY(pickTrait(h, reg, "luck"));        // no class entry: +1
        QCOMPARE(h.trait("luck"), 1);
        QCOMPARE(h.pendingTraitPicks, 1);
    }

    void pickTraitRejections() {
        Hero& h = *gs.hero(make("knight"));
        QString err;
        QVERIFY(!pickTrait(h, reg, "attack", &err));
        QVERIFY(err.contains("no trait pick"));
        addExperience(h, reg, 1000);
        QVERIFY(!pickTrait(h, reg, "spell_points_max", &err));
        QVERIFY(err.contains("cannot be picked"));
        QVERIFY(!pickTrait(h, reg, "nonsense", &err));
        QVERIFY(err.contains("unknown trait"));
        QCOMPARE(h.pendingTraitPicks, 1);  // failed picks are not consumed
    }

    void spellPointRegenerationIsCapped() {
        Hero& h = *gs.hero(make("knight"));  // max 10, recovery 1
        h.spellPoints = 3;
        QCOMPARE(regenSpellPoints(h), 1);
        QCOMPARE(h.spellPoints, 4);
        h.spellPoints = 10;
        QCOMPARE(regenSpellPoints(h), 0);

        Hero& w = *gs.hero(make("wizard"));  // max 30, recovery 2
        w.spellPoints = 29;
        QCOMPARE(regenSpellPoints(w), 1);
        QCOMPARE(w.spellPoints, 30);
        w.spellPoints = 0;
        QCOMPARE(regenSpellPoints(w), 2);
    }
};

QTEST_APPLESS_MAIN(TstHeroRules)
#include "tst_herorules.moc"
