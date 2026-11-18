#pragma once

#include <QHashFunctions>
#include <QtGlobal>
#include <array>
#include <compare>
#include <utility>

namespace kot::core {

// The id of a piece of content (a unit type, a building, a resource, ...): its index
// in the ContentRegistry's list of that kind, which is sorted by the text key from the
// data files (so ids, and the order of loops over content, do not depend on file
// order). Invalid = -1. Saves and network messages use these numbers too, so they
// only make sense with the same data (the content checksum guards that).
template <typename Tag>
class ContentId {
public:
    constexpr ContentId() = default;
    constexpr explicit ContentId(int index) : m_index(static_cast<qint16>(index)) {}

    constexpr int index() const { return m_index; }
    constexpr bool isValid() const { return m_index >= 0; }

    friend constexpr auto operator<=>(const ContentId&, const ContentId&) = default;

private:
    qint16 m_index = -1;
};

template <typename Tag>
inline size_t qHash(const ContentId<Tag>& id, size_t seed = 0) noexcept {
    return ::qHash(id.index(), seed);
}

using ResourceId = ContentId<struct ResourceTag>;
using TerrainId = ContentId<struct TerrainTag>;
using TraitId = ContentId<struct TraitTag>;
using UnitTypeId = ContentId<struct UnitTypeTag>;
using BuildingTypeId = ContentId<struct BuildingTypeTag>;
using HeroClassId = ContentId<struct HeroClassTag>;
using HeroTemplateId = ContentId<struct HeroTemplateTag>;
using SpellId = ContentId<struct SpellTag>;
using ArtifactId = ContentId<struct ArtifactTag>;
using RaceId = ContentId<struct RaceTag>;
using WeekEventId = ContentId<struct WeekEventTag>;

// A number per content id of one kind (resource amounts, trait values), no allocation.
// Range-for gives (id, value) for the ids whose value is not 0, in id order:
//   for (const auto [resource, amount] : cost) ...
template <typename Id, int N>
class ContentValues {
public:
    static constexpr int kMax = N;  // the data may have at most this many of the kind

    constexpr ContentValues() = default;

    int value(Id id) const { return id.isValid() && id.index() < N ? m_values[id.index()] : 0; }
    int& operator[](Id id) {
        Q_ASSERT(id.isValid() && id.index() < N);
        return m_values[id.index()];
    }
    bool isEmpty() const {
        for (const int v : m_values)
            if (v) return false;
        return true;
    }
    friend bool operator==(const ContentValues&, const ContentValues&) = default;

    class Iterator {
    public:
        Iterator(const ContentValues* owner, int i) : m_owner(owner), m_i(i) { skip(); }
        std::pair<Id, int> operator*() const { return {Id(m_i), m_owner->m_values[m_i]}; }
        Iterator& operator++() {
            ++m_i;
            skip();
            return *this;
        }
        bool operator!=(const Iterator& o) const { return m_i != o.m_i; }

    private:
        void skip() {
            while (m_i < N && m_owner->m_values[m_i] == 0) ++m_i;
        }
        const ContentValues* m_owner;
        int m_i;
    };
    Iterator begin() const { return Iterator(this, 0); }
    Iterator end() const { return Iterator(this, N); }

private:
    std::array<int, N> m_values{};
};

// Amounts of resources: a cost, a stock, an income.
using ResourceAmounts = ContentValues<ResourceId, 16>;
// A hero's trait values (also what a class starts with, what an artifact adds).
using TraitValues = ContentValues<TraitId, 32>;

}  // namespace kot::core
