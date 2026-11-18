#pragma once

#include <QHashFunctions>
#include <compare>
#include <cstddef>

namespace kot::core {

// A map tile coordinate. x grows to the right, y grows downwards.
struct Pos {
    int x = 0;
    int y = 0;

    friend constexpr auto operator<=>(const Pos&, const Pos&) = default;
};

inline size_t qHash(const Pos& p, size_t seed = 0) noexcept {
    return ::qHashMulti(seed, p.x, p.y);
}

}  // namespace kot::core
