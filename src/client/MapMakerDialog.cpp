#include "client/MapMakerDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "client/Art.h"

namespace kot::client {

namespace {
QString idFrom(const QString& name) {
    QString id = name.trimmed().toLower();
    id.replace(QRegularExpression(QStringLiteral("[^a-z0-9_-]+")), QStringLiteral("_"));
    id = id.left(40);
    return id.isEmpty() ? QStringLiteral("random_map") : id;
}
QSpinBox* spin(QWidget* parent, int lo, int hi, int value, int step = 1) {
    auto* s = new QSpinBox(parent);
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setValue(value);
    return s;
}
QComboBox* combo(QWidget* parent, const QStringList& items, int current) {
    auto* c = new QComboBox(parent);
    c->addItems(items);
    c->setCurrentIndex(current);
    return c;
}
}  // namespace

MapMakerDialog::MapMakerDialog(const QStringList& races, QWidget* parent) : QDialog(parent), m_races(races) {
    setObjectName("mapMaker");
    setWindowTitle(tr("Make a map"));
    resize(560, 620);
    auto* layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    layout->addWidget(m_tabs, 1);
    const int seed = int(QRandomGenerator::global()->bounded(1, 999999));

    // ---- a scenario
    auto* page = new QWidget();
    auto* form = new QFormLayout(page);
    m_name = new QLineEdit(tr("My map"), page);
    m_name->setObjectName("mapName");
    form->addRow(tr("Name"), m_name);
    m_size = new QComboBox(page);
    for (const auto& [name, side] : {std::pair{tr("Small"), 40}, std::pair{tr("Medium"), 64}, std::pair{tr("Large"), 96},
                                     std::pair{tr("Extra large"), 128}, std::pair{tr("Huge"), 160}})
        m_size->addItem(tr("%1 (%2x%2)").arg(name).arg(side), side);
    m_size->setCurrentIndex(1);
    form->addRow(tr("Map size"), m_size);
    m_type = combo(page, {tr("Continent"), tr("Islands (boats needed)")}, 0);
    form->addRow(tr("Land"), m_type);
    m_players = spin(page, 2, 8, 3);
    m_players->setObjectName("mapPlayers");
    form->addRow(tr("Players"), m_players);
    auto* raceHolder = new QWidget(page);
    m_raceBox = new QVBoxLayout(raceHolder);
    m_raceBox->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("Races"), raceHolder);
    m_water = combo(page, {tr("Little"), tr("Normal"), tr("Much")}, 1);
    form->addRow(tr("Water"), m_water);
    m_mountains = combo(page, {tr("Few"), tr("Normal"), tr("Many")}, 1);
    form->addRow(tr("Mountains"), m_mountains);
    m_rivers = combo(page, {tr("None"), tr("A few"), tr("Normal"), tr("Many")}, 2);
    form->addRow(tr("Rivers"), m_rivers);
    m_roads = new QCheckBox(tr("Roads between the towns"), page);
    m_roads->setChecked(true);
    form->addRow(QString(), m_roads);
    m_monsters = combo(page, {tr("Weak"), tr("Normal"), tr("Strong"), tr("Very strong")}, 1);
    form->addRow(tr("Monsters"), m_monsters);
    m_goal = combo(page, {tr("Defeat everyone"), tr("Take the enemy capital"), tr("Collect gold"), tr("Most towns after a number of days")}, 0);
    m_goalAmount = spin(page, 10, 1000000, 90);
    m_goalAmount->setVisible(false);
    auto* goalRow = new QHBoxLayout();
    goalRow->addWidget(m_goal, 1);
    goalRow->addWidget(m_goalAmount);
    form->addRow(tr("Goal"), goalRow);
    connect(m_goal, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_goalAmount->setVisible(i >= 2);
        m_goalAmount->setValue(i == 2 ? 50000 : 90);
        m_goalAmount->setSuffix(i == 2 ? tr(" gold") : tr(" days"));
    });
    m_seed = spin(page, 1, 999999, seed);
    auto* seedRow = new QHBoxLayout();
    seedRow->addWidget(m_seed, 1);
    auto* dice = new QPushButton(tr("New"), page);
    connect(dice, &QPushButton::clicked, this, [this]() { m_seed->setValue(int(QRandomGenerator::global()->bounded(1, 999999))); });
    seedRow->addWidget(dice);
    form->addRow(tr("Seed"), seedRow);
    m_tabs->addTab(page, art::buttonIcon("town"), tr("A scenario"));
    connect(m_players, &QSpinBox::valueChanged, this, &MapMakerDialog::rebuildPlayers);
    rebuildPlayers();

    // ---- a campaign
    auto* cpage = new QWidget();
    auto* cform = new QFormLayout(cpage);
    m_campaignName = new QLineEdit(tr("My campaign"), cpage);
    m_campaignName->setObjectName("campaignName");
    cform->addRow(tr("Name"), m_campaignName);
    m_chapters = spin(cpage, 2, 6, 3);
    cform->addRow(tr("Chapters"), m_chapters);
    m_campaignRace = new QComboBox(cpage);
    m_campaignRace->addItem(tr("Random"), QStringLiteral("random"));
    for (const QString& r : races) m_campaignRace->addItem(r, r);
    cform->addRow(tr("Your race"), m_campaignRace);
    m_firstSize = spin(cpage, 32, 112, 48, 8);
    m_firstSize->setSuffix(tr(" tiles (each chapter 16 more)"));
    cform->addRow(tr("First map"), m_firstSize);
    m_carryHeroes = spin(cpage, 0, 4, 1);
    cform->addRow(tr("Heroes carried over"), m_carryHeroes);
    m_carryArmy = new QCheckBox(tr("Their armies go with them"), cpage);
    m_carryArmy->setChecked(true);
    cform->addRow(QString(), m_carryArmy);
    m_campaignIslands = new QCheckBox(tr("Island maps (boats needed)"), cpage);
    cform->addRow(QString(), m_campaignIslands);
    m_campaignSeed = spin(cpage, 1, 999999, seed);
    cform->addRow(tr("Seed"), m_campaignSeed);
    cform->addRow(new QLabel(tr("<i>Every chapter is bigger, with one more enemy, stronger monsters and another goal; the last one is conquest.</i>"), cpage));
    m_tabs->addTab(cpage, art::buttonIcon("army"), tr("A campaign"));

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    auto* make = new QPushButton(tr("Make it"), this);
    make->setObjectName("mapMakeButton");
    make->setDefault(true);
    for (QPushButton* b : {cancel, make}) {
        b->setMinimumSize(120, 38);
        buttons->addWidget(b);
    }
    layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(make, &QPushButton::clicked, this, &QDialog::accept);
}

void MapMakerDialog::rebuildPlayers() {
    // One race choice per player (the first is yours).
    while (m_raceCombos.size() > m_players->value()) delete m_raceCombos.takeLast();
    while (m_raceCombos.size() < m_players->value()) {
        auto* c = new QComboBox();
        c->addItem(tr("Random"), QStringLiteral("random"));
        for (const QString& r : m_races) c->addItem(r, r);
        m_raceBox->addWidget(c);
        m_raceCombos << c;
    }
    for (int i = 0; i < m_raceCombos.size(); ++i) m_raceCombos[i]->setToolTip(i == 0 ? tr("Player 1 (you)") : tr("Player %1").arg(i + 1));
}

QJsonObject MapMakerDialog::params() const {
    if (m_tabs->currentIndex() == 1) {
        return {{"kind", "campaign"}, {"id", idFrom(m_campaignName->text())}, {"name", m_campaignName->text().trimmed()},
                {"chapters", m_chapters->value()}, {"race", m_campaignRace->currentData().toString()}, {"size", m_firstSize->value()},
                {"carryHeroes", m_carryHeroes->value()}, {"carryArmy", m_carryArmy->isChecked()},
                {"islands", m_campaignIslands->isChecked()}, {"seed", m_campaignSeed->value()}};
    }
    QJsonArray races;
    for (QComboBox* c : m_raceCombos) races.append(c->currentData().toString());
    static const double water[] = {0.18, 0.32, 0.48};
    static const double mountains[] = {0.02, 0.06, 0.12};
    static const int rivers[] = {0, 1, -1, 8};
    static const int monsters[] = {60, 100, 150, 220};
    static const char* goals[] = {"conquest", "capital", "gold", "days"};
    return {{"kind", "scenario"}, {"id", idFrom(m_name->text())}, {"name", m_name->text().trimmed()},
            {"width", m_size->currentData().toInt()}, {"height", m_size->currentData().toInt()}, {"islands", m_type->currentIndex() == 1},
            {"races", races}, {"seaLevel", water[m_water->currentIndex()]}, {"mountains", mountains[m_mountains->currentIndex()]},
            {"rivers", rivers[m_rivers->currentIndex()]}, {"roads", m_roads->isChecked()},
            {"monsterPercent", monsters[m_monsters->currentIndex()]}, {"victory", goals[m_goal->currentIndex()]},
            {"victoryAmount", m_goal->currentIndex() >= 2 ? m_goalAmount->value() : 0}, {"seed", m_seed->value()}};
}

}  // namespace kot::client
