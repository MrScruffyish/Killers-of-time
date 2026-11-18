#include "client/SaveGameDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace kot::client {

SaveGameDialog::SaveGameDialog(const QString& suggestedName, QWidget* parent) : QDialog(parent) {
    setObjectName("saveGameDialog");
    setWindowTitle(tr("Save game"));
    resize(540, 220);
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    auto* title = new QLabel(tr("<b>Save this game</b>"), this);
    title->setStyleSheet("font-size: 14pt;");
    layout->addWidget(title);
    auto* hint = new QLabel(tr("Name (letters, digits, _ and -):"), this);
    hint->setStyleSheet("font-size: 11pt;");
    layout->addWidget(hint);
    m_name = new QLineEdit(suggestedName, this);
    m_name->setObjectName("saveName");
    m_name->setMinimumHeight(40);
    m_name->setStyleSheet("font-size: 13pt; padding: 2px 8px;");
    m_name->selectAll();
    layout->addWidget(m_name);
    layout->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    for (QPushButton* b : buttons->findChildren<QPushButton*>()) b->setMinimumSize(120, 38);
    layout->addWidget(buttons);
    if (QPushButton* save = buttons->button(QDialogButtonBox::Save)) save->setDefault(true);
    m_name->setFocus();
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString SaveGameDialog::name() const { return m_name->text().trimmed(); }

}  // namespace kot::client
