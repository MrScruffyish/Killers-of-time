#pragma once

#include <QDialog>
#include <QJsonObject>

#include "client/ViewModel.h"

class QTabWidget;

namespace kot::core { class ContentRegistry; }

namespace kot::client {

// One town: buildings (with "build, trade first" when only resources are missing),
// recruiting, and the tavern. Every action is sent as a command; the dialog redraws
// itself when the next view arrives.
class TownDialog : public QDialog {
    Q_OBJECT
public:
    explicit TownDialog(const core::ContentRegistry* content, QWidget* parent = nullptr);

    void showTown(const GameView& view, int townId);
    void refresh(const GameView& view);
    int townId() const { return m_townId; }

signals:
    void commandRequested(const QString& type, const QJsonObject& body, const QString& description);

private:
    void rebuild();
    QString costText(const core::ResourceAmounts& cost) const;
    QString unitName(const QString& id) const;

    const core::ContentRegistry* m_content;
    GameView m_view;
    int m_townId = 0;
    QTabWidget* m_tabs;
};

}  // namespace kot::client
