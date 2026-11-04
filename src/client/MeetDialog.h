#pragma once

#include <QDialog>
#include <QJsonObject>

#include "client/ViewModel.h"

namespace kot::core { class ContentRegistry; }

namespace kot::client {

// Two of your heroes side by side: their armies and artifacts in two columns. Each
// stack has a button to send it (or part of it, with the number) to the other hero,
// each artifact one to hand it over, and "All units" moves the whole army across.
// Sends move_units and give_artifact.
class MeetDialog : public QDialog {
    Q_OBJECT
public:
    MeetDialog(const core::ContentRegistry* content, QWidget* parent = nullptr);
    void showMeeting(const GameView& view, int leftHero, int rightHero);
    void refresh(const GameView& view);

signals:
    void commandRequested(const QString& type, const QJsonObject& body, const QString& description);

private:
    void rebuild();
    QWidget* column(const ViewHero& hero, const ViewHero& other, bool left);

    const core::ContentRegistry* m_content;
    GameView m_view;
    int m_left = 0;
    int m_right = 0;
    QWidget* m_body = nullptr;
};

}  // namespace kot::client
