#pragma once

#include <QDialog>

#include "client/ViewModel.h"

class QListWidget;
class QSlider;
class QLabel;
class QPushButton;

namespace kot::core { class ContentRegistry; }

namespace kot::client {

// The market (M): give some of one resource for another at your trade rate
// (balance.json, better with Marketplaces). Shows what you would get before you
// trade; sends `trade`.
class MarketDialog : public QDialog {
    Q_OBJECT
public:
    explicit MarketDialog(const core::ContentRegistry* content, QWidget* parent = nullptr);
    void setView(const GameView& view);

signals:
    void commandRequested(const QString& type, const QJsonObject& body, const QString& description);

private:
    void refreshPreview();
    core::ResourceId chosen(const QListWidget* list) const;
    QString keyOf(core::ResourceId r) const;  // "gold" (for messages)

    const core::ContentRegistry* m_content;
    GameView m_view;
    QListWidget* m_give;
    QListWidget* m_get;
    QSlider* m_amount;      // 0 .. what you have of the given resource
    QLabel* m_amountLabel;
    QLabel* m_preview;
    QLabel* m_rate;
    QPushButton* m_trade;
};

}  // namespace kot::client
