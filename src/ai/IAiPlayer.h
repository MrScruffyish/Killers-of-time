#pragma once

#include <QString>

namespace kot::ai {

// Placeholder interface. In later milestones this gets "decide next command"
// and "decide battle action" functions that work on a fog-filtered view.
class IAiPlayer {
public:
    virtual ~IAiPlayer() = default;
    virtual QString profileName() const = 0;
};

}  // namespace kot::ai
