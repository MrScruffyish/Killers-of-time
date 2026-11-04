#pragma once

#include <QDialog>

#include "client/ViewModel.h"

namespace kot::client {

// The end of a game: who won, and how it went: each player's army worth, towns,
// mines and gold week by week, as lines in the players' colours.
class GameOverDialog : public QDialog {
    Q_OBJECT
public:
    GameOverDialog(const GameView& view, const QString& text, QWidget* parent = nullptr);
};

}  // namespace kot::client
