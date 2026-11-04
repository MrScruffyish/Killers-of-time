#include "core/battle/HexGrid.h"

#include <algorithm>
#include <cstdlib>

namespace kot::core::hex {

bool inBounds(Pos h) {
    return h.x >= 0 && h.y >= 0 && h.x < kWidth && h.y < kHeight;
}

QList<Pos> neighbours(Pos h) {
    static const int even[6][2] = {{1, 0}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}, {0, 1}};
    static const int odd[6][2] = {{1, 0}, {1, -1}, {0, -1}, {-1, 0}, {0, 1}, {1, 1}};
    const auto& d = (h.y & 1) ? odd : even;
    QList<Pos> out;
    for (const auto& step : d) {
        const Pos n{h.x + step[0], h.y + step[1]};
        if (inBounds(n)) out.append(n);
    }
    return out;
}

int distance(Pos a, Pos b) {
    // Offset -> cube coordinates.
    const int ax = a.x - (a.y - (a.y & 1)) / 2, az = a.y, ay = -ax - az;
    const int bx = b.x - (b.y - (b.y & 1)) / 2, bz = b.y, by = -bx - bz;
    return std::max({std::abs(ax - bx), std::abs(ay - by), std::abs(az - bz)});
}

bool adjacent(Pos a, Pos b) {
    return distance(a, b) == 1;
}

}  // namespace kot::core::hex
