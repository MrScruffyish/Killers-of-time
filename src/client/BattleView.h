#pragma once

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
// "Cast spell" picks one of the hero's battle spells; the next click on a stack
// casts it there.
// The view only asks; the server decides (battle_action).
class BattleView : public QWidget {
    Q_OBJECT
public:
    BattleView(const core::ContentRegistry* content, QWidget* parent = nullptr);

    void setBattle(const ViewBattle& battle, const QColor& attacker, const QColor& defender);
    // Hex centre on screen, for tests that click.
    QPoint hexCentre(core::Pos hex) const;
    QWidget* field() const;

signals:
    void actionRequested(const QJsonObject& body);  // a battle_action body

private:
    void onHexClicked(core::Pos hex);
    void chooseSpell();
    void updateInfo();

    const core::ContentRegistry* m_content;
    ViewBattle m_battle;
    HexField* m_field;
    QLabel* m_info;
    QPushButton* m_defend;
    QPushButton* m_auto;
    QPushButton* m_cast;
    QString m_castSpell;  // chosen spell waiting for a target click
};

}  // namespace kot::client
