#pragma once

#include <QDialog>
#include <QStringList>

namespace kot::core { class ContentRegistry; }

namespace kot::client {

// Choosing a spell: a page of cards, one per spell, each with a rune in its school's
// colour, the name, the cost and what it does. Spells that cannot be cast now are
// greyed out. chooseSpell() returns the chosen id (empty: cancelled).
class SpellBook : public QDialog {
    Q_OBJECT
public:
    struct Entry {
        QString id;
        bool castable = true;
        QString why;  // when not castable
    };
    static QString chooseSpell(const core::ContentRegistry* content, const QList<Entry>& spells, const QString& title,
                               QWidget* parent);

private:
    SpellBook(const core::ContentRegistry* content, const QList<Entry>& spells, const QString& title, QWidget* parent);
    QString m_chosen;
};

// A round rune for a spell school (air, water, earth, nature, dark; others get a star).
QPixmap schoolRune(const QString& school, int size);

}  // namespace kot::client
