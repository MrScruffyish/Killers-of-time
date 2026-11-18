#pragma once

#include <QString>

namespace kot::core {

// Finds the game data directory (the one with resources.json). Looks at
// `explicitDir` if given (and nowhere else), then $KOT_DATA_DIR, then ./data and
// the data folders near the executable (so it works from build/bin). Returns an
// empty string if nothing is found.
QString findDataDirectory(const QString& explicitDir = QString());

}  // namespace kot::core
