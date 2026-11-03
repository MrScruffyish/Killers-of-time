#include "client/TownDialog.h"

#include <QCheckBox>
#include <QSettings>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QSet>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

#include "client/Art.h"
#include "client/Settings.h"
#include "core/content/ContentRegistry.h"

namespace kot::client {

TownDialog::TownDialog(const core::ContentRegistry* content, QWidget* parent)
    : QDialog(parent), m_content(content) {
    setObjectName("townDialog");
    rememberGeometry(this, QStringLiteral("townDialog"));
    resize(900, 560);
    auto* layout = new QVBoxLayout(this);
    m_scene = new QLabel(this);
    m_scene->setObjectName("townScene");
    m_scene->setMinimumHeight(170);
    m_scene->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_scene);
    m_tabs = new QTabWidget(this);
    layout->addWidget(m_tabs, 1);
    auto* bottom = new QHBoxLayout();
    bottom->addStretch(1);
    auto* close = new QPushButton(tr("Close"), this);
    close->setObjectName("townCloseButton");
    close->setMinimumSize(140, 40);
    close->setShortcut(Qt::Key_Escape);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    bottom->addWidget(close);
    layout->addLayout(bottom);
    m_onlyBuildable = QSettings().value(QStringLiteral("town/onlyBuildable"), false).toBool();
    // G: the hero in the town takes all units of the garrison.
    auto* g = new QShortcut(QKeySequence(Qt::Key_G), this);
    connect(g, &QShortcut::activated, this, [this]() {
        if (auto* b = findChild<QPushButton*>("giveAllToHero"); b && b->isEnabled()) b->click();
    });
}

QString TownDialog::costText(const core::ResourceAmounts& cost) const {
    QStringList parts;
    for (auto it = cost.begin(); it != cost.end(); ++it) parts << QStringLiteral("%1 %2").arg(it.value()).arg(it.key());
    return parts.isEmpty() ? tr("free") : parts.join(", ");
}

QString TownDialog::unitName(const QString& id) const {
    const core::UnitDef* u = m_content->unit(id);
    return u ? u->name : id;
}

QString TownDialog::effectsText(const core::BuildingDef& b) const {
    using T = core::BuildingEffect::Type;
    QStringList parts;
    for (const core::BuildingEffect& e : b.effects) {
        switch (e.type) {
        case T::Dwelling: {
            const core::UnitDef* u = m_content->unit(e.unit);
            const QString unit = u ? u->name : e.unit;
            const QString stats = u ? tr(" (attack %1, defense %2, %3 HP, +%4/week)").arg(u->attack).arg(u->defense).arg(u->maxHp).arg(u->weeklyGrowth) : QString();
            const core::BuildingDef* old = b.upgradeOf.isEmpty() ? nullptr : m_content->building(b.upgradeOf);
            parts << (old ? tr("Upgrades the %1: recruit %2%3").arg(old->name, unit, stats) : tr("Recruit %1%2").arg(unit, stats));
            break;
        }
        case T::Income: parts << tr("+%1 %2 every day").arg(e.amount).arg(e.resource); break;
        case T::Tavern: parts << tr("Hire heroes here"); break;
        case T::MageGuild: parts << tr("Heroes visiting learn spells up to level %1").arg(e.amount); break;
        case T::Defense: parts << tr("Walls: the town's defenders get +%1 defense").arg(e.amount); break;
        case T::Market: parts << tr("Trade resources at %1% better rates").arg(e.amount); break;
        case T::Shipyard: parts << tr("Buy boats (coastal towns)"); break;
        }
    }
    return parts.isEmpty() ? tr("Needed for other buildings") : parts.join("; ");
}

void TownDialog::drawScene(const ViewTown& town) {
    // The town as a picture: one figure per building (the top of each upgrade
    // chain), the walls in front. It fills up as the town grows.
    using K = art::SceneBuilding::Kind;
    QSet<QString> replaced;
    for (const QString& id : town.buildings)
        if (const core::BuildingDef* b = m_content->building(id); b && !b->upgradeOf.isEmpty()) replaced.insert(b->upgradeOf);
    QList<art::SceneBuilding> scene;
    for (const QString& id : town.buildings) {
        const core::BuildingDef* b = m_content->building(id);
        if (!b || replaced.contains(id)) continue;
        art::SceneBuilding sb;
        for (const core::BuildingEffect& e : b->effects) {
            switch (e.type) {
            case core::BuildingEffect::Type::Dwelling:
                sb.kind = K::Dwelling;
                sb.unit = e.unit;
                if (const core::UnitDef* u = m_content->unit(e.unit)) sb.level = u->rank;
                break;
            case core::BuildingEffect::Type::MageGuild: sb.kind = K::MageGuild; sb.level = e.amount; break;
            case core::BuildingEffect::Type::Tavern: sb.kind = K::Tavern; break;
            case core::BuildingEffect::Type::Market: sb.kind = K::Market; break;
            case core::BuildingEffect::Type::Shipyard: sb.kind = K::Shipyard; break;
            case core::BuildingEffect::Type::Defense: sb.kind = K::Walls; break;
            case core::BuildingEffect::Type::Income:
                if (id == QLatin1String("village_hall")) sb.kind = K::Hall;
                break;
            }
        }
        scene.append(sb);
    }
    const ViewPlayer* owner = m_view.player(town.owner);
    const int w = std::max(600, width() - 30), h = 170;
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    art::drawTownScene(p, QRectF(0, 0, w, h), art::TownLook{owner ? QColor(owner->color) : QColor("#999999"), town.race, town.walls, town.size}, scene);
    p.end();
    m_scene->setPixmap(pm);
    m_scene->setToolTip(tr("%1: %2 buildings%3").arg(town.name).arg(town.buildings.size())
                            .arg(town.walls >= 2 ? tr(", a citadel") : town.walls == 1 ? tr(", stone walls") : tr(", a palisade")));
}

void TownDialog::showTown(const GameView& view, int townId) {
    m_townId = townId;
    m_tabs->setCurrentIndex(0);
    refresh(view);
    show();
    raise();
}

void TownDialog::refresh(const GameView& view) {
    m_view = view;
    if (isVisible() || m_townId) rebuild();
}

void TownDialog::rebuild() {
    const ViewTown* town = m_view.town(m_townId);
    if (!town || !town->mine) {
        hide();
        return;
    }
    const bool myTurn = m_view.isMyTurn();
    setWindowTitle(tr("%1 (%2)").arg(town->name, town->race));
    drawScene(*town);
    const int keep = m_tabs->currentIndex();
    while (m_tabs->count() > 0) {
        QWidget* old = m_tabs->widget(0);
        m_tabs->removeTab(0);
        delete old;
    }

    const auto send = [this](const QString& type, const QJsonObject& body, const QString& what) {
        emit commandRequested(type, body, what);
    };

    // ---- Buildings: what can be built now first, then what only needs a trade,
    // then what is blocked, then what stands already.
    {
        auto* page = new QWidget();
        auto* layout = new QVBoxLayout(page);
        auto* only = new QCheckBox(tr("Only show what I can build"), page);
        only->setObjectName("onlyBuildable");
        only->setChecked(m_onlyBuildable);
        connect(only, &QCheckBox::toggled, this, [this](bool on) {
            m_onlyBuildable = on;
            QSettings().setValue(QStringLiteral("town/onlyBuildable"), on);
            QMetaObject::invokeMethod(this, [this]() { rebuild(); }, Qt::QueuedConnection);
        });
        layout->addWidget(only);
        const auto name = [&](const QString& id) {
            const core::BuildingDef* b = m_content->building(id);
            return b ? b->name : id;
        };
        struct Row {
            QString id;
            QString status;
            int rank;  // 0 now, 1 after a trade, 2 blocked, 3 built
        };
        QList<Row> rows;
        // Left out: buildings that can never come here any more because a rival
        // one was built (excludes, either way round).
        const auto ruledOut = [&](const QString& id) {
            const core::BuildingDef* b = m_content->building(id);
            for (const QString& built : town->buildings) {
                const core::BuildingDef* other = m_content->building(built);
                if ((b && b->excludes.contains(built)) || (other && other->excludes.contains(id))) return true;
            }
            return false;
        };
        for (const BuildOption& o : town->buildOptions) {
            if (ruledOut(o.id)) continue;
            const int rank = o.why.isEmpty() ? 0 : o.why.startsWith("not enough") ? 1 : 2;
            rows.append({o.id, o.why, rank});
        }
        for (const QString& id : town->buildings) rows.append({id, tr("built"), 3});
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.rank < b.rank; });
        if (m_onlyBuildable) rows.erase(std::remove_if(rows.begin(), rows.end(), [](const Row& r) { return r.rank > 1; }), rows.end());

        auto* table = new QTableWidget(page);
        table->setObjectName("buildingTable");
        table->setColumnCount(5);
        table->setHorizontalHeaderLabels({tr("Building"), QString(), tr("Cost"), tr("What it does"), tr("Why not")});
        table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->setVisible(false);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->setWordWrap(true);
        table->setRowCount(static_cast<int>(rows.size()));
        const int tid = town->id;
        for (int row = 0; row < rows.size(); ++row) {
            const Row& r = rows[row];
            const core::BuildingDef* b = m_content->building(r.id);
            auto* nameItem = new QTableWidgetItem(name(r.id));
            if (r.rank == 0) nameItem->setForeground(QColor("#ffe27a"));
            if (r.rank >= 2) nameItem->setForeground(QColor("#9a8a70"));
            table->setItem(row, 0, nameItem);
            table->setItem(row, 2, new QTableWidgetItem(r.rank == 3 || !b ? QString() : costText(b->cost)));
            auto* what = new QTableWidgetItem(b ? effectsText(*b) : QString());
            what->setToolTip(what->text());
            table->setItem(row, 3, what);
            // Only blocked rows say why (a button already says it can be built).
            auto* why = new QTableWidgetItem(r.rank == 0 ? QString() : r.status);
            why->setToolTip(why->text());
            if (r.rank == 3) why->setForeground(QColor("#8fbf6a"));
            table->setItem(row, 4, why);
            if (r.rank <= 1) {
                const bool trade = r.rank == 1;
                const QString id = r.id;
                auto* button = new QPushButton(trade ? tr("Build (trade first)") : tr("Build"));
                button->setObjectName(QStringLiteral("build_%1").arg(id));
                button->setEnabled(myTurn);
                button->setMinimumHeight(30);
                connect(button, &QPushButton::clicked, this, [=, this]() {
                    send("build", {{"town", tid}, {"building", id}, {"useTrade", trade}}, tr("Build %1").arg(name(id)));
                });
                table->setCellWidget(row, 1, button);
            }
        }
        table->resizeColumnsToContents();
        table->setColumnWidth(3, std::min(table->columnWidth(3), 340));
        table->resizeRowsToContents();
        for (int row = 0; row < table->rowCount(); ++row) table->setRowHeight(row, std::max(table->rowHeight(row), 38));
        layout->addWidget(table);
        bool any = false;
        for (const Row& r : rows) any = any || r.rank <= 1;
        if (!any) {
            QString why = tr("Nothing can be built here right now.");
            for (const BuildOption& o : town->buildOptions)
                if (o.why.contains("today")) why = tr("Nothing more can be built here today (one building per day).");
            layout->addWidget(new QLabel(why, page));
        }
        m_tabs->addTab(page, art::buttonIcon("build"), tr("Buildings"));
    }

    // ---- Recruit (only when there is something to recruit)
    int recruitable = 0;
    for (const RecruitChoice& rc : town->recruitOptions) recruitable += rc.available;
    if (recruitable > 0) {
        auto* page = new QWidget();
        auto* layout = new QVBoxLayout(page);
        const ViewArmy* garrison = m_view.army(town->garrison);
        QStringList stacks;
        if (garrison)
            for (const ViewStack& s : garrison->stacks) stacks << QStringLiteral("%1 x %2").arg(s.count).arg(unitName(s.unit));
        layout->addWidget(new QLabel(tr("Garrison: %1").arg(stacks.isEmpty() ? tr("empty") : stacks.join(", ")), page));

        auto* table = new QTableWidget(page);
        table->setObjectName("recruitTable");
        table->setIconSize(QSize(40, 40));
        table->verticalHeader()->setVisible(false);
        table->verticalHeader()->setDefaultSectionSize(48);
        table->setColumnCount(5);
        table->setHorizontalHeaderLabels({tr("Unit"), tr("Available"), tr("Cost each"), tr("Count"), QString()});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setRowCount(static_cast<int>(town->recruitOptions.size()));
        int row = 0;
        for (const RecruitChoice& rc : town->recruitOptions) {
            const core::UnitDef* u = m_content->unit(rc.unit);
            auto* unitItem = new QTableWidgetItem(QIcon(art::unitIcon(rc.unit, u ? u->rank : 0, 40)), unitName(rc.unit));
            if (u) unitItem->setToolTip(tr("Attack %1, Defense %2, Damage %3-%4, %5 HP, Speed %6%7")
                                            .arg(u->attack).arg(u->defense).arg(u->damageMin).arg(u->damageMax).arg(u->maxHp).arg(u->battleSpeed)
                                            .arg(u->range > 0 ? tr(", shoots (range %1)").arg(u->range) : QString()));
            table->setItem(row, 0, unitItem);
            table->setItem(row, 1, new QTableWidgetItem(QString::number(rc.available)));
            table->setItem(row, 2, new QTableWidgetItem(u ? costText(u->cost) : QString()));
            auto* count = new QSpinBox();
            count->setRange(0, rc.available);
            count->setValue(rc.available);
            table->setCellWidget(row, 3, count);
            auto* button = new QPushButton(tr("Recruit"));
            button->setObjectName(QStringLiteral("recruit_%1").arg(rc.unit));
            button->setEnabled(myTurn && rc.available > 0);
            const QString unit = rc.unit;
            const int tid = town->id;
            connect(button, &QPushButton::clicked, this, [=, this]() {
                if (count->value() > 0)
                    send("recruit", {{"town", tid}, {"unit", unit}, {"count", count->value()}},
                         tr("Recruit %1 x %2").arg(count->value()).arg(unitName(unit)));
            });
            table->setCellWidget(row, 4, button);
            ++row;
        }
        table->resizeColumnsToContents();
        layout->addWidget(table, 1);
        auto* buyAll = new QPushButton(tr("Buy all units here (trading spare resources if needed)"), page);
        buyAll->setObjectName("buyAllHere");
        buyAll->setEnabled(myTurn);
        const int tid = town->id;
        connect(buyAll, &QPushButton::clicked, this, [=, this]() {
            send("buy_all_units", {{"town", tid}, {"useTrade", true}}, tr("Buy all units in %1").arg(town->name));
        });
        layout->addWidget(buyAll);
        // A hero in the town takes the whole garrison along (G).
        const ViewHero* visitor = nullptr;
        for (const ViewHero* h : m_view.myHeroes())
            if (h->pos == town->pos) visitor = h;
        if (visitor && garrison) {
            auto* give = new QPushButton(tr("Give all units to %1  (G)").arg(visitor->name), page);
            give->setObjectName("giveAllToHero");
            give->setEnabled(myTurn && !garrison->stacks.isEmpty());
            const int fromId = garrison->id, toId = visitor->army;
            const QString name = visitor->name;
            connect(give, &QPushButton::clicked, this, [=, this]() {
                send("move_units", {{"from", fromId}, {"to", toId}, {"all", true}}, tr("Give the garrison to %1").arg(name));
            });
            layout->addWidget(give);
        }
        m_tabs->addTab(page, art::buttonIcon("army"), tr("Recruit"));
    }

    // ---- Tavern
    {
        auto* page = new QWidget();
        auto* layout = new QVBoxLayout(page);
        bool hasTavern = false;
        for (const QString& id : town->buildings) {
            const core::BuildingDef* b = m_content->building(id);
            if (!b) continue;
            for (const core::BuildingEffect& e : b->effects)
                hasTavern = hasTavern || (e.type == core::BuildingEffect::Type::Tavern);
        }
        if (!hasTavern) {
            layout->addWidget(new QLabel(tr("Build a Tavern to hire heroes."), page));
        } else if (town->tavernOffers.isEmpty()) {
            layout->addWidget(new QLabel(tr("No heroes are offered right now. New ones arrive every week."), page));
        }
        const int cost = m_content->balance().economy.heroCost;
        const int gold = m_view.resources.value(m_content->currency() ? m_content->currency()->id : QString());
        for (const QString& offer : town->tavernOffers) {
            const core::HeroTemplateDef* t = m_content->heroTemplate(offer);
            auto* row = new QHBoxLayout();
            auto* face = new QLabel(page);
            const ViewPlayer* me = m_view.player(m_view.you);
            if (t) face->setPixmap(art::portrait(t->classId, me ? QColor(me->color) : QColor("#999999"), int(qHash(offer) % 6), 56));
            row->addWidget(face);
            const core::HeroClassDef* cls = t ? m_content->heroClass(t->classId) : nullptr;
            row->addWidget(new QLabel(t ? tr("<b>%1</b><br>%2").arg(t->name, cls ? cls->name : t->classId) : offer, page), 1);
            auto* hire = new QPushButton(gold >= cost ? tr("Hire for %1").arg(cost) : tr("Hire for %1 (trade first)").arg(cost), page);
            hire->setObjectName(QStringLiteral("hire_%1").arg(offer));
            hire->setEnabled(myTurn);
            const int tid = town->id;
            const bool trade = gold < cost;
            connect(hire, &QPushButton::clicked, this, [=, this]() {
                send("hire_hero", {{"town", tid}, {"hero", offer}, {"useTrade", trade}}, tr("Hire %1").arg(t ? t->name : offer));
            });
            row->addWidget(hire);
            layout->addLayout(row);
        }
        layout->addStretch(1);
        m_tabs->addTab(page, art::buttonIcon("tavern"), tr("Tavern"));
    }

    // ---- Armies: the garrison and a hero visiting the town
    {
        auto* page = new QWidget();
        page->setObjectName("armiesPage");
        auto* layout = new QVBoxLayout(page);
        const ViewArmy* garrison = m_view.army(town->garrison);
        const ViewHero* visitor = nullptr;
        for (const ViewHero* h : m_view.myHeroes())
            if (h->pos == town->pos) visitor = h;
        const ViewArmy* heroArmy = visitor ? m_view.army(visitor->army) : nullptr;
        const auto section = [&](const QString& title, const ViewArmy* from, const ViewArmy* to, const QString& toName) {
            layout->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(title), page));
            if (!from || from->stacks.isEmpty()) {
                layout->addWidget(new QLabel(tr("(empty)"), page));
                return;
            }
            for (int i = 0; i < from->stacks.size(); ++i) {
                const ViewStack& s = from->stacks[i];
                auto* row = new QHBoxLayout();
                row->addWidget(new QLabel(QStringLiteral("%1 x %2%3").arg(s.count).arg(unitName(s.unit))
                                              .arg(s.stars > 0 ? QStringLiteral(" ") + QString(s.stars, QChar(0x2605)) : QString()), page), 1);
                if (to) {
                    auto* count = new QSpinBox(page);
                    count->setRange(1, s.count);
                    count->setValue(s.count);
                    row->addWidget(count);
                    auto* move = new QPushButton(tr("to %1").arg(toName), page);
                    move->setObjectName(QStringLiteral("move_%1_%2").arg(from->id).arg(i));
                    move->setEnabled(myTurn);
                    const int fromId = from->id, toId = to->id, index = i;
                    const QString unit = unitName(s.unit);
                    connect(move, &QPushButton::clicked, this, [=, this]() {
                        send("move_units", {{"from", fromId}, {"to", toId}, {"stack", index}, {"count", count->value()}},
                             tr("Move %1 %2").arg(count->value()).arg(unit));
                    });
                    row->addWidget(move);
                }
                layout->addLayout(row);
            }
            if (to) {
                auto* all = new QPushButton(tr("Move everything to %1").arg(toName), page);
                all->setObjectName(QStringLiteral("moveAll_%1").arg(from->id));
                all->setEnabled(myTurn);
                const int fromId = from->id, toId = to->id;
                connect(all, &QPushButton::clicked, this, [=, this]() {
                    send("move_units", {{"from", fromId}, {"to", toId}, {"all", true}}, tr("Move everything to %1").arg(toName));
                });
                layout->addWidget(all);
            }
        };
        bool shipyard = false;
        for (const QString& id : town->buildings)
            if (const core::BuildingDef* b = m_content->building(id))
                for (const core::BuildingEffect& e : b->effects) shipyard = shipyard || e.type == core::BuildingEffect::Type::Shipyard;
        if (shipyard && visitor) {
            QStringList cost;
            const core::ResourceAmounts& c = m_content->balance().economy.boatCost;
            for (auto it = c.begin(); it != c.end(); ++it) cost << QStringLiteral("%1 %2").arg(it.value()).arg(it.key());
            auto* boat = new QPushButton(tr("Buy a boat for %1 (%2)").arg(visitor->name, cost.join(", ")), page);
            boat->setObjectName("buyBoat");
            boat->setEnabled(myTurn);
            const int hid = visitor->id;
            connect(boat, &QPushButton::clicked, this, [=, this]() { send("buy_boat", {{"hero", hid}}, tr("Buy a boat")); });
            layout->addWidget(boat);
        }
        if (!visitor) {
            section(tr("Garrison"), garrison, nullptr, {});
            layout->addWidget(new QLabel(tr("A hero standing in the town can take units from the garrison or leave them here."), page));
        } else {
            section(tr("Garrison"), garrison, heroArmy, visitor->name);
            section(tr("%1's army").arg(visitor->name), heroArmy, garrison, tr("the garrison"));
        }
        layout->addStretch(1);
        m_tabs->addTab(page, art::buttonIcon("defend"), tr("Armies"));
    }

    m_tabs->setCurrentIndex(std::min(keep, m_tabs->count() - 1));
}

}  // namespace kot::client
