// kot_editor: the scenario (map) editor.
//
// Paint terrain, place towns, mines, dwellings, treasures, artifacts, monsters and the
// players' starting heroes, set up the player slots, and save it as a scenario
// (data/scenarios/<id>.json) that the lobby offers as a map. A random map can be the
// starting point. kot_editor [--data dir] [scenario.json]

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include "core/content/ContentRegistry.h"
#include "core/data/DataDir.h"
#include "core/rules/GameSetup.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Scenario.h"

using namespace kot::core;

namespace {

const QStringList kColours = {"#d62728", "#1f77b4", "#2ca02c", "#ff7f0e", "#9467bd", "#8c564b", "#e377c2", "#17becf"};

// The map, drawn straight from the game state; clicks and drags call `onTile`.
class EditorMap : public QWidget {
public:
    EditorMap(const ContentRegistry& content, GameState& state, QWidget* parent)
        : QWidget(parent), m_content(content), m_state(state) {
        setMouseTracking(true);
    }
    int tile = 12;
    std::function<void(Pos, bool drag)> onTile;
    std::function<void(Pos)> onHover;

    void refresh() {
        setFixedSize(m_state.map().width() * tile, m_state.map().height() * tile);
        update();
    }

protected:
    void paintEvent(QPaintEvent* e) override {
        QPainter p(this);
        const QRect area = e->rect();
        const GameMap& map = m_state.map();
        for (int y = area.top() / tile; y <= std::min(map.height() - 1, area.bottom() / tile); ++y)
            for (int x = area.left() / tile; x <= std::min(map.width() - 1, area.right() / tile); ++x) {
                const TerrainDef* t = m_content.terrainType(map.terrainAt({x, y}));
                p.fillRect(x * tile, y * tile, tile, tile, t ? QColor(t->color) : QColor(Qt::magenta));
            }
        p.setRenderHint(QPainter::Antialiasing);
        const auto rect = [this](Pos pos, int m) { return QRect(pos.x * tile + m, pos.y * tile + m, tile - 2 * m, tile - 2 * m); };
        const auto colour = [this](PlayerId owner) {
            const Player* pl = m_state.player(owner);
            return pl ? QColor(pl->color) : QColor("#999999");
        };
        for (const Mine& m : m_state.mines()) {
            p.setPen(QPen(Qt::black, 1));
            p.setBrush(colour(m.owner));
            const QRect r = rect(m.pos, 2);
            p.drawPolygon(QPolygon({QPoint(r.center().x(), r.top()), QPoint(r.right(), r.center().y()),
                                    QPoint(r.center().x(), r.bottom()), QPoint(r.left(), r.center().y())}));
        }
        for (const MapDwelling& d : m_state.mapDwellings()) {
            p.setPen(QPen(Qt::black, 1));
            p.setBrush(Qt::white);
            const QRect r = rect(d.pos, 2);
            p.drawPolygon(QPolygon({QPoint(r.center().x(), r.top()), r.bottomRight(), r.bottomLeft()}));
        }
        for (const MapObject& o : m_state.mapObjects()) {
            p.setPen(QPen(Qt::black, 1));
            p.setBrush(o.kind == MapObject::Kind::Artifact ? QColor("#b04de0") : QColor("#d4a017"));
            p.drawRect(rect(o.pos, 3));
        }
        for (const Town& t : m_state.towns()) {
            p.setPen(QPen(Qt::black, 2));
            p.setBrush(colour(t.owner));
            p.drawRect(rect(t.pos, 0));
        }
        QSet<ArmyId> garrisons;
        for (const Town& t : m_state.towns()) garrisons.insert(t.garrison);
        for (const Army& a : m_state.armies()) {
            if (garrisons.contains(a.id)) continue;
            p.setPen(QPen(a.hasHero() ? Qt::white : Qt::black, 2));
            p.setBrush(a.hasHero() ? colour(a.owner) : QColor("#8b1a1a"));
            if (a.hasHero()) p.drawEllipse(rect(a.pos, 2));
            else p.drawRoundedRect(rect(a.pos, 2), 2, 2);
        }
    }
    void mousePressEvent(QMouseEvent* e) override { emitTile(e, false); }
    void mouseMoveEvent(QMouseEvent* e) override {
        const Pos t{int(e->position().x()) / tile, int(e->position().y()) / tile};
        if (onHover) onHover(t);
        if (e->buttons() & Qt::LeftButton) emitTile(e, true);
    }

private:
    void emitTile(QMouseEvent* e, bool drag) {
        const Pos t{int(e->position().x()) / tile, int(e->position().y()) / tile};
        if (m_state.map().inBounds(t) && onTile) onTile(t, drag);
    }
    const ContentRegistry& m_content;
    GameState& m_state;
};

class EditorWindow : public QMainWindow {
public:
    EditorWindow(const ContentRegistry& content) : m_content(content) {
        resize(1300, 850);
        auto* scroll = new QScrollArea(this);
        m_map = new EditorMap(content, m_state, scroll);
        scroll->setWidget(m_map);
        setCentralWidget(scroll);
        m_map->onTile = [this](Pos p, bool drag) { apply(p, drag); };
        m_map->onHover = [this](Pos p) { hover(p); };

        auto* bar = addToolBar(tr("File"));
        bar->addAction(tr("New..."), this, [this]() { newMap(); });
        bar->addAction(tr("Random map..."), this, [this]() { randomMap(); });
        bar->addAction(tr("Open..."), this, [this]() { open(); });
        bar->addAction(tr("Save"), this, [this]() { save(false); });
        bar->addAction(tr("Save as..."), this, [this]() { save(true); });
        bar->addSeparator();
        bar->addWidget(new QLabel(tr(" Zoom "), this));
        auto* zoom = new QSpinBox(this);
        zoom->setRange(4, 40);
        zoom->setValue(m_map->tile);
        bar->addWidget(zoom);
        connect(zoom, &QSpinBox::valueChanged, this, [this](int v) {
            m_map->tile = v;
            m_map->refresh();
        });

        auto* dock = new QWidget(this);
        auto* side = new QVBoxLayout(dock);
        auto* form = new QFormLayout();
        m_id = new QLineEdit(dock);
        m_name = new QLineEdit(dock);
        m_description = new QLineEdit(dock);
        form->addRow(tr("Id (file name)"), m_id);
        form->addRow(tr("Name"), m_name);
        form->addRow(tr("Description"), m_description);
        side->addLayout(form);

        side->addWidget(new QLabel(tr("<b>Tool</b>"), dock));
        m_tool = new QComboBox(dock);
        for (const char* t : {"Terrain", "Town", "Mine", "Dwelling", "Treasure", "Artifact", "Monsters", "Hero (start)", "Erase"})
            m_tool->addItem(tr(t));
        side->addWidget(m_tool);
        auto* opts = new QFormLayout();
        m_terrain = new QComboBox(dock);
        for (const TerrainDef& t : content.terrain()) m_terrain->addItem(t.name, t.id);
        m_brush = new QSpinBox(dock);
        m_brush->setRange(1, 9);
        m_brush->setSingleStep(2);
        m_owner = new QComboBox(dock);
        m_race = new QComboBox(dock);
        for (const QString& r : content.raceIds()) m_race->addItem(content.race(r).name.isEmpty() ? r : content.race(r).name, r);
        m_resource = new QComboBox(dock);
        for (const ResourceDef& r : content.resources()) m_resource->addItem(r.name, r.id);
        m_amount = new QSpinBox(dock);
        m_amount->setRange(1, 100000);
        m_amount->setValue(10);
        m_unit = new QComboBox(dock);
        for (const UnitDef& u : content.units()) m_unit->addItem(QStringLiteral("%1 (%2)").arg(u.name, u.race), u.id);
        m_artifact = new QComboBox(dock);
        for (const ArtifactDef& a : content.artifacts()) m_artifact->addItem(a.name, a.id);
        m_heroClass = new QComboBox(dock);
        for (const HeroClassDef& c : content.heroClasses()) m_heroClass->addItem(QStringLiteral("%1 (%2)").arg(c.name, c.race), c.id);
        opts->addRow(tr("Terrain"), m_terrain);
        opts->addRow(tr("Brush size"), m_brush);
        opts->addRow(tr("Owner"), m_owner);
        opts->addRow(tr("Town race"), m_race);
        opts->addRow(tr("Resource"), m_resource);
        opts->addRow(tr("Amount / count"), m_amount);
        opts->addRow(tr("Unit"), m_unit);
        opts->addRow(tr("Artifact"), m_artifact);
        opts->addRow(tr("Hero class"), m_heroClass);
        side->addLayout(opts);
        auto* help = new QLabel(tr("Mines: amount = daily output. Treasure: amount of the resource. "
                                   "Monsters and dwellings: count = units. Placing on an object replaces it."), dock);
        help->setWordWrap(true);
        side->addWidget(help);

        side->addWidget(new QLabel(tr("<b>Player slots</b> (turn order)"), dock));
        m_players = new QListWidget(dock);
        m_players->setMaximumHeight(140);
        side->addWidget(m_players);
        auto* prow = new QHBoxLayout();
        auto* addPlayer = new QPushButton(tr("Add player (race above)"), dock);
        auto* removePlayer = new QPushButton(tr("Remove selected"), dock);
        prow->addWidget(addPlayer);
        prow->addWidget(removePlayer);
        side->addLayout(prow);
        connect(addPlayer, &QPushButton::clicked, this, [this]() { addSlot(m_race->currentData().toString()); });
        connect(removePlayer, &QPushButton::clicked, this, [this]() { removeSlot(); });
        side->addStretch(1);
        auto* dockScroll = new QScrollArea(this);
        dockScroll->setWidget(dock);
        dockScroll->setWidgetResizable(true);
        dockScroll->setMinimumWidth(340);
        dockScroll->setMaximumWidth(420);
        auto* d = new QDockWidget(tr("Editor"), this);
        d->setWidget(dockScroll);
        d->setFeatures(QDockWidget::NoDockWidgetFeatures);
        addDockWidget(Qt::RightDockWidgetArea, d);

        blank(48, 48, QStringLiteral("grass"));
    }

    void openFile(const QString& path) {
        Scenario s;
        QStringList errors;
        if (!loadScenario(path, m_content, &s, &errors)) {
            QMessageBox::warning(this, tr("Open"), errors.join('\n'));
            return;
        }
        GameState gs;
        GameState::fromJson(s.game, m_content, &gs, &errors);
        m_state = std::move(gs);
        m_path = path;
        m_id->setText(s.info.id);
        m_name->setText(s.info.name);
        m_description->setText(s.info.description);
        changed();
    }

private:
    void blank(int w, int h, const QString& terrain) {
        m_state = GameState(1);
        m_state.setMap(GameMap(w, h, terrain));
        m_path.clear();
        changed();
    }

    void changed() {
        m_map->refresh();
        const QString keep = m_owner->currentData().toString();
        m_owner->clear();
        m_owner->addItem(tr("neutral"), 0);
        m_players->clear();
        int i = 1;
        for (const PlayerId p : m_state.turnOrder()) {
            const Player* pl = m_state.player(p);
            m_owner->addItem(QStringLiteral("%1 (%2)").arg(i).arg(pl->race), int(p.value()));
            int towns = 0, heroes = 0;
            for (const Town& t : m_state.towns()) towns += t.owner == p;
            for (const Hero& h : m_state.heroes()) heroes += h.owner == p;
            auto* item = new QListWidgetItem(tr("Player %1: %2 - %3 town(s), %4 hero(es)").arg(i).arg(pl->race).arg(towns).arg(heroes), m_players);
            item->setForeground(QColor(pl->color));
            item->setData(Qt::UserRole, int(p.value()));
            ++i;
        }
        m_owner->setCurrentIndex(std::max(0, m_owner->findData(keep.toInt())));
        setWindowTitle(tr("Killers of Time - map editor - %1").arg(m_path.isEmpty() ? tr("(new)") : m_path));
    }

    PlayerId owner() const { return PlayerId(quint32(m_owner->currentData().toInt())); }

    void addSlot(const QString& race) {
        const int n = static_cast<int>(m_state.turnOrder().size());
        m_state.addPlayer(tr("Player %1").arg(n + 1), m_state.newTeam(), Player::Controller::Human, race, kColours[n % kColours.size()]);
        m_state.player(m_state.turnOrder().last())->resources = m_content.balance().economy.startingResources;
        changed();
        m_owner->setCurrentIndex(m_owner->count() - 1);
    }

    void removeSlot() {
        auto* item = m_players->currentItem();
        if (!item) return;
        const PlayerId p(quint32(item->data(Qt::UserRole).toInt()));
        for (Town& t : m_state.towns())
            if (t.owner == p) {
                t.owner = PlayerId();
                if (Army* g = m_state.army(t.garrison)) g->owner = PlayerId();
            }
        for (Mine& m : m_state.mines())
            if (m.owner == p) m.owner = PlayerId();
        for (const Hero& h : QList<Hero>(m_state.heroes().values()))
            if (h.owner == p) {
                m_state.armies().remove(h.army);
                m_state.heroes().remove(h.id);
            }
        QList<PlayerId> order = m_state.turnOrder();
        order.removeAll(p);
        m_state.players().remove(p);
        m_state.setTurnOrder(order);
        changed();
    }

    // Removes whatever object stands on the tile (a town with its garrison, a hero
    // with his army, ...).
    void erase(Pos p) {
        for (const Town& t : QList<Town>(m_state.towns().values()))
            if (t.pos == p) {
                m_state.armies().remove(t.garrison);
                m_state.towns().remove(t.id);
            }
        for (const Hero& h : QList<Hero>(m_state.heroes().values()))
            if (m_state.army(h.army) && m_state.army(h.army)->pos == p) {
                m_state.armies().remove(h.army);
                m_state.heroes().remove(h.id);
            }
        for (const Army& a : QList<Army>(m_state.armies().values()))
            if (a.pos == p && !a.hasHero()) {
                bool garrison = false;
                for (const Town& t : m_state.towns()) garrison = garrison || t.garrison == a.id;
                if (!garrison) m_state.armies().remove(a.id);
            }
        for (const Mine& m : QList<Mine>(m_state.mines().values()))
            if (m.pos == p) m_state.mines().remove(m.id);
        for (const MapDwelling& d : QList<MapDwelling>(m_state.mapDwellings().values()))
            if (d.pos == p) m_state.mapDwellings().remove(d.id);
        for (const MapObject& o : QList<MapObject>(m_state.mapObjects().values()))
            if (o.pos == p) m_state.mapObjects().remove(o.id);
    }

    void apply(Pos p, bool drag) {
        const int tool = m_tool->currentIndex();
        if (tool != 0 && drag) return;  // only terrain paints while dragging
        switch (tool) {
        case 0: {  // terrain
            const int r = m_brush->value() / 2;
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx)
                    if (m_state.map().inBounds({p.x + dx, p.y + dy}))
                        m_state.map().setTerrain({p.x + dx, p.y + dy}, m_terrain->currentData().toString());
            m_map->update();
            return;
        }
        case 1: {  // town
            erase(p);
            const PlayerId o = owner();
            const QString race = o.isValid() ? m_state.player(o)->race : m_race->currentData().toString();
            const Player* pl = m_state.player(o);
            m_state.addTown(pl ? pl->name + QStringLiteral("'s capital") : tr("Town %1").arg(m_state.towns().size() + 1), race, p, o);
            break;
        }
        case 2:
            erase(p);
            m_state.addMine(m_resource->currentData().toString(), m_amount->value(), p, owner());
            break;
        case 3:
            erase(p);
            m_state.addMapDwelling(m_unit->currentData().toString(), p, m_amount->value(), owner());
            break;
        case 4: {
            erase(p);
            MapObject o;
            o.kind = MapObject::Kind::Treasure;
            o.pos = p;
            o.resources[m_resource->currentData().toString()] = m_amount->value();
            m_state.addMapObject(o);
            break;
        }
        case 5: {
            erase(p);
            MapObject o;
            o.kind = MapObject::Kind::Artifact;
            o.pos = p;
            o.artifact = m_artifact->currentData().toString();
            m_state.addMapObject(o);
            break;
        }
        case 6: {
            erase(p);
            const UnitDef* u = m_content.unit(m_unit->currentData().toString());
            if (u) m_state.army(m_state.addArmy(PlayerId(), p))->addUnits(u->id, m_amount->value(), u->maxHp);
            break;
        }
        case 7: {  // a player's starting hero with his class's army
            const PlayerId o = owner();
            if (!o.isValid()) {
                statusBar()->showMessage(tr("Choose a player as owner for a hero"), 4000);
                return;
            }
            erase(p);
            QString error;
            const HeroId h = createHero(m_state, m_content, o, tr("Hero"), m_heroClass->currentData().toString(), p, &error);
            if (!h.isValid()) {
                statusBar()->showMessage(error, 4000);
                return;
            }
            const HeroClassDef* cls = m_content.heroClass(m_state.hero(h)->classId);
            for (auto it = cls->startingArmy.begin(); it != cls->startingArmy.end(); ++it)
                if (const UnitDef* u = m_content.unit(it.key())) m_state.army(m_state.hero(h)->army)->addUnits(u->id, it.value(), u->maxHp);
            break;
        }
        case 8:
            erase(p);
            break;
        }
        changed();
    }

    void hover(Pos p) {
        if (!m_state.map().inBounds(p)) return;
        const TerrainDef* t = m_content.terrainType(m_state.map().terrainAt(p));
        statusBar()->showMessage(QStringLiteral("(%1, %2) %3").arg(p.x).arg(p.y).arg(t ? t->name : QString()));
    }

    void newMap() {
        QDialog d(this);
        auto* f = new QFormLayout(&d);
        auto* w = new QSpinBox(&d);
        auto* h = new QSpinBox(&d);
        w->setRange(16, 256);
        h->setRange(16, 256);
        w->setValue(48);
        h->setValue(48);
        auto* terrain = new QComboBox(&d);
        for (const TerrainDef& t : m_content.terrain()) terrain->addItem(t.name, t.id);
        f->addRow(tr("Width"), w);
        f->addRow(tr("Height"), h);
        f->addRow(tr("Terrain"), terrain);
        auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
        f->addRow(b);
        connect(b, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(b, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (d.exec() == QDialog::Accepted) blank(w->value(), h->value(), terrain->currentData().toString());
    }

    void randomMap() {
        QDialog d(this);
        auto* f = new QFormLayout(&d);
        auto* w = new QSpinBox(&d);
        auto* h = new QSpinBox(&d);
        w->setRange(16, 256);
        h->setRange(16, 256);
        w->setValue(48);
        h->setValue(48);
        auto* seed = new QSpinBox(&d);
        seed->setRange(1, 1000000);
        auto* races = new QLineEdit(m_content.raceIds().join(','), &d);
        f->addRow(tr("Width"), w);
        f->addRow(tr("Height"), h);
        f->addRow(tr("Seed"), seed);
        f->addRow(tr("Player races (comma separated)"), races);
        auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
        f->addRow(b);
        connect(b, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(b, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (d.exec() != QDialog::Accepted) return;
        GameSetupParams params;
        params.mapWidth = w->value();
        params.mapHeight = h->value();
        params.seed = seed->value();
        const QStringList list = races->text().split(',', Qt::SkipEmptyParts);
        for (int i = 0; i < list.size(); ++i)
            params.players.append({tr("Player %1").arg(i + 1), false, {}, list[i].trimmed(), i + 1, kColours[i % kColours.size()]});
        GameState gs;
        QString error;
        if (!createGame(m_content, params, &gs, nullptr, &error)) {
            QMessageBox::warning(this, tr("Random map"), error);
            return;
        }
        m_state = std::move(gs);
        m_path.clear();
        changed();
    }

    void open() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Open scenario"), QDir(m_content.dataDir()).filePath("scenarios"),
                                                          tr("Scenarios (*.json)"));
        if (!path.isEmpty()) openFile(path);
    }

    void save(bool ask) {
        // Every slot needs something to play with.
        QStringList problems;
        if (m_state.turnOrder().size() < 2) problems << tr("A scenario needs at least two player slots.");
        for (const PlayerId p : m_state.turnOrder()) {
            bool has = false;
            for (const Town& t : m_state.towns()) has = has || t.owner == p;
            for (const Hero& h : m_state.heroes()) has = has || h.owner == p;
            if (!has) problems << tr("%1 has no town and no hero.").arg(m_state.player(p)->name);
        }
        if (m_id->text().isEmpty()) problems << tr("Give the scenario an id.");
        if (!problems.isEmpty()) {
            QMessageBox::warning(this, tr("Save"), problems.join('\n'));
            return;
        }
        QString path = m_path;
        if (ask || path.isEmpty()) {
            path = QFileDialog::getSaveFileName(this, tr("Save scenario"),
                                                QDir(m_content.dataDir()).filePath("scenarios/" + m_id->text() + ".json"),
                                                tr("Scenarios (*.json)"));
            if (path.isEmpty()) return;
        }
        m_state.setCurrentIndex(0);
        QString error;
        if (!saveScenario(path, m_id->text(), m_name->text().isEmpty() ? m_id->text() : m_name->text(), m_description->text(),
                          m_state, &error)) {
            QMessageBox::warning(this, tr("Save"), error);
            return;
        }
        // Check it the way the game will read it.
        Scenario s;
        QStringList errors;
        if (!loadScenario(path, m_content, &s, &errors)) {
            QMessageBox::warning(this, tr("Saved, but the game cannot use it"), errors.join('\n'));
            return;
        }
        m_path = path;
        changed();
        statusBar()->showMessage(tr("Saved %1").arg(path), 5000);
    }

    const ContentRegistry& m_content;
    GameState m_state;
    QString m_path;
    EditorMap* m_map;
    QLineEdit *m_id, *m_name, *m_description;
    QComboBox *m_tool, *m_terrain, *m_owner, *m_race, *m_resource, *m_unit, *m_artifact, *m_heroClass;
    QSpinBox *m_brush, *m_amount;
    QListWidget* m_players;
};

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("kot_editor"));
    QStringList args = app.arguments();
    QString dataArg;
    const int i = args.indexOf(QStringLiteral("--data"));
    if (i >= 0 && i + 1 < args.size()) {
        dataArg = args[i + 1];
        args.remove(i, 2);
    }
    const QString dir = findDataDirectory(dataArg);
    ContentRegistry content;
    QStringList errors;
    if (dir.isEmpty() || !content.loadFromDirectory(dir, &errors)) {
        QMessageBox::critical(nullptr, QStringLiteral("kot_editor"), QStringLiteral("Cannot load the game data:\n") + errors.join('\n'));
        return 1;
    }
    EditorWindow w(content);
    if (args.size() > 1) w.openFile(args[1]);
    w.show();
    // For documentation and tests: KOT_SCREENSHOT=file.png saves the window and quits.
    const QString shot = qEnvironmentVariable("KOT_SCREENSHOT");
    if (!shot.isEmpty())
        QTimer::singleShot(300, &w, [&w, shot]() {
            w.grab().save(shot);
            QApplication::quit();
        });
    return app.exec();
}
