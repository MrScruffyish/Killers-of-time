#include "client/MarketDialog.h"

#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include "client/Art.h"
#include "client/Settings.h"
#include "core/content/ContentRegistry.h"
#include "core/rules/Trade.h"

namespace kot::client {

MarketDialog::MarketDialog(const core::ContentRegistry* content, QWidget* parent) : QDialog(parent), m_content(content) {
    setObjectName("marketDialog");
    setWindowTitle(tr("Market"));
    resize(560, 420);
    rememberGeometry(this, QStringLiteral("marketDialog"));
    auto* layout = new QVBoxLayout(this);
    m_rate = new QLabel(this);
    layout->addWidget(m_rate);
    auto* lists = new QHBoxLayout();
    const auto column = [&](const QString& title, QListWidget** list, const char* name) {
        auto* v = new QVBoxLayout();
        v->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(title), this));
        *list = new QListWidget(this);
        (*list)->setObjectName(QString::fromLatin1(name));
        (*list)->setIconSize(QSize(28, 28));
        v->addWidget(*list, 1);
        lists->addLayout(v, 1);
    };
    column(tr("Give"), &m_give, "marketGive");
    column(tr("Get"), &m_get, "marketGet");
    layout->addLayout(lists, 1);
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("Give"), this));
    m_amount = new QSlider(Qt::Horizontal, this);
    m_amount->setObjectName("marketAmount");
    m_amount->setRange(0, 0);
    m_amount->setMinimumWidth(240);
    row->addWidget(m_amount, 1);
    m_amountLabel = new QLabel(this);
    m_amountLabel->setMinimumWidth(70);
    m_amountLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(m_amountLabel);
    auto* half = new QPushButton(tr("Half"), this);
    auto* all = new QPushButton(tr("All"), this);
    row->addWidget(half);
    row->addWidget(all);
    layout->addLayout(row);
    m_preview = new QLabel(this);
    m_preview->setObjectName("marketPreview");
    layout->addWidget(m_preview);
    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    m_trade = new QPushButton(tr("Trade"), this);
    m_trade->setObjectName("marketTradeButton");
    auto* close = new QPushButton(tr("Close"), this);
    for (QPushButton* b : {m_trade, close}) {
        b->setMinimumSize(120, 36);
        buttons->addWidget(b);
    }
    layout->addLayout(buttons);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(all, &QPushButton::clicked, this, [this]() { m_amount->setValue(m_amount->maximum()); });
    connect(half, &QPushButton::clicked, this, [this]() { m_amount->setValue(m_amount->maximum() / 2); });
    connect(m_give, &QListWidget::currentRowChanged, this, [this]() {
        // The slider runs from nothing to all you have of it; start at half.
        const int have = m_view.resources.value(chosen(m_give));
        const QSignalBlocker block(m_amount);
        m_amount->setRange(0, std::max(0, have));
        m_amount->setPageStep(std::max(1, have / 10));
        m_amount->setValue(have / 2);
        refreshPreview();
    });
    connect(m_get, &QListWidget::currentRowChanged, this, &MarketDialog::refreshPreview);
    connect(m_amount, &QSlider::valueChanged, this, &MarketDialog::refreshPreview);
    connect(m_trade, &QPushButton::clicked, this, [this]() {
        const core::ResourceId give = chosen(m_give), get = chosen(m_get);
        emit commandRequested("trade", {{"give", give.index()}, {"amount", m_amount->value()}, {"get", get.index()}},
                              tr("Trade %1 %2 for %3").arg(m_amount->value()).arg(keyOf(give), keyOf(get)));
    });
}

core::ResourceId MarketDialog::chosen(const QListWidget* list) const {
    const QListWidgetItem* item = list->currentItem();
    return item ? core::ResourceId(item->data(Qt::UserRole).toInt()) : core::ResourceId();
}

QString MarketDialog::keyOf(core::ResourceId r) const { return r.isValid() ? m_content->resource(r).key : QString(); }

void MarketDialog::setView(const GameView& view) {
    m_view = view;
    const core::ResourceId give = chosen(m_give), get = chosen(m_get);
    for (QListWidget* list : {m_give, m_get}) {
        list->blockSignals(true);
        list->clear();
        for (const core::ResourceDef& r : m_content->resources()) {
            auto* item = new QListWidgetItem(QIcon(art::resourceIcon(r.key, 28)),
                                             list == m_give ? tr("%1  (%2)").arg(r.name).arg(view.resources.value(r.id)) : r.name, list);
            item->setData(Qt::UserRole, r.id.index());
        }
        list->blockSignals(false);
    }
    const auto select = [](QListWidget* list, core::ResourceId id, int fallback) {
        for (int i = 0; i < list->count(); ++i)
            if (id.isValid() && list->item(i)->data(Qt::UserRole).toInt() == id.index()) return list->setCurrentRow(i);
        list->setCurrentRow(std::min(fallback, list->count() - 1));
    };
    select(m_give, give, 1);
    {
        const int have = view.resources.value(chosen(m_give));
        const QSignalBlocker block(m_amount);
        const int keep = m_amount->value();
        m_amount->setRange(0, std::max(0, have));
        m_amount->setPageStep(std::max(1, have / 10));
        m_amount->setValue(std::min(keep, have));
    }
    select(m_get, get, 0);
    m_rate->setText(tr("You get <b>%1%</b> of the fair value (more with Marketplaces in your towns).").arg(view.tradePercent));
    refreshPreview();
}

void MarketDialog::refreshPreview() {
    const core::ResourceId give = chosen(m_give), get = chosen(m_get);
    const int have = m_view.resources.value(give);
    const int amount = m_amount->value();
    m_amountLabel->setText(QStringLiteral("<b>%1</b> / %2").arg(amount).arg(have));
    const int yield = !give.isValid() || !get.isValid() || give == get ? 0
                      : core::tradeYield(*m_content, give, amount, get, m_view.tradePercent);
    QString why;
    if (give == get) why = tr("Choose two different resources.");
    else if (have <= 0) why = tr("You have no %1 to give.").arg(keyOf(give));
    else if (amount <= 0) why = tr("Slide to choose how much to give.");
    else if (amount > have) why = tr("You only have %1 %2.").arg(have).arg(keyOf(give));
    else if (yield <= 0) why = tr("Too little to get anything back; give more.");
    else if (!m_view.isMyTurn()) why = tr("Trading is possible on your turn.");
    m_preview->setText(why.isEmpty() ? tr("You give <b>%1 %2</b> and get <b>%3 %4</b>.").arg(amount).arg(keyOf(give)).arg(yield).arg(keyOf(get)) : why);
    m_trade->setEnabled(why.isEmpty());
}

}  // namespace kot::client
