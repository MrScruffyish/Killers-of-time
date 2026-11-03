#include "client/TownDialog.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/content/ContentRegistry.h"

namespace kot::client {

TownDialog::TownDialog(const core::ContentRegistry* content, QWidget* parent)
    : QDialog(parent), m_content(content) {
    setObjectName("townDialog");
    resize(640, 480);
    auto* layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    layout->addWidget(m_tabs, 1);
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
    const int keep = m_tabs->currentIndex();
    while (m_tabs->count() > 0) {
        QWidget* old = m_tabs->widget(0);
        m_tabs->removeTab(0);
        delete old;
    }

    const auto send = [this](const QString& type, const QJsonObject& body, const QString& what) {
        emit commandRequested(type, body, what);
    };

    // ---- Buildings
    {
        auto* page = new QWidget();
        auto* layout = new QVBoxLayout(page);
        auto* table = new QTableWidget(page);
        table->setObjectName("buildingTable");
        table->setColumnCount(4);
        table->setHorizontalHeaderLabels({tr("Building"), tr("Cost"), tr("Status"), QString()});
        table->horizontalHeader()->setStretchLastSection(true);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->setRowCount(static_cast<int>(town->buildings.size() + town->buildOptions.size()));
        int row = 0;
        const auto name = [&](const QString& id) {
            const core::BuildingDef* b = m_content->building(id);
            return b ? b->name : id;
        };
        for (const QString& id : town->buildings) {
            table->setItem(row, 0, new QTableWidgetItem(name(id)));
            table->setItem(row, 1, new QTableWidgetItem(QString()));
            table->setItem(row, 2, new QTableWidgetItem(tr("Built")));
            ++row;
        }
        for (const BuildOption& o : town->buildOptions) {
            const core::BuildingDef* b = m_content->building(o.id);
            table->setItem(row, 0, new QTableWidgetItem(name(o.id)));
            table->setItem(row, 1, new QTableWidgetItem(b ? costText(b->cost) : QString()));
            table->setItem(row, 2, new QTableWidgetItem(o.why.isEmpty() ? tr("Available") : o.why));
            const QString id = o.id;
            const int tid = town->id;
            if (o.why.isEmpty() || o.why.startsWith("not enough")) {
                const bool trade = !o.why.isEmpty();
                auto* button = new QPushButton(trade ? tr("Build (trade first)") : tr("Build"));
                button->setObjectName(QStringLiteral("build_%1").arg(id));
                button->setEnabled(myTurn);
                connect(button, &QPushButton::clicked, this, [=, this]() {
                    send("build", {{"town", tid}, {"building", id}, {"useTrade", trade}},
                         tr("Build %1").arg(name(id)));
                });
                table->setCellWidget(row, 3, button);
            }
            ++row;
        }
        table->resizeColumnsToContents();
        layout->addWidget(table);
        m_tabs->addTab(page, tr("Buildings"));
    }

    // ---- Recruit
    {
        auto* page = new QWidget();
        auto* layout = new QVBoxLayout(page);
        const ViewArmy* garrison = m_view.army(town->garrison);
        QStringList stacks;
        if (garrison)
            for (const ViewStack& s : garrison->stacks) stacks << QStringLiteral("%1 x %2").arg(s.count).arg(unitName(s.unit));
        layout->addWidget(new QLabel(tr("Garrison: %1").arg(stacks.isEmpty() ? tr("empty") : stacks.join(", ")), page));

        auto* table = new QTableWidget(page);
        table->setObjectName("recruitTable");
        table->setColumnCount(5);
        table->setHorizontalHeaderLabels({tr("Unit"), tr("Available"), tr("Cost each"), tr("Count"), QString()});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setRowCount(static_cast<int>(town->recruitOptions.size()));
        int row = 0;
        for (const RecruitChoice& rc : town->recruitOptions) {
            const core::UnitDef* u = m_content->unit(rc.unit);
            table->setItem(row, 0, new QTableWidgetItem(unitName(rc.unit)));
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
        m_tabs->addTab(page, tr("Recruit"));
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
            row->addWidget(new QLabel(t ? tr("%1 (%2)").arg(t->name, t->classId) : offer, page), 1);
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
        m_tabs->addTab(page, tr("Tavern"));
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
                const QList<ViewStack> stacks = from->stacks;
                connect(all, &QPushButton::clicked, this, [=, this]() {
                    // Always the first stack: the rest moves up as each one leaves.
                    for (const ViewStack& s : stacks)
                        send("move_units", {{"from", fromId}, {"to", toId}, {"stack", 0}, {"count", s.count}},
                             tr("Move %1 %2").arg(s.count).arg(unitName(s.unit)));
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
        m_tabs->addTab(page, tr("Armies"));
    }

    m_tabs->setCurrentIndex(std::min(keep, m_tabs->count() - 1));
}

}  // namespace kot::client
