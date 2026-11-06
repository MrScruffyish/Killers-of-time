#include "client/TownDialog.h"
#include "core/rules/HeroRules.h"

#include <QTimer>
#include "net/ActionTiming.h"

#include <QSignalBlocker>
#include <QComboBox>
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
#include <functional>

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
    connect(m_tabs, &QTabWidget::currentChanged, this, &TownDialog::buildTab);  // a tab not built yet
    layout->addWidget(m_tabs, 1);
    auto* bottom = new QHBoxLayout();
    // Going to my other towns without closing: arrows, a list, T / PgUp / PgDn.
    auto* prev = new QPushButton(QStringLiteral("\u25C0"), this);
    prev->setObjectName("townPrevButton");
    prev->setToolTip(tr("Previous town (PgUp)"));
    auto* next = new QPushButton(QStringLiteral("\u25B6"), this);
    next->setObjectName("townNextButton");
    next->setToolTip(tr("Next town (T or PgDn)"));
    for (QPushButton* b : {prev, next}) b->setMinimumSize(44, 40);
    m_townChoice = new QComboBox(this);
    m_townChoice->setObjectName("townChoice");
    m_townChoice->setMinimumWidth(220);
    m_townChoice->setToolTip(tr("Go to another of your towns"));
    connect(prev, &QPushButton::clicked, this, [this]() { stepTown(-1); });
    connect(next, &QPushButton::clicked, this, [this]() { stepTown(1); });
    connect(m_townChoice, &QComboBox::activated, this, [this](int i) {
        const int id = m_townChoice->itemData(i).toInt();
        if (id && id != m_townId) {
            m_townId = id;
            rebuild();
        }
    });
    for (const auto& [key, step] : {std::pair{Qt::Key_T, 1}, std::pair{Qt::Key_PageDown, 1}, std::pair{Qt::Key_PageUp, -1}}) {
        auto* sc = new QShortcut(QKeySequence(key), this);
        const int s = step;
        connect(sc, &QShortcut::activated, this, [this, s]() { stepTown(s); });
    }
    bottom->addWidget(prev);
    bottom->addWidget(m_townChoice);
    bottom->addWidget(next);
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
    for (const auto [r, amount] : cost) parts << QStringLiteral("%1 %2").arg(amount).arg(m_content->resource(r).key);
    return parts.isEmpty() ? tr("free") : parts.join(", ");
}

QString TownDialog::unitName(core::UnitTypeId id) const {
    const core::UnitDef* u = m_content->unit(id);
    return u ? u->name : QString();
}

QString TownDialog::effectsText(const core::BuildingDef& b, bool brief) const {
    using T = core::BuildingEffect::Type;
    QStringList parts;
    for (const core::BuildingEffect& e : b.effects) {
        switch (e.type) {
        case T::Dwelling: {
            const core::UnitDef* u = m_content->unit(e.unit);
            const QString unit = u ? u->name : QString();
            if (brief) {
                parts << (u ? tr("%1 +%2/wk").arg(unit).arg(u->weeklyGrowth) : unit);
                break;
            }
            const QString stats = u ? tr(" (attack %1, defense %2, %3 HP, +%4/week)").arg(u->attack).arg(u->defense).arg(u->maxHp).arg(u->weeklyGrowth) : QString();
            const core::BuildingDef* old = !b.upgradeOf.isValid() ? nullptr : m_content->building(b.upgradeOf);
            parts << (old ? tr("Upgrades the %1: recruit %2%3").arg(old->name, unit, stats) : tr("Recruit %1%2").arg(unit, stats));
            break;
        }
        case T::Income: parts << (brief ? tr("+%1 %2/day") : tr("+%1 %2 every day")).arg(e.amount).arg(m_content->resource(e.resource).key); break;
        case T::Tavern: parts << tr("Hire heroes here"); break;
        case T::MageGuild: parts << (brief ? tr("Spells to level %1") : tr("Heroes visiting learn spells up to level %1")).arg(e.amount); break;
        case T::Defense: parts << (brief ? tr("Walls +%1 defense") : tr("Walls: the town's defenders get +%1 defense")).arg(e.amount); break;
        case T::Market: parts << (brief ? tr("Trade %1% better") : tr("Trade resources at %1% better rates")).arg(e.amount); break;
        case T::Shipyard: parts << (brief ? tr("Buy boats") : tr("Buy boats (coastal towns)")); break;
        case T::Growth: parts << (brief ? tr("Dwellings +%1% growth") : tr("Every dwelling here grows %1% more each week")).arg(e.amount); break;
        case T::Vision: parts << (brief ? tr("Sees %1 tiles farther") : tr("The town sees %1 tiles farther")).arg(e.amount); break;
        }
    }
    if (parts.isEmpty()) return brief ? tr("Needed for others") : tr("Needed for other buildings");
    return parts.join(brief ? QStringLiteral(", ") : QStringLiteral("; "));
}

bool TownDialog::isUpgrade(const core::BuildingDef& b) const {
    if (b.upgradeOf.isValid()) return true;
    for (const core::BuildingTypeId need : b.requiredBuildings) {
        const core::BuildingDef* other = m_content->building(need);
        if (!other) continue;
        for (const core::BuildingEffect& mine : b.effects)
            for (const core::BuildingEffect& theirs : other->effects)
                if (mine.type != core::BuildingEffect::Type::Dwelling  // dwellings use upgradeOf; others just need a lower one
                    && mine.type == theirs.type && mine.resource == theirs.resource)
                    return true;
    }
    return false;
}

void TownDialog::drawScene(const ViewTown& town) {
    // The town as a picture: one figure per building (the top of each upgrade
    // chain), the walls in front. It fills up as the town grows.
    using K = art::SceneBuilding::Kind;
    QSet<core::BuildingTypeId> replaced;
    for (const core::BuildingTypeId id : town.buildings)
        if (const core::BuildingDef* b = m_content->building(id); b && b->upgradeOf.isValid()) replaced.insert(b->upgradeOf);
    // Only the biggest hall stands in the picture.
    const core::BuildingTypeId village = m_content->buildingId(QStringLiteral("village_hall")),
                               townHall = m_content->buildingId(QStringLiteral("town_hall")),
                               city = m_content->buildingId(QStringLiteral("city_hall"));
    if (town.buildings.contains(townHall) || town.buildings.contains(city)) replaced.insert(village);
    if (town.buildings.contains(city)) replaced.insert(townHall);
    QList<art::SceneBuilding> scene;
    for (const core::BuildingTypeId id : town.buildings) {
        const core::BuildingDef* b = m_content->building(id);
        if (!b || replaced.contains(id)) continue;
        art::SceneBuilding sb;
        for (const core::BuildingEffect& e : b->effects) {
            switch (e.type) {
            case core::BuildingEffect::Type::Dwelling:
                sb.kind = K::Dwelling;
                sb.unit = art::unitKey(e.unit);
                if (const core::UnitDef* u = m_content->unit(e.unit)) sb.level = u->rank;
                break;
            case core::BuildingEffect::Type::MageGuild: sb.kind = K::MageGuild; sb.level = e.amount; break;
            case core::BuildingEffect::Type::Tavern: sb.kind = K::Tavern; break;
            case core::BuildingEffect::Type::Market: sb.kind = K::Market; break;
            case core::BuildingEffect::Type::Shipyard: sb.kind = K::Shipyard; break;
            case core::BuildingEffect::Type::Defense: sb.kind = K::Walls; break;
            case core::BuildingEffect::Type::Income:
                if (id == village || id == townHall || id == city) {
                    sb.kind = K::Hall;
                    sb.level = id == city ? 2 : id == townHall ? 1 : 0;
                } else if (sb.kind == K::Other) {
                    sb.kind = K::Workshop;  // a resource building: a workshop with a heap of what it makes
                    sb.unit = m_content->resource(e.resource).key;
                }
                break;
            case core::BuildingEffect::Type::Growth: sb.kind = K::Granary; break;
            case core::BuildingEffect::Type::Vision: sb.kind = K::Watchtower; break;
            }
        }
        scene.append(sb);
    }
    const ViewPlayer* owner = m_view.player(town.owner);
    const int w = std::max(600, width() - 30), h = 170;
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    art::drawTownScene(p, QRectF(0, 0, w, h), art::TownLook{owner ? QColor(owner->color) : QColor("#999999"), art::raceKey(town.race), town.walls, town.size}, scene);
    p.end();
    m_scene->setPixmap(pm);
    m_scene->setToolTip(tr("%1: %2 buildings%3").arg(town.name).arg(town.buildings.size())
                            .arg(town.walls >= 2 ? tr(", a citadel") : town.walls == 1 ? tr(", stone walls") : tr(", a palisade")));
}

void TownDialog::showTown(const GameView& view, int townId) {
    m_townId = townId;
    m_tabs->setCurrentIndex(0);
    m_view = view;
    rebuild();
    show();
    raise();
}

void TownDialog::refresh(const GameView& view) {
    m_view = view;
    if (isVisible()) rebuild();  // closed: showTown builds it when it opens
}

QList<int> TownDialog::myTowns() const {
    QList<int> ids;
    for (const ViewTown& t : m_view.towns)
        if (t.mine) ids << t.id;
    return ids;
}

void TownDialog::stepTown(int direction) {
    const QList<int> ids = myTowns();
    if (ids.size() < 2) return;
    const int at = std::max(0, int(ids.indexOf(m_townId)));
    m_townId = ids[(at + direction + ids.size()) % ids.size()];
    rebuild();
}

void TownDialog::rebuild() {
    const ViewTown* town = m_view.town(m_townId);
    if (!town || !town->mine) {
        hide();
        return;
    }
    {
        const QSignalBlocker block(m_townChoice);
        m_townChoice->clear();
        for (const ViewTown& t : m_view.towns)
            if (t.mine) m_townChoice->addItem(art::townSprite(QColor(m_view.player(t.owner) ? m_view.player(t.owner)->color : QString("#cccccc")), 24), t.name, t.id);
        m_townChoice->setCurrentIndex(m_townChoice->findData(m_townId));
        for (const char* name : {"townPrevButton", "townNextButton"})
            if (auto* b = findChild<QPushButton*>(name)) b->setEnabled(m_townChoice->count() > 1);
        m_townChoice->setEnabled(m_townChoice->count() > 1);
    }
    const bool myTurn = m_view.isMyTurn();
    setWindowTitle(tr("%1 (%2)").arg(town->name, m_content->race(town->race).name));
    const qint64 sceneStart = net::timing::now();
    drawScene(*town);
    net::timing::add("town scene", net::timing::now() - sceneStart);
    const qint64 tabsStart = net::timing::now();
    const int keep = m_tabs->currentIndex();
    ++m_tabGeneration;
    m_tabFill.clear();
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
    addLazyTab(art::buttonIcon("build"), tr("Buildings"), [=, this](QWidget* page) {
        const ViewTown* town = m_view.town(m_townId);
        if (!town) return;
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
        // Said once here instead of on every row: built today, and what stands already.
        const bool builtToday = town->lastBuildDay == m_view.day;
        if (builtToday) {
            auto* today = new QLabel(tr("<b style='color:#ffd27a'>Something was built here today.</b> One building per day: the next one tomorrow."), page);
            today->setObjectName("builtToday");
            layout->addWidget(today);
        }
        {
            QStringList standing;
            for (const core::BuildingTypeId id : town->buildings)
                if (const core::BuildingDef* b = m_content->building(id)) standing << b->name;
            auto* built = new QLabel(tr("<i>Built here:</i> %1").arg(standing.isEmpty() ? tr("nothing yet") : standing.join(", ")), page);
            built->setObjectName("builtList");
            built->setWordWrap(true);
            layout->addWidget(built);
        }
        const auto name = [&](core::BuildingTypeId id) {
            const core::BuildingDef* b = m_content->building(id);
            return b ? b->name : QString();
        };
        struct Row {
            core::BuildingTypeId id;
            QString status;
            int rank;  // 0 now, 1 after a trade, 2 blocked otherwise, 2 + n: n buildings missing first
            bool upgrade;  // within a rank: new buildings first, then upgrades
        };
        QList<Row> rows;
        // Left out: buildings that can never come here any more because a rival
        // one was built (excludes, either way round).
        const auto ruledOut = [&](core::BuildingTypeId id) {
            const core::BuildingDef* b = m_content->building(id);
            for (const core::BuildingTypeId built : town->buildings) {
                const core::BuildingDef* other = m_content->building(built);
                if ((b && b->excludes.contains(built)) || (other && other->excludes.contains(id))) return true;
            }
            return false;
        };
        // How many buildings are still missing before this one: 0 = none, 1 = one
        // needed building can be built first, 2 = that one needs another, ...
        QHash<core::BuildingTypeId, int> stepsMemo;
        const std::function<int(core::BuildingTypeId, int)> stepsTo = [&](core::BuildingTypeId id, int guard) -> int {
            if (town->buildings.contains(id) || guard > 20) return 0;
            if (stepsMemo.contains(id)) return stepsMemo.value(id);
            const core::BuildingDef* b = m_content->building(id);
            int steps = 0;
            if (b) {
                QList<core::BuildingTypeId> needs = b->requiredBuildings;
                if (b->upgradeOf.isValid()) needs << b->upgradeOf;
                for (const core::BuildingTypeId n : needs)
                    if (!town->buildings.contains(n)) steps = std::max(steps, stepsTo(n, guard + 1) + 1);
            }
            stepsMemo.insert(id, steps);
            return steps;
        };
        QHash<core::BuildingTypeId, QString> whyOf;
        for (const BuildOption& o : town->buildOptions) whyOf.insert(o.id, o.why);
        const auto missingNow = [&](core::BuildingTypeId id) {
            QList<core::BuildingTypeId> out;
            const core::BuildingDef* b = m_content->building(id);
            if (!b) return out;
            QList<core::BuildingTypeId> needs = b->requiredBuildings;
            if (b->upgradeOf.isValid()) needs << b->upgradeOf;
            for (const core::BuildingTypeId n : needs)
                if (!town->buildings.contains(n)) out << n;
            return out;
        };
        for (const BuildOption& o : town->buildOptions) {
            if (ruledOut(o.id) || town->buildings.contains(o.id)) continue;  // built ones are listed above
            if (o.why.contains(QStringLiteral("at the coast"))) continue;  // a shipyard inland: never possible
            const int steps = stepsTo(o.id, 0);
            int rank = o.why.isEmpty() ? 0 : o.why.startsWith("not enough") ? 1 : 2;
            QString status = o.why;
            if (steps > 0) {
                // What to build first, and whether that can be done now.
                QStringList parts;
                for (const core::BuildingTypeId n : missingNow(o.id)) {
                    const QString w = whyOf.value(n, QStringLiteral("?"));
                    parts << (w.isEmpty() ? tr("%1 (can build now)").arg(name(n))
                              : w.startsWith("not enough") ? tr("%1 (after a trade)").arg(name(n)) : name(n));
                }
                status = tr("Requires: %1").arg(parts.join(", "));  // only what is not built yet
                rank = 2 + steps;  // nearer ones first
            } else if (o.why.contains(QStringLiteral("today"))) {
                status = QString();  // said once above
            }
            const core::BuildingDef* def = m_content->building(o.id);
            rows.append({o.id, status, rank, def && isUpgrade(*def)});
        }
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            return a.rank != b.rank ? a.rank < b.rank : a.upgrade < b.upgrade;
        });
        if (m_onlyBuildable) rows.erase(std::remove_if(rows.begin(), rows.end(), [](const Row& r) { return r.rank > 1; }), rows.end());

        auto* table = new QTableWidget(page);
        table->setShowGrid(false);
        table->setAlternatingRowColors(true);
        table->setObjectName("buildingTable");
        table->setColumnCount(5);
        table->setHorizontalHeaderLabels({tr("Building"), QString(), tr("Cost"), tr("What it does"), tr("What first / why not")});
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
            table->setItem(row, 2, new QTableWidgetItem(!b ? QString() : costText(b->cost)));
            auto* what = new QTableWidgetItem(b ? effectsText(*b, true) : QString());
            what->setToolTip(b ? effectsText(*b) : QString());
            table->setItem(row, 3, what);
            // Only blocked rows say why (a button already says it can be built).
            auto* why = new QTableWidgetItem(r.rank == 0 ? QString() : r.status);
            why->setToolTip(why->text());
            if (r.rank == 3) why->setForeground(QColor("#d8c890"));  // one step: worth a look
            table->setItem(row, 4, why);
            if (r.rank <= 1) {
                const bool trade = r.rank == 1;
                const core::BuildingTypeId id = r.id;
                auto* button = new QPushButton(trade ? tr("Build (trade first)") : tr("Build"));
                button->setObjectName(QStringLiteral("build_%1").arg(m_content->building(id)->key));
                button->setEnabled(myTurn);
                button->setMinimumHeight(30);
                connect(button, &QPushButton::clicked, this, [=, this]() {
                    send("build", {{"town", tid}, {"building", id.index()}, {"useTrade", trade}}, tr("Build %1").arg(name(id)));
                });
                table->setCellWidget(row, 1, button);
            }
        }
        table->resizeColumnsToContents();
        table->setColumnWidth(1, std::max(table->columnWidth(1), 86));  // room for the styled button
        table->setColumnWidth(0, std::min(table->columnWidth(0), 170));
        table->setColumnWidth(2, std::min(table->columnWidth(2), 150));
        table->setColumnWidth(3, std::min(table->columnWidth(3), 280));
        table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        table->resizeRowsToContents();
        for (int row = 0; row < table->rowCount(); ++row) table->setRowHeight(row, std::max(table->rowHeight(row), 38));
        layout->addWidget(table);
        bool any = false;
        for (const Row& r : rows) any = any || r.rank <= 1;
        if (!any && !builtToday) layout->addWidget(new QLabel(tr("Nothing can be built here right now."), page));
    });

    // ---- Recruit (whenever the town has a dwelling, also with nothing waiting: then
    // when new units come; or a hero is here and the garrison has units: Give all)
    int recruitable = 0;
    for (const RecruitChoice& rc : town->recruitOptions) recruitable += rc.available;
    bool heroCanTake = false;
    if (const ViewArmy* g = m_view.army(town->garrison); g && !g->stacks.isEmpty())
        for (const ViewHero* h : m_view.myHeroes()) heroCanTake = heroCanTake || h->pos == town->pos;
    if (!town->recruitOptions.isEmpty() || heroCanTake) {
        addLazyTab(art::buttonIcon("army"), tr("Recruit"), [=, this](QWidget* page) {
        const ViewTown* town = m_view.town(m_townId);
        if (!town) return;
        auto* layout = new QVBoxLayout(page);
        const ViewArmy* garrison = m_view.army(town->garrison);
        QStringList stacks;
        if (garrison)
            for (const ViewStack& s : garrison->stacks) stacks << QStringLiteral("%1 x %2").arg(s.count).arg(unitName(s.unit));
        layout->addWidget(new QLabel(tr("Garrison: %1").arg(stacks.isEmpty() ? tr("empty") : stacks.join(", ")), page));
        int waiting = 0;
        for (const RecruitChoice& rc : town->recruitOptions) waiting += rc.available;
        if (waiting == 0 && !town->recruitOptions.isEmpty()) {
            const int daysLeft = 7 - (m_view.day - 1) % 7;
            auto* note = new QLabel(tr("<i>No units waiting. Dwellings get new units at the start of each week (in %1 day(s)). "
                                       "An upgraded dwelling keeps the units of the one it upgrades: if they were all recruited, "
                                       "the upgrade has none until then.</i>").arg(daysLeft), page);
            note->setWordWrap(true);
            layout->addWidget(note);
        }

        auto* table = new QTableWidget(page);
        table->setShowGrid(false);
        table->setAlternatingRowColors(true);
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
            button->setObjectName(QStringLiteral("recruit_%1").arg(art::unitKey(rc.unit)));
            button->setEnabled(myTurn && rc.available > 0);
            const core::UnitTypeId unit = rc.unit;
            const int tid = town->id;
            connect(button, &QPushButton::clicked, this, [=, this]() {
                if (count->value() > 0)
                    send("recruit", {{"town", tid}, {"unit", unit.index()}, {"count", count->value()}},
                         tr("Recruit %1 x %2").arg(count->value()).arg(unitName(unit)));
            });
            table->setCellWidget(row, 4, button);
            ++row;
        }
        table->resizeColumnsToContents();
        table->setColumnWidth(4, std::max(table->columnWidth(4), 96));  // room for the styled button
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
        });
    }

    // ---- Tavern
    addLazyTab(art::buttonIcon("tavern"), tr("Tavern"), [=, this](QWidget* page) {
        const ViewTown* town = m_view.town(m_townId);
        if (!town) return;
        auto* layout = new QVBoxLayout(page);
        bool hasTavern = false;
        for (const core::BuildingTypeId id : town->buildings) {
            const core::BuildingDef* b = m_content->building(id);
            if (!b) continue;
            for (const core::BuildingEffect& e : b->effects)
                hasTavern = hasTavern || (e.type == core::BuildingEffect::Type::Tavern);
        }
        if (!hasTavern) {
            layout->addWidget(new QLabel(tr("Build a Tavern to hire heroes."), page));
        } else if (town->tavernOffers.isEmpty()) {
            layout->addWidget(new QLabel(tr("No heroes are offered right now. New ones arrive every week."), page));
        } else if (town->hiredThisWeek) {
            auto* done = new QLabel(tr("<b style='color:#ffd27a'>A hero was hired here this week.</b> The tavern hires out one hero a week: the next one when the week turns."), page);
            done->setObjectName("hiredThisWeek");
            done->setWordWrap(true);
            layout->addWidget(done);
        }
        const int cost = m_content->balance().economy.heroCost;
        const int gold = m_view.resources.value(m_content->currency());
        for (const core::HeroTemplateId offer : town->tavernOffers) {
            const core::HeroTemplateDef* t = m_content->heroTemplate(offer);
            auto* row = new QHBoxLayout();
            auto* face = new QLabel(page);
            const ViewPlayer* me = m_view.player(m_view.you);
            if (t) face->setPixmap(art::portrait(t->classId, me ? QColor(me->color) : QColor("#999999"), offer.index() % 6, 56));
            row->addWidget(face);
            const core::HeroClassDef* cls = t ? m_content->heroClass(t->classId) : nullptr;
            const QString special = t && !t->specialty.isEmpty() ? core::describeSpecialty(t->specialty, *m_content) : QString();
            auto* who = new QLabel(t ? tr("<b>%1</b><br>%2%3").arg(t->name, cls ? cls->name : QString(),
                                                                  special.isEmpty() ? QString() : tr("<br><span style='color:#e8c872'>Specialty: %1</span>").arg(special.toHtmlEscaped()))
                                     : QString(), page);
            who->setWordWrap(true);
            row->addWidget(who, 1);
            auto* hire = new QPushButton(gold >= cost ? tr("Hire for %1").arg(cost) : tr("Hire for %1 (trade first)").arg(cost), page);
            hire->setObjectName(QStringLiteral("hire_%1").arg(t ? t->key : QString()));
            hire->setEnabled(myTurn && !town->hiredThisWeek);
            const int tid = town->id;
            const bool trade = gold < cost;
            connect(hire, &QPushButton::clicked, this, [=, this]() {
                send("hire_hero", {{"town", tid}, {"hero", offer.index()}, {"useTrade", trade}}, tr("Hire %1").arg(t ? t->name : QString()));
            });
            row->addWidget(hire);
            layout->addLayout(row);
        }
        layout->addStretch(1);
    });

    // ---- Armies: the garrison and a hero visiting the town
    addLazyTab(art::buttonIcon("defend"), tr("Armies"), [=, this](QWidget* page) {
        const ViewTown* town = m_view.town(m_townId);
        if (!town) return;
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
        for (const core::BuildingTypeId id : town->buildings)
            if (const core::BuildingDef* b = m_content->building(id))
                for (const core::BuildingEffect& e : b->effects) shipyard = shipyard || e.type == core::BuildingEffect::Type::Shipyard;
        if (shipyard && visitor) {
            QStringList cost;
            const core::ResourceAmounts& c = m_content->balance().economy.boatCost;
            for (const auto [r, amount] : c) cost << QStringLiteral("%1 %2").arg(amount).arg(m_content->resource(r).key);
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
    });

    // The tab shown is built now, the others a moment later (after this is on the
    // screen), or at once when one is opened.
    m_tabs->setCurrentIndex(std::min(keep, m_tabs->count() - 1));
    buildTab(m_tabs->currentIndex());
    net::timing::add("town tabs", net::timing::now() - tabsStart);
    QTimer::singleShot(30, this, [this, generation = m_tabGeneration]() {
        if (generation != m_tabGeneration) return;
        for (int i = 0; i < m_tabs->count(); ++i) buildTab(i);
    });
}

void TownDialog::addLazyTab(const QIcon& icon, const QString& title, std::function<void(QWidget*)> fill) {
    auto* page = new QWidget();
    m_tabs->addTab(page, icon, title);
    m_tabFill.append(std::move(fill));
}

void TownDialog::buildTab(int index) {
    if (index < 0 || index >= m_tabFill.size() || !m_tabFill[index]) return;
    const auto fill = std::move(m_tabFill[index]);
    m_tabFill[index] = nullptr;
    QWidget* page = m_tabs->widget(index);
    const QSignalBlocker block(m_tabs);
    fill(page);
}

}  // namespace kot::client
