#pragma once

#include <QDialog>
#include <QStringList>

#include "core/content/ContentIds.h"

namespace kot::core { class ContentRegistry; struct SpellDef; }

namespace kot::client {

// Choosing a spell: an open book of two parchment pages, one card per spell with
// the spell's illustration (a replaceable PNG from data/art/spell, or a school rune
// if that file is missing), the name, the cost and what it does. Non-castable
// spells are greyed out. chooseSpell() returns the chosen spell (invalid: cancelled).
class SpellBook : public QDialog {
    Q_OBJECT
public:
    struct Entry {
        core::SpellId id;
        bool castable = true;
        QString why;  // when not castable
    };
    static core::SpellId chooseSpell(const core::ContentRegistry* content, const QList<Entry>& spells, const QString& title,
                               QWidget* parent);
    // Whom a battle spell is cast on and how far it reaches (battle spells have no
    // range limit: any stack on the field).
    static QString targetText(const core::SpellDef& spell);
    // Public so a book can be shown non-modally and inspected (tests build one
    // directly; chooseSpell() is the usual modal entry point).
    SpellBook(const core::ContentRegistry* content, const QList<Entry>& spells, const QString& title, QWidget* parent);

private:
    core::SpellId m_chosen;
};

// A round rune for a spell school (air, water, earth, nature, dark; others get a star).
QPixmap schoolRune(const QString& school, int size);

}  // namespace kot::client
