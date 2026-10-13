#include "test_framework.hpp"

#include "gamev2/ecs/entity.hpp"

#include <unordered_set>

using gamev2::ecs::EntityId;

TEST(entity_id_roundtrip)
{
    const auto e = EntityId::make(1234, 7);
    CHECK_EQ(e.index(), 1234u);
    CHECK_EQ(e.version(), 7u);
    CHECK(e == EntityId::make(1234, 7));
}

TEST(entity_id_default_is_invalid)
{
    CHECK(!EntityId{});
    CHECK_EQ(EntityId{}.value, 0ull);
}

TEST(entity_id_first_slot_is_not_invalid)
{
    // 索引 0 + 版本 1 是首个合法实体，不应撞上 value==0 的哨兵。
    const auto e = EntityId::make(0, 1);
    CHECK(static_cast<bool>(e));
    CHECK_EQ(e.index(), 0u);
}

TEST(entity_id_equality_is_by_value)
{
    CHECK(EntityId::make(5, 1) != EntityId::make(5, 2));
    CHECK(EntityId::make(5, 1) != EntityId::make(6, 1));
}

TEST(entity_id_is_usable_as_hash_key)
{
    std::unordered_set<EntityId> seen;
    seen.insert(EntityId::make(1, 1));
    seen.insert(EntityId::make(1, 1));
    seen.insert(EntityId::make(2, 1));
    CHECK_EQ(seen.size(), std::size_t{2});
}
