#pragma once

#include <QList>

#include "core/model/Position.h"

namespace kot::core {

// The battle field: a hex grid in "odd-r" offset coordinates (x = column, y = row;
// odd rows are shifted half a hex to the right). Pos is reused as the hex coordinate.
namespace hex {

inline constexpr int kWidth = 15;
inline constexpr int kHeight = 11;

bool inBounds(Pos h);
// The (up to) 6 neighbours inside the field.
QList<Pos> neighbours(Pos h);
// Steps between two hexes.
int distance(Pos a, Pos b);
bool adjacent(Pos a, Pos b);

}  // namespace hex

}  // namespace kot::core
