#pragma once

#include <functional>

#include <QDialog>
#include <QJsonObject>

#include "client/ViewModel.h"

class QTabWidget;
class QLabel;
class QComboBox;

namespace kot::core { class ContentRegistry; }

namespace kot::client {

// One town: buildings (with "build, trade first" when only resources are missing),
// recruiting, the tavern and the armies (move units between the garrison and a
// visiting hero). Every action is sent as a command; the dialog redraws
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
    // Another of my towns, keeping the open tab: +1 next, -1 previous.
    void stepTown(int direction);
    QList<int> myTowns() const;
    void drawScene(const ViewTown& town);
    QString costText(const core::ResourceAmounts& cost) const;
    QString unitName(core::UnitTypeId id) const;
    // What a building does (from its effects): brief for the table cell, full for its tooltip.
    QString effectsText(const core::BuildingDef& b, bool brief = false) const;
    // A better version of something: an upgradeOf dwelling, or the next hall,
    // guild level or wall (it requires a non-dwelling building with the same kind of effect).
    bool isUpgrade(const core::BuildingDef& b) const;

    const core::ContentRegistry* m_content;
    GameView m_view;
    int m_townId = 0;
    QTabWidget* m_tabs;
    // The tabs are filled when first shown (or a moment after the window), not all at once.
    QList<std::function<void(QWidget*)>> m_tabFill;
    int m_tabGeneration = 0;
    void addLazyTab(const QIcon& icon, const QString& title, std::function<void(QWidget*)> fill);
    void buildTab(int index);
    QLabel* m_scene;
    QComboBox* m_townChoice;
    bool m_onlyBuildable = false;  // remembered in QSettings
};

}  // namespace kot::client
