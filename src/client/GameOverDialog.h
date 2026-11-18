#pragma once

#include <QDialog>
#include <QStringList>

#include "client/ViewModel.h"

namespace kot::client {

// The end of a game: who won, and how it went: each player's army worth, towns,
// mines and gold week by week, as lines in the players' colours.
class GameOverDialog : public QDialog {
    Q_OBJECT
public:
    GameOverDialog(const GameView& view, const QString& text, QWidget* parent = nullptr);

    // Plain-language notes on how the game went from the weekly stats: where the
    // winner pulled ahead, and whether the player hoarded instead of fielding an
    // army ("too careful"). Empty when there are no stats to go on.
    static QStringList analysis(const GameView& view);
};

}  // namespace kot::client
