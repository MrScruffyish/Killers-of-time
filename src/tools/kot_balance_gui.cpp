// kot_balance_gui: tweak unit numbers and see the balance reports change at once.
// The same calculations as kot_balance. "Save" writes the changed numbers back
// into data/units/<id>.json (other fields are kept).

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/content/ContentRegistry.h"
#include "core/data/DataDir.h"
#include "tools/BalanceCalc.h"

using namespace kot::core;
using namespace kot::tools;

namespace {

ResourceId g_gold;  // the currency, set when the data is loaded (for the gold column)

// Editable columns: header, JSON field, getter, setter.
struct Column {
    const char* title;
    const char* field;
    int (*get)(const UnitDef&);
    void (*set)(UnitDef&, int);
};
const QList<Column>& columns() {
    static const QList<Column> c = {
        {"tier", "tier", [](const UnitDef& u) { return u.tier; }, [](UnitDef& u, int v) { u.tier = v; }},
        {"attack", "attack", [](const UnitDef& u) { return u.attack; }, [](UnitDef& u, int v) { u.attack = v; }},
        {"defense", "defense", [](const UnitDef& u) { return u.defense; }, [](UnitDef& u, int v) { u.defense = v; }},
        {"dmg min", "damageMin", [](const UnitDef& u) { return u.damageMin; }, [](UnitDef& u, int v) { u.damageMin = v; }},
        {"dmg max", "damageMax", [](const UnitDef& u) { return u.damageMax; }, [](UnitDef& u, int v) { u.damageMax = v; }},
        {"hp", "maxHp", [](const UnitDef& u) { return u.maxHp; }, [](UnitDef& u, int v) { u.maxHp = std::max(1, v); }},
        {"speed", "battleSpeed", [](const UnitDef& u) { return u.battleSpeed; }, [](UnitDef& u, int v) { u.battleSpeed = std::max(1, v); }},
        {"initiative", "initiative", [](const UnitDef& u) { return u.initiative; }, [](UnitDef& u, int v) { u.initiative = std::max(1, v); }},
        {"growth", "weeklyGrowth", [](const UnitDef& u) { return u.weeklyGrowth; }, [](UnitDef& u, int v) { u.weeklyGrowth = v; }},
        {"gold", "cost.gold", [](const UnitDef& u) { return u.cost.value(g_gold); }, [](UnitDef& u, int v) { u.cost[g_gold] = v; }},
    };
    return c;
}

class BalanceWindow : public QMainWindow {
public:
    explicit BalanceWindow(const QString& dataDir) : m_dataDir(dataDir) {
        setWindowTitle(QStringLiteral("Killers of Time - balance (%1)").arg(dataDir));
        resize(1300, 700);
        auto* split = new QSplitter(this);
        setCentralWidget(split);

        auto* left = new QWidget(split);
        auto* l = new QVBoxLayout(left);
        l->addWidget(new QLabel(tr("Edit the numbers (double-click a cell). Power per 1000 gold is compared within each tier; "
                                   "<span style='color:#b00000'>red</span> = strong for its cost, "
                                   "<span style='color:#0050b0'>blue</span> = weak."), left));
        m_units = new QTableWidget(left);
        l->addWidget(m_units, 1);
        auto* buttons = new QHBoxLayout();
        auto* save = new QPushButton(tr("Save changes to data/units"), left);
        auto* reload = new QPushButton(tr("Reload from disk"), left);
        buttons->addWidget(save);
        buttons->addWidget(reload);
        buttons->addStretch(1);
        l->addLayout(buttons);

        auto* tabs = new QTabWidget(split);
        m_races = new QTableWidget(tabs);
        tabs->addTab(m_races, tr("Races"));
        auto* resPage = new QWidget(tabs);
        auto* rl = new QVBoxLayout(resPage);
        rl->addWidget(new QLabel(tr("Trade value of each resource in gold. Costs in other resources count with these "
                                    "values here, and trading uses them (minus the trade fee)."), resPage));
        m_resources = new QTableWidget(resPage);
        rl->addWidget(m_resources, 1);
        tabs->addTab(resPage, tr("Resources"));
        connect(m_resources, &QTableWidget::cellChanged, this, [this](int row, int col) { onResourceEdited(row, col); });
        auto* duelPage = new QWidget(tabs);
        auto* d = new QVBoxLayout(duelPage);
        auto* opts = new QHBoxLayout();
        opts->addWidget(new QLabel(tr("Tier"), duelPage));
        m_tier = new QSpinBox(duelPage);
        m_tier->setRange(0, 7);
        m_tier->setSpecialValueText(tr("all"));
        opts->addWidget(m_tier);
        opts->addWidget(new QLabel(tr("Gold"), duelPage));
        m_gold = new QSpinBox(duelPage);
        m_gold->setRange(500, 200000);
        m_gold->setSingleStep(1000);
        m_gold->setValue(5000);
        opts->addWidget(m_gold);
        opts->addWidget(new QLabel(tr("Battles"), duelPage));
        m_runs = new QSpinBox(duelPage);
        m_runs->setRange(2, 1000);
        m_runs->setValue(40);
        opts->addWidget(m_runs);
        auto* run = new QPushButton(tr("Run duels"), duelPage);
        opts->addWidget(run);
        opts->addStretch(1);
        d->addLayout(opts);
        m_duels = new QTableWidget(duelPage);
        d->addWidget(m_duels, 1);
        tabs->addTab(duelPage, tr("Duels (equal cost)"));
        split->setStretchFactor(0, 3);
        split->setStretchFactor(1, 2);

        connect(m_units, &QTableWidget::cellChanged, this, [this](int row, int col) { onEdited(row, col); });
        connect(save, &QPushButton::clicked, this, [this]() { saveChanges(); });
        connect(reload, &QPushButton::clicked, this, [this]() { load(); });
        connect(run, &QPushButton::clicked, this, [this]() { runDuels(); });
        load();
    }

private:
    void load() {
        QStringList errors;
        if (!m_content.loadFromDirectory(m_dataDir, &errors)) {
            QMessageBox::critical(this, tr("Cannot load"), errors.join('\n'));
            return;
        }
        g_gold = m_content.currency();
        m_changed.clear();
        m_changedResources = false;
        fillUnits();
        fillRaces();
        fillResources();
        statusBar()->showMessage(tr("Loaded %1 units").arg(m_content.units().size()));
    }

    void fillUnits() {
        m_filling = true;
        const QList<Column>& cols = columns();
        QStringList headers{tr("unit"), tr("race")};
        for (const Column& c : cols) headers << QString::fromLatin1(c.title);
        headers << tr("power") << tr("cost") << tr("pow/1000g") << tr("note");
        m_units->setColumnCount(headers.size());
        m_units->setHorizontalHeaderLabels(headers);
        m_units->setRowCount(m_content.units().size());
        const QList<UnitRow> rows = unitRows(m_content);
        int r = 0;
        for (const UnitDef& u : m_content.units()) {
            const auto fixed = [&](int col, const QString& text) {
                auto* item = new QTableWidgetItem(text);
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                m_units->setItem(r, col, item);
                return item;
            };
            fixed(0, u.key);
            fixed(1, m_content.race(u.race).key);
            for (int c = 0; c < cols.size(); ++c) {
                auto* item = new QTableWidgetItem();
                item->setData(Qt::EditRole, cols[c].get(u));
                if (m_changed.contains(u.key)) item->setBackground(QColor("#fff3c0"));
                m_units->setItem(r, 2 + c, item);
            }
            const UnitRow& row = rows[r];
            const int base = 2 + cols.size();
            fixed(base, QString::number(row.power, 'f', 0));
            fixed(base + 1, QString::number(row.cost, 'f', 0));
            fixed(base + 2, QString::number(row.powerPer1000, 'f', 0));
            QTableWidgetItem* note = fixed(base + 3, row.note);
            const QColor colour = row.note.startsWith("STRONG") ? QColor("#b00000") : row.note.isEmpty() ? QColor() : QColor("#0050b0");
            if (colour.isValid())
                for (int c = base + 2; c <= base + 3; ++c) m_units->item(r, c)->setForeground(colour);
            Q_UNUSED(note);
            ++r;
        }
        m_units->resizeColumnsToContents();
        m_filling = false;
    }

    void fillRaces() {
        const QList<RaceRow> rows = raceRows(m_content);
        m_races->clear();
        m_races->setColumnCount(5);
        m_races->setHorizontalHeaderLabels({tr("race"), tr("week power"), tr("week cost"), tr("pow/1000g"), tr("town gold")});
        m_races->setRowCount(rows.size());
        for (int i = 0; i < rows.size(); ++i) {
            const RaceRow& r = rows[i];
            const QStringList cells{r.race, QString::number(r.weekPower, 'f', 0), QString::number(r.weekCost, 'f', 0),
                                    QString::number(r.powerPer1000, 'f', 0), QString::number(r.townGold)};
            for (int c = 0; c < cells.size(); ++c) m_races->setItem(i, c, new QTableWidgetItem(cells[c]));
        }
        m_races->resizeColumnsToContents();
    }

    void fillResources() {
        m_filling = true;
        m_resources->clear();
        m_resources->setColumnCount(2);
        m_resources->setHorizontalHeaderLabels({tr("resource"), tr("value (gold)")});
        m_resources->setRowCount(m_content.resources().size());
        int r = 0;
        for (const ResourceDef& res : m_content.resources()) {
            auto* name = new QTableWidgetItem(res.key);
            name->setFlags(name->flags() & ~Qt::ItemIsEditable);
            m_resources->setItem(r, 0, name);
            auto* value = new QTableWidgetItem();
            value->setData(Qt::EditRole, res.value);
            if (res.isCurrency) value->setFlags(value->flags() & ~Qt::ItemIsEditable);
            m_resources->setItem(r, 1, value);
            ++r;
        }
        m_resources->resizeColumnsToContents();
        m_filling = false;
    }

    void onResourceEdited(int row, int col) {
        if (m_filling || col != 1) return;
        const ResourceId id = m_content.resourceId(m_resources->item(row, 0)->text());
        const ResourceDef* old = id.isValid() ? &m_content.resource(id) : nullptr;
        if (!old || old->isCurrency) return;
        ResourceDef r = *old;
        r.value = std::max(1, m_resources->item(row, 1)->data(Qt::EditRole).toInt());
        m_content.replaceResource(r);
        m_changedResources = true;
        fillUnits();
        fillRaces();
        statusBar()->showMessage(tr("Resource values changed (not saved yet)"));
    }

    void onEdited(int row, int col) {
        if (m_filling) return;
        const int c = col - 2;
        if (c < 0 || c >= columns().size()) return;
        const QString id = m_units->item(row, 0)->text();
        const UnitDef* old = m_content.unitByKey(id);
        if (!old) return;
        UnitDef u = *old;
        columns()[c].set(u, m_units->item(row, col)->data(Qt::EditRole).toInt());
        if (u.damageMin > u.damageMax) std::swap(u.damageMin, u.damageMax);
        m_content.replaceUnit(u);
        m_changed.insert(id);
        fillUnits();
        fillRaces();
        statusBar()->showMessage(tr("%1 changed (not saved yet)").arg(m_changed.size()));
    }

    void runDuels() {
        statusBar()->showMessage(tr("Running duels..."));
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QList<DuelRow> rows = duelRows(m_content, m_tier->value(), m_gold->value(), m_runs->value());
        QApplication::restoreOverrideCursor();
        m_duels->clear();
        m_duels->setColumnCount(4);
        m_duels->setHorizontalHeaderLabels({tr("unit"), tr("against"), tr("wins"), tr("note")});
        m_duels->setRowCount(rows.size());
        for (int i = 0; i < rows.size(); ++i) {
            const DuelRow& r = rows[i];
            const QStringList cells{r.a, r.b, QString::number(r.share * 100, 'f', 0) + "%", r.note};
            for (int c = 0; c < cells.size(); ++c) {
                auto* item = new QTableWidgetItem(cells[c]);
                if (!r.note.isEmpty()) item->setForeground(QColor("#b00000"));
                m_duels->setItem(i, c, item);
            }
        }
        m_duels->resizeColumnsToContents();
        statusBar()->showMessage(tr("%1 pairs").arg(rows.size()));
    }

    void saveChanges() {
        int saved = 0;
        for (const QString& id : m_changed) {
            const UnitDef* u = m_content.unitByKey(id);
            QFile f(QDir(m_dataDir).filePath(QStringLiteral("units/%1.json").arg(id)));
            if (!u || !f.open(QIODevice::ReadOnly)) continue;
            QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            f.close();
            for (const Column& c : columns()) {
                const QString field = QString::fromLatin1(c.field);
                if (field == QLatin1String("cost.gold")) {
                    QJsonObject cost = o.value("cost").toObject();
                    cost.insert("gold", c.get(*u));
                    o.insert("cost", cost);
                } else if (field != QLatin1String("initiative") || o.contains("initiative") || u->initiative != u->battleSpeed) {
                    o.insert(field, c.get(*u));
                }
            }
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) continue;
            f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
            ++saved;
        }
        if (m_changedResources) {
            QFile f(QDir(m_dataDir).filePath(QStringLiteral("resources.json")));
            if (f.open(QIODevice::ReadOnly)) {
                QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
                f.close();
                QJsonArray list = root.value("resources").toArray();
                for (int i = 0; i < list.size(); ++i) {
                    QJsonObject o = list[i].toObject();
                    if (const ResourceId id = m_content.resourceId(o.value("id").toString()); id.isValid() && !m_content.resource(id).isCurrency)
                        o.insert("value", m_content.resource(id).value);
                    list[i] = o;
                }
                root.insert("resources", list);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
                    ++saved;
                }
            }
        }
        m_changed.clear();
        statusBar()->showMessage(tr("Saved %1 file(s)").arg(saved));
        load();  // the game's own checks run again
    }

    QString m_dataDir;
    ContentRegistry m_content;
    QSet<QString> m_changed;
    bool m_filling = false;
    bool m_changedResources = false;
    QTableWidget* m_resources;
    QTableWidget* m_units;
    QTableWidget* m_races;
    QTableWidget* m_duels;
    QSpinBox* m_tier;
    QSpinBox* m_gold;
    QSpinBox* m_runs;
};

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("kot_balance_gui"));
    QString explicitDir;
    const QStringList args = app.arguments();
    const int i = args.indexOf(QStringLiteral("--data"));
    if (i >= 0 && i + 1 < args.size()) explicitDir = args[i + 1];
    const QString dir = findDataDirectory(explicitDir);
    if (dir.isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("kot_balance_gui"), QStringLiteral("Cannot find the game data. Use --data <dir>."));
        return 1;
    }
    BalanceWindow w(dir);
    w.show();
    return app.exec();
}
