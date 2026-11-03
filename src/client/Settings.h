#pragma once

#include <QString>

class QWidget;

namespace kot::client {

// The player's settings are kept between sessions with QSettings, in
// ~/.config/KillersOfTime/client.conf on Linux (organisation "KillersOfTime",
// application "client"): window and dialog sizes, the last server and name, the
// map's zoom step, sound, and view choices.
QString settingsFile();

// Restores `window`'s size and position from `key` when it is first shown and
// saves them whenever it is hidden or closed.
void rememberGeometry(QWidget* window, const QString& key);

}  // namespace kot::client
