#include "gamev2/genetics/species.hpp"

#include "test_framework.hpp"

#include <cstddef>
#include <initializer_list>

using namespace gamev2::genetics;

namespace {

SpeciesMask mask_of(std::initializer_list<std::size_t> loci)
{
    SpeciesMask m{};
    for (auto l : loci)
    {
        m.active.set(l);
    }
    return m;
}

}  // namespace

TEST(species_rejects_empty_mask)
{
    SpeciesRegistry registry;
    const auto r = registry.register_species(SpeciesMask{});
    CHECK(!r.has_value());
    CHECK(r.error() == SpeciesError::EmptyMask);
}

TEST(species_register_returns_stable_ids)
{
    SpeciesRegistry registry;
    const auto a = registry.register_species(mask_of({0, 1, 2}));
    const auto b = registry.register_species(mask_of({3, 4}));
    CHECK(a.has_value());
    CHECK(b.has_value());
    CHECK(a->value != b->value);
    CHECK_EQ(registry.species_count(), std::size_t{2});
}

TEST(species_mask_roundtrips)
{
    SpeciesRegistry registry;
    const auto id = registry.register_species(mask_of({7, 8, 9}));
    CHECK(id.has_value());
    const auto& m = registry.mask(*id);
    CHECK(m.active.test(7));
    CHECK(m.active.test(8));
    CHECK(m.active.test(9));
    CHECK(!m.active.test(10));
}

TEST(species_mask_of_an_unregistered_id_is_empty)
{
    // id 0 是保留的非法值，越界 id 同样由调用方给出——settle 是拿
    // ParentPool 里调用方给的 species 调它的。SpeciesId{} 过去会变成
    // masks_[SIZE_MAX]，越界 id 同理，都是一次栈上越界读；守卫之后两者都
    // 得到一张全空掩码，即登记表拒绝空掩码时的同一个答案。
    SpeciesRegistry registry;
    const auto id = registry.register_species(mask_of({7, 8, 9}));
    CHECK(id.has_value());

    CHECK(registry.mask(SpeciesId{}).active.none());
    CHECK(registry.mask(SpeciesId{2}).active.none());
    CHECK(registry.mask(SpeciesId{0xFFFFFFFFu}).active.none());
    // 合法 id 不受守卫影响。
    CHECK(registry.mask(*id).active.test(7));
}

TEST(species_default_id_is_invalid)
{
    const SpeciesId id{};
    CHECK(!static_cast<bool>(id));
}
