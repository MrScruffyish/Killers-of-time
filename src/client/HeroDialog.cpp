#include "client/HeroDialog.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "client/Settings.h"
#include "core/content/ContentRegistry.h"

namespace kot::client {

HeroDialog::HeroDialog(const core::ContentRegistry* content, QWidget* parent) : QDialog(parent), m_content(content) {
    rememberGeometry(this, QStringLiteral("heroDialog"));
    setObjectName("heroDialog");
    resize(620, 560);
    auto* layout = new QVBoxLayout(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    layout->addWidget(scroll);
    m_body = new QWidget(scroll);
    scroll->setWidget(m_body);
    auto* bodyLayout = new QVBoxLayout(m_body);
    bodyLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);  // never squeeze the rows
}

void HeroDialog::showHero(const GameView& view, int heroId) {
    m_heroId = heroId;
    m_view = view;
    show();
    rebuild();
    raise();
}

void HeroDialog::refresh(const GameView& view) {
    m_view = view;
    if (isVisible()) rebuild();
}

void HeroDialog::rebuild() {
    const ViewHero* h = m_view.hero(m_heroId);
    if (!h || !h->own) {
        hide();
        return;
    }
    // Start over: drop the old contents.
    QLayout* layout = m_body->layout();
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    auto* v = static_cast<QVBoxLayout*>(layout);
    const core::HeroClassDef* cls = m_content->heroClass(h->classId);
    setWindowTitle(tr("%1, %2").arg(h->name, cls ? cls->name : h->classId));

    // Level and experience.
    const QList<int>& xp = m_content->xpThresholds();
    const int prev = h->level >= 2 ? xp.value(h->level - 2) : 0;
    const int next = h->level - 1 < xp.size() ? xp[h->level - 1] : 0;
    v->addWidget(new QLabel(tr("<h2>%1</h2>Level %2 %3").arg(h->name).arg(h->level).arg(cls ? cls->name : h->classId), m_body));
    if (next > 0) {
        auto* bar = new QProgressBar(m_body);
        bar->setRange(prev, next);
        bar->setValue(std::clamp(h->experience, prev, next));
        bar->setFormat(tr("%1 / %2 experience to level %3").arg(h->experience).arg(next).arg(h->level + 1));
        v->addWidget(bar);
    } else {
        v->addWidget(new QLabel(tr("Highest level (%1 experience)").arg(h->experience), m_body));
    }
    v->addWidget(new QLabel(tr("Movement %1    Spell points %2 / %3")
                                .arg(h->movementPoints / 100.0, 0, 'f', 1)
                                .arg(h->spellPoints)
                                .arg(h->traits.value("spell_points_max")),
                            m_body));
    if (h->pendingTraitPicks > 0)
        v->addWidget(new QLabel(tr("<b style='color:#ffd84a'>Level up! Choose %1 trait(s) to improve with the + buttons.</b>")
                                    .arg(h->pendingTraitPicks),
                                m_body));

    // Traits by group.
    auto* groups = new QHBoxLayout();
    for (const core::TraitGroup g : {core::TraitGroup::Battle, core::TraitGroup::Magic, core::TraitGroup::Explore}) {
        QString title = core::traitGroupName(g);
        if (!title.isEmpty()) title[0] = title[0].toUpper();
        auto* box = new QGroupBox(title, m_body);
        auto* grid = new QGridLayout(box);
        int row = 0;
        for (const core::TraitDef& t : m_content->traits()) {
            if (t.group != g) continue;
            auto* name = new QLabel(t.name, box);
            name->setToolTip(t.description);
            grid->addWidget(name, row, 0);
            grid->addWidget(new QLabel(QString::number(h->traits.value(t.id)), box), row, 1);
            if (h->pendingTraitPicks > 0 && t.pickable && m_view.isMyTurn()) {
                auto* plus = new QPushButton(QStringLiteral("+"), box);
                plus->setObjectName(QStringLiteral("pick_%1").arg(t.id));
                plus->setFixedWidth(30);
                plus->setToolTip(tr("Improve %1 (+%2)").arg(t.name).arg(cls ? cls->pickIncrement.value(t.id, 1) : 1));
                const QString id = t.id;
                const int hero = h->id;
                connect(plus, &QPushButton::clicked, this, [this, id, hero, name = t.name]() {
                    emit commandRequested("pick_trait", {{"hero", hero}, {"trait", id}}, tr("Improve %1").arg(name));
                });
                grid->addWidget(plus, row, 2);
            }
            ++row;
        }
        grid->setRowStretch(row, 1);  // rows stay at the top
        groups->addWidget(box);
    }
    v->addLayout(groups);

    // Army.
    auto* armyBox = new QGroupBox(tr("Army"), m_body);
    auto* al = new QVBoxLayout(armyBox);
    if (const ViewArmy* a = m_view.army(h->army)) {
        for (const ViewStack& s : a->stacks) {
            const core::UnitDef* u = m_content->unit(s.unit);
            QString text = QStringLiteral("%1 x <b>%2</b>").arg(s.count).arg(u ? u->name : s.unit);
            if (s.stars > 0) text += QStringLiteral(" <span style='color:#ffd84a'>%1</span>").arg(QString(s.stars, QChar(0x2605)));
            if (u)
                text += tr(" &nbsp; <i>att %1 def %2 dmg %3-%4 hp %5 speed %6%7</i>")
                            .arg(u->attack).arg(u->defense).arg(u->damageMin).arg(u->damageMax).arg(u->maxHp).arg(u->battleSpeed)
                            .arg(u->abilities.isEmpty() ? QString() : ", " + u->abilities.join(", "));
            al->addWidget(new QLabel(text, armyBox));
        }
        if (a->stacks.isEmpty()) al->addWidget(new QLabel(tr("No units. Visit a town to take some along."), armyBox));
    }
    v->addWidget(armyBox);

    // Spells and artifacts.
    auto* spellBox = new QGroupBox(tr("Spellbook"), m_body);
    auto* sl = new QVBoxLayout(spellBox);
    for (const QString& id : h->spells) {
        const core::SpellDef* s = m_content->spell(id);
        if (!s) continue;
        const bool canUse = h->traits.value("magic") >= s->requiredMagic;
        sl->addWidget(new QLabel(tr("<b>%1</b> (%2, level %3, %4 SP%5) %6")
                                     .arg(s->name, s->school).arg(s->level).arg(s->cost)
                                     .arg(canUse ? QString() : tr(", needs Magic %1").arg(s->requiredMagic))
                                     .arg(s->description),
                                 spellBox));
    }
    if (h->spells.isEmpty()) sl->addWidget(new QLabel(tr("No spells. A Mage Guild in your town teaches them."), spellBox));
    v->addWidget(spellBox);
    auto* artBox = new QGroupBox(tr("Artifacts"), m_body);
    auto* artl = new QVBoxLayout(artBox);
    for (const QString& id : h->artifacts) {
        const core::ArtifactDef* a = m_content->artifact(id);
        artl->addWidget(new QLabel(a ? tr("<b>%1</b>: %2").arg(a->name, a->description) : id, artBox));
    }
    if (h->artifacts.isEmpty()) artl->addWidget(new QLabel(tr("None yet. Artifacts lie on the map, often guarded."), artBox));
    v->addWidget(artBox);
    v->addStretch(1);
}

}  // namespace kot::client
