#pragma once

#include <QDialog>
#include <QJsonObject>

#include "client/ViewModel.h"

namespace kot::core { class ContentRegistry; }

namespace kot::client {

// The hero screen: level and experience to the next level, traits by group (with
// "+" buttons while a level-up pick is pending), the army with veterancy stars,
// spells and artifacts with their descriptions. Picks are sent as pick_trait.
class HeroDialog : public QDialog {
    Q_OBJECT
public:
    HeroDialog(const core::ContentRegistry* content, QWidget* parent = nullptr);
    void showHero(const GameView& view, int heroId);
    void refresh(const GameView& view);
    int heroId() const { return m_heroId; }

signals:
    void commandRequested(const QString& type, const QJsonObject& body, const QString& description);

private:
    void rebuild();

    const core::ContentRegistry* m_content;
    GameView m_view;
    int m_heroId = 0;
    QWidget* m_body = nullptr;
};

}  // namespace kot::client
