#pragma once

#include <QString>
#include <QStringList>

namespace kot::server {

// The server's settings: from a config file (strict JSON, unknown fields are errors),
// then the command line on top. See docs/server.md.
//   { "port": 45454, "data": "/path/to/data", "saves": "/path/to/saves", "savesMaxMB": 500, "resume": true }
struct ServerConfig {
    int port = 0;          // 0 = the default port
    QString dataDir;       // empty = found automatically
    QString saveDir;       // empty = the user's app data folder
    int savesMaxMB = 0;    // most space the saves may take; 0 = no limit
    bool resume = false;   // at start, go on with the newest saved game (waits for the players)

    // Where the config file is looked for when --config is not given.
    static QString defaultPath();
    // Reads `path` over the current values. A missing file at the default path is
    // fine (returns true); a missing file that was asked for is an error.
    bool load(const QString& path, bool mustExist, QStringList* errors);
};

}  // namespace kot::server
