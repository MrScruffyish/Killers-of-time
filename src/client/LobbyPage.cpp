#include "client/LobbyPage.h"

#include "client/Art.h"
#include "client/LoadDialog.h"
#include "core/Version.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <tuple>

#include "core/content/ContentRegistry.h"

namespace kot::client {

namespace {
bool isHostClient(const net::LobbyState& lobby, int clientId) {
    for (const net::LobbyMember& m : lobby.members)
        if (m.clientId == clientId) return m.isHost;
    return false;
}
}  // namespace

using net::SlotInfo;
using net::SlotKind;

namespace {
QIcon swatch(const QString& color) {
    QPixmap pm(16, 16);
    pm.fill(QColor(color));
    return QIcon(pm);
}
}  // namespace

LobbyPage::LobbyPage(net::LobbyClient* client, const core::ContentRegistry* content, QWidget* parent)
    : QWidget(parent), m_client(client), m_content(content) {
    auto* outer = new QVBoxLayout(this);
    outer->addWidget(new QLabel(tr("<h2>Game setup</h2>"), this));

    m_slotHolder = new QWidget(this);
    outer->addWidget(m_slotHolder);

    auto* mapRow = new QHBoxLayout();
    mapRow->addWidget(new QLabel(tr("Map"), this));
    m_scenario = new QComboBox(this);
    m_scenario->setObjectName("scenario");
    mapRow->addWidget(m_scenario);
    m_scenarioInfo = new QLabel(this);
    m_scenarioInfo->setWordWrap(true);
    mapRow->addWidget(m_scenarioInfo, 1);
    outer->addLayout(mapRow);
    connect(m_scenario, &QComboBox::activated, this, [this](int) {
        const QString choice = m_scenario->currentData().toString();
        if (choice.startsWith("campaign:")) m_client->setSettings({{"campaign", choice.mid(9)}});
        else m_client->setSettings({{"scenario", choice}, {"campaign", QString()}});
    });

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
    settings->addSpacing(20);
    settings->addWidget(new QLabel(tr("Type"), this));
    m_mapType = new QComboBox(this);
    m_mapType->setObjectName("mapType");
    m_mapType->addItem(tr("Continent"), QStringLiteral("continent"));
    m_mapType->addItem(tr("Islands (boats needed)"), QStringLiteral("islands"));
    settings->addWidget(m_mapType);
    connect(m_mapType, &QComboBox::activated, this, [this](int) {
        m_client->setSettings({{"mapType", m_mapType->currentData().toString()}});
    });
    settings->addSpacing(20);
    settings->addWidget(new QLabel(tr("Difficulty"), this));
    m_difficulty = new QComboBox(this);
    m_difficulty->setObjectName("difficulty");
    for (const auto& [id, name, tip] : {std::tuple{"easy", tr("Easy"), tr("More starting resources, weaker monsters, AI players get less")},
                                        std::tuple{"normal", tr("Normal"), tr("The standard game")},
                                        std::tuple{"hard", tr("Hard"), tr("Fewer starting resources, stronger monsters, AI players get more")},
                                        std::tuple{"expert", tr("Expert"), tr("Much harder in every way")}}) {
        m_difficulty->addItem(name, QString::fromLatin1(id));
        m_difficulty->setItemData(m_difficulty->count() - 1, tip, Qt::ToolTipRole);
    }
    settings->addWidget(m_difficulty);
    settings->addSpacing(20);
    m_fog = new QCheckBox(tr("Fog of war"), this);
    m_fog->setObjectName("fogOfWar");
    m_fog->setToolTip(tr("On: you only see what your heroes and towns see. Off: everyone sees the whole map"));
    settings->addWidget(m_fog);
    connect(m_fog, &QCheckBox::clicked, this, [this](bool on) { m_client->setSettings({{"fogOfWar", on}}); });
    settings->addSpacing(20);
    settings->addWidget(new QLabel(tr("Autosaves"), this));
    m_autosaves = new QSpinBox(this);
    m_autosaves->setObjectName("autosaves");
    m_autosaves->setRange(0, 100);
    m_autosaves->setToolTip(tr("A save at the start of every day; this many are kept (0 = none). "
                               "Campaign chapters also get a save when they start."));
    settings->addWidget(m_autosaves);
    connect(m_autosaves, &QSpinBox::editingFinished, this, [this]() {
        if (m_autosaves->value() != m_client->lobby().settings.autosaves) m_client->setSettings({{"autosaves", m_autosaves->value()}});
    });
    settings->addStretch(1);
    outer->addLayout(settings);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    outer->addWidget(m_status);
    m_versions = new QLabel(this);
    m_versions->setObjectName("versionLabel");
    outer->addWidget(m_versions);
    outer->addStretch(1);

    auto* buttons = new QHBoxLayout();
    auto* leave = new QPushButton(tr("Leave"), this);
    leave->setObjectName("leaveButton");
    m_load = new QPushButton(tr("Load game..."), this);
    m_load->setObjectName("loadButton");
    buttons->addWidget(m_load);
    m_start = new QPushButton(tr("Start game"), this);
    m_start->setObjectName("startButton");
    m_quick = new QPushButton(tr("Play against the computer"), this);
    m_quick->setObjectName("quickStartButton");
    m_quick->setToolTip(tr("Adds a computer opponent if there is none, then starts"));
    buttons->addWidget(leave);
    buttons->addStretch(1);
    buttons->addWidget(m_quick);
    buttons->addWidget(m_start);
    outer->addLayout(buttons);

    connect(leave, &QPushButton::clicked, this, &LobbyPage::leaveRequested);
    connect(m_start, &QPushButton::clicked, this, [this]() { m_client->startGame(); });
    connect(m_quick, &QPushButton::clicked, this, [this]() {
        // One human against the computer: the host needs no Ready, only an opponent.
        const net::LobbyState& lobby = m_client->lobby();
        bool opponent = false;
        for (const net::SlotInfo& s : lobby.slotList) opponent = opponent || (s.occupied() && s.clientId != m_client->clientId());
        if (!opponent)
            for (const net::SlotInfo& s : lobby.slotList)
                if (s.kind == net::SlotKind::Open) {
                    m_client->setSlot(s.index, {{"kind", "ai"}});
                    break;
                }
        m_client->startGame();
    });
    connect(m_load, &QPushButton::clicked, this, [this]() { m_listSavesSeq = m_client->listSaves(); });
    connect(m_client, &net::LobbyClient::commandResult, this, [this](int replyTo, const QJsonObject& info) {
        if (replyTo == 0 || replyTo != m_listSavesSeq) return;
        m_listSavesSeq = 0;
        const QJsonArray games = info["games"].toArray();
        if (games.isEmpty()) {
            QMessageBox::information(this, tr("Load game"), tr("There are no saved games on the server."));
            return;
        }
        LoadDialog dialog(games, this);
        if (dialog.exec() == QDialog::Accepted && !dialog.chosen().isEmpty()) m_client->loadGame(dialog.chosen());
    });
    // Only send what the user really changed (editingFinished also fires when focus just moves away).
    connect(m_width, &QSpinBox::editingFinished, this, [this]() {
        if (m_width->value() != m_client->lobby().settings.mapWidth) m_client->setSettings({{"mapWidth", m_width->value()}});
    });
    connect(m_height, &QSpinBox::editingFinished, this, [this]() {
        if (m_height->value() != m_client->lobby().settings.mapHeight) m_client->setSettings({{"mapHeight", m_height->value()}});
    });
    connect(m_difficulty, &QComboBox::activated, this, [this](int) {
        m_client->setSettings({{"difficulty", m_difficulty->currentData().toString()}});
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
    // Most games are one human against the computer: a host alone in a new lobby
    // gets one AI opponent (once; it can be changed or removed like any slot).
    {
        const net::LobbyState& lobby = m_client->lobby();
        const bool host = isHostClient(lobby, m_client->clientId());
        bool others = false;
        for (const net::SlotInfo& s : lobby.slotList) others = others || (s.occupied() && s.clientId != m_client->clientId());
        if (host && !m_defaultOpponentAdded && !lobby.started && lobby.members.size() == 1 && !others) {
            for (const net::SlotInfo& s : lobby.slotList)
                if (s.kind == net::SlotKind::Open) {
                    m_client->setSlot(s.index, {{"kind", "ai"}});
                    break;
                }
        }
        if (host) m_defaultOpponentAdded = true;
    }
    // Which builds are talking: the same commit on both sides is the safe case.
    const QString mine = QString::fromLatin1(core::versionText());
    const QString server = m_client->serverVersion();
    m_versions->setText(server.isEmpty() || server == mine
                            ? tr("<small>Version %1</small>").arg(mine)
                            : tr("<small><b>Client %1, server %2</b> - different builds; if something goes wrong, use the same build for both</small>")
                                  .arg(mine, server));
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
    m_difficulty->setCurrentIndex(std::max(0, m_difficulty->findData(lobby.settings.difficulty)));
    m_fog->setChecked(lobby.settings.fogOfWar);
    if (!m_autosaves->hasFocus()) {
        m_autosaves->blockSignals(true);
        m_autosaves->setValue(lobby.settings.autosaves);
        m_autosaves->blockSignals(false);
    }
    // The map: a random one or a scenario.
    if (m_scenario->count() != m_client->scenarios().size() + m_client->campaigns().size() + 1) {
        m_scenario->clear();
        m_scenario->addItem(tr("Random map"), QString());
        for (const QJsonValue& v : m_client->scenarios())
            m_scenario->addItem(v.toObject()["name"].toString(), v.toObject()["id"].toString());
        for (const QJsonValue& v : m_client->campaigns())
            m_scenario->addItem(tr("Campaign: %1").arg(v.toObject()["name"].toString()), "campaign:" + v.toObject()["id"].toString());
    }
    const QString chosen = lobby.settings.campaign.isEmpty() ? lobby.settings.scenario : "campaign:" + lobby.settings.campaign;
    m_scenario->setCurrentIndex(std::max(0, m_scenario->findData(chosen)));
    m_scenario->setEnabled(host);
    QString info = tr("A new random map with the size and seed below.");
    QString scenarioId = lobby.settings.scenario;
    QString campaignText;
    for (const QJsonValue& v : m_client->campaigns()) {
        const QJsonObject c = v.toObject();
        if (c["id"].toString() != lobby.settings.campaign) continue;
        const QJsonArray ids = c["chapterIds"].toArray();
        const int next = c["next"].toInt() < ids.size() ? c["next"].toInt() : 0;
        scenarioId = ids.at(next).toString();
        campaignText = tr("%1 Chapter %2 of %3: %4. ").arg(c["description"].toString()).arg(next + 1).arg(ids.size())
                           .arg(c["chapters"].toArray().at(next).toString());
    }
    for (const QJsonValue& v : m_client->scenarios()) {
        const QJsonObject o = v.toObject();
        if (o["id"].toString() != scenarioId) continue;
        QStringList races;
        for (const QJsonValue& r : o["races"].toArray()) races << r.toString();
        info = tr("%1 (%2x%3). For %4 players, in slot order: %5.")
                   .arg(o["description"].toString()).arg(o["width"].toInt()).arg(o["height"].toInt())
                   .arg(races.size()).arg(races.join(", "));
    }
    m_scenarioInfo->setText(campaignText + info);
    const bool random = scenarioId.isEmpty();
    m_mapType->setCurrentIndex(std::max(0, m_mapType->findData(lobby.settings.mapType)));
    for (QWidget* w : {static_cast<QWidget*>(m_width), static_cast<QWidget*>(m_height), static_cast<QWidget*>(m_seed),
                       static_cast<QWidget*>(m_mapType)})
        w->setEnabled(host && random);
    m_difficulty->setEnabled(host);
    m_autosaves->setEnabled(host);
    m_fog->setEnabled(host);
    m_start->setEnabled(host);
    m_quick->setVisible(host);
    m_load->setEnabled(host);
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
        const bool canEdit = s.occupied() && (host || mine) && !((s.kind == SlotKind::Ai || s.kind == SlotKind::HotSeat) && !host);
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
            kind->addItems({tr("Open"), tr("AI"), tr("Closed"), tr("Human on this computer")});
            kind->setItemData(kind->count() - 1, tr("Hot-seat: another player takes turns at this computer"), Qt::ToolTipRole);
            const int current = s.kind == SlotKind::Human ? 0
                              : s.kind == SlotKind::Open ? 0 : s.kind == SlotKind::Ai ? 1 : s.kind == SlotKind::Closed ? 2 : 3;
            kind->setCurrentIndex(current);
            const int offset = (s.kind == SlotKind::Human) ? 1 : 0;
            connect(kind, &QComboBox::activated, this, [this, slot, offset, kind](int index) {
                static const char* kinds[] = {"open", "ai", "closed", "hotseat"};
                const int k = index - offset;
                if (k >= 0 && k < 4) m_client->setSlot(slot, {{"kind", kinds[k]}});
            });
            whoLayout->addWidget(kind);
            if (s.kind == SlotKind::HotSeat) {  // the hot-seat player's name
                auto* name = new QLineEdit(s.name, who);
                name->setObjectName(QStringLiteral("hotseatName%1").arg(slot));
                name->setMaximumWidth(140);
                connect(name, &QLineEdit::editingFinished, this, [this, slot, name]() {
                    if (!name->text().trimmed().isEmpty()) m_client->setSlot(slot, {{"name", name->text().trimmed()}});
                });
                whoLayout->addWidget(name);
            }
        } else {
            QString text = s.kind == SlotKind::Human ? s.name + (mine ? tr(" (you)") : QString())
                         : s.kind == SlotKind::HotSeat ? tr("%1 (at the host's computer)").arg(s.name)
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
        race->setIconSize(QSize(24, 24));
        for (const QString& id : m_client->races()) {
            const core::RaceDef* def = m_content && m_content->races().contains(id) ? &m_content->race(id) : nullptr;
            // A face for the race: one of its typical units.
            static const QMap<QString, QString> face = {{"humans", "cavalier"}, {"sylvan", "elf_archer"}, {"undead", "skeleton"}};
            race->addItem(QIcon(art::unitIcon(face.value(id, id), 0, 32)), def ? def->name : id, id);
            if (def) race->setItemData(race->count() - 1, def->description, Qt::ToolTipRole);
        }
        race->setCurrentIndex(std::max(0, race->findData(s.race)));
        race->setEnabled(canEdit);
        connect(race, &QComboBox::activated, this, [this, slot, race](int) {
            m_client->setSlot(slot, {{"race", race->currentData().toString()}});
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

        if (s.kind == SlotKind::Human && isHostClient(m_client->lobby(), s.clientId)) {
            // The host starts the game when the others are ready: no box to tick.
            auto* host = new QLabel(tr("host"), m_slotHolder);
            host->setToolTip(tr("The host does not need to be ready: Start is enough"));
            m_grid->addWidget(host, row, 5);
        } else if (s.kind == SlotKind::Human) {
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
