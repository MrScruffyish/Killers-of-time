#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "TestContent.h"

#include "core/battle/AutoBattle.h"
#include "core/battle/Battle.h"
#include "core/battle/BattleAi.h"
#include "core/content/ContentRegistry.h"
#include "core/magic/Spells.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Movement.h"
#include "core/rules/TurnCycle.h"

using namespace kot::core;

class TstSpells : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId p1, p2;

    HeroId wizard(PlayerId owner = {}, Pos pos = {0, 0}) {
        return createHero(gs, reg, owner.isValid() ? owner : p1, "W", C("wizard"), pos);
    }
    ArmyId monsters(const QString& unit, int count, Pos pos = {1, 0}) {
        const ArmyId a = gs.addArmy(PlayerId(), pos);
        gs.army(a)->addUnits(U(qPrintable(unit)), count, reg.unit(U(qPrintable(unit)))->maxHp);
        return a;
    }
    static BattleAction castOn(const QString& spell, int target) {
        BattleAction a;
        a.type = BattleAction::Type::Cast;
        a.spell = S(qPrintable(spell));
        a.target = target;
        return a;
    }
    static QString copyDataWith(QTemporaryDir& tmp, const QString& file, const QString& text) {
        const QDir src(KOT_DATA_DIR);
        QDirIterator it(KOT_DATA_DIR, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QString rel = src.relativeFilePath(it.filePath());
            QDir().mkpath(QFileInfo(tmp.filePath(rel)).absolutePath());
            QFile::copy(it.filePath(), tmp.filePath(rel));
        }
        QFile f(tmp.filePath(file));
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(text.toUtf8());
        return tmp.path();
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
        useContent(reg);
    }
    void init() {
        gs = GameState(9);
        gs.setMap(GameMap(30, 30, T("grass")));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, RC("humans"), "#ff0000");
        p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Human, RC("humans"), "#0000ff");
    }

    // ---- data
    void spellsLoad() {
        QVERIFY(reg.spells().size() >= 8);
        const SpellDef* arrow = reg.spell(S("magic_arrow"));
        QVERIFY(arrow);
        QCOMPARE(arrow->scope, SpellDef::Scope::Battle);
        QCOMPARE(arrow->target, SpellDef::Target::Enemy);
        QCOMPARE(arrow->effects.first().type, SpellEffect::Type::Damage);
        QCOMPARE(reg.spell(S("swift_march"))->scope, SpellDef::Scope::Adventure);
        QVERIFY(reg.heroClass(C("wizard"))->startingSpells.contains(S("magic_arrow")));
        QVERIFY(gs.hero(wizard())->spells.contains(S("magic_arrow")));
    }

    void badSpellDataIsRefused() {
        QTemporaryDir tmp;
        ContentRegistry r;
        QStringList errors;
        QVERIFY(!r.loadFromDirectory(copyDataWith(tmp, "spells/magic_arrow.json",
            R"({"id":"magic_arrow","name":"X","level":1,"cost":5,"scope":"adventure",)"
            R"("effects":[{"type":"damage","amount":5}]})"), &errors));
        QVERIFY2(errors.join(" ").contains("does not fit"), qPrintable(errors.join("\n")));
    }

    // ---- who can cast
    void castingRules() {
        Hero& w = *gs.hero(wizard());  // magic 3, 30 SP
        QVERIFY(whyCannotCast(w, reg, S("magic_arrow"), SpellDef::Scope::Battle, w.spellPoints).isEmpty());
        QVERIFY(whyCannotCast(w, reg, S("nonsense"), SpellDef::Scope::Battle, 99).contains("unknown"));
        QVERIFY(whyCannotCast(w, reg, S("haste"), SpellDef::Scope::Battle, 99).contains("does not know"));
        QVERIFY(whyCannotCast(w, reg, S("magic_arrow"), SpellDef::Scope::Adventure, 99).contains("only"));
        QVERIFY(whyCannotCast(w, reg, S("magic_arrow"), SpellDef::Scope::Battle, 2).contains("spell points"));
        w.spells << S("lightning_bolt");
        w.traits[TR("magic")] = 2;
        QVERIFY(whyCannotCast(w, reg, S("lightning_bolt"), SpellDef::Scope::Battle, 99).contains("Magic 3"));
    }

    // ---- adventure map
    void swiftMarchGivesMovement() {
        const HeroId h = wizard();
        Hero& w = *gs.hero(h);
        w.movementPoints = 100;
        const int sp = w.spellPoints;
        QVERIFY(castAdventureSpell(gs, reg, h, S("swift_march")).ok);
        QCOMPARE(w.movementPoints, 100 + 300 + 50 * w.trait(TR("spell_power")));
        QCOMPARE(w.spellPoints, sp - reg.spell(S("swift_march"))->cost);
        QVERIFY(!castAdventureSpell(gs, reg, h, S("magic_arrow")).ok);
    }

    void farSightReveals() {
        const HeroId h = wizard(p1, {15, 15});
        gs.hero(h)->spells << S("far_sight");
        const TeamId team = gs.teamOf(p1);
        QCOMPARE(int(gs.exploredMap(team)[15 * 30 + 24]), 0);
        QVERIFY(castAdventureSpell(gs, reg, h, S("far_sight")).ok);  // radius 7 + spell power 3
        QCOMPARE(int(gs.exploredMap(team)[15 * 30 + 24]), 1);
        QCOMPARE(int(gs.exploredMap(team)[15 * 30 + 27]), 0);
    }

    void mageGuildTeaches() {
        const TownId town = gs.addTown("Home", RC("humans"), {5, 5}, p1);
        const HeroId h = createHero(gs, reg, p1, "K", C("knight"), {5, 5});
        QVERIFY(learnSpellsInTown(gs, reg, h).isEmpty());  // no guild yet
        gs.town(town)->buildings << B("village_hall") << B("mage_guild");
        QCOMPARE(mageGuildLevel(*gs.town(town), reg), 2);
        const QList<SpellId> learned = learnSpellsInTown(gs, reg, h);
        QVERIFY(learned.contains(S("magic_arrow")) && learned.contains(S("haste")));
        QVERIFY(!learned.contains(S("lightning_bolt")));  // level 3
        QVERIFY(!learned.contains(S("cure")));             // knights know it already
        QVERIFY(learnSpellsInTown(gs, reg, h).isEmpty());
    }

    void heroesLearnWhenTheyWalkIn() {
        const TownId town = gs.addTown("Home", RC("humans"), {3, 0}, p1);
        gs.town(town)->buildings << B("village_hall") << B("mage_guild");
        const HeroId h = createHero(gs, reg, p1, "K", C("knight"), {0, 0});
        refreshHeroMovement(gs, reg, p1);
        const MoveResult r = moveHero(gs, reg, h, {3, 0});
        QVERIFY2(r.ok && r.arrived, qPrintable(r.error));
        QVERIFY(r.learnedSpells.contains(S("slow")));
        QVERIFY(gs.hero(h)->spells.contains(S("slow")));
    }

    void spellbookIsSaved() {
        const HeroId h = wizard();
        gs.hero(h)->spells << S("far_sight");
        gs.setTurnOrder({p1, p2});
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(gs.toJson(reg), reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(loaded.hero(h)->spells, gs.hero(h)->spells);
    }

    // ---- battle
    void magicArrowInBattle() {
        const HeroId h = wizard();
        gs.army(gs.hero(h)->army)->addUnits(U("militia"), 5, 6);
        const ArmyId m = monsters("pikeman", 20);
        Battle b(gs, reg, gs.hero(h)->army, m, 1);
        QCOMPARE(b.activeUnit()->side, 0);
        const int enemy = 2;
        const int before = b.unit(enemy)->stack.totalHp();
        QList<BattleEvent> ev;
        QVERIFY(!b.act(castOn("magic_arrow", 1), &ev).isEmpty());  // not on your own stack
        QVERIFY(b.act(castOn("magic_arrow", enemy), &ev).isEmpty());
        const int power = gs.hero(h)->trait(TR("spell_power"));
        QCOMPARE(b.unit(enemy)->stack.totalHp(), before - (10 + 10 * power));
        QCOMPARE(ev.last().type, BattleEvent::Type::Cast);
        QCOMPARE(b.activeUnit()->id, 1);  // still the militia's turn
        QVERIFY(b.act(castOn("magic_arrow", enemy)).contains("already"));
        QCOMPARE(b.heroSpellPoints(0), gs.hero(h)->spellPoints - 5);

        // Spell points are written back after the battle.
        while (!b.isOver()) b.act(chooseBattleAction(b));
        const int left = b.heroSpellPoints(0);
        applyBattleResult(gs, reg, b);
        if (gs.hero(h)) QCOMPARE(gs.hero(h)->spellPoints, left);
    }

    void stoneSkinLastsThreeRounds() {
        const HeroId h = wizard();
        gs.army(gs.hero(h)->army)->addUnits(U("pikeman"), 5, 10);
        Battle b(gs, reg, gs.hero(h)->army, monsters("battle_snail", 1), 1);
        QVERIFY(b.act(castOn("stone_skin", 1)).isEmpty());
        const int bonus = 3 + gs.hero(h)->trait(TR("spell_power"));
        QCOMPARE(b.unit(1)->modifier("defense"), bonus);
        BattleAction defend;
        for (int i = 0; i < 4; ++i) b.act(defend);  // two rounds (pikeman, snail each)
        QCOMPARE(b.round(), 3);
        QCOMPARE(b.unit(1)->modifier("defense"), bonus);
        b.act(defend);
        b.act(defend);
        QCOMPARE(b.round(), 4);
        QCOMPARE(b.unit(1)->modifier("defense"), 0);
    }

    void slowShortensTheMove() {
        const HeroId h = wizard();
        gs.hero(h)->spells << S("slow");
        gs.army(gs.hero(h)->army)->addUnits(U("militia"), 5, 6);
        Battle b(gs, reg, gs.hero(h)->army, monsters("pikeman", 5), 1);
        const int before = b.reachable(*b.unit(2)).size();
        QVERIFY(b.act(castOn("slow", 2)).isEmpty());
        QVERIFY(b.reachable(*b.unit(2)).size() < before);
    }

    void aiHeroCastsInAutoBattles() {
        const HeroId h = wizard();
        gs.army(gs.hero(h)->army)->addUnits(U("pikeman"), 20, 10);
        const int sp = gs.hero(h)->spellPoints;
        const BattleReport r = resolveBattle(gs, reg, gs.hero(h)->army, monsters("militia", 30));
        QVERIFY(r.attackerWon());
        QVERIFY(gs.hero(h)->spellPoints < sp);
    }

    // Not only damage: a hero who knows only Bless blesses one of his stacks.
    void aiHeroUsesBlessingsAndCurses() {
        const HeroId h = wizard();
        gs.hero(h)->spells = {S("bless")};
        gs.army(gs.hero(h)->army)->addUnits(U("militia"), 10, 6);
        const ArmyId m = monsters("pikeman", 20);
        Battle b(gs, reg, gs.hero(h)->army, m, 3);
        const BattleAction a = chooseBattleAction(b);
        QCOMPARE(a.type, BattleAction::Type::Cast);
        QCOMPARE(a.spell, S("bless"));
        QCOMPARE(b.unit(a.target)->side, 0);
    }

    void unitsCastTheirOwnSpells() {
        const HeroId h = wizard();
        gs.army(gs.hero(h)->army)->addUnits(U("unicorn"), 3, 80);
        const ArmyId m = monsters("wraith", 4);
        Battle b(gs, reg, gs.hero(h)->army, m, 1);
        // The wraiths (initiative 7) or unicorns (10): find the unicorns' turn.
        while (b.activeUnit()->def->id != U("unicorn")) b.act(BattleAction{});
        const BattleUnit* uni = b.activeUnit();
        QCOMPARE(uni->spellPoints, reg.unit(U("unicorn"))->spellPoints);
        BattleAction cure;
        cure.type = BattleAction::Type::UnitCast;
        cure.spell = S("cure");
        cure.target = 2;  // an enemy: refused
        QVERIFY(!b.act(cure).isEmpty());
        cure.spell = S("magic_arrow");
        cure.target = 1;
        QVERIFY(b.act(cure).contains("cannot cast"));
        cure.spell = S("cure");
        QList<BattleEvent> ev;
        QVERIFY(b.act(cure, &ev).isEmpty());
        QCOMPARE(b.unit(1)->spellPoints, reg.unit(U("unicorn"))->spellPoints - reg.spell(S("cure"))->cost);
        QVERIFY(b.activeUnit()->id != 1);  // it used the turn
        bool cast = false;
        for (const BattleEvent& e : ev) cast = cast || (e.type == BattleEvent::Type::Cast && e.unit == 1);
        QVERIFY(cast);
    }

    void unitSpellDataIsChecked() {
        QTemporaryDir tmp;
        ContentRegistry r;
        QStringList errors;
        QVERIFY(!r.loadFromDirectory(copyDataWith(tmp, "units/militia.json",
            R"({"id":"militia","name":"M","race":"humans","tier":1,"attack":1,"defense":1,"damageMin":1,"damageMax":1,)"
            R"("maxHp":5,"battleSpeed":4,"spells":["swift_march"],"spellPoints":5})"), &errors));
        QVERIFY2(errors.join(" ").contains("not a battle spell"), qPrintable(errors.join("\n")));
    }

    void battleJsonShowsYourSpells() {
        const HeroId h = wizard();
        gs.army(gs.hero(h)->army)->addUnits(U("militia"), 5, 6);
        Battle b(gs, reg, gs.hero(h)->army, monsters("pikeman", 5), 1);
        const QJsonObject j = battleToJson(b, 0);
        QVERIFY(j.contains("hero"));
        QCOMPARE(j["hero"].toObject()["spellPoints"].toInt(), gs.hero(h)->spellPoints);
        bool arrow = false;
        for (const QJsonValue& v : j["hero"].toObject()["spells"].toArray())
            arrow = arrow || (v.toObject()["id"].toInt() == S("magic_arrow").index() && v.toObject()["castable"].toBool());
        QVERIFY(arrow);
        QVERIFY(!battleToJson(b, 1).contains("hero"));  // monsters have no hero
    }
};

QTEST_GUILESS_MAIN(TstSpells)
#include "tst_spells.moc"
