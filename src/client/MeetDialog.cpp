#include "client/MeetDialog.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "client/Art.h"
#include "core/content/ContentRegistry.h"
#include "net/Protocol.h"

namespace kot::client {

MeetDialog::MeetDialog(const core::ContentRegistry* content, QWidget* parent) : QDialog(parent), m_content(content) {
    setObjectName("meetDialog");
    setWindowTitle(tr("Heroes meet"));
    auto* outer = new QVBoxLayout(this);
    m_body = new QWidget(this);
    outer->addWidget(m_body, 1);
    auto* close = new QPushButton(tr("Close"), this);
    close->setShortcut(Qt::Key_Escape);
    connect(close, &QPushButton::clicked, this, &QDialog::hide);
    outer->addWidget(close, 0, Qt::AlignRight);
}

void MeetDialog::showMeeting(const GameView& view, int leftHero, int rightHero) {
    m_view = view;
    m_left = leftHero;
    m_right = rightHero;
    rebuild();
    show();
    raise();
    activateWindow();
}

void MeetDialog::refresh(const GameView& view) {
    m_view = view;
    if (isVisible()) rebuild();
}

void MeetDialog::rebuild() {
    const ViewHero* a = m_view.hero(m_left);
    const ViewHero* b = m_view.hero(m_right);
    const auto meet = [](core::Pos p, core::Pos q) { return std::max(std::abs(p.x - q.x), std::abs(p.y - q.y)) <= 1; };
    if (!a || !b || !a->own || !b->own || !meet(a->pos, b->pos)) {
        hide();  // one fell, walked off, or is not ours any more
        return;
    }
    delete m_body->layout();
    qDeleteAll(m_body->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly));
    auto* row = new QHBoxLayout(m_body);
    row->addWidget(column(*a, *b, true), 1);
    row->addWidget(column(*b, *a, false), 1);
}

QWidget* MeetDialog::column(const ViewHero& hero, const ViewHero& other, bool left) {
    const bool canAct = m_view.isMyTurn() && hero.owner == m_view.you;
    const QString arrow = left ? QStringLiteral("→") : QStringLiteral("←");
    auto* box = new QGroupBox(m_body);
    box->setObjectName(left ? "meetLeft" : "meetRight");
    auto* v = new QVBoxLayout(box);
    auto* head = new QHBoxLayout();
    auto* face = new QLabel(box);
    const ViewPlayer* owner = m_view.player(hero.owner);
    face->setPixmap(art::portrait(hero.classId, owner ? QColor(owner->color) : QColor("#999999"), hero.id, 64));
    head->addWidget(face);
    head->addWidget(new QLabel(tr("<b>%1</b><br>level %2").arg(hero.name).arg(hero.level), box), 1);
    v->addLayout(head);

    const ViewArmy* army = m_view.army(hero.army);
    const ViewArmy* to = m_view.army(other.army);
    v->addWidget(new QLabel(tr("<b>Army</b>"), box));
    if (army && to) {
        for (int i = 0; i < army->stacks.size(); ++i) {
            const ViewStack& s = army->stacks[i];
            const core::UnitDef* u = m_content->unit(s.unit);
            auto* line = new QHBoxLayout();
            auto* icon = new QLabel(box);
            icon->setPixmap(art::unitIcon(s.unit, u ? u->rank : 0, 32));
            line->addWidget(icon);
            QString text = QStringLiteral("%1 x %2").arg(s.count).arg(u ? u->name : QString());
            if (s.stars > 0) text += QStringLiteral(" ") + QString(s.stars, QChar(0x2605));
            line->addWidget(new QLabel(text, box), 1);
            auto* count = new QSpinBox(box);
            count->setRange(1, s.count);
            count->setValue(s.count);
            line->addWidget(count);
            auto* give = new QPushButton(arrow, box);
            give->setObjectName(QStringLiteral("meetGive_%1_%2").arg(army->id).arg(i));
            give->setToolTip(tr("Give to %1").arg(other.name));
            give->setEnabled(canAct);
            const int fromId = army->id, toId = to->id, index = i;
            const QString name = u ? u->name : QString();
            connect(give, &QPushButton::clicked, this, [=, this]() {
                emit commandRequested(net::msg::moveUnits, {{"from", fromId}, {"to", toId}, {"stack", index}, {"count", count->value()}},
                                      tr("Give %1 %2 to %3").arg(count->value()).arg(name, other.name));
            });
            line->addWidget(give);
            v->addLayout(line);
        }
        if (army->stacks.isEmpty()) v->addWidget(new QLabel(tr("(no units)"), box));
        auto* all = new QPushButton(left ? tr("All units %1").arg(arrow) : tr("%1 All units").arg(arrow), box);
        all->setObjectName(left ? "meetAllLeft" : "meetAllRight");
        all->setEnabled(canAct && !army->stacks.isEmpty());
        const int fromId = army->id, toId = to->id;
        connect(all, &QPushButton::clicked, this, [=, this]() {
            emit commandRequested(net::msg::moveUnits, {{"from", fromId}, {"to", toId}, {"all", true}},
                                  tr("Give all units to %1").arg(other.name));
        });
        v->addWidget(all);
    }

    v->addWidget(new QLabel(tr("<b>Artifacts</b>"), box));
    QList<core::ArtifactId> gear = hero.artifacts;
    gear += hero.backpack;
    for (int i = 0; i < gear.size(); ++i) {
        const core::ArtifactId id = gear[i];
        const core::ArtifactDef* a = m_content->artifact(id);
        const QString key = a ? a->key : QString();
        auto* line = new QHBoxLayout();
        auto* icon = new QLabel(box);
        icon->setPixmap(art::artifactIcon(key, 28));
        line->addWidget(icon);
        auto* label = new QLabel(QStringLiteral("%1%2").arg(a ? a->name : QString(), i < hero.artifacts.size() ? QString() : tr(" (carried)")), box);
        if (a) label->setToolTip(a->description);
        line->addWidget(label, 1);
        auto* give = new QPushButton(arrow, box);
        give->setObjectName(QStringLiteral("meetArtifact_%1_%2").arg(hero.id).arg(key));
        give->setToolTip(tr("Give to %1").arg(other.name));
        give->setEnabled(canAct);
        const int fromId = hero.id, toId = other.id;
        const QString name = a ? a->name : QString();
        connect(give, &QPushButton::clicked, this, [=, this]() {
            emit commandRequested(net::msg::giveArtifact, {{"from", fromId}, {"to", toId}, {"artifact", id.index()}},
                                  tr("Give the %1 to %2").arg(name, other.name));
        });
        line->addWidget(give);
        v->addLayout(line);
    }
    if (gear.isEmpty()) v->addWidget(new QLabel(tr("(none)"), box));
    v->addStretch(1);
    return box;
}

}  // namespace kot::client
