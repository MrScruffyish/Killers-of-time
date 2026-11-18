#pragma once

#include <QHashFunctions>
#include <compare>
#include <cstddef>
#include <cstdint>

namespace kot::core {

// A typed integer id. PlayerId and HeroId cannot be mixed up by accident.
// Value 0 means "invalid / none".
template <typename Tag, typename Rep = std::uint32_t>
class StrongId {
public:
    using ValueType = Rep;

    constexpr StrongId() = default;
    constexpr explicit StrongId(Rep value) : m_value(value) {}

    constexpr Rep value() const { return m_value; }
    constexpr bool isValid() const { return m_value != 0; }

    friend constexpr auto operator<=>(const StrongId&, const StrongId&) = default;

private:
    Rep m_value = 0;
};

// Lets StrongId be used as a QHash key.
template <typename Tag, typename Rep>
inline size_t qHash(const StrongId<Tag, Rep>& id, size_t seed = 0) noexcept {
    return ::qHash(id.value(), seed);
}

}  // namespace kot::core
