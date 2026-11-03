#include "client/GamePage.h"

#include <QMessageBox>

#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QActionGroup>
#include <QSettings>
#include <QToolTip>
#include <QCursor>
#include <QAction>
#include <QDialog>
#include <QShortcut>
#include <QStyle>
#include <QTimer>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "client/BattleResultDialog.h"
#include "client/BattleView.h"
#include "client/Art.h"
#include "client/MapView.h"
#include "client/MarketDialog.h"
#include "client/MovePlanner.h"
#include "client/Panels.h"
#include "client/Settings.h"
#include "client/SpellBook.h"
#include "client/Toast.h"
#include "client/HeroDialog.h"
#include "client/MiniMap.h"
#include "client/Sound.h"
#include "client/TownDialog.h"
#include "core/content/ContentRegistry.h"
#include "net/Protocol.h"

namespace kot::client {

GamePage::GamePage(net::LobbyClient* client, const core::ContentRegistry* content, QWidget* parent)
    : QWidget(parent), m_client(client), m_content(content) {
    auto* outer = new QVBoxLayout(this);
    auto* top = new QHBoxLayout();
    auto* menuButton = new QToolButton(this);
    menuButton->setObjectName("gameMenuButton");
    menuButton->setText(tr("\u2630 Game"));
    menuButton->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(menuButton);
    menu->setObjectName("gameMenu");
    menuButton->setMenu(menu);
    top->addWidget(menuButton);
    m_status = new QLabel(this);
    m_status->setObjectName("statusLabel");
    top->addWidget(m_status);
    m_resources = new ResourceBar(content, this);
    top->addWidget(m_resources, 1);
    outer->addLayout(top);

    auto* middle = new QHBoxLayout();
    m_centre = new QStackedWidget(this);
    m_map = new MapView(content, this);
    m_map->setObjectName("mapView");
    m_battleView = new BattleView(content, this);
    m_battleView->setObjectName("battleView");
    m_centre->addWidget(m_map);
    m_centre->addWidget(m_battleView);
    middle->addWidget(m_centre, 1);
    connect(m_battleView, &BattleView::actionRequested, this,
            [this](const QJsonObject& body) { send(net::msg::battleAction, body, tr("Battle")); });

    auto* side = new QVBoxLayout();
    m_miniMap = new MiniMap(content, this);
    m_miniMap->setObjectName("miniMap");
    side->addWidget(m_miniMap);
    m_players = new QLabel(this);
    m_players->setObjectName("playerColours");
    m_players->setWordWrap(true);
    m_players->setToolTip(tr("Each side's colour: rings on the map, flags, roofs and banners"));
    side->addWidget(m_players);
    m_heroCombo = new QComboBox(this);
    m_heroCombo->setObjectName("heroCombo");
    {
        // The hero list and a button that brings the map back to him (Home).
        auto* row = new QHBoxLayout();
        row->addWidget(m_heroCombo, 1);
        auto* home = new QToolButton(this);
        home->setObjectName("centreOnHeroButton");
        home->setText(tr("\u2316"));
        home->setToolTip(tr("Look at the selected hero (Home)"));
        home->setMinimumSize(30, 26);
        connect(home, &QToolButton::clicked, this, [this]() {
            if (const ViewHero* h = m_view.hero(m_selectedHero)) m_map->centerOn(h->pos);
        });
        row->addWidget(home);
        side->addLayout(row);
    }
    m_heroPanel = new HeroPanel(content, this);
    side->addWidget(m_heroPanel);
    m_heroInfo = new QLabel(this);
    m_heroInfo->setObjectName("heroInfo");
    m_heroInfo->setWordWrap(true);
    m_heroInfo->setTextFormat(Qt::RichText);
    m_heroInfo->setAlignment(Qt::AlignTop);
    side->addWidget(m_heroInfo);
    m_levelUp = new QPushButton(tr("Level up: choose a trait"), this);
    m_levelUp->setObjectName("levelUpButton");
    side->addWidget(m_levelUp);
    m_heroDialog = new HeroDialog(content, this);
    connect(m_heroPanel, &HeroPanel::clicked, this, [this]() {
        if (m_selectedHero) m_heroDialog->showHero(m_view, m_selectedHero);
    });
    connect(m_heroDialog, &HeroDialog::commandRequested, this,
            [this](const QString& type, const QJsonObject& body, const QString& what) { send(type, body, what); });
    m_castSpell = new QPushButton(tr("Cast spell..."), this);
    m_castSpell->setObjectName("castAdventureSpellButton");
    side->addWidget(m_castSpell);
    auto* townsLabel = new QLabel(tr("<b>My towns</b> (T: next town, O or double-click: open)"), this);
    townsLabel->setWordWrap(true);
    side->addWidget(townsLabel);
    m_townList = new QListWidget(this);
    m_townList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_townList->setTextElideMode(Qt::ElideRight);
    m_townList->setObjectName("townList");
    m_townList->setMaximumHeight(120);
    side->addWidget(m_townList);
    m_buildAndBuy = new QPushButton(tr("Build, then buy units"), this);
    m_buildAndBuy->setObjectName("buildAndBuyButton");
    m_buyAll = new QPushButton(tr("Buy all units"), this);
    m_buyAll->setObjectName("buyAllButton");
    m_sendUnits = new QPushButton(tr("Bring dwelling units home"), this);
    m_sendUnits->setObjectName("sendUnitsButton");
    m_save = menu->addAction(tr("Save game..."));
    m_save->setObjectName("saveAction");
    menu->addSeparator();
    QAction* zoomIn = menu->addAction(tr("Bigger map icons\t+"));
    QAction* zoomOut = menu->addAction(tr("Smaller map icons\t-"));
    connect(zoomIn, &QAction::triggered, this, [this]() { m_map->zoomStep(1); });
    connect(zoomOut, &QAction::triggered, this, [this]() { m_map->zoomStep(-1); });
    QMenu* sizes = menu->addMenu(tr("Map icon size"));
    for (const int step : art::kZoomSteps)
        connect(sizes->addAction(tr("%1 pixels").arg(step)), &QAction::triggered, this, [this, step]() { m_map->setTileSize(step); });
    QAction* gridAction = menu->addAction(tr("Show grid (big icons)"));
    gridAction->setCheckable(true);
    gridAction->setChecked(m_map->showGrid());
    connect(gridAction, &QAction::toggled, this, [this](bool on) { m_map->setShowGrid(on); });
    // How fast heroes walk on the map: yours and everybody else's (Instant: no walk).
    for (const bool own : {true, false}) {
        QMenu* speedMenu = menu->addMenu(own ? tr("Your heroes move") : tr("Other heroes move"));
        speedMenu->setObjectName(own ? "ownSpeedMenu" : "otherSpeedMenu");
        auto* group = new QActionGroup(speedMenu);
        using S = MapView::WalkSpeed;
        for (const auto& [speed, label] : {std::pair{S::Slow, tr("Slow")}, std::pair{S::Normal, tr("Normal")}, std::pair{S::Fast, tr("Fast")},
                                           std::pair{S::Instant, own ? tr("Instant") : tr("Instant (do not show)")}}) {
            QAction* a = speedMenu->addAction(label);
            a->setCheckable(true);
            a->setChecked(MapView::walkSpeed(own) == speed);
            group->addAction(a);
            connect(a, &QAction::triggered, this, [own, speed]() { MapView::setWalkSpeed(own, speed); });
        }
    }
    QAction* market = menu->addAction(tr("Market...\tM"));
    connect(market, &QAction::triggered, this, [this]() { openMarket(); });
    QAction* mute = menu->addAction(tr("Sound"));
    mute->setCheckable(true);
    mute->setChecked(!sound::muted());
    connect(mute, &QAction::toggled, this, [](bool on) { sound::setMuted(!on); });
    connect(menu, &QMenu::aboutToShow, this, [mute]() { mute->setChecked(!sound::muted()); });
    menu->addSeparator();
    QAction* where = menu->addAction(tr("Settings are saved in %1").arg(settingsFile()));
    where->setEnabled(false);
    connect(m_save, &QAction::triggered, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save game"), tr("Name (letters, digits, _ and -):"),
                                                   QLineEdit::Normal, tr("day%1").arg(m_view.day), &ok);
        if (ok && !name.isEmpty()) send(net::msg::saveGame, {{"name", name}}, tr("Save"));
    });
    m_endTurn = new QPushButton(tr("End turn"), this);
    m_endTurn->setObjectName("endTurnButton");
    m_buildAndBuy->setIcon(art::buttonIcon("build"));
    m_buyAll->setIcon(art::buttonIcon("buy"));
    m_sendUnits->setIcon(art::buttonIcon("wagon"));
    m_endTurn->setIcon(art::buttonIcon("end"));
    m_castSpell->setIcon(art::buttonIcon("spell"));
    for (QPushButton* b : {m_buildAndBuy, m_buyAll, m_sendUnits, m_endTurn, m_castSpell}) b->setIconSize(QSize(22, 22));
    side->addWidget(m_buildAndBuy);
    side->addWidget(m_buyAll);
    side->addWidget(m_sendUnits);
    side->addStretch(1);
    side->addWidget(m_endTurn);
    auto* sideHolder = new QWidget(this);
    sideHolder->setLayout(side);
    sideHolder->setFixedWidth(270);
    middle->addWidget(sideHolder);
    outer->addLayout(middle, 1);

    m_log = new QPlainTextEdit(this);
    m_log->setObjectName("logView");
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(300);
    m_log->setMaximumHeight(110);
    m_hoverInfo = new QLabel(this);
    m_hoverInfo->setObjectName("hoverInfo");
    m_hoverInfo->setMinimumHeight(22);
    outer->addWidget(m_hoverInfo);
    connect(m_map, &MapView::hoverInfo, m_hoverInfo, &QLabel::setText);
    outer->addWidget(m_log);
    // The map's zoom step is remembered.
    m_map->setTileSize(QSettings().value(QStringLiteral("map/tileSize"), art::kDefaultTile).toInt());
    connect(m_map, &MapView::zoomChanged, this, [](int size) { QSettings().setValue(QStringLiteral("map/tileSize"), size); });

    m_townDialog = new TownDialog(content, this);

    connect(m_map, &MapView::tileClicked, this, &GamePage::onTileClicked);
    connect(m_map, &MapView::tileShiftClicked, this, [this](core::Pos tile) {
        const ViewTown* t = m_view.townCovering(tile);
        if (t && t->mine) m_townDialog->showTown(m_view, t->id);
        else onTileClicked(tile);
    });
    connect(m_map, &MapView::tileDoubleClicked, this, [this](core::Pos tile) {
        const ViewHero* at = m_view.heroAt(tile);
        if (at && at->owner == m_view.you) return selectHero(at->id);
        if (!m_selectedHero) return;
        if (const ViewTown* t = m_view.townCovering(tile)) tile = t->pos;
        m_routes.insert(m_selectedHero, tile);
        m_map->setRoute(tile);
        goAlongRoute();
    });
    connect(m_map, &MapView::scrolled, this, [this]() { m_miniMap->setVisibleTiles(m_map->visibleTiles()); });
    connect(m_miniMap, &MiniMap::centerRequested, this, [this](core::Pos t) {
        m_map->centerOn(t);
        m_miniMap->setVisibleTiles(m_map->visibleTiles());
    });
    // Keyboard: E end turn, N next hero, C centre on the hero, T open the town.
    const auto shortcut = [this](const char* keys, auto fn) {
        auto* s = new QShortcut(QKeySequence(QString::fromLatin1(keys)), this);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    shortcut("E", [this]() { if (m_endTurn->isEnabled()) m_endTurn->click(); });
    shortcut("N", [this]() { selectNextHero(); });
    shortcut("Return", [this]() { goAlongRoute(); });
    shortcut("+", [this]() { m_map->zoomStep(1); });
    shortcut("=", [this]() { m_map->zoomStep(1); });
    shortcut("-", [this]() { m_map->zoomStep(-1); });
    shortcut("Enter", [this]() { goAlongRoute(); });
    shortcut("M", [this]() { openMarket(); });
    shortcut("H", [this]() {
        if (m_selectedHero) m_heroDialog->showHero(m_view, m_selectedHero);
    });
    shortcut("Home", [this]() {
        if (const ViewHero* h = m_view.hero(m_selectedHero)) m_map->centerOn(h->pos);
    });
    shortcut("C", [this]() {
        if (const ViewHero* h = m_view.hero(m_selectedHero)) m_map->centerOn(h->pos);
    });
    // T: the next of your towns (the map looks there), O: open it.
    shortcut("T", [this]() {
        const QList<const ViewTown*> towns = m_view.myTowns();
        if (towns.isEmpty()) return;
        int index = 0;
        for (int i = 0; i < towns.size(); ++i)
            if (towns[i]->id == m_selectedTown) index = (i + 1) % towns.size();
        selectTown(towns[index]->id);
    });
    shortcut("O", [this]() {
        const ViewTown* t = m_view.town(m_selectedTown);
        if (!t) {
            const ViewHero* h = m_view.hero(m_selectedHero);
            t = h ? m_view.townAt(h->pos) : nullptr;
        }
        if (!t && !m_view.myTowns().isEmpty()) t = m_view.myTowns().first();
        if (t && t->mine) m_townDialog->showTown(m_view, t->id);
    });
    m_endTurn->setToolTip(tr("End your turn (E)"));
    m_pulse = new QTimer(this);
    m_pulse->setInterval(650);
    connect(m_pulse, &QTimer::timeout, this, [this]() {
        m_pulseOn = !m_pulseOn;
        m_endTurn->setProperty("pulse", m_pulseOn);
        m_endTurn->style()->unpolish(m_endTurn);
        m_endTurn->style()->polish(m_endTurn);
    });
    connect(m_map, &MapView::tileRightClicked, this, &GamePage::onTileRightClicked);
    connect(m_heroCombo, &QComboBox::activated, this, [this](int i) { selectHero(m_heroCombo->itemData(i).toInt()); });
    connect(m_levelUp, &QPushButton::clicked, this, [this]() {
        if (m_selectedHero) m_heroDialog->showHero(m_view, m_selectedHero);  // picks are made there
    });
    connect(m_castSpell, &QPushButton::clicked, this, &GamePage::castAdventureSpell);
    connect(m_townList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) { selectTown(item->data(Qt::UserRole).toInt()); });
    connect(m_townList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        m_townDialog->showTown(m_view, item->data(Qt::UserRole).toInt());
    });
    connect(m_endTurn, &QPushButton::clicked, this, [this]() {
        // A level-up pick not made yet is easy to forget: ask first.
        QStringList waiting;
        for (const ViewHero* h : m_view.myHeroes())
            if (h->pendingTraitPicks > 0) waiting << h->name;
        if (!waiting.isEmpty()) {
            QMessageBox box(QMessageBox::Question, tr("Level up"),
                            tr("%1 can still choose a trait from the level-up. End the turn anyway?").arg(waiting.join(", ")),
                            QMessageBox::NoButton, this);
            QPushButton* choose = box.addButton(tr("Choose a trait"), QMessageBox::AcceptRole);
            box.addButton(tr("End turn"), QMessageBox::DestructiveRole);
            box.setDefaultButton(choose);
            box.exec();
            if (box.clickedButton() == choose) {
                for (const ViewHero* h : m_view.myHeroes())
                    if (h->pendingTraitPicks > 0) {
                        selectHero(h->id);
                        m_heroDialog->showHero(m_view, h->id);
                        break;
                    }
                return;
            }
        }
        send(net::msg::endTurn, {}, tr("End turn"));
    });
    connect(m_buildAndBuy, &QPushButton::clicked, this, [this]() {
        send(net::msg::buildThenBuy, {{"useTrade", true}}, tr("Build, then buy units"));
    });
    connect(m_buyAll, &QPushButton::clicked, this, [this]() { send(net::msg::buyAllUnits, {{"useTrade", true}}, tr("Buy all units")); });
    m_buyAll->setToolTip(tr("Buy as many units as possible in every town, trading spare resources if needed"));
    connect(m_sendUnits, &QPushButton::clicked, this, [this]() {
        send(net::msg::sendDwellingUnits, {}, tr("Send dwelling units to the closest town"));
    });
    connect(m_townDialog, &TownDialog::commandRequested, this,
            [this](const QString& type, const QJsonObject& body, const QString& what) { send(type, body, what); });

    connect(m_client, &net::LobbyClient::gameStateChanged, this, &GamePage::onViewChanged);
    connect(m_client, &net::LobbyClient::commandResult, this, [this](int replyTo, const QJsonObject& info) {
        const Pending p = m_pending.take(replyTo);
        const QString text = describeResult(p.type, info);
        // A found artifact: a note that fades by itself, saying whether it is worn.
        {
            const QJsonArray found = info["foundArtifacts"].toArray();
            const QJsonArray stowed = info["stowedArtifacts"].toArray();
            for (const QJsonValue& v : found) {
                const core::ArtifactDef* a = m_content->artifact(v.toString());
                if (!a) continue;
                const bool carried = stowed.contains(v);
                const QString where = carried ? tr("Put in the backpack: something is already worn there. Open the hero (H) to swap.")
                                              : tr("Worn at once.");
                Toast::show(this, art::artifactIcon(a->id, a->slot, 128),
                            tr("<b style='color:#ffd84a; font-size:13pt'>Found the %1!</b><br>%2<br><i>%3</i>")
                                .arg(a->name.toHtmlEscaped(), a->description.toHtmlEscaped(), where));
            }
            if (!found.isEmpty()) sound::play(sound::Effect::Bell);
        }
        using sound::Effect;
        if (p.type == net::msg::moveHero) sound::play(info["battleStarted"].toBool() ? Effect::Drums : Effect::Step);
        if (p.type == net::msg::moveHero && info.contains("joinOffer")) offerToJoin(info["joinOffer"].toObject());
        if ((p.type == net::msg::battleAction || p.type == net::msg::moveHero || p.type == net::msg::answerJoin) &&
            !info["battleOver"].toBool())
            m_battleView->playEvents(info["events"].toArray());
        else if (p.type == net::msg::endTurn) sound::play(Effect::Click);
        else if (p.type == net::msg::castSpell) sound::play(Effect::Spell);
        else if (p.type == net::msg::buyAllUnits || p.type == net::msg::recruit || p.type == net::msg::build ||
                 p.type == net::msg::buildThenBuy || p.type == net::msg::hireHero)
            sound::play(Effect::Coins);
        else if (p.type == net::msg::battleAction) {
            bool cast = false, strike = false;
            for (const QJsonValue& v : info["events"].toArray()) {
                const QString t = v.toObject()["type"].toString();
                cast = cast || t == "cast";
                strike = strike || t == "attack" || t == "shoot" || t == "retaliate";
            }
            if (cast) sound::play(Effect::Spell);
            else if (strike) sound::play(Effect::Clash);
        }
        log(QStringLiteral("%1: %2").arg(p.what.isEmpty() ? tr("Done") : p.what, text));
    });
    connect(m_client, &net::LobbyClient::errorReceived, this, [this](const QString&, const QString& text, int replyTo) {
        const Pending p = m_pending.take(replyTo);
        log(QStringLiteral("\u26A0 %1%2").arg(p.what.isEmpty() ? QString() : p.what + ": ", text));
    });

    onViewChanged();
}

QString GamePage::logText() const { return m_log->toPlainText(); }

void GamePage::log(const QString& text) { m_log->appendPlainText(text); }

QString GamePage::unitName(const QString& id) const {
    const core::UnitDef* u = m_content->unit(id);
    return u ? u->name : id;
}

void GamePage::send(const QString& type, const QJsonObject& body, const QString& what) {
    const int seq = m_client->sendCommand(type, body);
    if (seq == 0) {
        log(tr("Not connected."));
        return;
    }
    m_pending.insert(seq, {type, what});
}

void GamePage::onViewChanged() {
    GameView view;
    QString error;
    if (!parseGameView(m_client->view(), &view, &error)) {
        if (!m_client->view().isEmpty()) log(tr("Bad view from the server: %1").arg(error));
        return;
    }
    const bool wasMyTurn = m_view.isMyTurn() && m_view.you == view.you;
    const bool first = (m_view.you == 0);
    // Hot-seat: the game moved on to another player at this computer.
    // (Within a battle between two of them the field is the same for both: no curtain.)
    const bool handover = m_view.you != 0 && view.you != m_view.you && !(view.battle.present && m_view.battle.present);
    if (handover) {
        m_centered = false;
        m_selectedHero = 0;
        m_townDialog->hide();
        m_heroDialog->hide();
        if (m_market) m_market->hide();
        const ViewPlayer* next = view.player(view.you);
        showHandover(next ? next->name : tr("the next player"), next ? QColor(next->color) : QColor("#999999"));
    }
    rememberObjects(&view);
    m_view = view;
    if (!m_view.myHeroes().isEmpty()) {
        bool stillThere = false;
        for (const ViewHero* h : m_view.myHeroes()) stillThere = stillThere || h->id == m_selectedHero;
        if (!stillThere) m_selectedHero = m_view.myHeroes().first()->id;
    } else {
        m_selectedHero = 0;
    }
    m_map->setView(m_view);
    m_map->setSelectedHero(m_selectedHero);
    syncRoutes();
    m_miniMap->setView(m_view);
    m_miniMap->setVisibleTiles(m_map->visibleTiles());
    {
        // Who is who: a shield in each player's colour; you, your allies, whose turn.
        const ViewPlayer* me = m_view.player(m_view.you);
        QStringList parts;
        for (const ViewPlayer& pl : m_view.players) {
            QString name = pl.name.toHtmlEscaped();
            if (pl.id == m_view.you) name = tr("%1 (you)").arg(name);
            else if (me && pl.team && pl.team == me->team) name = tr("%1 (ally)").arg(name);
            if (pl.id == m_view.currentPlayer) name = QStringLiteral("<b>%1</b>").arg(name);
            if (pl.eliminated) name = QStringLiteral("<s>%1</s>").arg(name);
            parts << QStringLiteral("<span style='color:%1; font-size:14pt'>&#x25C6;</span>&nbsp;%2").arg(pl.color, name);
        }
        parts << QStringLiteral("<span style='color:#9a9a9a; font-size:14pt'>&#x25C7;</span>&nbsp;%1").arg(tr("neutral"));
        m_players->setText(parts.join(QStringLiteral(" &nbsp; ")));
    }
    if (!m_centered && m_selectedHero) {
        const ViewHero* h = m_view.hero(m_selectedHero);
        if (h) m_map->centerOn(h->pos);
        m_centered = true;
    }
    announceLevelUps(first);
    updateStatus();
    updateHeroPanel();
    m_townDialog->refresh(m_view);
    if (m_market && m_market->isVisible()) m_market->setView(m_view);
    m_heroDialog->refresh(m_view);
    if (m_view.isMyTurn() && !wasMyTurn && m_view.day > 1) {
        log(tr("Day %1: your turn.").arg(m_view.day));
        const int weekDay = (m_view.day - 1) % 7 + 1;
        m_map->showBanner(weekDay == 1 ? tr("Week %1").arg((m_view.day - 1) / 7 + 1) : tr("Day %1").arg(m_view.day),
                          weekDay == 1 && !m_view.weekEventName.isEmpty() ? m_view.weekEventName : tr("day %1 of the week").arg(weekDay));
    }
    if (first && !m_view.victory.isEmpty()) log(tr("Goal: %1.").arg(m_view.victory));
    // A new week brings its event.
    const int week = (m_view.day - 1) / 7 + 1;
    if (week != m_announcedWeek) {
        if (!first && week > m_announcedWeek && !m_view.weekEventName.isEmpty()) {
            const QString text = tr("Week %1: %2. %3").arg(week).arg(m_view.weekEventName, m_view.weekEventText);
            sound::play(sound::Effect::Bell);
            log(text);
            if (m_view.weekEventName != QLatin1String("A quiet week")) {
                auto* box = new QMessageBox(QMessageBox::Information, tr("A new week"), text, QMessageBox::Ok, this);
                box->setObjectName("weekEventBox");
                box->setAttribute(Qt::WA_DeleteOnClose);
                box->setModal(false);
                box->show();
            }
        }
        m_announcedWeek = week;
    }
    showBattle(first);
    announceEnd();
}

void GamePage::showBattle(bool firstView) {
    const auto colourOf = [this](int owner) {
        const ViewPlayer* p = m_view.player(owner);
        return p ? QColor(p->color) : QColor("#6e5a86");  // neutral monsters: a dusky violet
    };
    if (m_view.battle.present) {
        m_battleView->setBattle(m_view.battle, colourOf(m_view.battle.sideOwner[0]), colourOf(m_view.battle.sideOwner[1]));
        m_centre->setCurrentWidget(m_battleView);
    } else {
        if (m_centre->currentWidget() == m_battleView) m_battleView->battleEnded();
        m_centre->setCurrentWidget(m_map);
    }
    // The result of your newest battle (attacker or defender), once.
    if (m_view.lastBattleId > m_seenBattleId) {
        const bool fresh = !firstView;
        m_seenBattleId = m_view.lastBattleId;
        if (fresh) {
            const QString text = describeBattle(m_view.lastBattleReport);
            auto* box = new BattleResultDialog(m_content, m_view.lastBattleReport, m_view.you, this);
            sound::play(box->outcome() == BattleResultDialog::Outcome::Victory ? sound::Effect::Fanfare : sound::Effect::Defeat);
            log(tr("Battle result: %1").arg(text));
            box->show();
            box->raise();
        }
    }
}

void GamePage::offerToJoin(const QJsonObject& offer) {
    // Much weaker monsters would rather join than fight: the player decides.
    QStringList who;
    for (const QJsonValue& v : offer["units"].toArray()) {
        const QJsonObject u = v.toObject();
        who << QStringLiteral("%1 %2").arg(u["count"].toInt()).arg(unitName(u["unit"].toString()));
    }
    auto* box = new QDialog(this);
    box->setObjectName("joinOfferDialog");
    box->setWindowTitle(tr("They want to join you"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    auto* v = new QVBoxLayout(box);
    auto* row = new QHBoxLayout();
    for (const QJsonValue& u : offer["units"].toArray()) {
        const core::UnitDef* def = m_content->unit(u.toObject()["unit"].toString());
        auto* icon = new QLabel(box);
        icon->setPixmap(art::unitIcon(u.toObject()["unit"].toString(), def ? def->rank : 0, 72));
        row->addWidget(icon);
    }
    const int cost = offer["cost"].toInt();
    const int gold = m_view.resources.value(QStringLiteral("gold"));
    auto* text = new QLabel(tr("<b>%1</b> are impressed by your army<br>and offer to join you for <b>%2 gold</b>%3.")
                                .arg(who.join(", ")).arg(cost)
                                .arg(gold < cost ? tr(" <span style='color:#ff6a5a'>(you have %1)</span>").arg(gold) : QString()),
                            box);
    text->setTextFormat(Qt::RichText);
    row->addWidget(text, 1);
    v->addLayout(row);
    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    const auto answer = [this, box](const QString& a, const QString& what) {
        return [this, box, a, what]() {
            box->close();
            send(net::msg::answerJoin, {{"answer", a}}, what);
        };
    };
    auto* accept = new QPushButton(tr("Pay %1 gold").arg(cost), box);
    accept->setObjectName("joinAccept");
    accept->setEnabled(gold >= cost);
    accept->setDefault(gold >= cost);
    auto* fight = new QPushButton(tr("Fight"), box);
    fight->setObjectName("joinFight");
    auto* leave = new QPushButton(tr("Leave them"), box);
    for (QPushButton* b : {accept, fight, leave}) {
        b->setMinimumSize(120, 38);
        buttons->addWidget(b);
    }
    v->addLayout(buttons);
    connect(accept, &QPushButton::clicked, this, answer("accept", tr("They join you")));
    connect(fight, &QPushButton::clicked, this, answer("fight", tr("Attack them")));
    connect(leave, &QPushButton::clicked, this, answer("leave", tr("Leave them be")));
    box->show();
    box->raise();
}

void GamePage::announceLevelUps(bool firstView) {
    // A hero who just gained a level: a fanfare and a window that says so, with
    // the way to choose the trait right there.
    for (const ViewHero* h : m_view.myHeroes()) {
        const int before = m_knownPicks.value(h->id, h->pendingTraitPicks);
        m_knownPicks.insert(h->id, h->pendingTraitPicks);
        if (firstView || h->pendingTraitPicks <= before) continue;
        sound::play(sound::Effect::Fanfare);
        log(tr("%1 reached level %2: choose a trait!").arg(h->name).arg(h->level));
        const ViewPlayer* me = m_view.player(m_view.you);
        auto* box = new QDialog(this);
        box->setObjectName("levelUpDialog");
        box->setWindowTitle(tr("Level up"));
        box->setAttribute(Qt::WA_DeleteOnClose);
        auto* v = new QVBoxLayout(box);
        auto* row = new QHBoxLayout();
        auto* face = new QLabel(box);
        face->setPixmap(art::portrait(h->classId, me ? QColor(me->color) : QColor("#999999"), h->id, 96));
        row->addWidget(face);
        auto* text = new QLabel(tr("<span style='font-size:20pt; font-weight:bold; color:#ffd84a'>Level %1!</span><br>"
                                   "<b>%2</b> has grown stronger.<br>Choose which trait to improve (%3 to choose).")
                                    .arg(h->level).arg(h->name.toHtmlEscaped()).arg(h->pendingTraitPicks),
                                box);
        text->setTextFormat(Qt::RichText);
        row->addWidget(text, 1);
        v->addLayout(row);
        auto* buttons = new QHBoxLayout();
        buttons->addStretch(1);
        auto* later = new QPushButton(tr("Later"), box);
        auto* now = new QPushButton(tr("Choose now"), box);
        now->setObjectName("levelUpChooseNow");
        now->setDefault(true);
        for (QPushButton* b : {later, now}) {
            b->setMinimumSize(130, 38);
            buttons->addWidget(b);
        }
        v->addLayout(buttons);
        const int id = h->id;
        connect(later, &QPushButton::clicked, box, &QDialog::close);
        connect(now, &QPushButton::clicked, this, [this, box, id]() {
            box->close();
            selectHero(id);
            m_heroDialog->showHero(m_view, id);
        });
        box->show();
        box->raise();
    }
}

void GamePage::announceEnd() {
    const ViewPlayer* me = m_view.player(m_view.you);
    QString text;
    if (m_view.gameOver && !m_announcedGameOver) {
        m_announcedGameOver = true;
        m_announcedDefeat = true;
        text = (me && m_view.winnerTeam == me->team) ? tr("Victory! Your team has won the game.")
                                                     : tr("Game over. Your team has lost.");
        if (!m_view.campaign.isEmpty()) {
            const bool won = m_view.campaign["result"].toString() == "won";
            const int chapter = m_view.campaign["chapter"].toInt(), chapters = m_view.campaign["chapters"].toInt();
            text += QStringLiteral("\n\n") +
                    (won ? (chapter < chapters ? tr("Chapter %1 of %2 is won. Your best hero continues in the next chapter: "
                                                    "choose the campaign in the lobby again.").arg(chapter).arg(chapters)
                                               : tr("The campaign is won!"))
                         : tr("Choose the campaign in the lobby to try this chapter again."));
        }
    } else if (me && me->eliminated && !m_announcedDefeat) {
        m_announcedDefeat = true;
        text = tr("You have lost all your towns and heroes. You are out of the game.");
    }
    for (const ViewPlayer& p : m_view.players) {
        if (p.eliminated && !m_announcedOut.contains(p.id) && p.id != m_view.you) {
            m_announcedOut.insert(p.id);
            log(tr("%1 is out of the game.").arg(p.name));
        }
    }
    if (text.isEmpty()) return;
    sound::play(text.startsWith(tr("Victory")) ? sound::Effect::Fanfare : sound::Effect::Defeat);
    log(text);
    auto* box = new QMessageBox(QMessageBox::Information, tr("Game over"), text, QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setModal(false);
    box->show();
}

// Objects you saw stay on the map, as last seen, while their tile is out of sight.
// What is in sight is always the truth: something gone from there is forgotten.
void GamePage::rememberObjects(GameView* view) {
    const auto keep = [view](auto& memory, auto& now) {
        auto merged = now;
        for (const auto& old : memory) {
            bool present = false;
            for (const auto& n : now) present = present || n.id == old.id;
            if (!present && view->known(old.pos) && !view->inSight(old.pos)) merged.append(old);
        }
        now = merged;
        memory = merged;
    };
    Memory& m = m_memory[view->you];
    keep(m.towns, view->towns);
    keep(m.mines, view->mines);
    keep(m.dwellings, view->dwellings);
    keep(m.objects, view->objects);
}

void GamePage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_curtain) m_curtain->setGeometry(rect());
}

void GamePage::showHandover(const QString& name, const QColor& colour) {
    // A curtain over the whole game: the next player clicks when the previous one
    // has looked away.
    if (!m_curtain) {
        m_curtain = new QWidget(this);
        m_curtain->setObjectName("handoverCurtain");
        m_curtain->setAutoFillBackground(true);
        auto* v = new QVBoxLayout(m_curtain);
        v->addStretch(1);
        auto* label = new QLabel(m_curtain);
        label->setObjectName("handoverText");
        label->setAlignment(Qt::AlignCenter);
        label->setTextFormat(Qt::RichText);
        v->addWidget(label);
        auto* go = new QPushButton(m_curtain);
        go->setObjectName("handoverButton");
        go->setMinimumSize(260, 50);
        auto* row = new QHBoxLayout();
        row->addStretch(1);
        row->addWidget(go);
        row->addStretch(1);
        v->addLayout(row);
        v->addStretch(1);
        connect(go, &QPushButton::clicked, m_curtain, &QWidget::hide);
    }
    m_curtain->setStyleSheet(QStringLiteral("QWidget#handoverCurtain { background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                                            "stop:0 #1d1610, stop:1 %1); }").arg(colour.darker(250).name()));
    m_curtain->findChild<QLabel*>("handoverText")
        ->setText(tr("<span style='font-size:28pt; font-weight:bold; color:%1'>%2</span><br><br>"
                     "<span style='font-size:13pt'>It is your turn. The others look away, please.</span>")
                      .arg(colour.lighter(130).name(), name.toHtmlEscaped()));
    m_curtain->findChild<QPushButton*>("handoverButton")->setText(tr("I am %1: start my turn").arg(name));
    m_curtain->setGeometry(rect());
    m_curtain->show();
    m_curtain->raise();
}

void GamePage::openMarket() {
    if (!m_market) {
        m_market = new MarketDialog(m_content, this);
        connect(m_market, &MarketDialog::commandRequested, this,
                [this](const QString& type, const QJsonObject& body, const QString& what) { send(type, body, what); });
    }
    m_market->setView(m_view);
    m_market->show();
    m_market->raise();
}

void GamePage::selectTown(int id) {
    const ViewTown* t = m_view.town(id);
    if (!t) return;
    m_selectedTown = id;
    m_map->centerOn(t->pos);
    m_miniMap->setVisibleTiles(m_map->visibleTiles());
    for (int i = 0; i < m_townList->count(); ++i)
        if (m_townList->item(i)->data(Qt::UserRole).toInt() == id) m_townList->setCurrentRow(i);
}

void GamePage::selectNextHero() {
    const QList<const ViewHero*> heroes = m_view.myHeroes();
    if (heroes.isEmpty()) return;
    int index = 0;
    for (int i = 0; i < heroes.size(); ++i)
        if (heroes[i]->id == m_selectedHero) index = (i + 1) % heroes.size();
    selectHero(heroes[index]->id);
    m_map->centerOn(heroes[index]->pos);
}

void GamePage::updateStatus() {
    m_resources->setView(m_view);
    const ViewPlayer* current = m_view.player(m_view.currentPlayer);
    const QString campaign = m_view.campaign.isEmpty() ? QString()
        : tr(" &nbsp;&mdash;&nbsp; %1, chapter %2/%3").arg(m_view.campaign["name"].toString())
              .arg(m_view.campaign["chapter"].toInt()).arg(m_view.campaign["chapters"].toInt());
    // Where in the week we are: seven marks, today lit, and when the towns grow.
    const int weekDay = (m_view.day - 1) % 7 + 1;
    QString marks;
    for (int d = 1; d <= 7; ++d)
        marks += d < weekDay ? QStringLiteral("<span style='color:#8a7a5a'>&#x25CF;</span>")
                 : d == weekDay ? QStringLiteral("<span style='color:#ffd84a; font-size:13pt'>&#x25C6;</span>")
                                : QStringLiteral("<span style='color:#5a4a34'>&#x25CB;</span>");
    const int toGrowth = 8 - weekDay;
    const QString growth = toGrowth == 7 ? tr("new units today") : toGrowth == 1 ? tr("new units tomorrow") : tr("new units in %1 days").arg(toGrowth);
    m_status->setToolTip(tr("Day %1 of the week %2. Towns and dwellings get new units at the start of each week.<br>Goal: %3")
                             .arg(weekDay).arg((m_view.day - 1) / 7 + 1).arg(m_view.victory));
    m_status->setText(tr("<b>Day %1</b> &nbsp;Week %3: %6 <span style='color:#c8b890'>%7</span> &nbsp;(%4) &nbsp;&mdash;&nbsp; %2%5")
                          .arg(m_view.day)
                          .arg(m_view.isMyTurn() ? tr("<b>your turn</b>") : tr("waiting for %1").arg(current ? current->name : "?"))
                          .arg((m_view.day - 1) / 7 + 1)
                          .arg(m_view.difficulty.isEmpty() ? tr("normal") : m_view.difficulty)
                          .arg(campaign, marks, growth));
    const bool myTurn = m_view.isMyTurn();
    const bool free = myTurn && !m_view.gameOver && !m_view.battle.present && !m_view.battleInProgress;
    for (QPushButton* b : {m_endTurn, m_buildAndBuy, m_buyAll, m_sendUnits, m_castSpell}) b->setEnabled(free);
    // Build, then buy: only while some town of yours has not built today.
    bool canStillBuild = false;
    for (const ViewTown* t : m_view.myTowns()) canStillBuild = canStillBuild || t->lastBuildDay != m_view.day;
    m_buildAndBuy->setEnabled(free && canStillBuild);
    m_buildAndBuy->setToolTip(canStillBuild ? tr("Builds the best building in each town (trading if needed), then buys all units")
                                            : tr("Every town has built today; use Buy all units"));
    m_save->setEnabled(!m_view.battle.present && !m_view.battleInProgress && !m_view.gameOver);
    updateEndTurnHighlight();

    m_townList->clear();
    for (const ViewTown* t : m_view.myTowns()) {
        // A building you could build now that shuts out a rival: the town needs your choice.
        bool choice = false;
        for (const BuildOption& b : t->buildOptions) {
            const core::BuildingDef* def = m_content->building(b.id);
            if (!def || !b.why.isEmpty()) continue;
            bool rival = !def->excludes.isEmpty();
            for (const core::BuildingDef& other : m_content->buildings()) rival = rival || other.excludes.contains(def->id);
            choice = choice || rival;
        }
        const QString text = QStringLiteral("%1 (%2, %3)%4").arg(t->name).arg(t->pos.x).arg(t->pos.y)
                                 .arg(choice ? tr("  \u2605 choose a building") : QString());
        auto* item = new QListWidgetItem(text, m_townList);
        item->setData(Qt::UserRole, t->id);
        if (t->id == m_selectedTown) m_townList->setCurrentItem(item);
        if (choice) item->setToolTip(tr("Auto-build leaves choices to you: open the town and pick one"));
    }
}

void GamePage::updateHeroPanel() {
    m_heroCombo->blockSignals(true);
    m_heroCombo->clear();
    for (const ViewHero* h : m_view.myHeroes()) m_heroCombo->addItem(h->name, h->id);
    const int index = m_heroCombo->findData(m_selectedHero);
    if (index >= 0) m_heroCombo->setCurrentIndex(index);
    m_heroCombo->blockSignals(false);

    const ViewHero* h = m_view.hero(m_selectedHero);
    if (!h) {
        m_heroInfo->setText(tr("No hero."));
        m_heroPanel->setHero(m_view, 0);
        m_levelUp->setVisible(false);
        return;
    }
    m_heroPanel->setHero(m_view, h->id);
    QStringList lines;
    QStringList spellNames;
    for (const QString& id : h->spells) {
        const core::SpellDef* s = m_content->spell(id);
        spellNames << (s ? s->name : id);
    }
    lines << tr("Spells: %1").arg(spellNames.isEmpty() ? tr("none") : spellNames.join(", "));
    if (!h->artifacts.isEmpty()) {
        QStringList names;
        for (const QString& id : h->artifacts) {
            const core::ArtifactDef* a = m_content->artifact(id);
            names << (a ? QStringLiteral("%1 (%2)").arg(a->name, a->description) : id);
        }
        lines << tr("Artifacts: %1").arg(names.join(", "));
    }
    m_heroInfo->setText(lines.join("<br>"));
    bool adventureSpell = false;
    for (const QString& id : h->spells) {
        const core::SpellDef* s = m_content->spell(id);
        adventureSpell = adventureSpell || (s && s->scope == core::SpellDef::Scope::Adventure);
    }
    m_castSpell->setVisible(adventureSpell);
    m_levelUp->setVisible(h->pendingTraitPicks > 0);
    m_levelUp->setText(tr("\u2605 Level %1! Choose a trait (%2) \u2605").arg(h->level).arg(h->pendingTraitPicks));
}

void GamePage::castAdventureSpell() {
    const ViewHero* h = m_view.hero(m_selectedHero);
    if (!h) return;
    QList<SpellBook::Entry> entries;
    for (const QString& id : h->spells) {
        const core::SpellDef* s = m_content->spell(id);
        if (!s || s->scope != core::SpellDef::Scope::Adventure) continue;
        entries.append({id, h->spellPoints >= s->cost, tr("Needs %1 spell points").arg(s->cost)});
    }
    if (entries.isEmpty()) return;
    const QString chosen = SpellBook::chooseSpell(m_content, entries, tr("%1's spell book").arg(h->name), this);
    if (!chosen.isEmpty()) send(net::msg::castSpell, {{"hero", h->id}, {"spell", chosen}}, tr("Cast %1").arg(m_content->spell(chosen)->name));
}

void GamePage::selectHero(int id) {
    m_selectedHero = id;
    m_map->setSelectedHero(id);
    const auto r = m_routes.constFind(id);
    m_map->setRoute(r == m_routes.cend() ? std::nullopt : std::optional<core::Pos>(*r));
    updateHeroPanel();
    const ViewHero* h = m_view.hero(id);
    if (h) m_map->centerOn(h->pos);
}

void GamePage::onTileClicked(core::Pos tile) {
    const ViewHero* at = m_view.heroAt(tile);
    if (at && at->owner == m_view.you) {
        selectHero(at->id);
        return;
    }
    if (!m_selectedHero) return;
    if (const ViewTown* t = m_view.townCovering(tile)) tile = t->pos;  // walls lead to the gate
    // A click plots the route; double click or Enter walks it.
    m_routes.insert(m_selectedHero, tile);
    m_map->setRoute(tile);
}

void GamePage::goAlongRoute() {
    if (!m_selectedHero) return;
    const auto it = m_routes.constFind(m_selectedHero);
    if (it == m_routes.cend()) {
        log(tr("Click a tile to plot the route first."));
        return;
    }
    if (!m_view.isMyTurn()) {
        log(tr("It is not your turn."));
        return;
    }
    send(net::msg::moveHero, {{"hero", m_selectedHero}, {"x", it->x}, {"y", it->y}}, tr("Move"));
}

void GamePage::syncRoutes() {
    QSet<int> alive;
    for (const ViewHero* h : m_view.myHeroes()) {
        alive.insert(h->id);
        const std::optional<core::Pos> dest = h->destination;
        if (!m_serverRoutes.contains(h->id) || m_serverRoutes.value(h->id) != dest) {
            m_serverRoutes.insert(h->id, dest);
            if (dest) m_routes.insert(h->id, *dest);
            else m_routes.remove(h->id);
        }
        if (m_routes.contains(h->id) && m_routes.value(h->id) == h->pos) m_routes.remove(h->id);
    }
    for (auto it = m_routes.begin(); it != m_routes.end();) it = alive.contains(it.key()) ? std::next(it) : m_routes.erase(it);
    const auto r = m_routes.constFind(m_selectedHero);
    m_map->setRoute(r == m_routes.cend() ? std::nullopt : std::optional<core::Pos>(*r));
}

bool GamePage::anyHeroCanMove() const {
    for (const ViewHero* h : m_view.myHeroes()) {
        MovePlanner planner;
        planner.prepare(m_view, *m_content, h->id);
        if (!planner.reachableToday().isEmpty()) return true;
    }
    return false;
}

void GamePage::updateEndTurnHighlight() {
    // Nothing left to move: the End turn button glows and pulses.
    const bool ready = m_endTurn->isEnabled() && !anyHeroCanMove();
    m_endTurn->setText(ready ? tr("End turn  \u25B6") : tr("End turn"));
    m_endTurn->setToolTip(ready ? tr("No hero can move any more today: end your turn (E)") : tr("End your turn (E)"));
    if (ready != m_endTurn->property("ready").toBool()) {
        m_endTurn->setProperty("ready", ready);
        m_endTurn->setProperty("pulse", false);
        m_pulseOn = false;
        m_endTurn->style()->unpolish(m_endTurn);
        m_endTurn->style()->polish(m_endTurn);
    }
    if (ready && !m_pulse->isActive()) m_pulse->start();
    if (!ready) m_pulse->stop();
}

void GamePage::onTileRightClicked(core::Pos tile) {
    // Your town: open it. Anything else: say what it is right away.
    const ViewTown* t = m_view.townCovering(tile);
    if (t && t->mine) return m_townDialog->showTown(m_view, t->id);
    const QString text = m_map->describeTile(tile);
    if (!text.isEmpty()) QToolTip::showText(QCursor::pos(), text, m_map);
}

QString GamePage::describeBattle(const QJsonObject& b) const {
    const auto losses = [this](const QJsonObject& side) {
        QStringList parts;
        const QJsonObject l = side["losses"].toObject();
        for (auto it = l.begin(); it != l.end(); ++it) parts << QStringLiteral("%1 %2").arg(it.value().toInt()).arg(unitName(it.key()));
        return parts.isEmpty() ? tr("nothing") : parts.join(", ");
    };
    // Seen from your side (you may have attacked or defended).
    const bool attacker = b["attacker"].toObject()["owner"].toInt() == m_view.you;
    const QJsonObject me = b[attacker ? "attacker" : "defender"].toObject();
    const QJsonObject them = b[attacker ? "defender" : "attacker"].toObject();
    const QString outcome = b["outcome"].toString();
    const bool won = outcome == (attacker ? "attacker" : "defender");
    QString text = outcome == "draw" ? tr("battle undecided") : won ? tr("battle won") : tr("battle lost");
    if (!attacker) text = tr("you were attacked: ") + text;
    text += tr(" after %1 rounds (you lost %2; the enemy lost %3)")
                .arg(b["rounds"].toInt())
                .arg(losses(me), losses(them));
    if (won && b["experience"].toInt() > 0) text += tr(", %1 experience").arg(b["experience"].toInt());
    if (won && b["levelsGained"].toInt() > 0) text += tr(", level up!");
    if (b["loserHeroKilled"].toBool()) text += won ? tr(", the enemy hero fell") : tr(", your hero fell");
    if (b["retreated"].toBool())
        text += won ? tr(", the enemy fled")
                    : b.contains("retreatedTo") ? tr(", your hero escaped to %1").arg(b["retreatedTo"].toString()) : tr(", your hero escaped");
    if (b["surrendered"].toBool())
        text += won ? tr(", the enemy surrendered and paid %1 gold").arg(b["surrenderGold"].toInt())
                    : tr(", you surrendered and paid %1 gold").arg(b["surrenderGold"].toInt());
    return text;
}

QString GamePage::describeBattleEvents(const QJsonArray& events) const {
    QStringList lines;
    const auto name = [this](int id) {
        const ViewBattleUnit* u = m_view.battle.unit(id);
        if (!u) return tr("a stack");
        return QStringLiteral("%1%2").arg(u->side == m_view.battle.yourSide ? tr("your ") : tr("enemy "), unitName(u->unit));
    };
    for (const QJsonValue& v : events) {
        const QJsonObject e = v.toObject();
        const QString type = e["type"].toString();
        if (type == "attack" || type == "shoot" || type == "retaliate") {
            const QString verb = type == "attack" ? tr("attacks") : type == "shoot" ? tr("shoots") : tr("strikes back at");
            lines << tr("%1 %2 %3: %4 damage, %5 killed%6")
                         .arg(name(e["unit"].toInt()), verb, name(e["target"].toInt()))
                         .arg(e["damage"].toInt())
                         .arg(e["killed"].toInt())
                         .arg(e["lucky"].toBool() ? tr(" (lucky strike!)") : QString());
        } else if (type == "cast") {
            const core::SpellDef* s = m_content->spell(e["spell"].toString());
            const QString caster = e["unit"].toInt() > 0 ? name(e["unit"].toInt())
                                   : e["side"].toInt() == m_view.battle.yourSide ? tr("your hero") : tr("the enemy hero");
            lines << tr("%1 casts %2 on %3%4").arg(caster, s ? s->name : e["spell"].toString(), name(e["target"].toInt()),
                                                         e["damage"].toInt() > 0 ? tr(" (%1)").arg(e["damage"].toInt()) : QString());
        } else if (type == "move") {
            lines << tr("%1 moves").arg(name(e["unit"].toInt()));
        } else if (type == "morale") {
            lines << tr("%1 is inspired and acts again").arg(name(e["unit"].toInt()));
        } else if (type == "falter") {
            lines << tr("%1 falters (bad morale)").arg(name(e["unit"].toInt()));
        } else if (type == "wait") {
            lines << tr("%1 waits").arg(name(e["unit"].toInt()));
        } else if (type == "retreat") {
            lines << tr("you retreat");
        } else if (type == "defend") {
            lines << tr("%1 defends").arg(name(e["unit"].toInt()));
        } else if (type == "round") {
            lines << tr("round %1").arg(e["round"].toInt());
        }
    }
    return lines.join("; ");
}

QString GamePage::describeResult(const QString& type, const QJsonObject& info) const {
    if (type == net::msg::moveHero && info.contains("joinOffer")) return tr("monsters offer to join you");
    if (type == net::msg::answerJoin && info["joined"].toBool()) return tr("they joined your army");
    if (type == net::msg::answerJoin && info["left"].toBool()) return tr("you leave them be");
    if (type == net::msg::moveHero && info["joined"].toBool()) return tr("monsters joined");
    if (type == net::msg::moveHero && info["ambushed"].toBool() && info["battleStarted"].toBool())
        return tr("you came too close: they attack! (zone of control)");
    if (type == net::msg::moveHero && info["battleStarted"].toBool()) return tr("a battle begins!");
    if (type == net::msg::moveHero && info["landed"].toBool()) return tr("landed (that ends the day's movement)");
    if (type == net::msg::buyBoat) return tr("the hero boards the boat; click the water to sail, the shore to land");
    if (type == net::msg::moveHero) {
        QString text = info["arrived"].toBool() ? tr("arrived")
                       : info["ambushed"].toBool() ? tr("ambushed after %1 steps (zone of control)").arg(info["stepsTaken"].toInt())
                       : info["spotted"].toBool() ? tr("stopped after %1 steps: an enemy came into sight. Look before you go on (Enter continues)").arg(info["stepsTaken"].toInt())
                       : tr("moved %1 steps, out of movement; the route is kept for the next day").arg(info["stepsTaken"].toInt());
        if (info.contains("capturedMine")) text += tr(", captured a mine");
        if (info.contains("capturedTown")) text += tr(", captured a town");
        if (info.contains("capturedDwelling")) text += tr(", captured a dwelling");
        if (info.contains("foundResources")) {
            QStringList parts;
            const QJsonObject res = info["foundResources"].toObject();
            for (auto it = res.begin(); it != res.end(); ++it) parts << QStringLiteral("%1 %2").arg(it.value().toInt()).arg(it.key());
            text += tr(", found %1").arg(parts.join(", "));
        }
        for (const QJsonValue& v : info["foundArtifacts"].toArray()) {
            const core::ArtifactDef* a = m_content->artifact(v.toString());
            text += tr(", found the %1 (%2)!").arg(a ? a->name : v.toString(), a ? a->description : QString());
        }
        if (info.contains("learnedSpells")) {
            QStringList names;
            for (const QJsonValue& v : info["learnedSpells"].toArray()) {
                const core::SpellDef* s = m_content->spell(v.toString());
                names << (s ? s->name : v.toString());
            }
            text += tr(", learned %1").arg(names.join(", "));
        }
        for (const QJsonValue& v : info["battles"].toArray()) text += QStringLiteral("; ") + describeBattle(v.toObject());
        return text;
    }
    if (type == net::msg::endTurn) return tr("day %1").arg(info["day"].toInt());
    if (type == net::msg::battleAction) {
        QString text = describeBattleEvents(info["events"].toArray());
        if (info["battleOver"].toBool()) text += (text.isEmpty() ? "" : "; ") + tr("the battle is over");
        return text.isEmpty() ? tr("ok") : text;
    }
    if (type == net::msg::saveGame) return tr("saved as \"%1\" on the server").arg(info["name"].toString());
    if (type == net::msg::buyAllUnits) {
        return tr("bought %1 units").arg(info["unitsBought"].toInt());
    }
    if (type == net::msg::autoBuild || type == net::msg::buildThenBuy) {
        const QJsonObject build = type == net::msg::buildThenBuy ? info["build"].toObject() : info;
        QStringList parts;
        for (const QJsonValue& v : build["built"].toArray()) {
            const core::BuildingDef* b = m_content->building(v.toObject()["building"].toString());
            const ViewTown* t = m_view.town(v.toObject()["town"].toInt());
            parts << tr("built %1 in %2").arg(b ? b->name : "?", t ? t->name : "?");
        }
        if (parts.isEmpty()) parts << tr("nothing to build");
        const QJsonArray choices = build["needsChoice"].toArray();
        if (!choices.isEmpty()) parts << tr("%1 town(s) need a building choice from you").arg(choices.size());
        if (type == net::msg::buildThenBuy) parts << tr("bought %1 units").arg(info["buy"].toObject()["unitsBought"].toInt());
        return parts.join("; ");
    }
    if (type == net::msg::sendDwellingUnits) return tr("%1 units sent").arg(info["unitsSent"].toInt());
    return tr("ok");
}

}  // namespace kot::client
