#pragma once

#include <QString>
#include <cstdint>
#include <random>

namespace kot::core {

// Deterministic random number generator owned by the game state.
// Same seed -> same sequence on every platform. All game rules (including
// battles) must use this, never rand() or QRandomGenerator.
//
// Note: std::uniform_int_distribution is NOT used on purpose, because its
// results differ between standard library implementations.
class Rng {
public:
    explicit Rng(std::uint64_t seed = 1);

    void reseed(std::uint64_t seed);
    std::uint64_t seed() const { return m_seed; }

    // Save/restore the generator state exactly (used by save games).
    QString engineState() const;
    bool restoreEngineState(const QString& state);

    std::uint32_t nextU32();
    // Uniform integer in [minInclusive, maxInclusive].
    int nextInt(int minInclusive, int maxInclusive);
    // Uniform double in [0, 1).
    double nextDouble();
    // True with probability p (p <= 0 never, p >= 1 always).
    bool chance(double p);

private:
    std::uint64_t nextU64() { return m_engine(); }

    std::uint64_t m_seed;
    std::mt19937_64 m_engine;
};

}  // namespace kot::core
