#include "client/LobbyPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace kot::client {

using net::SlotInfo;
using net::SlotKind;

namespace {
QIcon swatch(const QString& color) {
    QPixmap pm(16, 16);
    pm.fill(QColor(color));
    return QIcon(pm);
}
}  // namespace

LobbyPage::LobbyPage(net::LobbyClient* client, QWidget* parent) : QWidget(parent), m_client(client) {
    auto* outer = new QVBoxLayout(this);
    outer->addWidget(new QLabel(tr("<h2>Game setup</h2>"), this));

    m_slotHolder = new QWidget(this);
    outer->addWidget(m_slotHolder);

    auto* settings = new QHBoxLayout();
    settings->addWidget(new QLabel(tr("Map size"), this));
    m_width = new QSpinBox(this);
    m_width->setRange(16, 256);
    m_width->setObjectName("mapWidth");
    m_height = new QSpinBox(this);
    m_height->setRange(16, 256);
    m_height->setObjectName("mapHeight");
    settings->addWidget(m_width);
    settings->addWidget(new QLabel(QStringLiteral("x"), this));
    settings->addWidget(m_height);
    settings->addSpacing(20);
    settings->addWidget(new QLabel(tr("Seed"), this));
    m_seed = new QLineEdit(this);
    m_seed->setObjectName("mapSeed");
    m_seed->setMaximumWidth(160);
    settings->addWidget(m_seed);
    settings->addStretch(1);
    outer->addLayout(settings);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    outer->addWidget(m_status);
    outer->addStretch(1);

    auto* buttons = new QHBoxLayout();
    auto* leave = new QPushButton(tr("Leave"), this);
    leave->setObjectName("leaveButton");
    m_start = new QPushButton(tr("Start game"), this);
    m_start->setObjectName("startButton");
    buttons->addWidget(leave);
    buttons->addStretch(1);
    buttons->addWidget(m_start);
    outer->addLayout(buttons);

    connect(leave, &QPushButton::clicked, this, &LobbyPage::leaveRequested);
    connect(m_start, &QPushButton::clicked, this, [this]() { m_client->startGame(); });
    // Only send what the user really changed (editingFinished also fires when focus just moves away).
    connect(m_width, &QSpinBox::editingFinished, this, [this]() {
        if (m_width->value() != m_client->lobby().settings.mapWidth) m_client->setSettings({{"mapWidth", m_width->value()}});
    });
    connect(m_height, &QSpinBox::editingFinished, this, [this]() {
        if (m_height->value() != m_client->lobby().settings.mapHeight) m_client->setSettings({{"mapHeight", m_height->value()}});
    });
    connect(m_seed, &QLineEdit::editingFinished, this, [this]() {
        if (!m_seed->text().isEmpty() && m_seed->text() != QString::number(m_client->lobby().settings.seed))
            m_client->setSettings({{"seed", m_seed->text()}});
    });
    connect(m_client, &net::LobbyClient::lobbyChanged, this, &LobbyPage::rebuild);
    connect(m_client, &net::LobbyClient::errorReceived, this,
            [this](const QString&, const QString& text, int) { showError(text); });
    rebuild();
}

void LobbyPage::showError(const QString& text) {
    m_status->setStyleSheet("color: #c00000;");
    m_status->setText(text);
}

void LobbyPage::rebuild() {
    const net::LobbyState& lobby = m_client->lobby();
    const bool host = m_client->isHost();
    const int me = m_client->clientId();

    // Settings (persistent widgets, only their values and enabled state change).
    for (QWidget* w : {static_cast<QWidget*>(m_width), static_cast<QWidget*>(m_height)}) w->blockSignals(true);
    m_width->setValue(lobby.settings.mapWidth);
    m_height->setValue(lobby.settings.mapHeight);
    m_width->blockSignals(false);
    m_height->blockSignals(false);
    if (!m_seed->hasFocus()) m_seed->setText(QString::number(lobby.settings.seed));
    for (QWidget* w : {static_cast<QWidget*>(m_width), static_cast<QWidget*>(m_height), static_cast<QWidget*>(m_seed)})
        w->setEnabled(host);
    m_start->setEnabled(host);
    m_start->setToolTip(host ? QString() : tr("Only the host can start the game"));

    // Slot table: rebuilt from scratch every time.
    delete m_slotHolder->layout();
    qDeleteAll(m_slotHolder->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly));
    m_grid = new QGridLayout(m_slotHolder);
    const QStringList headers = {tr("Slot"), tr("Player"), tr("Race"), tr("Team"), tr("Colour"), tr("Ready")};
    for (int c = 0; c < headers.size(); ++c) {
        auto* h = new QLabel(QStringLiteral("<b>%1</b>").arg(headers[c]), m_slotHolder);
        m_grid->addWidget(h, 0, c);
    }

    for (const SlotInfo& s : lobby.slotList) {
        const int row = s.index + 1;
        const bool mine = (s.kind == SlotKind::Human && s.clientId == me);
        const bool canEdit = s.occupied() && (host || mine) && !(s.kind == SlotKind::Ai && !host);
        const int slot = s.index;

        m_grid->addWidget(new QLabel(QString::number(slot + 1), m_slotHolder), row, 0);

        // Player column: who is in the slot, with host controls.
        auto* who = new QWidget(m_slotHolder);
        auto* whoLayout = new QHBoxLayout(who);
        whoLayout->setContentsMargins(0, 0, 0, 0);
        if (host) {
            auto* kind = new QComboBox(who);
            kind->setObjectName(QStringLiteral("kind%1").arg(slot));
            if (s.kind == SlotKind::Human) kind->addItem(tr("Human: %1").arg(s.name));
            kind->addItems({tr("Open"), tr("AI"), tr("Closed")});
            const int current = s.kind == SlotKind::Human ? 0
                              : s.kind == SlotKind::Open ? 0 : s.kind == SlotKind::Ai ? 1 : 2;
            kind->setCurrentIndex(current);
            const int offset = (s.kind == SlotKind::Human) ? 1 : 0;
            connect(kind, &QComboBox::activated, this, [this, slot, offset, kind](int index) {
                static const char* kinds[] = {"open", "ai", "closed"};
                const int k = index - offset;
                if (k >= 0 && k < 3) m_client->setSlot(slot, {{"kind", kinds[k]}});
            });
            whoLayout->addWidget(kind);
        } else {
            QString text = s.kind == SlotKind::Human ? s.name + (mine ? tr(" (you)") : QString())
                         : s.kind == SlotKind::Ai ? tr("AI: %1").arg(s.name)
                         : s.kind == SlotKind::Open ? tr("Open") : tr("Closed");
            whoLayout->addWidget(new QLabel(text, who));
        }
        if (s.kind == SlotKind::Ai && host) {
            auto* diff = new QComboBox(who);
            diff->addItems({"easy", "normal", "hard"});
            diff->setCurrentText(s.difficulty);
            connect(diff, &QComboBox::activated, this, [this, slot, diff](int) {
                m_client->setSlot(slot, {{"difficulty", diff->currentText()}});
            });
            whoLayout->addWidget(diff);
        }
        if (s.kind == SlotKind::Open) {
            auto* take = new QPushButton(tr("Take slot"), who);
            take->setObjectName(QStringLiteral("take%1").arg(slot));
            connect(take, &QPushButton::clicked, this, [this, slot]() { m_client->claimSlot(slot); });
            whoLayout->addWidget(take);
        }
        m_grid->addWidget(who, row, 1);

        if (!s.occupied()) continue;

        auto* race = new QComboBox(m_slotHolder);
        race->addItems(m_client->races());
        race->setCurrentText(s.race);
        race->setEnabled(canEdit);
        connect(race, &QComboBox::activated, this, [this, slot, race](int) {
            m_client->setSlot(slot, {{"race", race->currentText()}});
        });
        m_grid->addWidget(race, row, 2);

        auto* team = new QSpinBox(m_slotHolder);
        team->setRange(1, net::kMaxTeams);
        team->setValue(s.team);
        team->setEnabled(canEdit);
        connect(team, &QSpinBox::editingFinished, this, [this, slot, team]() {
            m_client->setSlot(slot, {{"team", team->value()}});
        });
        m_grid->addWidget(team, row, 3);

        auto* color = new QComboBox(m_slotHolder);
        const QStringList colors = net::defaultPlayerColors();
        for (const QString& c : colors) color->addItem(swatch(c), QString(), c);
        if (!colors.contains(s.color, Qt::CaseInsensitive)) color->addItem(swatch(s.color), QString(), s.color);
        color->setCurrentIndex(color->findData(s.color, Qt::UserRole, Qt::MatchFixedString));
        color->setEnabled(canEdit);
        connect(color, &QComboBox::activated, this, [this, slot, color](int i) {
            m_client->setSlot(slot, {{"color", color->itemData(i).toString()}});
        });
        m_grid->addWidget(color, row, 4);

        if (s.kind == SlotKind::Human) {
            auto* ready = new QCheckBox(m_slotHolder);
            ready->setObjectName(QStringLiteral("ready%1").arg(slot));
            ready->setChecked(s.ready);
            ready->setEnabled(mine);
            connect(ready, &QCheckBox::clicked, this, [this](bool on) { m_client->setReady(on); });
            m_grid->addWidget(ready, row, 5);
        } else {
            m_grid->addWidget(new QLabel(tr("always"), m_slotHolder), row, 5);
        }
    }
    m_grid->setColumnStretch(1, 1);
    m_status->clear();
}

}  // namespace kot::client
