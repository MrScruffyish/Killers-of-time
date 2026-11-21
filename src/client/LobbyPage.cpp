#include "client/LobbyPage.h"

#include "client/Art.h"
#include "client/LoadDialog.h"
#include "client/MapMakerDialog.h"
#include "core/Version.h"
#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>
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
#include <QShortcut>
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

int objectPercent(const net::LobbySettings& s, const QString& key) {
    if (key == QLatin1String("neutralTownPercent")) return s.neutralTownPercent;
    if (key == QLatin1String("mapDwellingPercent")) return s.mapDwellingPercent;
    if (key == QLatin1String("minePercent")) return s.minePercent;
    if (key == QLatin1String("wanderingMonsterPercent")) return s.wanderingMonsterPercent;
    return 100;
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
    outer->setContentsMargins(28, 18, 28, 18);  // room around the setup, not edge to edge
    outer->setSpacing(10);
    outer->addWidget(new QLabel(tr("<h2>Game setup</h2>"), this));

    m_modeHolder = new QWidget(this);
    m_modeHolder->setObjectName(QStringLiteral("gameMode"));
    auto* modeLayout = new QVBoxLayout(m_modeHolder);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->addWidget(new QLabel(tr("<b>Play</b>"), this));
    m_modeMap = new QPushButton(this);
    m_modeMap->setObjectName(QStringLiteral("gameModeMap"));
    m_modeMap->setCheckable(true);
    m_modeMap->setAutoExclusive(true);
    m_modeMap->setIcon(art::buttonIcon(QStringLiteral("town")));
    m_modeMap->setIconSize(QSize(32, 32));
    m_modeMap->setText(tr("<b>Play a map</b><br><small>Random or pre-built, with all the usual settings</small>"));
    m_modeCampaign = new QPushButton(this);
    m_modeCampaign->setObjectName(QStringLiteral("gameModeCampaign"));
    m_modeCampaign->setCheckable(true);
    m_modeCampaign->setAutoExclusive(true);
    m_modeCampaign->setIcon(art::buttonIcon(QStringLiteral("army")));
    m_modeCampaign->setIconSize(QSize(32, 32));
    m_modeCampaign->setText(tr("<b>Play a campaign</b><br><small>One human, AI opponents, chapters that carry your heroes over</small>"));
    for (QPushButton* card : {m_modeMap, m_modeCampaign}) {
        card->setMinimumHeight(54);
        card->setStyleSheet(QStringLiteral(
            "QPushButton#gameModeMap, QPushButton#gameModeCampaign { text-align: left; padding: 6px 12px; }"
            "QPushButton#gameModeMap:checked, QPushButton#gameModeCampaign:checked {"
            " background: #dce9fb; border: 2px solid #4a80d0; }"));
        modeLayout->addWidget(card);
    }
    outer->addWidget(m_modeHolder);
    m_modeMap->setChecked(true);
    connect(m_modeMap, &QPushButton::clicked, this, [this](bool on) {
        if (!on) return;
        m_client->setSettings({{"scenario", m_scenario->currentData().toString()}, {"campaign", QString()}});
    });
    connect(m_modeCampaign, &QPushButton::clicked, this, [this](bool on) {
        if (!on) return;
        const QString id = m_campaignChoice->currentData().toString();
        if (id == QLatin1String("__random__")) {  // generate one first, then play it
            makeMap(true);
            return;
        }
        if (!id.isEmpty()) m_client->setSettings({{"campaign", id}});
    });

    m_slotHolder = new QWidget(this);
    outer->addWidget(m_slotHolder);

    m_mapHolder = new QWidget(this);
    auto* mapRow = new QHBoxLayout(m_mapHolder);
    mapRow->addWidget(new QLabel(tr("Map"), this));
    m_scenario = new QComboBox(this);
    m_scenario->setObjectName("scenario");
    mapRow->addWidget(m_scenario);
    m_scenarioInfo = new QLabel(this);
    m_scenarioInfo->setWordWrap(true);
    mapRow->addWidget(m_scenarioInfo, 1);
    outer->addWidget(m_mapHolder);
    connect(m_scenario, &QComboBox::activated, this, [this](int) {
        m_client->setSettings({{"scenario", m_scenario->currentData().toString()}, {"campaign", QString()}});
    });

    m_campaignHolder = new QWidget(this);
    auto* campaignRow = new QHBoxLayout(m_campaignHolder);
    campaignRow->addWidget(new QLabel(tr("Campaign"), this));
    m_campaignChoice = new QComboBox(this);
    m_campaignChoice->setObjectName("campaignChoice");
    campaignRow->addWidget(m_campaignChoice);
    m_campaignNote = new QLabel(this);
    m_campaignNote->setWordWrap(true);
    campaignRow->addWidget(m_campaignNote, 1);
    outer->addWidget(m_campaignHolder);
    connect(m_campaignChoice, &QComboBox::activated, this, [this](int) {
        const QString id = m_campaignChoice->currentData().toString();
        if (id == QLatin1String("__random__")) {
            makeMap(true);
            return;
        }
        if (!id.isEmpty()) m_client->setSettings({{"campaign", id}});
    });

    m_setupRows = new QWidget(this);
    auto* setupRows = new QVBoxLayout(m_setupRows);
    setupRows->setContentsMargins(0, 0, 0, 0);

    auto* settings = new QHBoxLayout();
    settings->setObjectName(QStringLiteral("setupSettingsRow1"));
    settings->addWidget(new QLabel(tr("Map size"), this));
    // A few named sizes: a couple of tiles more or less makes no difference.
    m_mapSize = new QComboBox(this);
    m_mapSize->setObjectName("mapSize");
    for (const auto& [name, side] : {std::pair{tr("Small"), 40}, std::pair{tr("Medium"), 64}, std::pair{tr("Large"), 96},
                                     std::pair{tr("Extra large"), 128}, std::pair{tr("Huge"), 160}})
        m_mapSize->addItem(tr("%1 (%2x%2)").arg(name).arg(side), side);
    settings->addWidget(m_mapSize);
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
    // Two short rows fit a smaller window; one long row needed a full-width one.
    auto* settings2 = new QHBoxLayout();
    settings2->setObjectName(QStringLiteral("setupSettingsRow2"));
    settings2->addWidget(new QLabel(tr("Out when"), this));
    m_defeat = new QComboBox(this);
    m_defeat->setObjectName("defeat");
    for (const auto& [id, name, tip] : {std::tuple{"week", tr("No town for 7 days"), tr("Losing your last town gives you 7 days to take one back")},
                                        std::tuple{"town", tr("Last town lost"), tr("Losing your last town puts you out at once, heroes and all")},
                                        std::tuple{"all", tr("No towns, no heroes"), tr("Out only when you have neither towns nor heroes")}}) {
        m_defeat->addItem(name, QString::fromLatin1(id));
        m_defeat->setItemData(m_defeat->count() - 1, tip, Qt::ToolTipRole);
    }
    settings2->addWidget(m_defeat);
    connect(m_defeat, &QComboBox::activated, this, [this](int) {
        m_client->setSettings({{"defeat", m_defeat->currentData().toString()}});
    });
    settings2->addSpacing(20);
    m_hidden = new QCheckBox(tr("Hidden map"), this);
    m_hidden->setObjectName("hiddenMap");
    m_hidden->setToolTip(tr("On: unexplored land is black until you explore it. Off: the whole map (land, towns, mines) is known from the start"));
    settings2->addWidget(m_hidden);
    connect(m_hidden, &QCheckBox::clicked, this, [this](bool on) { m_client->setSettings({{"hiddenMap", on}}); });
    m_fog = new QCheckBox(tr("Fog of war"), this);
    m_fog->setObjectName("fogOfWar");
    m_fog->setToolTip(tr("On: explored land you cannot see now is shaded and its armies are hidden. Off: what you explored stays in full view"));
    settings2->addWidget(m_fog);
    connect(m_fog, &QCheckBox::clicked, this, [this](bool on) { m_client->setSettings({{"fogOfWar", on}}); });
    settings2->addSpacing(20);
    settings2->addWidget(new QLabel(tr("Autosaves"), this));
    m_autosaves = new QSpinBox(this);
    m_autosaves->setObjectName("autosaves");
    m_autosaves->setRange(0, 100);
    m_autosaves->setToolTip(tr("A save at the start of every day; this many are kept (0 = none). "
                               "Campaign chapters also get a save when they start."));
    settings2->addWidget(m_autosaves);
    connect(m_autosaves, &QSpinBox::editingFinished, this, [this]() {
        if (m_autosaves->value() != m_client->lobby().settings.autosaves) m_client->setSettings({{"autosaves", m_autosaves->value()}});
    });
    settings->addStretch(1);
    settings2->addStretch(1);
    setupRows->addLayout(settings);
    setupRows->addLayout(settings2);

    // How much of each kind of object a random map gets. Player capitals, their
    // starting mines and the guards stay as they are; only the shared map changes.
    auto* settings3 = new QHBoxLayout();
    settings3->setObjectName(QStringLiteral("setupSettingsRow3"));
    const auto addPercent = [&](const char* key, const QString& label, const QString& tip) {
        settings3->addWidget(new QLabel(label, this));
        auto* spin = new QSpinBox(this);
        spin->setObjectName(QString::fromLatin1(key));
        spin->setRange(0, 400);
        spin->setSuffix(tr("%"));
        spin->setValue(100);
        spin->setToolTip(tip);
        settings3->addWidget(spin);
        connect(spin, &QSpinBox::editingFinished, this, [this, spin, key]() {
            const QString k = QString::fromLatin1(key);
            if (spin->value() != objectPercent(m_client->lobby().settings, k)) m_client->setSettings({{k, spin->value()}});
        });
        return spin;
    };
    const QString percentTip = tr("100% is the usual amount for this map size; 0 = none, 200 = twice as many. "
                                  "Own towns and their starting mines are not affected.");
    m_neutralTowns = addPercent("neutralTownPercent", tr("Cities"), percentTip);
    m_mapDwellings = addPercent("mapDwellingPercent", tr("Dwellings"), percentTip);
    m_mines = addPercent("minePercent", tr("Mines"), percentTip);
    m_wanderingMonsters = addPercent("wanderingMonsterPercent", tr("Roving enemies"), percentTip);
    settings3->addStretch(1);
    setupRows->addLayout(settings3);
    outer->addWidget(m_setupRows);

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
    m_makeMap = new QPushButton(tr("Make a map..."), this);
    m_makeMap->setObjectName("makeMapButton");
    m_makeMap->setToolTip(tr("A random scenario or campaign with your settings"));
    buttons->addWidget(m_makeMap);
    buttons->addWidget(m_load);
    m_start = new QPushButton(tr("Start game"), this);
    m_start->setObjectName("startButton");
    m_start->setToolTip(tr("Starts the game (with a computer opponent if there is nobody to play against)"));
    m_start->setMinimumHeight(42);
    m_start->setStyleSheet("QPushButton#startButton { font-size: 13pt; font-weight: bold; padding: 6px 22px; }");
    buttons->addWidget(leave);
    buttons->addStretch(1);
    buttons->addWidget(m_start);
    outer->addLayout(buttons);

    connect(leave, &QPushButton::clicked, this, &LobbyPage::leaveRequested);
    // Ctrl+L lists the saves, the same as the Load game button.
    auto* loadKey = new QShortcut(QKeySequence(QStringLiteral("Ctrl+L")), this);
    loadKey->setObjectName(QStringLiteral("loadShortcut"));
    loadKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(loadKey, &QShortcut::activated, this, &LobbyPage::requestLoad);
    connect(m_start, &QPushButton::clicked, this, [this]() {
        // In a campaign the server has already filled the seats with AI. For a map,
        // one human against the computer needs at least one opponent.
        const net::LobbyState& lobby = m_client->lobby();
        if (lobby.settings.campaign.isEmpty()) {
            bool opponent = false;
            for (const net::SlotInfo& s : lobby.slotList)
                opponent = opponent || (s.occupied() && (s.clientId != m_client->clientId() || s.kind == net::SlotKind::HotSeat));
            if (!opponent)
                for (const net::SlotInfo& s : lobby.slotList)
                    if (s.kind == net::SlotKind::Open) {
                        m_client->setSlot(s.index, {{"kind", "ai"}});
                        break;
                    }
        }
        m_client->startGame();
    });
    connect(m_load, &QPushButton::clicked, this, &LobbyPage::requestLoad);
    connect(m_makeMap, &QPushButton::clicked, this, [this]() { makeMap(false); });
    connect(m_client, &net::LobbyClient::commandResult, this, [this](int replyTo, const QJsonObject& info) {
        if (replyTo == 0 || replyTo != m_makeSeq) return;
        m_makeSeq = 0;
        // Play it: choose the new scenario or campaign (and leave the other mode).
        if (info["kind"].toString() == QLatin1String("campaign"))
            m_client->setSettings({{"campaign", info["id"].toString()}});
        else
            m_client->setSettings({{"scenario", info["id"].toString()}, {"campaign", QString()}});
        m_status->setText(tr("Made \"%1\" and chose it.").arg(m_makeParams["name"].toString()));
    });
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
    connect(m_mapSize, &QComboBox::activated, this, [this](int) {
        const int side = m_mapSize->currentData().toInt();
        if (side > 0) m_client->setSettings({{"mapWidth", side}, {"mapHeight", side}});
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

void LobbyPage::requestLoad() { m_listSavesSeq = m_client->listSaves(); }

void LobbyPage::makeMap(bool campaignTab) {
    MapMakerDialog dialog(m_client->races(), this, campaignTab);
    if (dialog.exec() != QDialog::Accepted) return;
    m_makeParams = dialog.params();
    m_makeSeq = m_client->generateMap(m_makeParams);
    m_status->setStyleSheet(QString());
    m_status->setText(tr("Making the map..."));
}

void LobbyPage::showError(const QString& text) {
    m_status->setStyleSheet("color: #c00000;");
    m_status->setText(text);
}

void LobbyPage::setHotSeatSetup(bool on) {
    m_hotSeatSetup = on;
    m_defaultOpponentAdded = false;  // this is a new connection; set the lobby up once
}

void LobbyPage::rebuild() {
    // Most games are one human against the computer: a host alone in a new lobby
    // gets one AI opponent (once; it can be changed or removed like any slot).
    // A hot-seat game gets a second human at this computer instead.
    {
        const net::LobbyState& lobby = m_client->lobby();
        const bool host = isHostClient(lobby, m_client->clientId());
        bool others = false;
        for (const net::SlotInfo& s : lobby.slotList) others = others || (s.occupied() && s.clientId != m_client->clientId());
        if (host && lobby.settings.campaign.isEmpty() && !m_defaultOpponentAdded && !lobby.started && lobby.members.size() == 1
            && !others) {
            for (const net::SlotInfo& s : lobby.slotList)
                if (s.kind == net::SlotKind::Open) {
                    if (m_hotSeatSetup)
                        m_client->setSlot(s.index, {{"kind", "hotseat"},
                                                    {"name", tr("Player %1").arg(s.index + 1)}});
                    else
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
    {
        const QSignalBlocker block(m_mapSize);
        const int custom = m_mapSize->findData(0);
        if (custom >= 0) m_mapSize->removeItem(custom);
        const int at = lobby.settings.mapWidth == lobby.settings.mapHeight ? m_mapSize->findData(lobby.settings.mapWidth) : -1;
        if (at >= 0) {
            m_mapSize->setCurrentIndex(at);
        } else {  // set elsewhere (a saved setting, another client): shown as it is
            m_mapSize->addItem(tr("Custom (%1x%2)").arg(lobby.settings.mapWidth).arg(lobby.settings.mapHeight), 0);
            m_mapSize->setCurrentIndex(m_mapSize->count() - 1);
        }
    }
    if (!m_seed->hasFocus()) m_seed->setText(QString::number(lobby.settings.seed));
    m_difficulty->setCurrentIndex(std::max(0, m_difficulty->findData(lobby.settings.difficulty)));
    m_defeat->setCurrentIndex(std::max(0, m_defeat->findData(lobby.settings.defeat)));
    m_fog->setChecked(lobby.settings.fogOfWar);
    m_hidden->setChecked(lobby.settings.hiddenMap);
    if (!m_autosaves->hasFocus()) {
        m_autosaves->blockSignals(true);
        m_autosaves->setValue(lobby.settings.autosaves);
        m_autosaves->blockSignals(false);
    }
    for (QSpinBox* spin : {m_neutralTowns, m_mapDwellings, m_mines, m_wanderingMonsters}) {
        spin->blockSignals(true);
        spin->setValue(objectPercent(lobby.settings, spin->objectName()));
        spin->blockSignals(false);
    }
    // Map mode: a random map or a pre-built scenario. Campaign mode replaces all of this
    // with a single picker: the campaign decides the map, the seats and the rules.
    if (m_scenario->count() != m_client->scenarios().size() + 1) {
        m_scenario->clear();
        m_scenario->addItem(tr("Random map"), QString());
        for (const QJsonValue& v : m_client->scenarios())
            m_scenario->addItem(v.toObject()["name"].toString(), v.toObject()["id"].toString());
    }
    m_scenario->setCurrentIndex(std::max(0, m_scenario->findData(lobby.settings.scenario)));
    m_scenario->setEnabled(host);

    if (m_campaignChoice->count() != m_client->campaigns().size() + 1) {
        m_campaignChoice->clear();
        for (const QJsonValue& v : m_client->campaigns())
            m_campaignChoice->addItem(v.toObject()["name"].toString(), v.toObject()["id"].toString());
        m_campaignChoice->addItem(tr("Random campaign..."), QStringLiteral("__random__"));
    }
    {
        const QSignalBlocker block(m_campaignChoice);
        m_campaignChoice->setCurrentIndex(std::max(0, m_campaignChoice->findData(lobby.settings.campaign)));
    }
    m_campaignChoice->setEnabled(host);

    const bool campaign = !lobby.settings.campaign.isEmpty() && m_campaignAllowed;
    {
        const QSignalBlocker mapBlock(m_modeMap), campaignBlock(m_modeCampaign);
        m_modeMap->setChecked(!campaign);
        m_modeCampaign->setChecked(campaign);
    }
    m_modeMap->setEnabled(host && m_campaignAllowed);
    m_modeCampaign->setEnabled(host && m_campaignAllowed);
    m_modeHolder->setVisible(m_campaignAllowed);

    QString info = tr("A new random map with the size and seed below.");
    QString scenarioId = lobby.settings.scenario;
    QString campaignText;
    int chapterSeats = 0;
    for (const QJsonValue& v : m_client->campaigns()) {
        const QJsonObject c = v.toObject();
        if (c["id"].toString() != lobby.settings.campaign) continue;
        const QJsonArray ids = c["chapterIds"].toArray();
        const int next = c["next"].toInt() < ids.size() ? c["next"].toInt() : 0;
        scenarioId = ids.at(next).toString();
        campaignText = tr("%1 Chapter %2 of %3: %4. ").arg(c["description"].toString()).arg(next + 1).arg(ids.size())
                           .arg(c["chapters"].toArray().at(next).toString());
        const QJsonArray seats = c["seats"].toArray();
        if (next < seats.size()) chapterSeats = seats.at(next).toInt();
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
    m_scenarioInfo->setText(info);
    m_campaignNote->setText(campaignText + tr("You are the only human; the AI opponents are added for you."));
    // Progress is saved between chapters: re-ask the server to resize the seats if the
    // next chapter has a different number of players.
    if (campaign && host && !lobby.started && chapterSeats > 0 && lobby.slotList.size() != chapterSeats)
        m_client->setSettings({{"campaign", lobby.settings.campaign}});
    const bool random = scenarioId.isEmpty();
    m_mapType->setCurrentIndex(std::max(0, m_mapType->findData(lobby.settings.mapType)));
    for (QWidget* w : {static_cast<QWidget*>(m_mapSize), static_cast<QWidget*>(m_seed),
                       static_cast<QWidget*>(m_mapType), static_cast<QWidget*>(m_neutralTowns),
                       static_cast<QWidget*>(m_mapDwellings), static_cast<QWidget*>(m_mines),
                       static_cast<QWidget*>(m_wanderingMonsters)})
        w->setEnabled(host && random && !campaign);
    for (QWidget* w : {static_cast<QWidget*>(m_difficulty), static_cast<QWidget*>(m_defeat), static_cast<QWidget*>(m_autosaves),
                       static_cast<QWidget*>(m_fog), static_cast<QWidget*>(m_hidden)})
        w->setEnabled(host && !campaign);
    m_start->setEnabled(host);
    m_load->setEnabled(host && !campaign);
    m_makeMap->setEnabled(host && !campaign);
    m_start->setToolTip(host ? QString() : tr("Only the host can start the game"));

    // A campaign has no map or slot settings to show: the pickers, rules, slots and
    // map-generation button are all hidden. Only the campaign and Start remain.
    m_mapHolder->setVisible(!campaign);
    m_slotHolder->setVisible(!campaign);
    m_setupRows->setVisible(!campaign);
    m_campaignHolder->setVisible(campaign);
    m_makeMap->setVisible(!campaign);
    m_load->setVisible(!campaign);

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
            const core::RaceId rid = m_content ? m_content->raceId(id) : core::RaceId();
            const core::RaceDef* def = rid.isValid() ? &m_content->race(rid) : nullptr;
            // A face for the race: one of its typical units.
            static const QMap<QString, QString> face = {{"humans", "cavalier"}, {"sylvan", "elf_archer"}, {"undead", "skeleton"},
                                                         {"dwarves", "dwarf_warrior"}, {"orcs", "orc_warrior"},
                                                         {"ashborn", "imp"}, {"tidefolk", "triton"}, {"necroman", "bone_acolyte"}, {"giants", "young_giant"}, {"halflings", "halfling_sling"}};
            race->addItem(QIcon(art::unitIcon(face.value(id, id), 0, 32)), def ? def->name : id, id);
            if (def)
                race->setItemData(race->count() - 1,
                                  def->abilityText.isEmpty() ? def->description : QStringLiteral("%1\n%2").arg(def->description, def->abilityText),
                                  Qt::ToolTipRole);
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
