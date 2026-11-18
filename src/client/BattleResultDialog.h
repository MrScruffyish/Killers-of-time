#pragma once

#include <QDialog>
#include <QJsonObject>

namespace kot::core { class ContentRegistry; }

namespace kot::client {

// The end of a battle, seen from your side: a big emblem (victory: laurel and
// crossed swords; defeat: a broken sword; undecided: grey), both sides' losses as
// unit pictures with counts, experience and what became of the heroes.
class BattleResultDialog : public QDialog {
    Q_OBJECT
public:
    enum class Outcome { Victory, Defeat, Draw };
    // `report` is a battle report (battleReportToJson), `you` your player id.
    BattleResultDialog(const core::ContentRegistry* content, const QJsonObject& report, int you, QWidget* parent = nullptr);
    Outcome outcome() const { return m_outcome; }

    static Outcome outcomeFor(const QJsonObject& report, int you);

private:
    Outcome m_outcome;
};

}  // namespace kot::client
