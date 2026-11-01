#include "core/util/Rng.h"

#include <QtGlobal>
#include <utility>

namespace kot::core {

Rng::Rng(std::uint64_t seed) : m_seed(seed), m_engine(seed) {}

void Rng::reseed(std::uint64_t seed) {
    m_seed = seed;
    m_engine.seed(seed);
}

std::uint32_t Rng::nextU32() {
    return static_cast<std::uint32_t>(nextU64() >> 32);
}

int Rng::nextInt(int minInclusive, int maxInclusive) {
    Q_ASSERT(minInclusive <= maxInclusive);
    if (minInclusive > maxInclusive) {
        std::swap(minInclusive, maxInclusive);
    }
    const std::uint64_t range =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(maxInclusive) -
                                   static_cast<std::int64_t>(minInclusive)) +
        1;
    // Rejection sampling to avoid modulo bias: 2^64 mod range.
    const std::uint64_t threshold = (0 - range) % range;
    std::uint64_t r = nextU64();
    while (r < threshold) {
        r = nextU64();
    }
    return static_cast<int>(static_cast<std::int64_t>(minInclusive) +
                            static_cast<std::int64_t>(r % range));
}

double Rng::nextDouble() {
    return static_cast<double>(nextU64() >> 11) * (1.0 / 9007199254740992.0);
}

bool Rng::chance(double p) {
    if (p <= 0.0) return false;
    if (p >= 1.0) return true;
    return nextDouble() < p;
}

}  // namespace kot::core
