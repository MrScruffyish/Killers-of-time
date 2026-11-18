#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

#include "client/ViewModel.h"

class QLabel;
class QPushButton;

namespace kot::core { class ContentRegistry; }

namespace kot::client {

class HexField;

// The battle screen (placeholder art): the hex field, whose turn it is, and the
// Defend / Auto buttons. Clicking a highlighted hex moves the active stack there;
// clicking a highlighted enemy attacks it (or shoots, if the stack can).
// "Hero's spell..." and "Stack's spell..." pick a battle spell (any distance, one
// side); the next click on a marked stack casts it there, an empty hex cancels.
// The view only asks; the server decides (battle_action).
class BattleView : public QWidget {
    Q_OBJECT
public:
    BattleView(const core::ContentRegistry* content, QWidget* parent = nullptr);

    void setBattle(const ViewBattle& battle, const QColor& attacker, const QColor& defender);
    // Shows what happened (battle events from the server): arrows, blows, spells.
    void playEvents(const QJsonArray& events);
    // True while the field is still showing the last attacks and losses.
    bool isAnimating() const;
    // The battle is over (the map is shown again): auto-play off, nothing left over.
    void battleEnded();
    // Hex centre on screen, for tests that click.
    QPoint hexCentre(core::Pos hex) const;
    QWidget* field() const;
    // How far to turn the attack sword so it points from `from` to `to` on screen.
    // Snapped to art::kPointerAngleStep, in [0, 360).
    static int attackPointerDegrees(QPointF from, QPointF to);

    // Starts aiming a battle spell (what choosing one in the book does): the field
    // then marks the stacks it can hit, at any distance. An invalid id stops aiming.
    void aimSpell(core::SpellId spell, bool unitSpell);
    bool isAimingSpell() const { return m_castSpell.isValid(); }
    bool spellTargetsEnemy() const { return m_castTargetsEnemy; }
    // For tests: whether the stack on `hex` is a valid target of the aimed spell.
    bool spellAimsAt(core::Pos hex) const;

signals:
    void actionRequested(const QJsonObject& body);  // a battle_action body

private:
    void onHexClicked(core::Pos hex, core::Pos strikeFrom = {-1, -1});
    void chooseSpell(bool unitSpell);
    void updateInfo();
    void updateStats();  // the stats panel: hovered stack, else the acting one
    void autoStep();     // auto-play: the next move when it is your turn

    const core::ContentRegistry* m_content;
    ViewBattle m_battle;
    HexField* m_field;
    class TextLine* m_info;
    QLabel* m_stats;
    int m_hoverUnit = 0;
    QPushButton* m_defend;
    QPushButton* m_auto;
    QPushButton* m_autoPlay;
    QPushButton* m_engine;
    bool m_autoPending = false;
    QPushButton* m_cast;
    QPushButton* m_unitCast;
    bool m_castByUnit = false;
    QPushButton* m_wait;
    QPushButton* m_retreat;
    QPushButton* m_surrender;
    core::SpellId m_castSpell;  // chosen spell waiting for a target click (invalid: none)
    bool m_castTargetsEnemy = true;  // which side the chosen spell is cast on
};

}  // namespace kot::client
