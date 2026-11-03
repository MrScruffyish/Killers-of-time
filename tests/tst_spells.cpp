#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

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
        return createHero(gs, reg, owner.isValid() ? owner : p1, "W", "wizard", pos);
    }
    ArmyId monsters(const QString& unit, int count, Pos pos = {1, 0}) {
        const ArmyId a = gs.addArmy(PlayerId(), pos);
        gs.army(a)->addUnits(unit, count, reg.unit(unit)->maxHp);
        return a;
    }
    static BattleAction castOn(const QString& spell, int target) {
        BattleAction a;
        a.type = BattleAction::Type::Cast;
        a.spell = spell;
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
    }
    void init() {
        gs = GameState(9);
        gs.setMap(GameMap(30, 30, "grass"));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, "humans", "#ff0000");
        p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Human, "humans", "#0000ff");
    }

    // ---- data
    void spellsLoad() {
        QVERIFY(reg.spells().size() >= 8);
        const SpellDef* arrow = reg.spell("magic_arrow");
        QVERIFY(arrow);
        QCOMPARE(arrow->scope, SpellDef::Scope::Battle);
        QCOMPARE(arrow->target, SpellDef::Target::Enemy);
        QCOMPARE(arrow->effects.first().type, SpellEffect::Type::Damage);
        QCOMPARE(reg.spell("swift_march")->scope, SpellDef::Scope::Adventure);
        QVERIFY(reg.heroClass("wizard")->startingSpells.contains("magic_arrow"));
        QVERIFY(gs.hero(wizard())->spells.contains("magic_arrow"));
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
        QVERIFY(whyCannotCast(w, reg, "magic_arrow", SpellDef::Scope::Battle, w.spellPoints).isEmpty());
        QVERIFY(whyCannotCast(w, reg, "nonsense", SpellDef::Scope::Battle, 99).contains("unknown"));
        QVERIFY(whyCannotCast(w, reg, "haste", SpellDef::Scope::Battle, 99).contains("does not know"));
        QVERIFY(whyCannotCast(w, reg, "magic_arrow", SpellDef::Scope::Adventure, 99).contains("only"));
        QVERIFY(whyCannotCast(w, reg, "magic_arrow", SpellDef::Scope::Battle, 2).contains("spell points"));
        w.spells << "lightning_bolt";
        w.traits["magic"] = 2;
        QVERIFY(whyCannotCast(w, reg, "lightning_bolt", SpellDef::Scope::Battle, 99).contains("Magic 3"));
    }

    // ---- adventure map
    void swiftMarchGivesMovement() {
        const HeroId h = wizard();
        Hero& w = *gs.hero(h);
        w.movementPoints = 100;
        const int sp = w.spellPoints;
        QVERIFY(castAdventureSpell(gs, reg, h, "swift_march").ok);
        QCOMPARE(w.movementPoints, 100 + 300 + 50 * w.trait("spell_power"));
        QCOMPARE(w.spellPoints, sp - reg.spell("swift_march")->cost);
        QVERIFY(!castAdventureSpell(gs, reg, h, "magic_arrow").ok);
    }

    void farSightReveals() {
        const HeroId h = wizard(p1, {15, 15});
        gs.hero(h)->spells << "far_sight";
        const TeamId team = gs.teamOf(p1);
        QCOMPARE(int(gs.exploredMap(team)[15 * 30 + 24]), 0);
        QVERIFY(castAdventureSpell(gs, reg, h, "far_sight").ok);  // radius 7 + spell power 3
        QCOMPARE(int(gs.exploredMap(team)[15 * 30 + 24]), 1);
        QCOMPARE(int(gs.exploredMap(team)[15 * 30 + 27]), 0);
    }

    void mageGuildTeaches() {
        const TownId town = gs.addTown("Home", "humans", {5, 5}, p1);
        const HeroId h = createHero(gs, reg, p1, "K", "knight", {5, 5});
        QVERIFY(learnSpellsInTown(gs, reg, h).isEmpty());  // no guild yet
        gs.town(town)->buildings << "village_hall" << "mage_guild";
        QCOMPARE(mageGuildLevel(*gs.town(town), reg), 2);
        const QStringList learned = learnSpellsInTown(gs, reg, h);
        QVERIFY(learned.contains("magic_arrow") && learned.contains("haste"));
        QVERIFY(!learned.contains("lightning_bolt"));  // level 3
        QVERIFY(!learned.contains("cure"));             // knights know it already
        QVERIFY(learnSpellsInTown(gs, reg, h).isEmpty());
    }

    void heroesLearnWhenTheyWalkIn() {
        const TownId town = gs.addTown("Home", "humans", {3, 0}, p1);
        gs.town(town)->buildings << "village_hall" << "mage_guild";
        const HeroId h = createHero(gs, reg, p1, "K", "knight", {0, 0});
        refreshHeroMovement(gs, reg, p1);
        const MoveResult r = moveHero(gs, reg, h, {3, 0});
        QVERIFY2(r.ok && r.arrived, qPrintable(r.error));
        QVERIFY(r.learnedSpells.contains("slow"));
        QVERIFY(gs.hero(h)->spells.contains("slow"));
    }

    void spellbookIsSaved() {
        const HeroId h = wizard();
        gs.hero(h)->spells << "far_sight";
        gs.setTurnOrder({p1, p2});
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(gs.toJson(), reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(loaded.hero(h)->spells, gs.hero(h)->spells);
    }

    // ---- battle
    void magicArrowInBattle() {
        const HeroId h = wizard();
        gs.army(gs.hero(h)->army)->addUnits("militia", 5, 6);
        const ArmyId m = monsters("pikeman", 20);
        Battle b(gs, reg, gs.hero(h)->army, m, 1);
        QCOMPARE(b.activeUnit()->side, 0);
        const int enemy = 2;
        const int before = b.unit(enemy)->stack.totalHp();
        QList<BattleEvent> ev;
        QVERIFY(!b.act(castOn("magic_arrow", 1), &ev).isEmpty());  // not on your own stack
        QVERIFY(b.act(castOn("magic_arrow", enemy), &ev).isEmpty());
        const int power = gs.hero(h)->trait("spell_power");
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
        gs.army(gs.hero(h)->army)->addUnits("pikeman", 5, 10);
        Battle b(gs, reg, gs.hero(h)->army, monsters("battle_snail", 1), 1);
        QVERIFY(b.act(castOn("stone_skin", 1)).isEmpty());
        const int bonus = 3 + gs.hero(h)->trait("spell_power");
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
        gs.hero(h)->spells << "slow";
        gs.army(gs.hero(h)->army)->addUnits("militia", 5, 6);
        Battle b(gs, reg, gs.hero(h)->army, monsters("pikeman", 5), 1);
        const int before = b.reachable(*b.unit(2)).size();
        QVERIFY(b.act(castOn("slow", 2)).isEmpty());
        QVERIFY(b.reachable(*b.unit(2)).size() < before);
    }

    void aiHeroCastsInAutoBattles() {
        const HeroId h = wizard();
        gs.army(gs.hero(h)->army)->addUnits("pikeman", 20, 10);
        const int sp = gs.hero(h)->spellPoints;
        const BattleReport r = resolveBattle(gs, reg, gs.hero(h)->army, monsters("militia", 30));
        QVERIFY(r.attackerWon());
        QVERIFY(gs.hero(h)->spellPoints < sp);
    }

    // Not only damage: a hero who knows only Bless blesses one of his stacks.
    void aiHeroUsesBlessingsAndCurses() {
        const HeroId h = wizard();
        gs.hero(h)->spells = {"bless"};
        gs.army(gs.hero(h)->army)->addUnits("militia", 10, 6);
        const ArmyId m = monsters("pikeman", 20);
        Battle b(gs, reg, gs.hero(h)->army, m, 3);
        const BattleAction a = chooseBattleAction(b);
        QCOMPARE(a.type, BattleAction::Type::Cast);
        QCOMPARE(a.spell, QString("bless"));
        QCOMPARE(b.unit(a.target)->side, 0);
    }

    void unitsCastTheirOwnSpells() {
        const HeroId h = wizard();
        gs.army(gs.hero(h)->army)->addUnits("unicorn", 3, 80);
        const ArmyId m = monsters("wraith", 4);
        Battle b(gs, reg, gs.hero(h)->army, m, 1);
        // The wraiths (initiative 7) or unicorns (10): find the unicorns' turn.
        while (b.activeUnit()->def->id != "unicorn") b.act(BattleAction{});
        const BattleUnit* uni = b.activeUnit();
        QCOMPARE(uni->spellPoints, reg.unit("unicorn")->spellPoints);
        BattleAction cure;
        cure.type = BattleAction::Type::UnitCast;
        cure.spell = "cure";
        cure.target = 2;  // an enemy: refused
        QVERIFY(!b.act(cure).isEmpty());
        cure.spell = "magic_arrow";
        cure.target = 1;
        QVERIFY(b.act(cure).contains("cannot cast"));
        cure.spell = "cure";
        QList<BattleEvent> ev;
        QVERIFY(b.act(cure, &ev).isEmpty());
        QCOMPARE(b.unit(1)->spellPoints, reg.unit("unicorn")->spellPoints - reg.spell("cure")->cost);
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
        gs.army(gs.hero(h)->army)->addUnits("militia", 5, 6);
        Battle b(gs, reg, gs.hero(h)->army, monsters("pikeman", 5), 1);
        const QJsonObject j = battleToJson(b, 0);
        QVERIFY(j.contains("hero"));
        QCOMPARE(j["hero"].toObject()["spellPoints"].toInt(), gs.hero(h)->spellPoints);
        bool arrow = false;
        for (const QJsonValue& v : j["hero"].toObject()["spells"].toArray())
            arrow = arrow || (v.toObject()["id"] == "magic_arrow" && v.toObject()["castable"].toBool());
        QVERIFY(arrow);
        QVERIFY(!battleToJson(b, 1).contains("hero"));  // monsters have no hero
    }
};

QTEST_GUILESS_MAIN(TstSpells)
#include "tst_spells.moc"
