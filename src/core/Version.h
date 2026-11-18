#pragma once

// The game version comes from CMakeLists.txt (project VERSION), the commit from
// git, both written into BuildInfo.h at every build (cmake/BuildInfo.cmake).
#include "BuildInfo.h"

namespace kot::core {

inline constexpr const char* kGameName = "Killers of Time";
inline constexpr const char* kGameVersion = KOT_VERSION;
inline constexpr const char* kBuildCommit = KOT_GIT_COMMIT;
// "0.3.0 (commit 1a2b3c4)", for --version, window titles and logs.
inline const char* versionText() { return KOT_VERSION " (commit " KOT_GIT_COMMIT ")"; }

}  // namespace kot::core
