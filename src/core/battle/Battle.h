#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "core/battle/HexGrid.h"
#include "core/model/GameState.h"
#include "core/util/Rng.h"

namespace kot::core {

class ContentRegistry;
struct UnitDef;

// A temporary change from a spell (Stone Skin, Slow, ...).
struct StatModifier {
    QString stat;    // attack, defense, battleSpeed
    int amount = 0;
    int roundsLeft = 0;
};

// One stack on the battle field (a copy of an army stack while the battle runs).
struct BattleUnit {
    int id = 0;          // 1.. within this battle
    int side = 0;        // 0 attacker, 1 defender
    int armyIndex = 0;   // index of the stack in its army
    const UnitDef* def = nullptr;
    UnitStack stack;
    Pos pos;             // hex
    int shotsLeft = 0;
    bool retaliated = false;  // this round
    bool waited = false;      // this round
    bool moraleUsed = false;  // got its extra action this round
    int baseMaxHp = 1;        // max HP per unit in the army (before the hero's Hit Points bonus)
    QList<StatModifier> modifiers;

    bool alive() const { return !stack.isEliminated(); }
    int modifier(const QString& stat) const;
};

struct BattleAction {
    enum class Type { Move, Attack, Shoot, Defend, Cast, Wait, Retreat };
    Type type = Type::Defend;
    Pos hex;               // Move: where to. Attack: hex to strike from (optional)
    bool hasHex = false;
    int target = 0;        // Attack / Shoot: enemy unit id; Cast: the spell's target
    QString spell;         // Cast
};

// What happened, for the battle log and the client.
struct BattleEvent {
    enum class Type { Move, Attack, Shoot, Retaliate, Defend, NewRound, Cast, Wait, Morale, Falter, Retreat };
    Type type = Type::Move;
    int unit = 0;
    int target = 0;
    Pos from, to;
    int damage = 0;   // Cast: damage done or HP healed
    int killed = 0;
    QString spell;
    int side = -1;    // Cast: the casting hero's side
    bool lucky = false;  // a lucky strike (double damage)
};

// A tactical battle on a hex grid (PROJECT.md 5.10, kept simple for now).
//
// The attacker's stacks start in the left column, the defender's in the right one.
// Each round every living stack acts once, by initiative, then battle speed, the
// attacker first on ties. The active stack can:
//   - Move: to a free hex within its battle speed (steps around other stacks),
//   - Attack: an enemy it can reach (moves next to it first); the target strikes
//     back once per round,
//   - Shoot: ranged units with shots left, when no enemy stands next to them;
//     no retaliation,
//   - Defend: do nothing this round,
//   - Wait: act again at the end of this round (once per round),
//   - Retreat (attacker only): give up; the army is lost, the hero survives.
// The hero's Morale trait may give a stack an extra action (or, negative, make it
// falter and lose its turn; undead ignore morale), his Luck may double a strike's
// damage, his Hit Points raise the max HP of his units. Unit abilities: flying,
// no_retaliation, double_attack, undead (UnitDef). A few rocks block hexes.
// Besides, the hero of the active stack's side may cast one battle spell per round
// (Cast; it does not use up the stack's turn): damage, heal or a stat change for a
// few rounds. Spell points are spent from a copy and written back afterwards.
// Damage = count * roll(damageMin..damageMax) * attack/defense factor (balance.json
// "battle"), hero Attack/Defense traits added. The battle ends when one side has no
// stacks left, or as a draw after maxRounds.
//
// The battle works on copies; applyBattleResult (AutoBattle.h) writes the outcome
// back into the game state. Randomness comes from its own Rng (seeded by the caller
// from the game's Rng), so a battle can be replayed from seed + actions.
class Battle {
public:
    enum class Outcome { Running, AttackerWon, DefenderWon, Draw };

    Battle(const GameState& state, const ContentRegistry& content, ArmyId attacker, ArmyId defender,
           std::uint64_t seed);

    const ContentRegistry& content() const { return m_content; }
    ArmyId army(int side) const { return m_army[side]; }
    PlayerId owner(int side) const { return m_owner[side]; }
    HeroId hero(int side) const { return m_hero[side]; }
    const QList<BattleUnit>& units() const { return m_units; }
    const BattleUnit* unit(int id) const;
    const BattleUnit* unitAt(Pos hex) const;
    const BattleUnit* activeUnit() const;  // null when over
    int round() const { return m_round; }
    Outcome outcome() const { return m_outcome; }
    bool isOver() const { return m_outcome != Outcome::Running; }
    bool sideAlive(int side) const;
    bool retreated(int side) const { return m_retreated[side]; }
    const QList<Pos>& obstacles() const { return m_obstacles; }
    bool blocked(Pos hex) const { return m_obstacles.contains(hex) || unitAt(hex); }

    // The side's hero (a copy taken when the battle started), or null.
    const Hero* heroOf(int side) const { return m_heroCopy[side].id.isValid() ? &m_heroCopy[side] : nullptr; }
    int heroSpellPoints(int side) const { return m_heroCopy[side].spellPoints; }
    bool heroHasCast(int side) const { return m_heroCast[side]; }
    // Why the active side's hero cannot cast this spell on `target` now (empty = can).
    // target 0 checks everything except the target.
    QString whyCannotCast(const QString& spellId, int target) const;

    // Hexes the unit can move to, with the number of steps.
    QHash<Pos, int> reachable(const BattleUnit& unit) const;
    bool canShoot(const BattleUnit& unit) const;
    // The hex the active unit would attack `target` from (its own hex if already
    // next to it); false if it cannot reach it.
    bool attackHex(const BattleUnit& attacker, const BattleUnit& target, Pos* from) const;

    // Performs an action for the active unit. Returns an error text (nothing happens)
    // or an empty string. `events` (optional) gets what happened.
    QString act(const BattleAction& action, QList<BattleEvent>* events = nullptr);

private:
    QString cast(const BattleAction& action, QList<BattleEvent>* events);
    void strike(BattleUnit& from, BattleUnit& to, BattleEvent::Type type, QList<BattleEvent>* events);
    void startRound(QList<BattleEvent>* events);
    bool chance(int percent);
    void advance(QList<BattleEvent>* events);
    void settle(QList<BattleEvent>* events);  // the next stack that can act
    bool ignoresMorale(const BattleUnit& u) const;
    void checkOver();
    BattleUnit* mutableUnit(int id);

    const ContentRegistry& m_content;
    Rng m_rng;
    ArmyId m_army[2];
    PlayerId m_owner[2];
    HeroId m_hero[2];
    int m_attackBonus[2] = {0, 0};
    int m_defenseBonus[2] = {0, 0};
    Hero m_heroCopy[2];
    int m_morale[2] = {0, 0};
    int m_luck[2] = {0, 0};
    bool m_retreated[2] = {false, false};
    QList<Pos> m_obstacles;
    bool m_heroCast[2] = {false, false};
    QList<BattleUnit> m_units;
    QList<int> m_order;  // unit ids, this round
    int m_orderPos = 0;
    int m_round = 0;
    Outcome m_outcome = Outcome::Running;
};

// The battle as the client sees it (both sides see everything on the field);
// `viewerSide` (0/1) also gets his hero's spells and spell points.
QJsonObject battleToJson(const Battle& battle, int viewerSide = -1);
QJsonObject battleEventToJson(const BattleEvent& event);

}  // namespace kot::core
