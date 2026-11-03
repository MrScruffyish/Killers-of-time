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
#include <QShortcut>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "client/BattleView.h"
#include "client/MapView.h"
#include "client/HeroDialog.h"
#include "client/MiniMap.h"
#include "client/TownDialog.h"
#include "core/content/ContentRegistry.h"
#include "net/Protocol.h"

namespace kot::client {

GamePage::GamePage(net::LobbyClient* client, const core::ContentRegistry* content, QWidget* parent)
    : QWidget(parent), m_client(client), m_content(content) {
    auto* outer = new QVBoxLayout(this);
    m_status = new QLabel(this);
    m_status->setObjectName("statusLabel");
    outer->addWidget(m_status);

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
    m_heroCombo = new QComboBox(this);
    m_heroCombo->setObjectName("heroCombo");
    side->addWidget(m_heroCombo);
    m_heroInfo = new QLabel(this);
    m_heroInfo->setObjectName("heroInfo");
    m_heroInfo->setWordWrap(true);
    m_heroInfo->setTextFormat(Qt::RichText);
    m_heroInfo->setAlignment(Qt::AlignTop);
    side->addWidget(m_heroInfo);
    m_levelUp = new QPushButton(tr("Level up: choose a trait"), this);
    m_levelUp->setObjectName("levelUpButton");
    side->addWidget(m_levelUp);
    auto* heroButton = new QPushButton(tr("Hero screen (H)"), this);
    heroButton->setObjectName("heroButton");
    side->addWidget(heroButton);
    m_heroDialog = new HeroDialog(content, this);
    connect(heroButton, &QPushButton::clicked, this, [this]() {
        if (m_selectedHero) m_heroDialog->showHero(m_view, m_selectedHero);
    });
    connect(m_heroDialog, &HeroDialog::commandRequested, this,
            [this](const QString& type, const QJsonObject& body, const QString& what) { send(type, body, what); });
    m_castSpell = new QPushButton(tr("Cast spell..."), this);
    m_castSpell->setObjectName("castAdventureSpellButton");
    side->addWidget(m_castSpell);
    auto* townsLabel = new QLabel(tr("<b>My towns</b> (double-click here, or right-click the town on the map)"), this);
    townsLabel->setWordWrap(true);
    side->addWidget(townsLabel);
    m_townList = new QListWidget(this);
    m_townList->setObjectName("townList");
    m_townList->setMaximumHeight(120);
    side->addWidget(m_townList);
    m_buildAndBuy = new QPushButton(tr("Build, then buy units"), this);
    m_buildAndBuy->setObjectName("buildAndBuyButton");
    m_buyAll = new QPushButton(tr("Buy all units"), this);
    m_buyAll->setObjectName("buyAllButton");
    m_sendUnits = new QPushButton(tr("Send dwelling units to closest town"), this);
    m_sendUnits->setObjectName("sendUnitsButton");
    m_save = new QPushButton(tr("Save game..."), this);
    m_save->setObjectName("saveButton");
    side->addWidget(m_save);
    connect(m_save, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save game"), tr("Name (letters, digits, _ and -):"),
                                                   QLineEdit::Normal, tr("day%1").arg(m_view.day), &ok);
        if (ok && !name.isEmpty()) send(net::msg::saveGame, {{"name", name}}, tr("Save"));
    });
    m_endTurn = new QPushButton(tr("End turn"), this);
    m_endTurn->setObjectName("endTurnButton");
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
    outer->addWidget(m_log);

    m_townDialog = new TownDialog(content, this);

    connect(m_map, &MapView::tileClicked, this, &GamePage::onTileClicked);
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
    shortcut("H", [this]() {
        if (m_selectedHero) m_heroDialog->showHero(m_view, m_selectedHero);
    });
    shortcut("C", [this]() {
        if (const ViewHero* h = m_view.hero(m_selectedHero)) m_map->centerOn(h->pos);
    });
    shortcut("T", [this]() {
        const ViewHero* h = m_view.hero(m_selectedHero);
        const ViewTown* t = h ? m_view.townAt(h->pos) : nullptr;
        if (!t && !m_view.myTowns().isEmpty()) t = m_view.myTowns().first();
        if (t && t->mine) m_townDialog->showTown(m_view, t->id);
    });
    m_endTurn->setToolTip(tr("End your turn (E)"));
    connect(m_map, &MapView::tileRightClicked, this, &GamePage::onTileRightClicked);
    connect(m_heroCombo, &QComboBox::activated, this, [this](int i) { selectHero(m_heroCombo->itemData(i).toInt()); });
    connect(m_levelUp, &QPushButton::clicked, this, [this]() {
        if (m_selectedHero) m_heroDialog->showHero(m_view, m_selectedHero);  // picks are made there
    });
    connect(m_castSpell, &QPushButton::clicked, this, &GamePage::castAdventureSpell);
    connect(m_townList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        m_townDialog->showTown(m_view, item->data(Qt::UserRole).toInt());
    });
    connect(m_endTurn, &QPushButton::clicked, this, [this]() { send(net::msg::endTurn, {}, tr("End turn")); });
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
    m_miniMap->setView(m_view);
    m_miniMap->setVisibleTiles(m_map->visibleTiles());
    if (!m_centered && m_selectedHero) {
        const ViewHero* h = m_view.hero(m_selectedHero);
        if (h) m_map->centerOn(h->pos);
        m_centered = true;
    }
    updateStatus();
    updateHeroPanel();
    m_townDialog->refresh(m_view);
    m_heroDialog->refresh(m_view);
    if (m_view.isMyTurn() && !wasMyTurn && m_view.day > 1) log(tr("Day %1: your turn.").arg(m_view.day));
    if (first && !m_view.victory.isEmpty()) log(tr("Goal: %1.").arg(m_view.victory));
    // A new week brings its event.
    const int week = (m_view.day - 1) / 7 + 1;
    if (week != m_announcedWeek) {
        if (!first && week > m_announcedWeek && !m_view.weekEventName.isEmpty()) {
            const QString text = tr("Week %1: %2. %3").arg(week).arg(m_view.weekEventName, m_view.weekEventText);
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
        return p ? QColor(p->color) : QColor("#8b1a1a");  // neutral monsters
    };
    if (m_view.battle.present) {
        m_battleView->setBattle(m_view.battle, colourOf(m_view.battle.sideOwner[0]), colourOf(m_view.battle.sideOwner[1]));
        m_centre->setCurrentWidget(m_battleView);
    } else {
        m_centre->setCurrentWidget(m_map);
    }
    // The result of your newest battle (attacker or defender), once.
    if (m_view.lastBattleId > m_seenBattleId) {
        const bool fresh = !firstView;
        m_seenBattleId = m_view.lastBattleId;
        if (fresh) {
            const QString text = describeBattle(m_view.lastBattleReport);
            log(tr("Battle result: %1").arg(text));
            auto* box = new QMessageBox(QMessageBox::Information, tr("Battle"), text, QMessageBox::Ok, this);
            box->setObjectName("battleResultBox");
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->setModal(false);
            box->show();
        }
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
    if (view->you != m_memoryOwner) {
        m_memTowns.clear();
        m_memMines.clear();
        m_memDwellings.clear();
        m_memObjects.clear();
        m_memoryOwner = view->you;
    }
    keep(m_memTowns, view->towns);
    keep(m_memMines, view->mines);
    keep(m_memDwellings, view->dwellings);
    keep(m_memObjects, view->objects);
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
    QStringList res;
    for (const core::ResourceDef& r : m_content->resources()) {
        const int n = m_view.resources.value(r.id);
        if (n > 0 || r.isCurrency) res << QStringLiteral("%1 %2").arg(n).arg(r.id);
    }
    const ViewPlayer* current = m_view.player(m_view.currentPlayer);
    const QString campaign = m_view.campaign.isEmpty() ? QString()
        : tr(" &nbsp;&mdash;&nbsp; %1, chapter %2/%3").arg(m_view.campaign["name"].toString())
              .arg(m_view.campaign["chapter"].toInt()).arg(m_view.campaign["chapters"].toInt());
    m_status->setToolTip(tr("Goal: %1").arg(m_view.victory));
    m_status->setText(tr("<b>Day %1</b> (week %4, %5) &nbsp;&mdash;&nbsp; %2 &nbsp;&mdash;&nbsp; %3%6")
                          .arg(m_view.day)
                          .arg(m_view.isMyTurn() ? tr("<b>your turn</b>") : tr("waiting for %1").arg(current ? current->name : "?"))
                          .arg(res.join(", "))
                          .arg((m_view.day - 1) / 7 + 1)
                          .arg(m_view.difficulty.isEmpty() ? tr("normal") : m_view.difficulty)
                          .arg(campaign));
    const bool myTurn = m_view.isMyTurn();
    const bool free = myTurn && !m_view.gameOver && !m_view.battle.present && !m_view.battleInProgress;
    for (QPushButton* b : {m_endTurn, m_buildAndBuy, m_buyAll, m_sendUnits, m_castSpell}) b->setEnabled(free);
    m_save->setEnabled(!m_view.battle.present && !m_view.battleInProgress && !m_view.gameOver);

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
        m_levelUp->setVisible(false);
        return;
    }
    QStringList lines;
    lines << tr("<b>%1</b> - %2, level %3").arg(h->name, h->classId).arg(h->level);
    lines << tr("Movement: %1 &nbsp; Spell points: %2/%3")
                 .arg(h->movementPoints / 100.0, 0, 'f', 1)
                 .arg(h->spellPoints)
                 .arg(h->traits.value("spell_points_max"));
    lines << tr("Attack %1, Defense %2, Spell power %3, Magic %4")
                 .arg(h->traits.value("attack")).arg(h->traits.value("defense"))
                 .arg(h->traits.value("spell_power")).arg(h->traits.value("magic"));
    const ViewArmy* army = m_view.army(h->army);
    if (army) {
        for (const ViewStack& s : army->stacks) {
            QString line = QStringLiteral("%1 x %2").arg(s.count).arg(unitName(s.unit));
            if (s.stars > 0) line += QStringLiteral(" ") + QString(s.stars, QChar(0x2605));
            if (s.totalHp >= 0 && s.totalHp < s.count * s.maxHp) line += tr(" (wounded)");
            lines << line;
        }
        if (army->stacks.isEmpty()) lines << tr("No army.");
    }
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
    m_levelUp->setText(tr("Level up: choose a trait (%1)").arg(h->pendingTraitPicks));
}

void GamePage::castAdventureSpell() {
    const ViewHero* h = m_view.hero(m_selectedHero);
    if (!h) return;
    QStringList names, ids;
    for (const QString& id : h->spells) {
        const core::SpellDef* s = m_content->spell(id);
        if (!s || s->scope != core::SpellDef::Scope::Adventure) continue;
        names << tr("%1 (%2 SP) - %3").arg(s->name).arg(s->cost).arg(s->description);
        ids << id;
    }
    if (names.isEmpty()) return;
    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, tr("Cast spell"), tr("%1 casts:").arg(h->name), names, 0, false, &ok);
    if (ok) send(net::msg::castSpell, {{"hero", h->id}, {"spell", ids[names.indexOf(chosen)]}},
                 tr("Cast %1").arg(m_content->spell(ids[names.indexOf(chosen)])->name));
}

void GamePage::selectHero(int id) {
    m_selectedHero = id;
    m_map->setSelectedHero(id);
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
    if (!m_view.isMyTurn()) {
        log(tr("It is not your turn."));
        return;
    }
    send(net::msg::moveHero, {{"hero", m_selectedHero}, {"x", tile.x}, {"y", tile.y}}, tr("Move"));
}

void GamePage::onTileRightClicked(core::Pos tile) {
    const ViewTown* t = m_view.townAt(tile);
    if (t && t->mine) m_townDialog->showTown(m_view, t->id);
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
    if (b["retreated"].toBool()) text += attacker ? tr(", your hero escaped") : tr(", the enemy fled");
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
    if (type == net::msg::moveHero && info["battleStarted"].toBool()) return tr("a battle begins!");
    if (type == net::msg::moveHero) {
        QString text = info["arrived"].toBool() ? tr("arrived") : tr("moved %1 steps, out of movement").arg(info["stepsTaken"].toInt());
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
