#include "client/LoadDialog.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "client/Settings.h"
#include "core/Version.h"

namespace kot::client {

LoadDialog::LoadDialog(const QJsonArray& games, QWidget* parent) : QDialog(parent), m_games(games) {
    setObjectName("loadDialog");
    rememberGeometry(this, QStringLiteral("loadDialog"));
    setWindowTitle(tr("Load a saved game"));
    resize(860, 440);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Saved games on the server, newest first. This is version %1.").arg(core::kGameVersion), this));
    m_table = new QTableWidget(this);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setObjectName("savesTable");
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Saved"), tr("Day"), tr("Players"), tr("Map"), tr("Version"), tr("Can load?")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setRowCount(static_cast<int>(games.size()));
    for (int row = 0; row < games.size(); ++row) {
        const QJsonObject g = games[row].toObject();
        const QString status = g["status"].toString();
        const QDateTime when = QDateTime::fromString(g["savedAt"].toString(), Qt::ISODate);
        QStringList players;
        for (const QJsonValue& p : g["players"].toArray()) players << p.toString();
        const QString version = g["gameVersion"].toString().isEmpty() ? tr("older") : g["gameVersion"].toString();
        const QString can = status == "ok" ? tr("yes") : status == "converted" ? tr("yes, converted from an older format")
                                                                                : tr("no: ") + g["problems"].toArray().first().toString();
        const QStringList cells = {g["name"].toString(), when.isValid() ? when.toString("yyyy-MM-dd hh:mm") : QString(),
                                   QString::number(g["day"].toInt()), players.join(", "),
                                   QStringLiteral("%1 x %2").arg(g["mapWidth"].toInt()).arg(g["mapHeight"].toInt()), version, can};
        for (int c = 0; c < cells.size(); ++c) {
            auto* item = new QTableWidgetItem(cells[c]);
            if (status == "incompatible") item->setForeground(QColor("#8a7b63"));
            if (c == 6 && status == "converted") item->setForeground(QColor("#ffd27a"));
            QStringList problems;
            for (const QJsonValue& p : g["problems"].toArray()) problems << p.toString();
            if (!problems.isEmpty()) item->setToolTip(problems.join("\n"));
            m_table->setItem(row, c, item);
        }
    }
    m_table->resizeColumnsToContents();
    layout->addWidget(m_table, 1);
    m_details = new QLabel(this);
    m_details->setWordWrap(true);
    layout->addWidget(m_details);
    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    m_loadButton = new QPushButton(tr("Load"), this);
    m_loadButton->setObjectName("loadChosenButton");
    m_loadButton->setDefault(true);
    for (QPushButton* b : {cancel, m_loadButton}) {
        b->setMinimumSize(120, 36);
        buttons->addWidget(b);
    }
    layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_loadButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &LoadDialog::updateSelection);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this]() {
        if (m_loadButton->isEnabled()) accept();
    });
    // Start on the newest save that can be loaded.
    for (int row = 0; row < games.size(); ++row)
        if (games[row].toObject()["status"].toString() != "incompatible") {
            m_table->selectRow(row);
            break;
        }
    updateSelection();
}

void LoadDialog::updateSelection() {
    const int row = m_table->currentRow();
    const QJsonObject g = row >= 0 && row < m_games.size() ? m_games[row].toObject() : QJsonObject();
    const QString status = g["status"].toString();
    m_loadButton->setEnabled(!g.isEmpty() && status != "incompatible" && !m_table->selectedItems().isEmpty());
    m_chosen = m_loadButton->isEnabled() ? g["name"].toString() : QString();
    QStringList problems;
    for (const QJsonValue& p : g["problems"].toArray()) problems << p.toString();
    if (status == "incompatible")
        m_details->setText(tr("<b>This game cannot be loaded</b> by this version: %1").arg(problems.join("; ").toHtmlEscaped()));
    else if (status == "converted")
        m_details->setText(tr("Saved by an older version; it will be converted when loaded: %1").arg(problems.join("; ").toHtmlEscaped()));
    else
        m_details->clear();
}

}  // namespace kot::client
